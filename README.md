# OffsetAllocator
Fast hard realtime O(1) offset allocator with minimal fragmentation. 

Uses 256 bins with 8 bit floating point distribution (3 bit mantissa + 5 bit exponent) and a two level bitfield to find the next available bin using 2x LZCNT instructions to make all operations O(1). Bin sizes following the floating point distribution ensures hard bounds for memory overhead percentage regarless of size class. Pow2 bins would waste up to +100% memory (+50% on average). Our float bins waste up to +12.5% (+6.25% on average).

The allocation metadata is stored in a separate data structure, making this allocator suitable for sub-allocating any resources, such as GPU heaps, buffers and arrays. Returns an offset to the first element of the allocated contiguous range.

**Bin size table:**
```
0->0 1->1 2->2 3->3 4->4 5->5 6->6 7->7 
8->8 9->9 10->10 11->11 12->12 13->13 14->14 15->15 
16->16 17->18 18->20 19->22 20->24 21->26 22->28 23->30 
24->32 25->36 26->40 27->44 28->48 29->52 30->56 31->60 
32->64 33->72 34->80 35->88 36->96 37->104 38->112 39->120 
40->128 41->144 42->160 43->176 44->192 45->208 46->224 47->240 
48->256 49->288 50->320 51->352 52->384 53->416 54->448 55->480 
56->512 57->576 58->640 59->704 60->768 61->832 62->896 63->960 
64->1024 65->1152 66->1280 67->1408 68->1536 69->1664 70->1792 71->1920 
72->2048 73->2304 74->2560 75->2816 76->3072 77->3328 78->3584 79->3840 
80->4096 81->4608 82->5120 83->5632 84->6144 85->6656 86->7168 87->7680 
88->8192 89->9216 90->10240 91->11264 92->12288 93->13312 94->14336 95->15360 
96->16384 97->18432 98->20480 99->22528 100->24576 101->26624 102->28672 103->30720 
104->32768 105->36864 106->40960 107->45056 108->49152 109->53248 110->57344 111->61440 
112->65536 113->73728 114->81920 115->90112 116->98304 117->106496 118->114688 119->122880 
120->131072 121->147456 122->163840 123->180224 124->196608 125->212992 126->229376 127->245760 
128->262144 129->294912 130->327680 131->360448 132->393216 133->425984 134->458752 135->491520 
136->524288 137->589824 138->655360 139->720896 140->786432 141->851968 142->917504 143->983040 
144->1048576 145->1179648 146->1310720 147->1441792 148->1572864 149->1703936 150->1835008 151->1966080 
152->2097152 153->2359296 154->2621440 155->2883584 156->3145728 157->3407872 158->3670016 159->3932160 
160->4194304 161->4718592 162->5242880 163->5767168 164->6291456 165->6815744 166->7340032 167->7864320 
168->8388608 169->9437184 170->10485760 171->11534336 172->12582912 173->13631488 174->14680064 175->15728640 
176->16777216 177->18874368 178->20971520 179->23068672 180->25165824 181->27262976 182->29360128 183->31457280 
184->33554432 185->37748736 186->41943040 187->46137344 188->50331648 189->54525952 190->58720256 191->62914560 
192->67108864 193->75497472 194->83886080 195->92274688 196->100663296 197->109051904 198->117440512 199->125829120 
200->134217728 201->150994944 202->167772160 203->184549376 204->201326592 205->218103808 206->234881024 207->251658240 
208->268435456 209->301989888 210->335544320 211->369098752 212->402653184 213->436207616 214->469762048 215->503316480 
216->536870912 217->603979776 218->671088640 219->738197504 220->805306368 221->872415232 222->939524096 223->1006632960 
224->1073741824 225->1207959552 226->1342177280 227->1476395008 228->1610612736 229->1744830464 230->1879048192 231->2013265920 
232->2147483648 233->2415919104 234->2684354560 235->2952790016 236->3221225472 237->3489660928 238->3758096384 239->4026531840
```

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
