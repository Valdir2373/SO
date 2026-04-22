#ifndef _LINUX_COMPAT64_H
#define _LINUX_COMPAT64_H

#include <types.h>


typedef struct {
    uint64_t rax;   
    uint64_t rdi;   
    uint64_t rsi;   
    uint64_t rdx;   
    uint64_t r10;   
    uint64_t r8;    
    uint64_t r9;    
    uint64_t rcx;   
    uint64_t r11;   
} syscall64_frame_t;

void linux_syscall64_init(void);
void linux_syscall64_handler(syscall64_frame_t *f);


int  lx64_register_kernel_service(const char *path);
int  lx64_ksvc_read(int svc_idx, void *buf, uint32_t max);
int  lx64_ksvc_write(int svc_idx, const void *buf, uint32_t len);
bool lx64_ksvc_has_client(int svc_idx);

#endif
