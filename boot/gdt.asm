
bits 64

section .text


global gdt_flush
gdt_flush:
    lgdt    [rdi]

    mov     ax, 0x10    
    mov     ss, ax
    mov     ds, ax
    mov     es, ax
    xor     ax, ax
    mov     fs, ax
    mov     gs, ax

    
    pop     rax
    push    qword 0x08
    push    rax
    retfq


global tss_flush
tss_flush:
    mov     ax, 0x30    
    ltr     ax
    ret
