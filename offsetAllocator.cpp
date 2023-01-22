typedef unsigned char uint8;
typedef unsigned int uint32;

class OffsetAllocator
{
public:
    static constexpr uint32 NUM_TOP_BINS = 32;
    static constexpr uint32 BINS_PER_LEAF = 8;
    static constexpr uint32 TOP_BINS_INDEX_SHIFT = 3;
    static constexpr uint32 LEAF_BINS_INDEX_MASK = 0x7;
    static constexpr uint32 NUM_LEAF_BINS = NUM_TOP_BINS * BINS_PER_LEAF;

    struct Allocation
    {
        uint32 offset;
        uint32 metadata; // internal: node index
    };
    
    OffsetAllocator(uint32 size, uint32 maxAllocs = 128 * 1024) : m_usedBinsTop(0), m_freeOffset(maxAllocs - 1)
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
        
        // Start state: Whole memory as one big node
        // Algorithm will split remainders and push them back as smaller nodes
        insertNodeIntoBin(size, 0);
    }
            
    ~OffsetAllocator()
    {
        delete m_nodes;
        delete m_freeNodes;
    }
    
    Allocation allocate(uint32 size)
    {
        // Round up to bin index to ensure that alloc >= bin
        // Gives us min bin index that fits the size
        uint32 minBinIndex = sizeToFloatBinIndexRoundUp(size);

        uint32 minTopBinIndex = minBinIndex >> TOP_BINS_INDEX_SHIFT;
        uint32 minLeafBinIndex = minBinIndex & LEAF_BINS_INDEX_MASK;

        uint32 topBinIndex = findLowestSetBitAfter(m_usedBinsTop, minTopBinIndex);
        
        // If the MSB bin is rounded up, zero LSB min bin requirement (larger MSB is enough as floats are monotonically increasing)
        if (minTopBinIndex != topBinIndex)
            minLeafBinIndex = 0;

        uint32 leafBinIndex = findLowestSetBitAfter(m_usedBins[topBinIndex], minLeafBinIndex);
        uint32 binIndex = (topBinIndex << TOP_BINS_INDEX_SHIFT) | leafBinIndex;

        // Pop the top node of the bin. Bin top = node.next.
        uint32 nodeIndex = m_binIndices[binIndex];
        Node& node = m_nodes[nodeIndex];
        node.used = true;
        m_binIndices[binIndex] = node.binListNext;
        
        // Push back reminder N elements to a lower bin
        uint32 reminderSize = node.dataSize - size;
        if (reminderSize > 0)
        {
            uint32 newNodeIndex = insertNodeIntoBin(reminderSize, node.dataOffset + size);
            
            // Link nodes next to each other in memory so that we can merge them later if both are free
            m_nodes[newNodeIndex].neighborPrev = nodeIndex;
            node.neighborNext = newNodeIndex;
        }
        
        return {.offset = node.dataOffset, .metadata = nodeIndex};
    }
    
    void free(Allocation allocation)
    {
        Node& node = m_nodes[allocation.metadata];
        node.used = false;
        
        // Merge with neighbors...
        uint32 offset = node.dataOffset;
        uint32 size = node.dataSize;
        
        if (node.neighborPrev != Node::unused && m_nodes[node.neighborPrev].used == false)
        {
            // Previous (contiguous) free node: Change offset to previous node offset. Sum sizes
            Node& prevNode = m_nodes[node.neighborPrev];
            offset = prevNode.dataOffset;
            size += prevNode.dataSize;
            
            // Remove node from the bin linked list and put it in the freelist
            removeNodeFromBin(node.neighborPrev);
        }

        if (node.neighborNext != Node::unused && m_nodes[node.neighborNext].used == false)
        {
            // Next (contiguous) free node: Offset remains the same. Sum sizes.
            Node& nextNode = m_nodes[node.neighborPrev];
            size += nextNode.dataSize;

            // Remove node from the bin linked list and put it in the freelist
            removeNodeFromBin(node.neighborPrev);
        }

        // Insert the (combined) free node to bin
        uint32 freedNodeIndex = insertNodeIntoBin(size, offset);
    }

