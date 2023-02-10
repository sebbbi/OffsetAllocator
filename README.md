# OffsetAllocator
Fast hard realtime O(1) offset allocator with minimal fragmentation. 

Uses 256 bins with (3 bit mantissa + 5 bit exponent) floating point distribution and a two level bitfield to find the bin at O(1) using 2x LZCNT instructions.

## Disclaimer
Early one weekend prototype. Unit tests are green, but coverage is still not 100%. Use at your own risk!

## License
MIT license (see file: LICENSE)
