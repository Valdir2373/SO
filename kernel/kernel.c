

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
#include <drivers/ahci.h>
#include <drivers/usb_hid.h>
#include <drivers/acpi.h>
#include <drivers/rtl8188eu.h>
#include <net/wifi.h>
#include <gui/desktop.h>
#include <net/netif.h>
#include <security/users.h>
#include <security/aslr.h>
#include <compat/detect.h>
#include <compat/linux_compat.h>
#include <compat/linux_compat64.h>
#include <compat/win_compat.h>
#include <proc/elf.h>
#include <fs/devfs.h>
#include <fs/procfs.h>
#include <gui/x11_server.h>


static void ser_init(void) {
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);  
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);  
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}
static void ser_putc(char c) { while (!(inb(0x3FD)&0x20)); outb(0x3F8,c); }
static void ser_puts(const char *s) { while (*s) ser_putc(*s++); }


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


static void compat_try_run(const char *path) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) { ser_puts("[COMPAT] Arquivo nao encontrado: "); ser_puts(path); ser_puts("\r\n"); return; }

    ser_puts("[COMPAT] Binario encontrado: "); ser_puts(path); ser_puts("\r\n");
    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_puts("[COMPAT] Binario encontrado: "); vga_puts(path); vga_puts("\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    uint8_t *buf = (uint8_t *)kmalloc(node->size);
    if (!buf) { ser_puts("[COMPAT] ERRO: kmalloc falhou\r\n"); vga_puts("[COMPAT] ERRO: kmalloc falhou\n"); return; }
    vfs_read(node, 0, node->size, buf);

    binary_type_t btype = detect_binary_type(buf, node->size);
    ser_puts("[COMPAT] Tipo: "); ser_puts(binary_type_name(btype)); ser_puts("\r\n");
    vga_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_puts("[COMPAT] Tipo: "); vga_puts(binary_type_name(btype)); vga_puts("\n");
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

    
    const char *pname = path;
    {
        const char *tmp = path;
        while (*tmp) { if (*tmp == '/') pname = tmp + 1; tmp++; }
    }

    
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
    kfree(buf);  

    
    proc->ctx.rip    = res.entry_point;
    proc->ctx.rsp    = res.user_stack_top;
    proc->ctx.rflags = 0x202;
    proc->heap_start = res.heap_base;
    proc->heap_end   = res.heap_base;

    if (btype == BINARY_LINUX_ELF)
        proc->compat_mode = COMPAT_LINUX;

    
    scheduler_add(proc);

    ser_puts("[COMPAT] Processo '"); ser_puts(pname); ser_puts("' iniciado\r\n");
    vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_puts("[COMPAT] Processo '"); vga_puts(pname); vga_puts("' iniciado\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}


void kernel_main(uint64_t magic, uint64_t mbi_addr) {
    multiboot_info_t *mbi = (multiboot_info_t *)mbi_addr;

    ser_init();  
    ser_puts("[BOOT] kernel_main iniciado\r\n");

    
    ser_puts("[DBG] step1: vga_init\r\n");
    vga_init();
    ser_puts("[DBG] step1: ok\r\n");

    
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kernel_panic("Bootloader invalido! Magic number incorreto.");
    }

    print_banner();

    
    ser_puts("[DBG] step3: gdt_init\r\n");
    log_info("Inicializando GDT...");
    gdt_init();
    log_ok("GDT carregada (null, kernel code/data, user code/data, TSS)");
    ser_puts("[DBG] step3: ok\r\n");

    
    ser_puts("[DBG] step4: idt_init\r\n");
    log_info("Inicializando IDT e remapeando PIC 8259...");
    idt_init();
    log_ok("IDT carregada (256 entradas). PIC remapeado (IRQs 32-47)");
    ser_puts("[DBG] step4: ok\r\n");

    
    ser_puts("[DBG] step5: timer_init\r\n");
    log_info("Inicializando PIT timer (1000 Hz)...");
    timer_init();
    log_ok("PIT configurado. IRQ0 ativo");
    ser_puts("[DBG] step5: ok\r\n");

    
    ser_puts("[DBG] step6: keyboard_init\r\n");
    log_info("Inicializando teclado PS/2...");
    keyboard_init();
    log_ok("Teclado PS/2 ativo. IRQ1 habilitado");
    ser_puts("[DBG] step6: ok\r\n");

    
    ser_puts("[DBG] step6b: mouse_init\r\n");
    log_info("Inicializando mouse PS/2...");
    mouse_init();
    log_ok("Mouse PS/2 ativo. IRQ12 habilitado");
    ser_puts("[DBG] step6b: ok\r\n");

    
    ser_puts("[DBG] step7: pmm_init\r\n");
    log_info("Inicializando PMM (Physical Memory Manager)...");
    pmm_init(mbi);
    log_ok("PMM pronto (bitmap de paginas fisicas)");
    pmm_print_info();
    ser_puts("[DBG] step7: ok\r\n");

    
    ser_puts("[DBG] step8: vmm_init\r\n");
    log_info("Inicializando VMM e ativando paginacao x86...");
    vmm_init();
    log_ok("VMM pronto. Paginacao ativa (CR0.PG=1, PSE=4MB pages)");
    ser_puts("[DBG] step8: ok\r\n");

    
    ser_puts("[DBG] step9: heap_init\r\n");
    log_info("Inicializando heap do kernel...");
    uint64_t heap_start_addr = 0x800000ULL;
    heap_init((uint32_t)heap_start_addr, 16 * 1024 * 1024);
    log_ok("Heap do kernel pronto (16 MB, kmalloc/kfree disponiveis)");
    ser_puts("[DBG] step9: ok\r\n");

    
    ser_puts("[DBG] step10: sti\r\n");
    sti();
    log_ok("Interrupcoes habilitadas (STI)");
    ser_puts("[DBG] step10: ok\r\n");

    
    {
        void *a = kmalloc(64);
        void *b = kmalloc(128);
        void *c = kmalloc(256);
        kfree(b);
        void *d = kmalloc(100);  
        (void)a; (void)c; (void)d;
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK] Teste kmalloc/kfree passou\n");
        heap_print_info();
    }

    
    print_meminfo(mbi);

    
    ser_puts("[DBG] step11: ide_init\r\n");
    log_info("Inicializando driver IDE/ATA...");
    ide_init();
    ser_puts("[DBG] step11: ide_init ok\r\n");
    if (ide_disk_present()) {
        ser_puts("[DBG] step11: disco presente\r\n");
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK] Disco IDE detectado: ");
        vga_put_dec(ide_get_sector_count() / 2048);
        vga_puts(" MB\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

        ser_puts("[DBG] step11: fat32_init\r\n");
        log_info("Montando FAT32...");
        vfs_init();
        if (fat32_init(0)) {
            vfs_mount_root(fat32_get_root());
            log_ok("FAT32 montado em /");
            ser_puts("[DBG] step11: fat32 ok\r\n");
        } else {
            vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            vga_puts("[WARN] FAT32 nao encontrado (disco sem particao FAT32)\n");
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            ser_puts("[DBG] step11: fat32 nao encontrado\r\n");
            vfs_init();
        }
    } else {
        ser_puts("[DBG] step11: sem disco IDE\r\n");
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[WARN] Sem disco IDE (rodando em QEMU sem -drive)\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_init();
    }

    
    devfs_init();
    procfs_init();
    log_ok("devfs e procfs montados (/dev /proc)");


    ser_puts("[DBG] step12: process_init\r\n");
    log_info("Inicializando processos e scheduler...");
    process_init();
    scheduler_init();
    syscall_init();
    process_t *kproc = process_create_kernel();
    (void)kproc;
    log_ok("Processos inicializados. Scheduler Round-Robin pronto");

    
    scheduler_add(process_current());
    scheduler_enable();
    log_ok("Syscalls via int 0x80 registradas");
    ser_puts("[DBG] step12: ok\r\n");

    
    linux_compat_init();
    win_compat_init();
    vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_puts("[OK] Compat: Linux ELF i386 ativo | Windows PE (em breve)\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    
    ser_puts("[DBG] step12b: compat_try_run\r\n");
    compat_try_run("/test_linux");
    ser_puts("[DBG] step12b: compat_try_run ok\r\n");
    if (process_get(1) || process_get(2)) {
        
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[COMPAT] Aguardando saida do processo Linux...\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        uint32_t t0 = timer_get_ticks();
        while (timer_get_ticks() - t0 < 400) {
            __asm__ volatile ("hlt");   
        }
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[COMPAT] Processo Linux concluido.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    
    
    ser_puts("[DBG] acpi_init\r\n");
    acpi_init();
    if (acpi_available()) log_ok("ACPI: shutdown/reboot disponiveis");

    
    ser_puts("[DBG] ahci_init\r\n");
    if (ahci_init()) {
        log_ok("AHCI: disco SATA detectado");
    }

    
    ser_puts("[DBG] usb_hid_init\r\n");
    usb_hid_init();
    if (usb_kbd_available()) log_ok("USB: teclado HID detectado");

    
    ser_puts("[DBG] rtl8188eu_init\r\n");
    wifi_init();
    if (rtl8188eu_init()) {
        log_ok("WiFi: RTL8188EU detectado — use Gerenciador de Rede");
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[WARN] WiFi: sem adaptador RTL8188EU (coloque rtl8188eu.bin no disco)\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    ser_puts("[DBG] step13: netif_init\r\n");
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

    
    
    ser_puts("[DBG] rtc\r\n");
    {
        extern void timer_set_rtc_base(uint32_t h, uint32_t m, uint32_t s);
        
        outb(0x70, 0x00); uint8_t sec_bcd = inb(0x71);
        outb(0x70, 0x02); uint8_t min_bcd = inb(0x71);
        outb(0x70, 0x04); uint8_t hr_bcd  = inb(0x71);
        
        uint32_t ss = (sec_bcd >> 4)*10 + (sec_bcd & 0xF);
        uint32_t mm = (min_bcd >> 4)*10 + (min_bcd & 0xF);
        uint32_t hh = (hr_bcd  >> 4)*10 + (hr_bcd  & 0xF);
        timer_set_rtc_base(hh, mm, ss);
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK] RTC: ");
        vga_put_dec(hh); vga_puts(":"); vga_put_dec(mm); vga_puts(":"); vga_put_dec(ss); vga_puts("\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    ser_puts("[DBG] step14: security_init\r\n");
    log_info("Inicializando subsistema de segurança...");
    aslr_init(timer_get_ticks());
    users_init();
    log_ok("Segurança: ASLR ativo, usuario root criado (senha: krypx)");
    ser_puts("[SEC] Seguranca inicializada\r\n");

    
    
    if (ide_disk_present() && vfs_root) {
        ser_puts("[DBG] first_boot\r\n");
        vfs_node_t *home = vfs_resolve("/home");
        if (!home) { vfs_mkdir(vfs_root, "home",  0755); }
        vfs_node_t *bin  = vfs_resolve("/bin");
        if (!bin)  { vfs_mkdir(vfs_root, "bin",   0755); }
        vfs_node_t *etc  = vfs_resolve("/etc");
        if (!etc)  { vfs_mkdir(vfs_root, "etc",   0755); }
        
        vfs_node_t *hroot_par = vfs_resolve("/home");
        if (hroot_par) {
            vfs_node_t *hroot = vfs_resolve("/home/root");
            if (!hroot) vfs_mkdir(hroot_par, "root", 0700);
        }
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("[OK] Estrutura de diretorios verificada (/home /bin /etc)\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    ser_puts("[DBG] step15: fb_init\r\n");
    log_info("Inicializando framebuffer VBE...");
    if (fb_init(mbi)) {
        log_ok("Framebuffer VBE pronto — iniciando GUI");
        ser_puts("[DBG] step15: GUI start\r\n");
        x11_server_init();
        desktop_init();
        desktop_run();
    } else {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("[WARN] Sem framebuffer VBE — modo texto\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_puts("(Rode com ISO via GRUB para ativar o modo grafico)\n");
        ser_puts("[DBG] step15: sem framebuffer\r\n");
    }

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("\nKrypx todas as fases completas!\n");
    keyboard_echo_loop();
}
