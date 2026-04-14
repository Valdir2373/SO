; boot/boot.asm — Stage 1: Multiboot header + entry point do Krypx
; GRUB lê o Multiboot header, carrega o kernel em 0x00100000
; e pula para _start com EAX=magic, EBX=&multiboot_info

bits 32

; ============================================================
; Constantes Multiboot
; ============================================================
MBOOT_MAGIC     equ 0x1BADB002
MBOOT_FLAGS     equ 0x00000007   ; bit 0 = alinhar módulos, bit 1 = mapa de memória, bit 2 = modo vídeo
MBOOT_CHECKSUM  equ -(MBOOT_MAGIC + MBOOT_FLAGS)

; ============================================================
; Seção .multiboot — deve aparecer nos primeiros 8 KB do kernel
; ============================================================
section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM
    ; Campos de endereço (só usados se bit 16 estiver set — colocamos 0s)
    dd 0            ; header_addr  (não usado — kernel ELF)
    dd 0            ; load_addr
    dd 0            ; load_end_addr
    dd 0            ; bss_end_addr
    dd 0            ; entry_addr
    ; Campos de modo de vídeo (usados porque bit 2 está set)
    dd 0            ; mode_type: 0 = framebuffer linear RGB
    dd 1024         ; largura preferida
    dd 768          ; altura preferida
    dd 32           ; bits por pixel preferido

; ============================================================
; BSS — stack do kernel (16 KB)
; ============================================================
section .bss
align 16
stack_bottom:
    resb 16384          ; 16 KB de stack para o kernel
stack_top:

; ============================================================
; Seção de código
; ============================================================
section .text
global _start
extern kernel_main      ; função C em kernel/kernel.c

_start:
    ; Configura stack pointer antes de qualquer coisa
    mov esp, stack_top

    ; Passa argumentos para kernel_main(uint32_t magic, uint32_t mbi_addr)
    ; Convenção cdecl: argumentos empilhados da direita para a esquerda
    push ebx            ; arg2: endereço da struct multiboot_info
    push eax            ; arg1: magic number (0x2BADB002)

    ; Chama o kernel em C
    call kernel_main

    ; kernel_main NÃO deve retornar.
    ; Se retornar por algum bug, trava a CPU.
    cli                 ; Desabilita interrupções
.hang:
    hlt                 ; Para a CPU
    jmp .hang           ; Se acordar (NMI), volta a parar
