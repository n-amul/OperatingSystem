# UNIX Utilities and xv6 Projects

This repository contains multiple projects developed as part of an operating system course. Each project focuses on different aspects of UNIX utilities, xv6 system calls, and shell development. Below is an overview of the projects included in this repository:

## P1: UNIX Utilities

### Overview
In this project, we implemented two essential UNIX utilities:
- **wisc-sed:** A simplified version of the `sed` tool, which is used for searching, editing, replacing, and deleting patterns in files.
- **wisc-tar:** A simplified version of the `tar` utility, used to combine and compress a collection of files into a single file.

### Objectives
- Re-familiarize with the C programming language.
- Learn the basics of implementing UNIX utilities.
- Practice compiling C programs and executing binaries from the command line.

### Usage
- **wisc-sed**: 
  - Replace a string in a file: 
    ```sh
    ./wisc-sed -c -s <search_string> -r <replacement_string> -f <filename>
    ```
- **wisc-tar**: 
  - Combine files into a tarball: 
    ```sh
    ./wisc-tar <output_tar_filename> <list_of_files>
    ```

## P2: Adding Syscalls to xv6

### Overview
This project involved adding new system calls to the educational operating system, xv6:
- **fsetoff(int fd, int pos):** Sets the file offset.
- **fgetoff(int fd):** Gets the current file offset.

### Objectives
- Gain experience with xv6 and its codebase.
- Learn how to add system calls to an existing operating system.
- Practice using the gdb debugger on xv6.

### Usage
- **fsetoff(int fd, int pos):** 
  ```c
  fsetoff(file_descriptor, position);
  ```
- **fgetoff(int fd):**
  ```c
  int offset = fgetoff(file_descriptor);
  ```

## P3: UNIX Shell

### Overview
This project involved creating a simple UNIX shell similar to `bash` and `sh`, named **wish** (Wisconsin Shell). The shell supports:
- Interactive and batch modes of execution.
- Command aliases.
- Environment variable management.
- Output redirection.

### Objectives
- Learn about process creation, management, and termination.
- Implement a custom shell in C.
- Practice handling input and output redirection in a shell.

### Usage
- **Running in interactive mode:** 
  ```sh
  ./wish
  ```
- **Running in batch mode:** 
  ```sh
  ./wish batch-file
  ```

## P4: xv6 Scheduler

### Overview
This project focused on modifying the xv6 scheduler to implement a compensating lottery scheduler, which uses a lottery system to determine process scheduling.

### Objectives
- Understand and implement a lottery scheduling algorithm.
- Modify and extend an existing operating system’s scheduler.

### Deliverables
- Modified xv6 source code with the lottery scheduler implemented.

### Usage
- The new scheduler is integrated into the xv6 kernel. The system calls are:
  - `settickets(int pid, int tickets)`: Sets the number of tickets for a process.
  - `srand(uint seed)`: Seeds the random number generator.
  - `getpinfo(struct pstat *)`: Retrieves process information.

## P5: xv6 Memory Mapping

### Overview
This project involved implementing memory mapping in xv6 by adding support for `wmap()` and `wunmap()` system calls, similar to `mmap()` and `munmap()` in Linux.

### Objectives
- Understand and implement memory mapping in an operating system.
- Learn about demand paging and memory management.
- Extend the xv6 kernel to support memory mapping.

### Usage
- **wmap**: 
  ```c
  uint address = wmap(addr, length, flags);
  ```
- **wunmap**: 
  ```c
  int result = wunmap(addr);
  ```

## P6: Concurrency (Map-Reduce)

### Overview
In this project, we implemented a simplified MapReduce framework for single-machine use, focusing on concurrency and efficient parallel processing.

### Objectives
- Understand the MapReduce paradigm.
- Implement a concurrent MapReduce framework using threads.
- Gain experience with designing concurrent data structures.

### Usage
  ```c
  MR_Run(argc, argv, Map, num_mappers, Reduce, num_reducers, MR_DefaultHashPartition, num_partitions);
  ```
