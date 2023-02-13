#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include "gfxTestFixture.hpp"

#include "offsetAllocator.hpp"

using namespace f;

namespace OffsetAllocator
{
    namespace SmallFloat
    {
        extern uint32 uintToFloatRoundUp(uint32 size);
        extern uint32 uintToFloatRoundDown(uint32 size);
    extern uint32 floatToUint(uint32 floatValue);
    }
}

namespace offsetAllocatorTests
{
    TEST_CASE("numbers", "[SmallFloat]")
    {
        SECTION("uintToFloat")
        {
            // Denorms, exp=1 and exp=2 + mantissa = 0 are all precise.
            // NOTE: Assuming 8 value (3 bit) mantissa.
            // If this test fails, please change this assumption!
            uint32 preciseNumberCount = 17;
            for (uint32 i = 0; i < preciseNumberCount; i++)
            {
                uint32 roundUp = OffsetAllocator::SmallFloat::uintToFloatRoundUp(i);
                uint32 roundDown = OffsetAllocator::SmallFloat::uintToFloatRoundDown(i);
                REQUIRE(i == roundUp);
                REQUIRE(i == roundDown);
            }
            
            // Test some random picked numbers
            struct NumberFloatUpDown
            {
                uint32 number;
                uint32 up;
                uint32 down;
            };
            
            NumberFloatUpDown testData[] = {
                {.number = 17, .up = 17, .down = 16},
                {.number = 118, .up = 39, .down = 38},
                {.number = 1024, .up = 64, .down = 64},
                {.number = 65536, .up = 112, .down = 112},
                {.number = 529445, .up = 137, .down = 136},
                {.number = 1048575, .up = 144, .down = 143},
            };
            
            for (uint32 i = 0; i < sizeof(testData) / sizeof(NumberFloatUpDown); i++)
            {
                NumberFloatUpDown v = testData[i];
                uint32 roundUp = OffsetAllocator::SmallFloat::uintToFloatRoundUp(v.number);
                uint32 roundDown = OffsetAllocator::SmallFloat::uintToFloatRoundDown(v.number);
                REQUIRE(roundUp == v.up);
                REQUIRE(roundDown == v.down);
            }
        }
        
        SECTION("floatToUint")
        {
            // Denorms, exp=1 and exp=2 + mantissa = 0 are all precise.
            // NOTE: Assuming 8 value (3 bit) mantissa.
            // If this test fails, please change this assumption!
            uint32 preciseNumberCount = 17;
            for (uint32 i = 0; i < preciseNumberCount; i++)
            {
                uint32 v = OffsetAllocator::SmallFloat::floatToUint(i);
                REQUIRE(i == v);
            }
            
            // Test that float->uint->float conversion is precise for all numbers
            // NOTE: Test values < 240. 240->4G = overflows 32 bit integer
            for (uint32 i = 0; i < 240; i++)
            {
                uint32 v = OffsetAllocator::SmallFloat::floatToUint(i);
                uint32 roundUp = OffsetAllocator::SmallFloat::uintToFloatRoundUp(v);
                uint32 roundDown = OffsetAllocator::SmallFloat::uintToFloatRoundDown(v);
                REQUIRE(i == roundUp);
                REQUIRE(i == roundDown);
                //if ((i%8) == 0) printf("\n");
                //printf("%u->%u ", i, v);
            }
        }
    }

    TEST_CASE("basic", "[offsetAllocator]")
    {
        OffsetAllocator::Allocator allocator(1024 * 1024 * 256);
        OffsetAllocator::Allocation a = allocator.allocate(1337);
        uint32 offset = a.offset;
        REQUIRE(offset == 0);
        allocator.free(a);
    }

