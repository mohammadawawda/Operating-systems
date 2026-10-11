# User level threads

`uthreads.cpp` is a static library (`libuthreads.a`) for user level threads that all run inside one process:

* every thread gets its own stack, and its context is saved and loaded with `sigsetjmp` and `siglongjmp`
* round robin scheduling, every thread runs for one quantum of virtual time, the virtual timer sends `SIGVTALRM` when it ends
* threads can be spawned, terminated, blocked, resumed and put to sleep for some quantums
* a new thread gets the smallest free id, the main thread is 0
* `SIGVTALRM` is masked while the library changes its lists, so the timer can't stop it in the middle

`translate_address` at the top of the file is not my code.

## How to build

Put `uthreads.h` (the interface header, not included here) next to `uthreads.cpp` and run `make`, it builds `libuthreads.a`.

## Notes

**A general use of user level threads, and why they fit it**

If we want to manage threads in a custom and controlled manner, for light tasks that do not heavily rely on system calls, user level threads may be the way to go, both because they have low overhead during context switch, but mainly due to the fact that we can tailor thread managing and switching as we wish, since it's not up to the OS to decide, which actually isn't aware of their existence. This advantage can give us consistency in thread management across operating systems.

**Chrome creates a new process for every tab, the advantages and disadvantages compared to kernel level threads**

Advantages:

* Tab architecture is more suited to different processes, since each tab is a standalone implementation of an interface with which a user can access the internet, whereas threads are designed to simultaneously complete related tasks, and therefore share memory and other resources.
* Lower potential for synchronization issues. Splitting tabs to processes means that they have less or no shared memory with each other.
* Each tab is more independent, which means if there is a problem with one, such as a thrown exception that causes the OS to close the process, the rest can remain intact.
* In addition to the previous advantage, this separation could add a level of security. The less shared memory and communication, the less weak spots for malware to intervene.

Disadvantages:

* Bigger overhead during context switch, i.e. when switching tabs.
* Communication between tabs is more limited compared to threads which have shared memory.

**The interrupts and signals involved in killing an application with `kill pid` from a shell**

When we type in the command, we cause keyboard generated hardware interrupts as we type. As we enter the command (also a hardware interrupt), the command creates a software interrupt, a trap, in the form of a system call, which prompts the OS to send a SIGTERM signal to the application, which handles the signal as it has defined, or by the default handling defined by the OS.

**The difference between real and virtual time, with an example of each**

To our understanding, virtual time is a time measurement paradigm that is used to cope with synchronization in a virtual system. Since most virtual systems do not execute in a truly linear fashion, that is, a set of commands is initiated and completed and only afterwards another set can be executed, but rather with interrupts that could shift in the midst of an executed set of commands, we need a non real time method to measure the execution of the set, as if it were not interrupted, to enable us to more accurately synchronize across the system.

If we wanted to measure the time it takes to open a certain application, we would want to do so in real time, because for this purpose the virtual time is not very informative. However, if this application included certain different components that need to operate together in a certain order, we would need to synchronize them using virtual time, that would account for all interrupts occurring during execution, and can give us a trusted indication to where each component is along its execution.

**What sigsetjmp and siglongjmp do**

* `sigsetjmp` saves the current environment of execution, which includes details such as PC, SP and masked signals. Additionally, it marks the location to jump to from `siglongjmp` (since this is where the environment is saved). The dual intent is decided by the return value, which is 0 for the first, and customizable for the second.
* `siglongjmp` jumps to a given environment, saved by `sigsetjmp`, with a given return code.
