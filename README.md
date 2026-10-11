# Operating Systems

Operating systems projects in C++ for Linux.

* [measuring_operation_times](measuring_operation_times) measures how long an addition, an empty function call and a trap take, on the computer, in a VM and in a container
* [user_level_threads](user_level_threads) a static library of user level threads with a round robin scheduler
* [mapreduce](mapreduce) a multithreaded MapReduce framework with pthreads
* [virtual_memory](virtual_memory) virtual memory with hierarchical page tables on a simulated physical memory
* [containers_and_sockets](containers_and_sockets) a small container (its own hostname, process ids, root directory and proc) and a client and server that talk over sockets

The interface headers (like `osm.h`, `uthreads.h` and `MapReduceFramework.h`) are not mine, so they are not here. To build a project put its headers in its folder and run `make`.
