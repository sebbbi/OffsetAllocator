// (C) Sebastian Aaltonen 2023
// MIT License (see file: LICENSE)

#include "offsetAllocator.hpp"

#ifdef DEBUG
#include <assert.h>
#define ASSERT(x) assert(x)
#define DEBUG_VERBOSE
#else
#define ASSERT(x)
#endif

#ifdef DEBUG_VERBOSE
#include <stdio.h>
#endif

namespace OffsetAllocator
{
    namespace SmallFloat
    {
        static constexpr uint32 MANTISSA_BITS = 3;
        static constexpr uint32 MANTISSA_VALUE = 1 << MANTISSA_BITS;
        static constexpr uint32 MANTISSA_MASK = MANTISSA_VALUE - 1;

        // Bin sizes follow floating point (exponent + mantissa) distribution (piecewise linear log approx)
        // This ensures that for each size class, the average overhead percentage stays the same
        uint32 uintToFloatRoundUp(uint32 size)
        {
            uint32 exp = 0;
            uint32 mantissa = 0;
            
            if (size < MANTISSA_VALUE)
            {
                // Denorm: 0..(MANTISSA_VALUE-1)
                mantissa = size;
            }
            else
            {
                // Normalized: Hidden high bit always 1. Not stored. Just like float.
                uint32 leadingZeros = __builtin_clz(size);
                uint32 highestSetBit = 31 - leadingZeros;
                
                uint32 mantissaStartBit = highestSetBit - MANTISSA_BITS;
                exp = mantissaStartBit + 1;
                mantissa = (size >> mantissaStartBit) & MANTISSA_MASK;
                
                uint32 lowBitsMask = (1 << mantissaStartBit) - 1;
                
                // Round up!
                if ((size & lowBitsMask) != 0)
                    mantissa++;
            }
            
            return (exp << MANTISSA_BITS) + mantissa; // + allows mantissa->exp overflow for round up
        }

        uint32 uintToFloatRoundDown(uint32 size)
        {
            uint32 exp = 0;
            uint32 mantissa = 0;
            
            if (size < MANTISSA_VALUE)
            {
                // Denorm: 0..(MANTISSA_VALUE-1)
                mantissa = size;
            }
            else
            {
                // Normalized: Hidden high bit always 1. Not stored. Just like float.
                uint32 leadingZeros = __builtin_clz(size);
                uint32 highestSetBit = 31 - leadingZeros;
                
                uint32 mantissaStartBit = highestSetBit - MANTISSA_BITS;
                exp = mantissaStartBit + 1;
                mantissa = (size >> mantissaStartBit) & MANTISSA_MASK;
            }
            
            return (exp << MANTISSA_BITS) | mantissa;
        }
    
        uint32 floatToUint(uint32 floatValue)
        {
            uint32 exponent = floatValue >> MANTISSA_BITS;
            uint32 mantissa = floatValue & MANTISSA_MASK;
            if (exponent == 0)
            {
                // Denorms
                return mantissa;
            }
            else
            {
                return (mantissa | MANTISSA_VALUE) << (exponent - 1);
            }
        }
    }
      
    Allocator::Allocator(uint32 size, uint32 maxAllocs) : m_size(size), m_freeStorage(0), m_usedBinsTop(0), m_freeOffset(maxAllocs - 1)
    {
        for (uint32 i = 0 ; i < NUM_TOP_BINS; i++)
            m_usedBins[i] = 0;
        
        for (uint32 i = 0 ; i < NUM_LEAF_BINS; i++)
            m_binIndices[i] = Node::unused;
        
        m_nodes = new Node[maxAllocs];
        m_freeNodes = new uint32[maxAllocs];
        
        // Freelist is a stack. Nodes in inverse order so that [0] pops first.
        for (uint32 i = 0; i < maxAllocs; i++)
        {
            m_freeNodes[i] = maxAllocs - i - 1;
        }
        
        // Start state: Whole storage as one big node
        // Algorithm will split remainders and push them back as smaller nodes
        insertNodeIntoBin(size, 0);
    }
    
    // Utility functions
    uint32 findLowestSetBitAfter(uint32 bitMask, uint32 startBitIndex)
    {
        uint32 maskBeforeStartIndex = (1 << startBitIndex) - 1;
        uint32 maskAfterStartIndex = ~maskBeforeStartIndex;
        uint32 bitsAfter = bitMask & maskAfterStartIndex;
        if (bitsAfter == 0) return Allocation::NO_SPACE;
        return __builtin_ctz(bitsAfter);
    }

    // Allocator
    Allocator::~Allocator()
    {
        // Leaks?
        StorageReport report = storageReport();
        ASSERT(report.totalFreeSpace == m_size);
        ASSERT(report.largestFreeRegion == m_size);
        
        delete[] m_nodes;
        delete[] m_freeNodes;
    }
    
