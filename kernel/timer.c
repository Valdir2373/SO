/*
 * kernel/timer.c — Inicialização do PIT e handler de tick
 * O PIT (8253/8254) gera interrupções em ~1000 Hz (IRQ0 → vetor 32).
 * Cada tick = 1 ms. O contador de ticks alimenta o scheduler e animações.
 */

#include <kernel/timer.h>
#include <kernel/idt.h>
#include <types.h>
#include <io.h>

/* Forward declaration — evita dependência circular no header */
extern void schedule(void);

/* Portas do PIT */
#define PIT_CHANNEL0  0x40   /* Canal 0 — ligado ao IRQ0 */
#define PIT_CMD       0x43   /* Registrador de comando */

/* Comando: canal 0, lobyte/hibyte, rate generator (modo 2) */
#define PIT_CMD_RATE  0x36

/* Contador de ticks global */
static volatile uint32_t ticks = 0;

/* Handler do timer — chamado a cada tick (IRQ0) */
static void timer_handler(registers_t *regs) {
    (void)regs;
    ticks++;
    pic_send_eoi(32);
    schedule();   /* Preempção Round-Robin */
}

void timer_init(void) {
    uint16_t divisor = PIT_BASE_FREQ / TIMER_HZ;

    /* Configura o PIT: canal 0, lobyte/hibyte, rate generator */
    outb(PIT_CMD, PIT_CMD_RATE);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

    /* Registra o handler no vetor 32 (IRQ0) */
    idt_register_handler(IRQ_TIMER, timer_handler);

    /* Garante que o IRQ0 está demascarado no PIC */
    pic_unmask_irq(0);
}

uint32_t timer_get_ticks(void) {
    return ticks;
}

uint32_t timer_get_seconds(void) {
    return ticks / TIMER_HZ;
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t target = ticks + ms;
    while (ticks < target) {
        __asm__ volatile ("hlt");
    }
}
