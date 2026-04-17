
#ifndef _COMPAT_LINUX_H
#define _COMPAT_LINUX_H

#include <types.h>
#include <kernel/idt.h>


#define LINUX_SYS_EXIT            1
#define LINUX_SYS_FORK            2
#define LINUX_SYS_READ            3
#define LINUX_SYS_WRITE           4
#define LINUX_SYS_OPEN            5
#define LINUX_SYS_CLOSE           6
#define LINUX_SYS_GETPID         20
#define LINUX_SYS_ACCESS         33
#define LINUX_SYS_BRK            45
#define LINUX_SYS_IOCTL          54
#define LINUX_SYS_MUNMAP         91
#define LINUX_SYS_STAT          106
#define LINUX_SYS_LSTAT         107
#define LINUX_SYS_FSTAT         108
#define LINUX_SYS_UNAME         122
#define LINUX_SYS_MMAP2         192
#define LINUX_SYS_FSTAT64       197
#define LINUX_SYS_SET_THREAD_AREA 243
#define LINUX_SYS_EXIT_GROUP    252
#define LINUX_SYS_SET_TID_ADDR  258
#define LINUX_SYS_CLOCK_GETTIME 265


#define LINUX_EPERM    1
#define LINUX_ENOENT   2
#define LINUX_ECHILD  10
#define LINUX_EBADF    9
#define LINUX_ENOMEM  12
#define LINUX_EEXIST  17
#define LINUX_ENOTDIR 20
#define LINUX_ENOTTY  25
#define LINUX_ERANGE  34
#define LINUX_EINVAL  22
#define LINUX_ENOSYS  38


typedef void (*linux_output_fn)(char c);
void linux_compat_set_output(linux_output_fn fn);


void linux_compat_init(void);


void linux_syscall_handler(registers_t *regs);

#endif 
