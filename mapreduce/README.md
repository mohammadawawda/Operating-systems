# MapReduce

A multithreaded MapReduce framework, built as a static library (`libMapReduceFramework.a`). The client writes only `map` and `reduce`, and the framework runs them on the input with as many threads as the client asks for. Every thread goes through these stages:

* map: takes the next input element with an atomic counter and calls `map` on it, the pairs it emits go to the thread's own vector so there are no races, and in the end the thread sorts its vector
* a barrier, so the shuffle starts only after all threads finished sorting
* shuffle (only thread 0): merges the sorted vectors with a priority queue into one vector for every key
* another barrier
* reduce: takes the next key with an atomic counter and calls `reduce` on its vector, the output pairs are added under a mutex

The state of a job (the stage, how many elements were processed and how many there are) is kept in one 64 bit atomic number, so `getJobState` reads it once and the stage and the percentage always match. Everything a job uses is kept in its own `JobContext`, so more than one job can run at the same time, and `waitForJob` can be called more than once.

`Barrier.h` and `Barrier.cpp` are a simple barrier that is not my code.

## How to build

Put `MapReduceClient.h` and `MapReduceFramework.h` (the interface headers, not included here) next to the code and run `make`, it builds `libMapReduceFramework.a`.
