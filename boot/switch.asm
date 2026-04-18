; switch.asm — 64-bit context switch
; context_switch(context_t *prev, context_t *next)
;   rdi = prev,  rsi = next
;
; context_t field offsets (must match proc/process.h exactly):
;   rax=0, rbx=8, rcx=16, rdx=24, rsi_f=32, rdi_f=40, rbp=48, rsp=56,
;   r8=64, r9=72, r10=80, r11=88, r12=96, r13=104, r14=112, r15=120,
;   rip=128, rflags=136, cr3=144

bits 64

section .text
global context_switch
global fork_return_to_user
extern fork_child_complete

context_switch:
    ; ── Save previous context ────────────────────────────────────────────────
    mov     [rdi +   0], rax
    mov     [rdi +   8], rbx
    mov     [rdi +  16], rcx
    mov     [rdi +  24], rdx
    mov     [rdi +  32], rsi
    mov     [rdi +  40], rdi
    mov     [rdi +  48], rbp
    ; RSP: value after this call returns (caller's RSP before the call pushed the return addr)
    lea     rax, [rsp + 8]
    mov     [rdi +  56], rax
    mov     [rdi +  64], r8
    mov     [rdi +  72], r9
    mov     [rdi +  80], r10
    mov     [rdi +  88], r11
    mov     [rdi +  96], r12
    mov     [rdi + 104], r13
    mov     [rdi + 112], r14
    mov     [rdi + 120], r15
    ; RIP: address to resume at = return address pushed by caller's call instruction
    mov     rax, [rsp]
    mov     [rdi + 128], rax
    ; RFLAGS
    pushfq
    pop     rax
    mov     [rdi + 136], rax
    ; CR3
    mov     rax, cr3
    mov     [rdi + 144], rax

    ; ── Load next context ────────────────────────────────────────────────────
    ; Switch address space if CR3 differs
    mov     rax, [rsi + 144]
    mov     rcx, cr3
    cmp     rax, rcx
    je      .skip_cr3
    mov     cr3, rax
.skip_cr3:
    ; Restore RFLAGS
    push    qword [rsi + 136]
    popfq
    ; Restore RSP
    mov     rsp, [rsi + 56]
    ; Restore GPRs (except rax/rsi/rdi which we use last)
    mov     rbx, [rsi +   8]
    mov     rcx, [rsi +  16]
    mov     rdx, [rsi +  24]
    mov     rbp, [rsi +  48]
    mov     r8,  [rsi +  64]
    mov     r9,  [rsi +  72]
    mov     r10, [rsi +  80]
    mov     r11, [rsi +  88]
    mov     r12, [rsi +  96]
    mov     r13, [rsi + 104]
    mov     r14, [rsi + 112]
    mov     r15, [rsi + 120]
    ; Load RIP onto stack so RET jumps there
    push    qword [rsi + 128]
    ; Restore rax, rdi, rsi last
    mov     rax, [rsi +   0]
    mov     rdi, [rsi +  40]
    mov     rsi, [rsi +  32]   ; must be very last use of rsi as pointer
    ret

; fork_return_to_user — entry point for a freshly-forked child process.
; The scheduler context_switches here (ctx.rip = fork_return_to_user).
; Calls the C helper fork_child_complete() which reads fork_user_rip/rsp/rflags
; from the current process struct and executes sysretq to return to user space
; with RAX=0 (fork returns 0 in child).
fork_return_to_user:
    ; Stack is a fresh kernel stack set up by lx64_fork.
    ; Call C helper — it will never return (executes sysretq).
    call fork_child_complete
    hlt