private:
    struct Node
    {
        static constexpr uint32 unused = 0xffffffff;
        
        uint32 dataOffset = 0;
        uint32 dataSize = 0;
        uint32 binListPrev = unused;
        uint32 binListNext = unused;
        uint32 neighborPrev = unused;
        uint32 neighborNext = unused;
        bool used = false; // TODO: Merge as bit flag
    };

    // Bin sizes follow floating point (exponent + mantissa) distribution (piecewise linear log approx)
    // This ensures that for each size class, the average overhead percentage stays the same
    uint32 sizeToFloatBinIndexRoundUp(uint32 size)
    {
        uint32 exp = 0;
        uint32 mantissa = 0;

        if (size < BINS_PER_LEAF)
        {
            // Denorm: 0..(BINS_PER_LEAF-1)
            mantissa = size;
        }
        else
        {
            // Normalized: Hidden high bit always 1. Not stored. Just like float.
            uint32 leadingZeros = __builtin_clz(size);
            uint32 highestSetBit = 31 - leadingZeros;
            
            uint32 mantissaStartBit = highestSetBit - TOP_BINS_INDEX_SHIFT;
            exp = mantissaStartBit;
            mantissa = (size >> mantissaStartBit) & LEAF_BINS_INDEX_MASK;
            
            uint32 lowBitsMask = (1 << mantissaStartBit) - 1;

            // Round up!
            if (lowBitsMask != 0)
                mantissa++;
        }
        
        return (exp << TOP_BINS_INDEX_SHIFT) + mantissa; // + allows mantissa->exp overflow for round up
    }
    
    uint32 sizeToFloatBinIndexRoundDown(uint32 size)
    {
        uint32 exp = 0;
        uint32 mantissa = 0;

        if (size < BINS_PER_LEAF)
        {
            // Denorm: 0..(BINS_PER_LEAF-1)
            mantissa = size;
        }
        else
        {
            // Normalized: Hidden high bit always 1. Not stored. Just like float.
            uint32 leadingZeros = __builtin_clz(size);
            uint32 highestSetBit = 31 - leadingZeros;
            
            uint32 mantissaStartBit = highestSetBit - TOP_BINS_INDEX_SHIFT;
            exp = mantissaStartBit;
            mantissa = (size >> mantissaStartBit) & LEAF_BINS_INDEX_MASK;
        }
        
        return (exp << TOP_BINS_INDEX_SHIFT) | mantissa;
    }
    
    uint32 findLowestSetBitAfter(uint32 bitMask, uint32 startBitIndex)
    {
        uint32 maskBeforeStartIndex = (1 << startBitIndex) - 1;
        uint32 maskAfterStartIndex = ~maskBeforeStartIndex;
        uint32 bitsAfter = bitMask & maskAfterStartIndex;
        return __builtin_ctz(bitsAfter);
    }

    uint32 insertNodeIntoBin(uint32 size, uint32 dataOffset)
    {
        // Round down to bin index to ensure that bin >= alloc
        uint32 binIndex = sizeToFloatBinIndexRoundDown(size);
        
        uint32 topBinIndex = binIndex >> TOP_BINS_INDEX_SHIFT;
        uint32 leafBinIndex = binIndex & LEAF_BINS_INDEX_MASK;

        // Bin was empty?
        if (m_binIndices[binIndex] == Node::unused)
        {
            // Set bin mask bits
            m_usedBins[topBinIndex] |= 1 << leafBinIndex;
            m_usedBinsTop |= 1 << topBinIndex;
        }

        // Take a freelist node and insert on top of the bin linked list (next = old top)
        uint32 topNodeIndex = m_binIndices[binIndex];
        uint32 nodeIndex = m_freeNodes[m_freeOffset--];
        m_nodes[nodeIndex] = { .dataOffset = dataOffset, .dataSize = size, .binListNext = topNodeIndex };
        m_nodes[topNodeIndex].binListPrev = nodeIndex;
        m_binIndices[binIndex] = nodeIndex;
        
        return nodeIndex;
    }

    void removeNodeFromBin(uint32 nodeIndex)
    {
        Node &node = m_nodes[nodeIndex];
        
        if (node.binListPrev)
        {
            // Easy case: We have previous node. Set prev.next = node.next.
            m_nodes[node.binListPrev].binListNext = node.binListNext;
        }
        else
        {
            // Hard case: We are the first node in a bin. Find the bin.
            
            // Round down to bin index to ensure that bin >= alloc
            uint32 binIndex = sizeToFloatBinIndexRoundDown(node.dataSize);
            
            uint32 topBinIndex = binIndex >> TOP_BINS_INDEX_SHIFT;
            uint32 leafBinIndex = binIndex & LEAF_BINS_INDEX_MASK;
            
            m_binIndices[binIndex] = node.binListNext;
            
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
        m_freeNodes[m_freeOffset++] = nodeIndex;
    }

private:
    uint32 m_usedBinsTop;
    uint8 m_usedBins[NUM_TOP_BINS];
    uint32 m_binIndices[NUM_LEAF_BINS];
    
    Node* m_nodes;
    uint32* m_freeNodes;
    uint32 m_freeOffset;
};

void testAllocator()
{
    OffsetAllocator allocator(1024 * 1024 * 256);
    OffsetAllocator::Allocation a = allocator.allocate(1337);
    uint32 offset = a.offset;
    allocator.free(a);
}
