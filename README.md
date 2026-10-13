# OS Lab Assignment 7 — xv6-riscv

This repository contains the implementation of OS Lab Assignment 7 using the xv6-riscv operating system.

## Questions

- **Q1 – Race Condition:** Demonstrates race conditions using a shared counter and synchronization.
- **Q2 – Semaphores:** Implements semaphore-based synchronization and the Producer–Consumer problem.
- **Q3 – Deadlock Detection:** Implements deadlock detection, Banker's Algorithm, and a pipe-based deadlock demonstration.
- **Q4 – Priority Scheduling:** Implements priority-based preemptive scheduling with aging to prevent starvation.

## Running

Build and launch xv6 from the project directory:

```bash
make qemu
```

Run the relevant test program from the xv6 shell:

```text
race
synctest
deadlock
priotest
```
