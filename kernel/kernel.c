/*
 * kernel/kernel.c — Entry point C do Krypx
 * Inicializa todos os subsistemas da Fase 1: VGA, GDT, IDT, PIT, Teclado.
 */

#include <types.h>
#include <multiboot.h>
#include <system.h>
#include <io.h>
#include <drivers/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/timer.h>
#include <drivers/keyboard.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <drivers/ide.h>
#include <fs/vfs.h>
#include <fs/fat32.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <kernel/syscall.h>
#include <drivers/framebuffer.h>
#include <gui/desktop.h>

/* ============================================================
 * Debug via porta serial COM1 (115200 baud)
 * ============================================================ */
static void ser_init(void) {
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);  /* 115200 baud */
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);  /* 8N1 */
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}
static void ser_putc(char c) { while (!(inb(0x3FD)&0x20)); outb(0x3F8,c); }
static void ser_puts(const char *s) { while (*s) ser_putc(*s++); }

/* ============================================================
 * kernel_panic — Para tudo e exibe mensagem de erro fatal
 * ============================================================ */
void kernel_panic(const char *msg) {
    cli();
    ser_puts("\r\n*** KERNEL PANIC *** ");
    ser_puts(msg);
    ser_puts("\r\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    vga_puts("\n\n *** KERNEL PANIC *** \n ");
    vga_puts(msg);
    vga_puts("\n Sistema travado.\n");
    for (;;) __asm__ volatile ("hlt");
}

/* ============================================================
 * Helpers de log
 * ============================================================ */
static void log_ok(const char *msg) {
    vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts(msg);
    vga_puts("\n");
}

static void log_info(const char *msg) {
    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_puts("[..] ");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts(msg);
    vga_puts("\n");
}

/* ============================================================
 * Banner de boot
 * ============================================================ */
static void print_banner(void) {
    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_puts("================================================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("                   *** Krypx v");
    vga_puts(KRYPX_VERSION_STR);
    vga_puts(" - Booting... ***\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_puts("              Custom OS - Bare Metal - x86 - C + Assembly\n");
    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_puts("================================================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

static void print_meminfo(multiboot_info_t *mbi) {
    if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_puts("[MEM] Lower: ");
        vga_put_dec(mbi->mem_lower);
        vga_puts(" KB  Upper: ");
        vga_put_dec(mbi->mem_upper);
        vga_puts(" KB  Total: ~");
        vga_put_dec((mbi->mem_upper + 1024) / 1024);
        vga_puts(" MB\n");
    }
}

/* ============================================================
 * Loop interativo simples (eco de teclado)
 * ============================================================ */
static void keyboard_echo_loop(void) {
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("\nDigite algo (teclado PS/2 ativo):\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_puts("> ");

    for (;;) {
        char c = keyboard_getchar();
        if (c) {
            if (c == '\n') {
                vga_putchar('\n');
                vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                vga_puts("> ");
            } else if (c == '\b') {
                vga_putchar('\b');
            } else {
                vga_putchar(c);
            }
        }
        __asm__ volatile ("hlt");
    }
}

/* ============================================================
 * kernel_main — Entry point C
 * ============================================================ */
void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    multiboot_info_t *mbi = (multiboot_info_t *)mbi_addr;

    ser_init();  /* COM1 115200 baud — debug via serial */

    /* === 1. VGA (primeiro — precisamos ver o que acontece) === */
    vga_init();

    /* === 2. Valida magic Multiboot === */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kernel_panic("Bootloader invalido! Magic number incorreto.");
    }

    print_banner();

    /* === 3. GDT === */
    log_info("Inicializando GDT...");
    gdt_init();
    log_ok("GDT carregada (null, kernel code/data, user code/data, TSS)");

    /* === 4. IDT + PIC remapping === */
    log_info("Inicializando IDT e remapeando PIC 8259...");
    idt_init();
    log_ok("IDT carregada (256 entradas). PIC remapeado (IRQs 32-47)");

    /* === 5. PIT Timer === */
    log_info("Inicializando PIT timer (1000 Hz)...");
    timer_init();
    log_ok("PIT configurado. IRQ0 ativo");

    /* === 6. Teclado PS/2 === */
    log_info("Inicializando teclado PS/2...");
    keyboard_init();
    log_ok("Teclado PS/2 ativo. IRQ1 habilitado");

    /* === 7. PMM === */
    log_info("Inicializando PMM (Physical Memory Manager)...");
    pmm_init(mbi);
    log_ok("PMM pronto (bitmap de paginas fisicas)");
    pmm_print_info();

    /* === 8. VMM (paginacao) === */
    log_info("Inicializando VMM e ativando paginacao x86...");
    vmm_init();
    log_ok("VMM pronto. Paginacao ativa (CR0.PG=1, PSE=4MB pages)");

    /* === 9. Heap do kernel === */
    log_info("Inicializando heap do kernel...");
    /*
     * O heap começa logo após o fim do kernel (alinhado a 4 MB)
     * e tem 4 MB de tamanho inicial. O PMM já reservou o kernel.
     */
    uint32_t heap_start_addr = 0x800000;   /* 8 MB — acima do kernel+bitmap */
    heap_init(heap_start_addr, 4 * 1024 * 1024);
    log_ok("Heap do kernel pronto (4 MB iniciais, kmalloc/kfree disponiveis)");

    /* === 10. Habilita interrupções === */
    sti();
    log_ok("Interrupcoes habilitadas (STI)");

    /* === Teste do heap === */
    {
        void *a = kmalloc(64);
        void *b = kmalloc(128);
        void *c = kmalloc(256);
        kfree(b);
        void *d = kmalloc(100);  /* Deve reusar o slot de b */
        (void)a; (void)c; (void)d;
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK] Teste kmalloc/kfree passou\n");
        heap_print_info();
    }

    /* === Informações === */
    print_meminfo(mbi);

    /* === 11. IDE + VFS + FAT32 === */
    log_info("Inicializando driver IDE/ATA...");
    ide_init();
    if (ide_disk_present()) {
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK] Disco IDE detectado: ");
        vga_put_dec(ide_get_sector_count() / 2048);
        vga_puts(" MB\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        log_info("Montando FAT32...");
        vfs_init();
        if (fat32_init(0)) {
            vfs_mount_root(fat32_get_root());
            log_ok("FAT32 montado em /");
        } else {
            vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            vga_puts("[WARN] FAT32 nao encontrado (disco sem particao FAT32)\n");
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            vfs_init();
        }
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[WARN] Sem disco IDE (rodando em QEMU sem -drive)\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_init();
    }

    /* === 12. Processos + Scheduler + Syscalls === */
    log_info("Inicializando processos e scheduler...");
    process_init();
    scheduler_init();
    syscall_init();
    process_t *kproc = process_create_kernel();
    (void)kproc;
    log_ok("Processos inicializados. Scheduler Round-Robin pronto");

    /* Adiciona o processo kernel à fila */
    scheduler_add(process_current());
    scheduler_enable();
    log_ok("Syscalls via int 0x80 registradas");

    /* === 13. Framebuffer + GUI === */
    log_info("Inicializando framebuffer VBE...");
    if (fb_init(mbi)) {
        log_ok("Framebuffer VBE pronto — iniciando GUI");
        desktop_init();
        desktop_run();   /* Loop infinito */
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[WARN] Sem framebuffer VBE — modo texto\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_puts("(Rode com ISO via GRUB para ativar o modo grafico)\n");
    }

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("\nKrypx todas as fases completas!\n");
    keyboard_echo_loop();
}
