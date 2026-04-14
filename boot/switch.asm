; boot/switch.asm — Context switch x86
; Salva os registradores do processo atual e carrega os do próximo.
;
; void context_switch(context_t *prev, context_t *next)
;   prev: [esp+4]  — context_t do processo saindo
;   next: [esp+8]  — context_t do processo entrando

bits 32

section .text
global context_switch

; Layout de context_t (em proc/process.h):
;  0: eax   4: ebx   8: ecx  12: edx
; 16: esi  20: edi  24: ebp  28: esp
; 32: eip  36: eflags  40: cr3

context_switch:
    ; Prologo sem frame pointer para não bagunçar EBP
    mov eax, [esp+4]    ; EAX = ponteiro para context_t do prev

    ; Salva registradores gerais
    mov [eax+0],  eax   ; eax ← sobrescreve com valor atual mas não importa
    mov [eax+4],  ebx
    mov [eax+8],  ecx
    mov [eax+12], edx
    mov [eax+16], esi
    mov [eax+20], edi
    mov [eax+24], ebp
    ; ESP: salva o ESP após o retorno desta função
    lea ecx, [esp+4]    ; ECX = ESP no momento do call + 4 (aponta ao return addr)
    mov [eax+28], ecx

    ; Salva EIP = endereço de retorno desta função
    mov ecx, [esp]      ; ECX = return address
    mov [eax+32], ecx

    ; Salva EFLAGS
    pushfd
    pop ecx
    mov [eax+36], ecx

    ; --- Carrega o contexto do próximo processo ---
    mov eax, [esp+8]    ; EAX = ponteiro para context_t do next

    ; Troca CR3 se necessário
    mov ecx, [eax+40]   ; ecx = cr3 do next
    mov edx, cr3
    cmp ecx, edx
    je  .skip_cr3
    mov cr3, ecx
.skip_cr3:

    ; Restaura EFLAGS
    mov ecx, [eax+36]
    push ecx
    popfd

    ; Restaura ESP
    mov esp, [eax+28]

    ; Restaura registradores gerais
    mov ebx, [eax+4]
    mov ecx, [eax+8]
    mov edx, [eax+12]
    mov esi, [eax+16]
    mov edi, [eax+20]
    mov ebp, [eax+24]

    ; Salta para o EIP do próximo processo (simula ret)
    jmp dword [eax+32]
