# OffsetAllocator
Fast hard realtime O(1) offset allocator with minimal fragmentation. 

Uses 256 bins with (3 bit mantissa + 5 bit exponent) floating point distribution and a two level bitfield to find the next available bin using 2x LZCNT instructions.

The allocation metadata is stored in a separate data structure, making this allocator suitable for sub-allocating any resources, such as GPU heaps, buffers and arrays. Returns an offset to the first element of the allocated contiguous range.

## Disclaimer
Early one weekend prototype. Unit tests are green, but coverage is still not 100%. Use at your own risk!

## License
MIT license (see file: LICENSE)
