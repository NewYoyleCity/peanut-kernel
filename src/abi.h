/* abi.h -- System call numbers and user-space ABI constants.
 *
 * Defines syscall numbers (SYS_READ, SYS_WRITE, etc.), network
 * syscalls, power-management syscalls, and segment selector values
 * used when switching to/from user space.
 */

#ifndef ABI_H
#define ABI_H

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_ERRNO   4

// Extended general syscalls
#define SYS_IOCTL       16
#define SYS_STAT        17
#define SYS_FSTAT       18
#define SYS_DUP         32
#define SYS_NANOSLEEP   35
#define SYS_GETPID      39
#define SYS_FORK        57
#define SYS_EXEC        59
#define SYS_EXIT        60
#define SYS_KILL        62
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_MKDIR       83
#define SYS_GETTIMEOFDAY 96
#define SYS_UNAME       160
#define SYS_REBOOT      169
#define SYS_YIELD       24
#define SYS_BRK         12

// Network syscalls
#define SYS_SOCKET  41
#define SYS_BIND    49
#define SYS_LISTEN  50
#define SYS_ACCEPT  43
#define SYS_CONNECT 42
#define SYS_SEND    44
#define SYS_RECV    45
#define SYS_SENDTO  46
#define SYS_RECVFROM 47
#define SYS_SHUTDOWN 48
#define SYS_GETSOCKNAME 51
#define SYS_GETPEERNAME 52
#define SYS_SETSOCKOPT 54
#define SYS_GETSOCKOPT 55

// DNS syscall
#define SYS_GETADDRINFO 56

// Power management syscalls
#define SYS_REBOOT2     136
#define SYS_POWEROFF    137

#define PEANUT_SIGHUP    1
#define PEANUT_SIGINT    2
#define PEANUT_SIGQUIT   3
#define PEANUT_SIGILL    4
#define PEANUT_SIGTRAP   5
#define PEANUT_SIGABRT   6
#define PEANUT_SIGBUS    7
#define PEANUT_SIGFPE    8
#define PEANUT_SIGKILL   9
#define PEANUT_SIGUSR1  10
#define PEANUT_SIGSEGV  11
#define PEANUT_SIGUSR2  12
#define PEANUT_SIGPIPE  13
#define PEANUT_SIGALRM  14
#define PEANUT_SIGTERM  15
#define PEANUT_SIGSTKFLT 16
#define PEANUT_SIGCHLD  17
#define PEANUT_SIGCONT  18
#define PEANUT_SIGSTOP  19
#define PEANUT_SIGTSTP  20
#define PEANUT_SIGTTIN  21
#define PEANUT_SIGTTOU  22
#define PEANUT_SIGURG   23
#define PEANUT_SIGXCPU  24
#define PEANUT_SIGXFSZ  25
#define PEANUT_SIGVTALRM 26
#define PEANUT_SIGPROF  27
#define PEANUT_SIGWINCH 28
#define PEANUT_SIGIO    29
#define PEANUT_SIGPWR   30
#define PEANUT_SIGSYS   31

#define PEANUT_SIG_TERM  0
#define PEANUT_SIG_CORE  1
#define PEANUT_SIG_IGN   2
#define PEANUT_SIG_STOP  3
#define PEANUT_SIG_CONT  4

#define PEANUT_USER_CS   0x23
#define PEANUT_USER_SS   0x1B
#define PEANUT_USER_RFLAGS_IF 0x202

#endif
