

#include <kernel/idt.h>
#include <kernel/gdt.h>
#include <types.h>
#include <io.h>
#include <drivers/vga.h>
#include <system.h>


#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1
#define PIC_EOI     0x20   


#define ICW1_INIT   0x10
#define ICW1_ICW4   0x01


#define ICW4_8086   0x01   


static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;


static interrupt_handler_t handlers[IDT_ENTRIES];


extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);
extern void isr128(void);


extern void idt_flush(idt_ptr_t *ptr);

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = (uint16_t)(base & 0xFFFF);
    idt[num].offset_mid  = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].reserved    = 0;
}

void pic_remap(void) {
    uint8_t mask1, mask2;

    
    mask1 = inb(PIC1_DATA);
    mask2 = inb(PIC2_DATA);

    
    outb(PIC1_CMD,  ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD,  ICW1_INIT | ICW1_ICW4);
    io_wait();

    
    outb(PIC1_DATA, 0x20);  
    io_wait();
    outb(PIC2_DATA, 0x28);  
    io_wait();

    
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 40) {
        
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t val;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) | (1 << irq);
    outb(port, val);
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t val;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) & ~(1 << irq);
    outb(port, val);
}

void idt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}


static const char *exception_names[] = {
    "Division by Zero",          
    "Debug",                     
    "Non-Maskable Interrupt",    
    "Breakpoint",                
    "Overflow",                  
    "Bound Range Exceeded",      
    "Invalid Opcode",            
    "Device Not Available",      
    "Double Fault",              
    "Coprocessor Segment",       
    "Invalid TSS",               
    "Segment Not Present",       
    "Stack-Segment Fault",       
    "General Protection Fault",  
    "Page Fault",                
    "Reserved",                  
    "x87 FPU Error",             
    "Alignment Check",           
    "Machine Check",             
    "SIMD FP Exception",         
};


void interrupt_handler(registers_t *regs) {
    uint64_t vec = regs->int_no;

    
    if (handlers[vec]) {
        handlers[vec](regs);
        return;
    }

    
    if (vec >= 32 && vec < 48) {
        pic_send_eoi(vec);
        return;
    }

    
    if (vec < 32) {
        
        {
            uint64_t v;
            int      k;
            static const char *hexc = "0123456789ABCDEF";
#define SP(c) do { while(!(inb(0x3FD)&0x20)); outb(0x3F8,(uint8_t)(c)); } while(0)
#define SH(x) do { int _j; v=(uint64_t)(x); SP('0'); SP('x'); for(_j=60;_j>=0;_j-=4) SP(hexc[(v>>_j)&0xF]); } while(0)
#define SS(s) do { const char *_p=(s); while(*_p) { SP(*_p); _p++; } } while(0)
            (void)k;
            SS("\r\n[EXCEPTION] vec=");
            SP('0' + (uint8_t)(vec / 10));
            SP('0' + (uint8_t)(vec % 10));
            SS(" rip="); SH(regs->rip);
            SS(" err="); SH(regs->err_code);
            if (vec == 14) {
                uint64_t cr2;
                __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
                SS(" cr2="); SH(cr2);
            }
            SS("\r\n");
#undef SP
#undef SH
#undef SS
        }

        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
        vga_puts("\n\n *** KERNEL PANIC - CPU EXCEPTION ***\n");
        vga_puts(" Excecao: ");
        if (vec < 20) {
            vga_puts(exception_names[(int)vec]);
        } else {
            vga_puts("Reservada");
        }
        vga_puts("\n Sistema travado.\n");
        __asm__ volatile ("cli");
        for (;;) __asm__ volatile ("hlt");
    }
}

void idt_init(void) {
    uint32_t i;

    
    for (i = 0; i < IDT_ENTRIES; i++) handlers[i] = 0;

    idt_ptr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
    idt_ptr.base  = (uint64_t)&idt;

    idt_set_gate( 0, (uint64_t)isr0,  0x08, 0x8E);
    idt_set_gate( 1, (uint64_t)isr1,  0x08, 0x8E);
    idt_set_gate( 2, (uint64_t)isr2,  0x08, 0x8E);
    idt_set_gate( 3, (uint64_t)isr3,  0x08, 0x8E);
    idt_set_gate( 4, (uint64_t)isr4,  0x08, 0x8E);
    idt_set_gate( 5, (uint64_t)isr5,  0x08, 0x8E);
    idt_set_gate( 6, (uint64_t)isr6,  0x08, 0x8E);
    idt_set_gate( 7, (uint64_t)isr7,  0x08, 0x8E);
    idt_set_gate( 8, (uint64_t)isr8,  0x08, 0x8E);
    idt_set_gate( 9, (uint64_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E);
    idt_set_gate(32, (uint64_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E);
    idt_set_gate(128, (uint64_t)isr128, 0x08, 0xEE);

    pic_remap();
    idt_flush(&idt_ptr);
}
