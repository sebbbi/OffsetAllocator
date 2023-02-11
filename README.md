# OffsetAllocator
Fast hard realtime O(1) offset allocator with minimal fragmentation. 

Uses 256 bins with (3 bit mantissa + 5 bit exponent) floating point distribution and a two level bitfield to find the next available bin using 2x LZCNT instructions.

The allocation metadata is stored in a separate data structure, making this allocator suitable for sub-allocating any resources, such as GPU heaps, buffers and arrays. Returns an offset to the first element of the allocated contiguous range.

## Integration
CMakeLists.txt exists for cmake folder include. Alternatively, just copy the OffsetAllocator.cpp and OffsetAllocator.hpp in your project. No other files are needed.

## How to use

```
#include "offsetAllocator.hpp"
using namespace OffsetAllocator;

Allocator allocator(12345);                 // Allocator with 12345 contiguous elements in total

Allocation a = allocator.allocate(1337);    // Allocate a 1337 element contiguous range
uint32 offset_a = a.offset;                 // Provides offset to the first element of the range
do_something(offset_a);

Allocation b = allocator.allocate(123);     // Allocate a 123 element contiguous range
uint32 offset_b = b.offset;                 // Provides offset to the first element of the range
do_something(offset_b);

allocator.free(a);                          // Free allocation a
allocator.free(b);                          // Free allocation b
```

## References
This allocator is similar to the two-level segregated fit (TLSF) algorithm. 

**Comparison paper shows that TLSF algorithm provides best in class performance and fragmentation:**
https://www.researchgate.net/profile/Alfons-Crespo/publication/234785757_A_comparison_of_memory_allocators_for_real-time_applications/links/5421d8550cf2a39f4af765f4/A-comparison-of-memory-allocators-for-real-time-applications.pdf

## Disclaimer
Early one weekend prototype. Unit tests are green, but coverage is still not 100%. Use at your own risk!

## License
MIT license (see file: LICENSE)
