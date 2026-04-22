
#ifndef _IDT_H
#define _IDT_H

#include <types.h>

#define IDT_ENTRIES 256


#define IRQ_BASE      32
#define IRQ_TIMER     32
#define IRQ_KEYBOARD  33
#define IRQ_CASCADE   34
#define IRQ_COM2      35
#define IRQ_COM1      36
#define IRQ_MOUSE     44
#define IRQ_NETWORK   43
#define IRQ_SYSCALL   0x80


typedef struct {
    uint16_t offset_low;    
    uint16_t selector;      
    uint8_t  ist;           
    uint8_t  type_attr;     
    uint16_t offset_mid;    
    uint32_t offset_high;   
    uint32_t reserved;      
} __attribute__((packed)) idt_entry_t;


typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

typedef void (*interrupt_handler_t)(registers_t *regs);

void idt_init(void);
void idt_register_handler(uint8_t vector, interrupt_handler_t handler);
void pic_remap(void);
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void interrupt_handler(registers_t *regs);

#endif

