# Virtual memory

`VirtualMemory.cpp` is a static library (`libVirtualMemory.a`) that gives a virtual memory (`VMinitialize`, `VMread`, `VMwrite`) with hierarchical page tables, on top of a simulated physical memory (`PMread`, `PMwrite`, `PMevict`, `PMrestore`) that is not my code.

The code uses no global variables and no dynamic allocation, so the page tables live in the frames of the physical memory itself, and the root table is always in frame 0. A row with 0 means that the table or page under it is not in memory, and then a frame for it is found by going over the whole tree, in this order:

1. a frame that holds an empty table (all zeros), it gets unlinked from its parent
2. an unused frame, the one right after the biggest frame index in the tree
3. if all frames are used, the page with the biggest cyclic distance from the page we need is evicted

The memory sizes and the depth of the tree come from `MemoryConstants.h`.

## How to build

Put `VirtualMemory.h`, `PhysicalMemory.h` and `MemoryConstants.h` (the interface headers, not included here) next to the code and run `make`, it builds `libVirtualMemory.a`. A program that uses it also needs the `PhysicalMemory.cpp` simulation.
