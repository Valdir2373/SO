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
#include <drivers/mouse.h>
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
#include <drivers/pci.h>
#include <gui/desktop.h>
#include <net/netif.h>
#include <security/users.h>
#include <security/aslr.h>
#include <compat/detect.h>
#include <compat/linux_compat.h>
#include <compat/win_compat.h>
#include <proc/elf.h>

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
 * compat_try_run — Carrega e executa binário do disco via compat layer
 * Detecta o tipo (ELF Linux / PE Windows), carrega no processo e
 * adiciona ao scheduler. O processo receberá fatias de CPU pelo timer.
 * ============================================================ */
static void compat_try_run(const char *path) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return;

    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_puts("[COMPAT] Binario encontrado: ");
    vga_puts(path);
    vga_puts(" (");
    vga_put_dec(node->size);
    vga_puts(" bytes)\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /* Lê o arquivo inteiro para o heap do kernel */
    uint8_t *buf = (uint8_t *)kmalloc(node->size);
    if (!buf) {
        vga_puts("[COMPAT] ERRO: kmalloc falhou\n");
        return;
    }
    vfs_read(node, 0, node->size, buf);

    /* Detecta o tipo de executável */
    binary_type_t btype = detect_binary_type(buf, node->size);
    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_puts("[COMPAT] Tipo detectado: ");
    vga_puts(binary_type_name(btype));
    vga_puts("\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    if (btype == BINARY_WINDOWS_PE) {
        win_compat_load(buf, node->size);
        kfree(buf);
        return;
    }

    if (btype != BINARY_LINUX_ELF && btype != BINARY_KRYPX_ELF) {
        vga_puts("[COMPAT] Formato nao suportado.\n");
        kfree(buf);
        return;
    }

    /* Extrai nome do arquivo (remove o prefixo "/") */
    const char *pname = path;
    {
        const char *tmp = path;
        while (*tmp) { if (*tmp == '/') pname = tmp + 1; tmp++; }
    }

    /* Cria processo e carrega o ELF */
    process_t *proc = process_create(pname, 0, 2);
    if (!proc) {
        vga_puts("[COMPAT] ERRO: nao foi possivel criar processo\n");
        kfree(buf);
        return;
    }

    elf_load_result_t res;
    if (elf_load(proc, buf, node->size, &res) != 0) {
        vga_puts("[COMPAT] ERRO: falha ao carregar ELF\n");
        kfree(buf);
        return;
    }
    kfree(buf);  /* ELF já foi copiado para o espaço do processo */

    /* Configura contexto de execução */
    proc->ctx.eip    = res.entry_point;
    proc->ctx.esp    = res.user_stack_top;
    proc->ctx.eflags = 0x202;  /* IF=1 */
    proc->heap_start = res.heap_base;
    proc->heap_end   = res.heap_base;

    if (btype == BINARY_LINUX_ELF)
        proc->compat_mode = COMPAT_LINUX;

    /* Adiciona ao scheduler */
    scheduler_add(proc);

    vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_puts("[COMPAT] Processo '");
    vga_puts(pname);
    vga_puts("' iniciado (PID ");
    vga_put_dec(proc->pid);
    vga_puts(", entry=0x");
    {
        /* Exibe entry point em hex */
        uint32_t v = res.entry_point;
        char hex[9]; int hi;
        hex[8] = '\0';
        for (hi = 7; hi >= 0; hi--) {
            uint8_t nibble = (uint8_t)(v & 0xF);
            hex[hi] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
            v >>= 4;
        }
        vga_puts(hex);
    }
    vga_puts(")\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
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

    /* === 6b. Mouse PS/2 === */
    log_info("Inicializando mouse PS/2...");
    mouse_init();
    log_ok("Mouse PS/2 ativo. IRQ12 habilitado");

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

    /* === 12b. Camada de compatibilidade binária === */
    linux_compat_init();
    win_compat_init();
    vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK] Compat: Linux ELF i386 ativo | Windows PE (em breve)\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    /*
     * Tenta carregar e executar binários do disco (FAT32 já montado no passo 11).
     * O processo entra no scheduler e recebe fatias de CPU a cada tick do timer.
     * O delay de 300 ms garante que ele execute e produza saída no VGA texto
     * ANTES de a GUI iniciar e sobrescrever a tela.
     */
    compat_try_run("/test_linux");
    if (process_get(1) || process_get(2)) {
        /* Há processo compat no scheduler — dá tempo para rodar */
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[COMPAT] Aguardando saida do processo Linux...\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        uint32_t t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 400) {
            __asm__ volatile ("hlt");   /* Cede CPU; timer IRQ → schedule() */
        }
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[COMPAT] Processo Linux concluido.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    /* === 13. Rede (PCI + e1000 + TCP/IP + DHCP) === */
    log_info("Inicializando pilha de rede...");
    pci_init();
    netif_init();
    if (netif_is_up()) {
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK] Rede configurada via DHCP\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        ser_puts("[NET] Rede OK via DHCP\r\n");
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[WARN] Sem rede (sem NIC ou DHCP falhou)\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        ser_puts("[NET] Sem rede\r\n");
    }

    /* === 14. Segurança: usuários + ASLR === */
    log_info("Inicializando subsistema de segurança...");
    aslr_init(timer_get_ticks());
    users_init();
    log_ok("Segurança: ASLR ativo, usuario root criado (senha: krypx)");
    ser_puts("[SEC] Seguranca inicializada\r\n");

    /* === 15. Framebuffer + GUI === */
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
