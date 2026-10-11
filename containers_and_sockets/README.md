# Containers and sockets

## container

`container.cpp` makes a small container and runs a program inside it:

```
sudo ./container <new_hostname> <new_filesystem_directory> <num_processes> <path_to_program_to_run_within_container> [program_argument]
```

It starts the program with `clone` in new UTS, PID and mount namespaces. Inside, it sets the new hostname, changes the root to the new filesystem with `chroot`, mounts a new `/proc` and runs the program (for example `/bin/bash`), so in the container the program is process 1 and sees only the processes of the container. When it finishes, the `/proc` of the container is unmounted. Limiting the number of processes with cgroups was not done, so `num_processes` is not used.

For the new filesystem we used the Ubuntu base image from http://cdimage.ubuntu.com/ubuntu-base/releases/14.04/release/ubuntu-base-14.04-core-amd64.tar.gz

## sockets

`sockets.cpp` runs a server or a client on the given port of this computer:

```
./sockets server <port>
./sockets client <port> <terminal_command_to_run>
```

The client sends the command to the server in a 256 byte buffer, and the server runs it with `system`. Up to 5 clients can wait for the server.

## How to build

Run `make`, it builds `container` and `sockets`. The container has to run as root.

## Notes

**A general use of Linux pid namespaces**

Namespaces can be used to limit the scope of a process, that is, limit which files are available to the process.

**How Linux mount namespaces can help isolate a process**

Mount namespaces define which filesystem the process uses. If we create a new filesystem, and direct the process's filesystem to this newly created one, we can isolate the process from its father's filesystem, meaning it's not aware of its existence.

**A general use of Linux cgroups**

cgroups allows us to control what a process created in a container can do. For example, limit the maximal number of processes it can create, limit the process's use of memory, etc.

**What clone does, and how it is different from fork**

The clone command enables us to create a new process as a child of the caller. The difference between it and fork is that with clone we can control the context of the new process, using namespaces and control groups, that is, we can control what the process will be able to see and do.

**What chroot does**

The chroot command enables us to change the root directory `/` of the calling process.

**The purpose of procfs, with an example of its use**

The proc file system contains information regarding active processes created in its context. The default location of the filesystem is `/proc` (in the current context). An example for information it contains is memory mappings of active processes (pages to frames).
