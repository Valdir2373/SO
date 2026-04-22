




bits 32


MBOOT_MAGIC    equ 0x1BADB002
MBOOT_FLAGS    equ 0x00000007   
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM
    dd 0, 0, 0, 0, 0   
    dd 0                
    dd 1024             
    dd 768              
    dd 32               


section .bss
align 4096



pml4_table:  resb 4096
pdpt_table:  resb 4096
pd_table0:   resb 4096   
pd_table1:   resb 4096   
pd_table2:   resb 4096   
pd_table3:   resb 4096   


align 16
stack_bottom:
    resb 65536
stack_top:


section .data
align 8
gdt64:
    dq 0x0000000000000000    
    dq 0x00AF9A000000FFFF    
    dq 0x00AF92000000FFFF    
gdt64_end:



gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64


section .text
global _start
extern kernel_main

_start:
    
    mov [mb_magic], eax
    mov [mb_info],  ebx

    

    
    mov edi, pd_table0
    xor eax, eax
.fill_pd0:
    mov ecx, eax
    shl ecx, 21             
    or  ecx, 0x83           
    mov [edi], ecx
    mov dword [edi+4], 0
    add edi, 8
    inc eax
    cmp eax, 512
    jne .fill_pd0

    
    mov edi, pd_table1
    mov eax, 512            
.fill_pd1:
    mov ecx, eax
    shl ecx, 21
    or  ecx, 0x83
    mov [edi], ecx
    mov dword [edi+4], 0
    add edi, 8
    inc eax
    cmp eax, 1024
    jne .fill_pd1

    
    mov edi, pd_table2
    mov eax, 1024
.fill_pd2:
    mov ecx, eax
    shl ecx, 21
    or  ecx, 0x83
    mov [edi], ecx
    mov dword [edi+4], 0
    add edi, 8
    inc eax
    cmp eax, 1536
    jne .fill_pd2

    
    mov edi, pd_table3
    mov eax, 1536
.fill_pd3:
    mov ecx, eax
    shl ecx, 21
    or  ecx, 0x83
    mov [edi], ecx
    mov dword [edi+4], 0
    add edi, 8
    inc eax
    cmp eax, 2048
    jne .fill_pd3

    
    mov eax, pd_table0
    or  eax, 0x03           
    mov [pdpt_table +  0], eax
    mov dword [pdpt_table +  4], 0

    mov eax, pd_table1
    or  eax, 0x03
    mov [pdpt_table +  8], eax
    mov dword [pdpt_table + 12], 0

    mov eax, pd_table2
    or  eax, 0x03
    mov [pdpt_table + 16], eax
    mov dword [pdpt_table + 20], 0

    mov eax, pd_table3
    or  eax, 0x03
    mov [pdpt_table + 24], eax
    mov dword [pdpt_table + 28], 0

    
    mov eax, pdpt_table
    or  eax, 0x03
    mov [pml4_table], eax
    mov dword [pml4_table + 4], 0

    

    
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    
    mov eax, pml4_table
    mov cr3, eax

    
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    
    mov eax, cr0
    or  eax, (1 << 31)
    mov cr0, eax
    

    
    lgdt [gdt64_ptr]

    
    jmp 0x08:.long_mode_64


bits 64
.long_mode_64:
    
    mov ax, 0x10        
    mov ss, ax
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    
    mov rsp, stack_top

    
    mov edi, [rel mb_magic]   
    mov esi, [rel mb_info]    

    
    call kernel_main

    
    cli
.hang:
    hlt
    jmp .hang


section .data
mb_magic: dd 0
mb_info:  dd 0
