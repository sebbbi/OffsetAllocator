// (C) Sebastian Aaltonen 2023
// MIT License (see file: LICENSE)

namespace OffsetAllocator
{
    typedef unsigned char uint8;
    typedef unsigned int uint32;

    struct Allocation
    {
        static constexpr uint32 NO_SPACE = 0xffffffff;
        
        uint32 offset;
        uint32 metadata; // internal: node index
    };

    class Allocator
    {
    public:
        static constexpr uint32 NUM_TOP_BINS = 32;
        static constexpr uint32 BINS_PER_LEAF = 8;
        static constexpr uint32 TOP_BINS_INDEX_SHIFT = 3;
        static constexpr uint32 LEAF_BINS_INDEX_MASK = 0x7;
        static constexpr uint32 NUM_LEAF_BINS = NUM_TOP_BINS * BINS_PER_LEAF;
        
        Allocator(uint32 size, uint32 maxAllocs = 128 * 1024);
        ~Allocator();
        
        Allocation allocate(uint32 size);
        void free(Allocation allocation);
        
    private:
        uint32 insertNodeIntoBin(uint32 size, uint32 dataOffset);
        void removeNodeFromBin(uint32 nodeIndex);

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
        
        uint32 m_usedBinsTop;
        uint8 m_usedBins[NUM_TOP_BINS];
        uint32 m_binIndices[NUM_LEAF_BINS];
        
        Node* m_nodes;
        uint32* m_freeNodes;
        uint32 m_freeOffset;
    };
}
