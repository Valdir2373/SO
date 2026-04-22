bits 64











global syscall_entry
global g_syscall_kernel_rsp
global g_syscall_user_rsp
extern linux_syscall64_handler

section .bss
align 16
syscall_kstack_space: resb 65536
syscall_kstack_top:

section .data
align 8
g_syscall_kernel_rsp: dq 0
g_syscall_user_rsp:   dq 0

section .text
syscall_entry:
    
    mov [rel g_syscall_user_rsp], rsp
    mov rsp, [rel g_syscall_kernel_rsp]

    
    push r11        
    push rcx        
    push r9         
    push r8         
    push r10        
    push rdx        
    push rsi        
    push rdi        
    push rax        

    
    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx

    
    lea rdi, [rsp + 48]
    call linux_syscall64_handler

    
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    
    pop rax         
    pop rdi
    pop rsi
    pop rdx
    pop r10
    pop r8
    pop r9
    pop rcx         
    pop r11         

    
    mov rsp, [rel g_syscall_user_rsp]

    
    sysretq