    Allocation Allocator::allocate(uint32 size)
    {
        // Round up to bin index to ensure that alloc >= bin
        // Gives us min bin index that fits the size
        uint32 minBinIndex = SmallFloat::uintToFloatRoundUp(size);
        
        uint32 minTopBinIndex = minBinIndex >> TOP_BINS_INDEX_SHIFT;
        uint32 minLeafBinIndex = minBinIndex & LEAF_BINS_INDEX_MASK;
        
        uint32 topBinIndex = findLowestSetBitAfter(m_usedBinsTop, minTopBinIndex);
        
        // Out of space?
        if (topBinIndex == Allocation::NO_SPACE)
        {
            return {.offset = Allocation::NO_SPACE, .metadata = Allocation::NO_SPACE};
        }
        
        // If the MSB bin is rounded up, zero LSB min bin requirement (larger MSB is enough as floats are monotonically increasing)
        if (minTopBinIndex != topBinIndex)
            minLeafBinIndex = 0;
        
        uint32 leafBinIndex = findLowestSetBitAfter(m_usedBins[topBinIndex], minLeafBinIndex);
        
        // Out of space?
        if (leafBinIndex == Allocation::NO_SPACE)
        {
            return {.offset = Allocation::NO_SPACE, .metadata = Allocation::NO_SPACE};
        }

        uint32 binIndex = (topBinIndex << TOP_BINS_INDEX_SHIFT) | leafBinIndex;
        
        // Pop the top node of the bin. Bin top = node.next.
        uint32 nodeIndex = m_binIndices[binIndex];
        Node& node = m_nodes[nodeIndex];
        uint32 nodeTotalSize = node.dataSize;
        node.dataSize = size;
        node.used = true;
        m_binIndices[binIndex] = node.binListNext;
        if (node.binListNext != Node::unused) m_nodes[node.binListNext].binListPrev = Node::unused;
        m_freeStorage -= nodeTotalSize;
#ifdef DEBUG_VERBOSE
        printf("Free storage: %u (-%u) (allocate)\n", m_freeStorage, nodeTotalSize);
#endif

        // Bin empty?
        if (m_binIndices[binIndex] == Node::unused)
        {
            // Remove a leaf bin mask bit
            m_usedBins[topBinIndex] &= ~(1 << leafBinIndex);
            
            // All leaf bins empty?
            if (m_usedBins[topBinIndex] == 0)
            {
                // Remove a top bin mask bit
                m_usedBinsTop &= ~(1 << topBinIndex);
            }
        }
        
        // Push back reminder N elements to a lower bin
        uint32 reminderSize = nodeTotalSize - size;
        if (reminderSize > 0)
        {
            uint32 newNodeIndex = insertNodeIntoBin(reminderSize, node.dataOffset + size);
            
            // Link nodes next to each other so that we can merge them later if both are free
            // And update the old next neighbor to point to the new node (in middle)
            if (node.neighborNext != Node::unused) m_nodes[node.neighborNext].neighborPrev = newNodeIndex;
            m_nodes[newNodeIndex].neighborPrev = nodeIndex;
            m_nodes[newNodeIndex].neighborNext = node.neighborNext;
            node.neighborNext = newNodeIndex;
        }
        
        return {.offset = node.dataOffset, .metadata = nodeIndex};
    }
    
    void Allocator::free(Allocation allocation)
    {
        ASSERT(allocation.metadata != Allocation::NO_SPACE);
        
        uint32 nodeIndex = allocation.metadata;
        Node& node = m_nodes[nodeIndex];
        
        // Merge with neighbors...
        uint32 offset = node.dataOffset;
        uint32 size = node.dataSize;
        
        if ((node.neighborPrev != Node::unused) && (m_nodes[node.neighborPrev].used == false))
        {
            // Previous (contiguous) free node: Change offset to previous node offset. Sum sizes
            Node& prevNode = m_nodes[node.neighborPrev];
            offset = prevNode.dataOffset;
            size += prevNode.dataSize;
            
            // Remove node from the bin linked list and put it in the freelist
            removeNodeFromBin(node.neighborPrev);
            
            ASSERT(prevNode.neighborNext == nodeIndex);
            node.neighborPrev = prevNode.neighborPrev;
        }
        
        if ((node.neighborNext != Node::unused) && (m_nodes[node.neighborNext].used == false))
        {
            // Next (contiguous) free node: Offset remains the same. Sum sizes.
            Node& nextNode = m_nodes[node.neighborNext];
            size += nextNode.dataSize;
            
            // Remove node from the bin linked list and put it in the freelist
            removeNodeFromBin(node.neighborNext);
            
            ASSERT(nextNode.neighborPrev == nodeIndex);
            node.neighborNext = nextNode.neighborNext;
        }

        uint32 neighborNext = node.neighborNext;
        uint32 neighborPrev = node.neighborPrev;
        
        // Insert the removed node to freelist
#ifdef DEBUG_VERBOSE
        printf("Putting node %u into freelist[%u] (free)\n", nodeIndex, m_freeOffset + 1);
#endif
        m_freeNodes[++m_freeOffset] = nodeIndex;

        // Insert the (combined) free node to bin
        uint32 combinedNodeIndex = insertNodeIntoBin(size, offset);

        // Connect neighbors with the new combined node
        if (neighborNext != Node::unused)
        {
            m_nodes[combinedNodeIndex].neighborNext = neighborNext;
            m_nodes[neighborNext].neighborPrev = combinedNodeIndex;
        }
        if (neighborPrev != Node::unused)
        {
            m_nodes[combinedNodeIndex].neighborPrev = neighborPrev;
            m_nodes[neighborPrev].neighborNext = combinedNodeIndex;
        }
    }

