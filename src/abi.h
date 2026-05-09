#ifndef ABI_H
#define ABI_H

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_ERRNO   4
#define SYS_FORK    57
#define SYS_YIELD   24
#define SYS_KILL    62
#define SYS_BRK     12
#define SYS_EXEC    59
#define SYS_EXIT    60

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

#define PEANUT_SIGINT   2
#define PEANUT_SIGKILL  9
#define PEANUT_SIGSEGV 11

#define PEANUT_USER_CS   0x23
#define PEANUT_USER_SS   0x1B
#define PEANUT_USER_RFLAGS_IF 0x202

#endif