    TEST_CASE("allocate", "[offsetAllocator]")
    {
        OffsetAllocator::Allocator allocator(1024 * 1024 * 256);

        SECTION("simple")
        {
            // Free merges neighbor empty nodes. Next allocation should also have offset = 0
            OffsetAllocator::Allocation a = allocator.allocate(0);
            REQUIRE(a.offset == 0);
            
            OffsetAllocator::Allocation b = allocator.allocate(1);
            REQUIRE(b.offset == 0);

            OffsetAllocator::Allocation c = allocator.allocate(123);
            REQUIRE(c.offset == 1);

            OffsetAllocator::Allocation d = allocator.allocate(1234);
            REQUIRE(d.offset == 124);

            allocator.free(a);
            allocator.free(b);
            allocator.free(c);
            allocator.free(d);
            
            // End: Validate that allocator has no fragmentation left. Should be 100% clean.
            OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
            REQUIRE(validateAll.offset == 0);
            allocator.free(validateAll);
        }

        SECTION("merge trivial")
        {
            // Free merges neighbor empty nodes. Next allocation should also have offset = 0
            OffsetAllocator::Allocation a = allocator.allocate(1337);
            REQUIRE(a.offset == 0);
            allocator.free(a);
            
            OffsetAllocator::Allocation b = allocator.allocate(1337);
            REQUIRE(b.offset == 0);
            allocator.free(b);
            
            // End: Validate that allocator has no fragmentation left. Should be 100% clean.
            OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
            REQUIRE(validateAll.offset == 0);
            allocator.free(validateAll);
        }
        
        SECTION("reuse trivial")
        {
            // Allocator should reuse node freed by A since the allocation C fits in the same bin (using pow2 size to be sure)
            OffsetAllocator::Allocation a = allocator.allocate(1024);
            REQUIRE(a.offset == 0);

            OffsetAllocator::Allocation b = allocator.allocate(3456);
            REQUIRE(b.offset == 1024);

            allocator.free(a);
            
            OffsetAllocator::Allocation c = allocator.allocate(1024);
            REQUIRE(c.offset == 0);

            allocator.free(c);
            allocator.free(b);
            
            // End: Validate that allocator has no fragmentation left. Should be 100% clean.
            OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
            REQUIRE(validateAll.offset == 0);
            allocator.free(validateAll);
        }

        SECTION("reuse complex")
        {
            // Allocator should not reuse node freed by A since the allocation C doesn't fits in the same bin
            // However node D and E fit there and should reuse node from A
            OffsetAllocator::Allocation a = allocator.allocate(1024);
            REQUIRE(a.offset == 0);

            OffsetAllocator::Allocation b = allocator.allocate(3456);
            REQUIRE(b.offset == 1024);

            allocator.free(a);
            
            OffsetAllocator::Allocation c = allocator.allocate(2345);
            REQUIRE(c.offset == 1024 + 3456);

            OffsetAllocator::Allocation d = allocator.allocate(456);
            REQUIRE(d.offset == 0);

            OffsetAllocator::Allocation e = allocator.allocate(512);
            REQUIRE(e.offset == 456);

            OffsetAllocator::StorageReport report = allocator.storageReport();
            REQUIRE(report.totalFreeSpace == 1024 * 1024 * 256 - 3456 - 2345 - 456 - 512);
            REQUIRE(report.largestFreeRegion != report.totalFreeSpace);

            allocator.free(c);
            allocator.free(d);
            allocator.free(b);
            allocator.free(e);
            
            // End: Validate that allocator has no fragmentation left. Should be 100% clean.
            OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
            REQUIRE(validateAll.offset == 0);
            allocator.free(validateAll);
        }
        
        SECTION("zero fragmentation")
        {
            // Allocate 256x 1MB. Should fit. Then free four random slots and reallocate four slots.
            // Plus free four contiguous slots an allocate 4x larger slot. All must be zero fragmentation!
            OffsetAllocator::Allocation allocations[256];
            for (uint i = 0; i < 256; i++)
            {
                allocations[i] = allocator.allocate(1024 * 1024);
                REQUIRE(allocations[i].offset == i * 1024 * 1024);
            }

            OffsetAllocator::StorageReport report = allocator.storageReport();
            REQUIRE(report.totalFreeSpace == 0);
            REQUIRE(report.largestFreeRegion == 0);

            // Free four random slots
            allocator.free(allocations[243]);
            allocator.free(allocations[5]);
            allocator.free(allocations[123]);
            allocator.free(allocations[95]);

            // Free four contiguous slot (allocator must merge)
            allocator.free(allocations[151]);
            allocator.free(allocations[152]);
            allocator.free(allocations[153]);
            allocator.free(allocations[154]);

            allocations[243] = allocator.allocate(1024 * 1024);
            allocations[5] = allocator.allocate(1024 * 1024);
            allocations[123] = allocator.allocate(1024 * 1024);
            allocations[95] = allocator.allocate(1024 * 1024);
            allocations[151] = allocator.allocate(1024 * 1024 * 4); // 4x larger
            REQUIRE(allocations[243].offset != OffsetAllocator::Allocation::NO_SPACE);
            REQUIRE(allocations[5].offset != OffsetAllocator::Allocation::NO_SPACE);
            REQUIRE(allocations[123].offset != OffsetAllocator::Allocation::NO_SPACE);
            REQUIRE(allocations[95].offset != OffsetAllocator::Allocation::NO_SPACE);
            REQUIRE(allocations[151].offset != OffsetAllocator::Allocation::NO_SPACE);

            for (uint i = 0; i < 256; i++)
            {
                if (i < 152 || i > 154)
                    allocator.free(allocations[i]);
            }
            
            OffsetAllocator::StorageReport report2 = allocator.storageReport();
            REQUIRE(report2.totalFreeSpace == 1024 * 1024 * 256);
            REQUIRE(report2.largestFreeRegion == 1024 * 1024 * 256);

            // End: Validate that allocator has no fragmentation left. Should be 100% clean.
            OffsetAllocator::Allocation validateAll = allocator.allocate(1024 * 1024 * 256);
            REQUIRE(validateAll.offset == 0);
            allocator.free(validateAll);
        }
    }
}