    uint32 Allocator::insertNodeIntoBin(uint32 size, uint32 dataOffset)
    {
        // Round down to bin index to ensure that bin >= alloc
        uint32 binIndex = SmallFloat::uintToFloatRoundDown(size);
        
        uint32 topBinIndex = binIndex >> TOP_BINS_INDEX_SHIFT;
        uint32 leafBinIndex = binIndex & LEAF_BINS_INDEX_MASK;
        
        // Bin was empty before?
        if (m_binIndices[binIndex] == Node::unused)
        {
            // Set bin mask bits
            m_usedBins[topBinIndex] |= 1 << leafBinIndex;
            m_usedBinsTop |= 1 << topBinIndex;
        }
        
        // Take a freelist node and insert on top of the bin linked list (next = old top)
        uint32 topNodeIndex = m_binIndices[binIndex];
        uint32 nodeIndex = m_freeNodes[m_freeOffset--];
#ifdef DEBUG_VERBOSE
        printf("Getting node %u from freelist[%u]\n", nodeIndex, m_freeOffset + 1);
#endif
        m_nodes[nodeIndex] = {.dataOffset = dataOffset, .dataSize = size, .binListNext = topNodeIndex};
        if (topNodeIndex != Node::unused) m_nodes[topNodeIndex].binListPrev = nodeIndex;
        m_binIndices[binIndex] = nodeIndex;
        
        m_freeStorage += size;
#ifdef DEBUG_VERBOSE
        printf("Free storage: %u (+%u) (insertNodeIntoBin)\n", m_freeStorage, size);
#endif

        return nodeIndex;
    }
    
    void Allocator::removeNodeFromBin(uint32 nodeIndex)
    {
        Node &node = m_nodes[nodeIndex];
        
        if (node.binListPrev != Node::unused)
        {
            // Easy case: We have previous node. Just remove this node from the middle of the list.
            m_nodes[node.binListPrev].binListNext = node.binListNext;
            if (node.binListNext != Node::unused) m_nodes[node.binListNext].binListPrev = node.binListPrev;
        }
        else
        {
            // Hard case: We are the first node in a bin. Find the bin.
            
            // Round down to bin index to ensure that bin >= alloc
            uint32 binIndex = SmallFloat::uintToFloatRoundDown(node.dataSize);
            
            uint32 topBinIndex = binIndex >> TOP_BINS_INDEX_SHIFT;
            uint32 leafBinIndex = binIndex & LEAF_BINS_INDEX_MASK;
            
            m_binIndices[binIndex] = node.binListNext;
            if (node.binListNext != Node::unused) m_nodes[node.binListNext].binListPrev = Node::unused;

            // Bin empty?
            if (m_binIndices[binIndex] == Node::unused)
            {
                // Remove a leaf bin mask bit
                m_usedBins[topBinIndex] &= ~(1 << leafBinIndex);
                
                // All leaf bins empty?
                if (m_usedBins[topBinIndex] == 0)
                {
                    // Remove a top bin mask bit
                    m_usedBinsTop &= ~(1 << topBinIndex);
                }
            }
        }
        
        // Insert the node to freelist
#ifdef DEBUG_VERBOSE
        printf("Putting node %u into freelist[%u] (removeNodeFromBin)\n", nodeIndex, m_freeOffset + 1);
#endif
        m_freeNodes[++m_freeOffset] = nodeIndex;

        m_freeStorage -= node.dataSize;
#ifdef DEBUG_VERBOSE
        printf("Free storage: %u (-%u) (removeNodeFromBin)\n", m_freeStorage, node.dataSize);
#endif
    }

    StorageReport Allocator::storageReport() const
    {
        uint32 largestFreeRegion = 0;
        if (m_usedBinsTop)
        {
            uint32 topBinIndex = 31 - __builtin_clz(m_usedBinsTop);
            uint32 leafBinIndex = 31 - __builtin_clz(m_usedBins[topBinIndex]);
            largestFreeRegion = SmallFloat::floatToUint((topBinIndex << TOP_BINS_INDEX_SHIFT) | leafBinIndex);
        }
        
        return {.totalFreeSpace = m_freeStorage, .largestFreeRegion = largestFreeRegion};
    }

    StorageReportFull Allocator::storageReportFull() const
    {
        // TODO: Implement
        StorageReportFull report;
        return report;
    }
}
