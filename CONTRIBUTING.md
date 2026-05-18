# Contributing to Peanut Kernel

Hello, dear Peanut Kernel contributor!

We appreciate your interest in contributing to our magnificent pile of questionable C code. Before submitting changes, please follow these rules to keep development organized and maintainable.

# I. Contributing rules

  ## 1. No Rust

  Unfortunately, Rust is not allowed in this project. The kernel is written entirely in C, and contributions should follow that standard.

  ## 2. Always Base Your Work on the Latest `main` Commit

  Before creating new commits, make sure your branch is up to date with the latest commit from the `main` branch. This helps reduce conflicts and keeps      development consistent.

  ## 3. Never Commit Directly to `main`

  All contributions must be committed to the `experimental` branch first.

  Once submitted, the changes will be reviewed and tested by our volunteers. After evaluation, one of the following actions will be taken:

  * **Accepted:** The contribution is considered stable and will be merged into `main`.
  * **Needs Improvement:** The contribution may require bug fixes, cleanup, documentation, or additional testing before being merged.
  * **Rejected:** If the contribution is not suitable for the kernel, it will be removed from the `experimental` branch.

  ## 4. Exception to Rule 3

  The only exception to the previous rule is for urgent fixes involving severe security vulnerabilities or critical crash issues, such as any kernel  segmentation/page faults, triple faults, or easily exploitable bugs.

## II. How to set up the build environment 

    You don't need much to start! All you need is:

    1. GNU C Compiler (GCC)
    2. Binutils
    3. The Netwide Assembler (NASM)
    4. Git
    5. libncurses-dev 
    6. Kconfig-mconf & Kconfig-conf (in /usr/bin)
---
    
