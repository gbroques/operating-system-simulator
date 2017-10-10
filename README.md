# Semaphores and Operating System Simulator
This program creates an operating system simulator that forks child processes, maintains a simulated system clock, and produces a log file of when each child process terminates.

Child processes randomly terminate within 1 to 1,000,000 nano seconds of being created.

## How to Build and Run
1. Clone or download the project

Within the root of the project:

2. Run `make`
3. Run `oss`

## Arguments
```
 -h  Show help.
 -s  The maximum number of slave processes spawned. Defaults to 5.
 -l  Specify the log file. Defaults to 'oss.out'.
 -t  Time in seconds master will terminate itself and all children. Defaults to 20.
 -m  Simulated time in seconds master will terminate itself and all children. Defaults to 2.
 ```

Read `cs4760Assignment3Fall2017Hauschild.pdf` for more details.
