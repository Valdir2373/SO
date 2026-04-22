

#include <gui/desktop.h>
#include <gui/canvas.h>
#include <gui/window.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <kernel/timer.h>
#include <security/users.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <proc/elf.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <net/netif.h>
#include <net/net.h>
#include <net/icmp.h>
#include <drivers/usb_hid.h>
#include <compat/detect.h>
#include <compat/linux_compat.h>
#include <apps/calculator.h>
#include <apps/task_manager.h>
#include <apps/about.h>
#include <apps/network_manager.h>
#include <apps/settings.h>
#include <apps/file_manager.h>
#include <apps/text_editor.h>
#include <apps/image_viewer.h>
#include <apps/kpkg.h>
#include <drivers/ac97.h>
#include <drivers/acpi.h>
#include <lib/png.h>
#include <lib/jpeg.h>
#include <lib/string.h>
#include <types.h>


static bool menu_open = false;


static uint32_t *wp_pixels = 0;
static int       wp_w = 0, wp_h = 0;


typedef struct { const char *name; void (*open)(void); int x,y; uint32_t col; } DIcon;
static void open_image_viewer(void) { image_viewer_open(0); }

static const DIcon ICONS[] = {
    { "Arquivos",  file_manager_open,   20,  20, 0x004A90D9 },
    { "Editor",    text_editor_open,    20,  90, 0x0000B894 },
    { "Imagens",   open_image_viewer,   20, 160, 0x009B59B6 },
    { "Calc",      calculator_open,     20, 230, 0x00E17055 },
    { "Terminal",  0,                   20, 300, 0x00636E72 },
    { 0, 0, 0, 0, 0 }
};
#define ICON_W 64
#define ICON_H 54


static uint32_t last_click_t  = 0;
static int      last_click_x  = -100;
static int      last_click_y  = -100;

#define TASKBAR_HEIGHT  40
#define TASKBAR_Y(h)    ((h) - TASKBAR_HEIGHT)


static window_t *terminal_win = 0;
#define TERM_LINES    22
#define TERM_COLS     72
static char term_buf[TERM_LINES][TERM_COLS + 1];
static int  term_row = 0;
static int  term_col = 0;
static char term_input[256];
static int  term_input_len = 0;
static char term_cwd[256];  


static process_t  *term_shell_proc   = 0;
static term_pipe_t term_shell_stdin_buf;   
static term_pipe_t term_shell_stdout_buf;  
static bool        term_linux_mode  = false;


#define TERM_HIST_SIZE  20
static char term_hist[TERM_HIST_SIZE][256];
static int  term_hist_count = 0;
static int  term_hist_idx   = -1;

static void term_scroll(void) {
    int i;
    for (i = 0; i < TERM_LINES - 1; i++) {
        memcpy(term_buf[i], term_buf[i+1], TERM_COLS + 1);
    }
    memset(term_buf[TERM_LINES-1], 0, TERM_COLS + 1);
    term_row = TERM_LINES - 1;
}

static void term_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
        if (term_row >= TERM_LINES) term_scroll();
    } else if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            term_buf[term_row][term_col] = ' ';
        }
    } else {
        if (term_col >= TERM_COLS) { term_col = 0; term_row++; }
        if (term_row >= TERM_LINES) term_scroll();
        term_buf[term_row][term_col++] = c;
    }
}

static void term_puts(const char *s) {
    while (*s) term_putchar(*s++);
}



static void term_resolve(const char *arg, char *out) {
    if (!arg || !arg[0]) { strcpy(out, term_cwd); return; }
    if (arg[0] == '/') { strncpy(out, arg, 255); out[255]=0; return; }
    if (strcmp(arg, "..") == 0) {
        strcpy(out, term_cwd);
        int l = strlen(out);
        if (l > 1) {
            int i = l - 1;
            if (out[i] == '/') i--;
            while (i > 0 && out[i] != '/') i--;
            out[i ? i : 1] = 0;
        }
        return;
    }
    strcpy(out, term_cwd);
    int l = strlen(out);
    if (out[l-1] != '/') { out[l]='/'; out[l+1]=0; }
    strncat(out, arg, 255 - strlen(out));
}


static void term_put_dec(uint32_t v) {
    char buf[12]; itoa((int)v, buf, 10); term_puts(buf);
}



static void term_cmd_ls(const char *arg) {
    char path[256]; term_resolve(arg, path);
    vfs_node_t *dir = vfs_resolve(path);
    if (!dir) { term_puts("ls: nao encontrado: "); term_puts(path); term_puts("\n"); return; }
    if ((dir->flags & 0x7) != VFS_DIRECTORY) { term_puts("ls: nao e diretorio\n"); return; }
    uint32_t i = 0;
    dirent_t *e;
    while ((e = vfs_readdir(dir, i++)) != 0) {
        if (e->type == VFS_DIRECTORY) term_puts("\x1b[34m[DIR]\x1b[0m ");
        else                          term_puts("      ");
        term_puts(e->name);
        term_puts("\n");
        kfree(e);
    }
}

static void term_cmd_cd(const char *arg) {
    if (!arg || !arg[0]) { strcpy(term_cwd, "/"); return; }
    char path[256]; term_resolve(arg, path);
    vfs_node_t *n = vfs_resolve(path);
    if (!n || (n->flags & 0x7) != VFS_DIRECTORY) { term_puts("cd: nao encontrado\n"); return; }
    strncpy(term_cwd, path, 255);
    int l = strlen(term_cwd);
    if (l > 1 && term_cwd[l-1] == '/') term_cwd[l-1] = 0;
}

static void term_cmd_cat(const char *arg) {
    if (!arg || !arg[0]) { term_puts("uso: cat <arquivo>\n"); return; }
    char path[256]; term_resolve(arg, path);
    vfs_node_t *n = vfs_resolve(path);
    if (!n) { term_puts("cat: nao encontrado\n"); return; }
    if ((n->flags & 0x7) == VFS_DIRECTORY) { term_puts("cat: e um diretorio\n"); return; }
    if (n->size == 0) return;
    uint32_t sz = n->size > 8192 ? 8192 : n->size;
    uint8_t *buf = (uint8_t*)kmalloc(sz + 1);
    if (!buf) { term_puts("cat: sem memoria\n"); return; }
    uint32_t rd = vfs_read(n, 0, sz, buf);
    buf[rd] = 0;
    uint32_t i;
    for (i = 0; i < rd; i++) term_putchar((char)buf[i]);
    if (rd > 0 && buf[rd-1] != '\n') term_putchar('\n');
    kfree(buf);
}

static void term_cmd_mkdir(const char *arg) {
    if (!arg || !arg[0]) { term_puts("uso: mkdir <dir>\n"); return; }
    char path[256]; term_resolve(arg, path);
    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir || !name[0]) { term_puts("mkdir: caminho invalido\n"); return; }
    if (vfs_mkdir(dir, name, 0755) == 0) { term_puts("OK\n"); }
    else term_puts("mkdir: falhou\n");
}

static void term_cmd_rm(const char *arg) {
    if (!arg || !arg[0]) { term_puts("uso: rm <arquivo>\n"); return; }
    char path[256]; term_resolve(arg, path);
    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(path, name);
    if (!dir || !name[0]) { term_puts("rm: caminho invalido\n"); return; }
    if (vfs_unlink(dir, name) == 0) { term_puts("OK\n"); }
    else term_puts("rm: falhou\n");
}

static void term_cmd_echo(const char *arg) {
    if (arg) term_puts(arg);
    term_puts("\n");
}

static void term_ps_cb(process_t *p, void *ctx) {
    (void)ctx;
    char buf[8]; itoa((int)p->pid, buf, 10);
    term_puts(buf); term_puts("   ");
    term_puts(p->name);
    int pad = 14 - (int)strlen(p->name);
    while (pad-- > 0) term_putchar(' ');
    const char *st[] = {"Pronto", "Rodando", "Bloq.", "Zumbi", "Livre"};
    term_puts(st[p->state < 5 ? p->state : 4]);
    term_puts("\n");
}

static void term_cmd_ps(void) {
    term_puts("PID  Nome          Estado\n");
    term_puts("---  ----          ------\n");
    process_iterate(term_ps_cb, 0);
}

static void term_cmd_kill(const char *arg) {
    if (!arg || !arg[0]) { term_puts("uso: kill <pid>\n"); return; }
    uint32_t pid = (uint32_t)atoi(arg);
    if (pid == 0) { term_puts("kill: pid invalido\n"); return; }
    process_kill(pid);
    term_puts("Sinal enviado\n");
}

static void term_cmd_ifconfig(void) {
    if (!net_is_configured()) { term_puts("Rede: nao configurada\n"); return; }
    term_puts("eth0  IP: ");
    uint32_t ip = net_ip;
    term_put_dec((ip >> 24) & 0xFF); term_putchar('.');
    term_put_dec((ip >> 16) & 0xFF); term_putchar('.');
    term_put_dec((ip >> 8)  & 0xFF); term_putchar('.');
    term_put_dec(ip & 0xFF);
    term_puts("\n      MAC: ");
    int i;
    for (i = 0; i < 6; i++) {
        uint8_t b = net_mac[i];
        char h[3];
        h[0] = "0123456789ABCDEF"[b>>4];
        h[1] = "0123456789ABCDEF"[b&0xF];
        h[2] = 0;
        term_puts(h);
        if (i < 5) term_putchar(':');
    }
    term_puts("\n      GW: ");
    uint32_t gw = net_gateway;
    term_put_dec((gw>>24)&0xFF); term_putchar('.');
    term_put_dec((gw>>16)&0xFF); term_putchar('.');
    term_put_dec((gw>>8) &0xFF); term_putchar('.');
    term_put_dec(gw&0xFF);
    term_puts("\n");
}

static uint32_t parse_ip(const char *s) {
    uint32_t a=0,b=0,c=0,d=0;
    int n = 0;
    while (*s) {
        if (*s == '.') n++;
        else {
            int v = *s - '0';
            if (v<0||v>9) return 0;
            if (n==0) a=a*10+v;
            else if(n==1) b=b*10+v;
            else if(n==2) c=c*10+v;
            else d=d*10+v;
        }
        s++;
    }
    return (a<<24)|(b<<16)|(c<<8)|d;
}

static void term_cmd_ping(const char *arg) {
    if (!arg || !arg[0]) { term_puts("uso: ping <ip>\n"); return; }
    if (!net_is_configured()) { term_puts("ping: sem rede\n"); return; }
    uint32_t dst = parse_ip(arg);
    if (!dst) { term_puts("ping: IP invalido\n"); return; }
    term_puts("PING ");
    term_puts(arg);
    term_puts(" ...\n");
    int i;
    for (i = 1; i <= 3; i++) {
        icmp_clear_ping_reply();
        icmp_send_echo(dst, 1, (uint16_t)i);
        uint32_t t0 = timer_get_ticks();
        while (!icmp_get_ping_reply() && timer_get_ticks() - t0 < 2000) {
            netif_poll();
            __asm__ volatile("hlt");
        }
        if (icmp_get_ping_reply()) {
            uint32_t ms = timer_get_ticks() - t0;
            term_puts("  seq="); term_put_dec((uint32_t)i);
            term_puts(" tempo="); term_put_dec(ms); term_puts("ms\n");
        } else {
            term_puts("  seq="); term_put_dec((uint32_t)i);
            term_puts(" timeout\n");
        }
    }
}

static void term_cmd_write(const char *arg) {
    
    if (!arg || !arg[0]) { term_puts("uso: write <arquivo> <texto>\n"); return; }
    const char *sp = arg;
    while (*sp && *sp != ' ') sp++;
    if (!*sp) { term_puts("uso: write <arquivo> <texto>\n"); return; }
    char path[256];
    int plen = (int)(sp - arg);
    if (plen > 255) plen = 255;
    int pi; for (pi=0;pi<plen;pi++) path[pi]=arg[pi]; path[pi]=0;
    sp++; 
    char rpath[256]; term_resolve(path, rpath);
    char name[256];
    vfs_node_t *dir = vfs_resolve_parent(rpath, name);
    if (!dir) { term_puts("write: caminho invalido\n"); return; }
    
    vfs_node_t *node = vfs_resolve(rpath);
    if (!node) {
        vfs_create(dir, name, 0644);
        node = vfs_resolve(rpath);
    }
    if (!node) { term_puts("write: falha ao criar\n"); return; }
    uint32_t len = (uint32_t)strlen(sp);
    vfs_write(node, 0, len, (const uint8_t*)sp);
    node->size = len;
    term_puts("Escrito "); term_put_dec(len); term_puts(" bytes\n");
}

static void term_cmd_help(void) {
    term_puts("Krypx Shell — comandos:\n");
    term_puts("  ls [dir]              listar diretorio\n");
    term_puts("  cd <dir>              mudar diretorio\n");
    term_puts("  pwd                   diretorio atual\n");
    term_puts("  cat <arq>             mostrar conteudo\n");
    term_puts("  write <arq> <txt>     escrever arquivo\n");
    term_puts("  cp <src> <dst>        copiar arquivo\n");
    term_puts("  mkdir <dir>           criar diretorio\n");
    term_puts("  rm <arq>              remover arquivo\n");
    term_puts("  echo <txt>            imprimir texto\n");
    term_puts("  ps                    listar processos\n");
    term_puts("  kill <pid>            matar processo\n");
    term_puts("  ifconfig              info de rede\n");
    term_puts("  ping <ip>             testar conexao\n");
    term_puts("  which <cmd>           localizar binario\n");
    term_puts("  <caminho>             executar binario ELF\n");
    term_puts("  kpkg install <pkg>    instalar pacote\n");
    term_puts("  kpkg list             pacotes instalados\n");
    term_puts("  kpkg search [nome]    buscar pacotes\n");
    term_puts("  clear                 limpar terminal\n");
    term_puts("  version               versao do sistema\n");
    term_puts("  shutdown / reboot     desligar / reiniciar\n");
}




static void term_drain_shell_output(void) {
    if (!term_shell_proc) return;
    int c;
    while ((c = process_stdout_read(term_shell_proc)) >= 0) {
        term_putchar((char)c);
    }
}


static void term_start_linux_shell(void) {
    
    const char *sh_paths[] = { "/bin/sh", "/bin/busybox", "/usr/bin/sh", 0 };
    vfs_node_t *sh_node = 0;
    const char *sh_path = 0;
    int i;
    for (i = 0; sh_paths[i]; i++) {
        sh_node = vfs_resolve(sh_paths[i]);
        if (sh_node && sh_node->size > 0 &&
            (sh_node->flags & 0x7) != VFS_DIRECTORY) {
            sh_path = sh_paths[i];
            break;
        }
    }

    if (!sh_node) {
        term_puts("Linux subsystem: /bin/sh nao encontrado\n");
        term_puts("Execute 'make install-alpine' no host para instalar Alpine base.\n");
        term_puts("Usando shell interno do Krypx (modo fallback).\n");
        return;
    }

    uint32_t size = sh_node->size;
    uint8_t *data = (uint8_t *)kmalloc(size);
    if (!data) { term_puts("Sem memoria para shell\n"); return; }
    vfs_read(sh_node, 0, size, data);

    if (size < 4 || data[0] != 0x7F || data[1] != 'E') {
        kfree(data);
        term_puts("Linux subsystem: /bin/sh invalido (nao e ELF)\n");
        return;
    }

    process_t *proc = process_create("sh", 0, 2);
    if (!proc) { kfree(data); term_puts("Falha ao criar processo\n"); return; }

    
    memset(&term_shell_stdin_buf,  0, sizeof(term_pipe_t));
    memset(&term_shell_stdout_buf, 0, sizeof(term_pipe_t));
    proc->stdin_pipe  = &term_shell_stdin_buf;
    proc->stdout_pipe = &term_shell_stdout_buf;

    strncpy(proc->cwd, "/", 255);
    proc->compat_mode = COMPAT_LINUX;

    elf_load_result_t res;
    if (elf_load(proc, data, size, &res) != 0) {
        kfree(data);
        process_kill(proc->pid);
        term_puts("Falha ao carregar /bin/sh\n");
        return;
    }
    kfree(data);

    proc->ctx.rip    = res.entry_point;
    proc->ctx.rsp    = res.user_stack_top;
    proc->ctx.rflags = 0x202;
    proc->heap_start = res.heap_base;
    proc->heap_end   = res.heap_base;

    scheduler_add(proc);
    term_shell_proc  = proc;
    term_linux_mode  = true;

    term_puts("=== Linux Subsystem (Alpine) ===\n");
    term_puts("Processo: "); term_puts(sh_path); term_puts("\n\n");
}


static const char *exec_path[] = {
    "",          
    "/bin",
    "/usr/bin",
    "/usr/local/bin",
    "/sbin",
    "/usr/sbin",
    0
};


static vfs_node_t *find_in_path(const char *cmd, char *full_out) {
    if (cmd[0] == '/') {
        vfs_node_t *n = vfs_resolve(cmd);
        if (n) { strncpy(full_out, cmd, 255); return n; }
        return 0;
    }
    int i;
    for (i = 0; exec_path[i]; i++) {
        if (!exec_path[i][0]) continue;
        char buf[256];
        strcpy(buf, exec_path[i]);
        strcat(buf, "/");
        strncat(buf, cmd, 255 - strlen(buf));
        vfs_node_t *n = vfs_resolve(buf);
        if (n && (n->flags & 0x7) != VFS_DIRECTORY) {
            strncpy(full_out, buf, 255);
            return n;
        }
    }
    return 0;
}

static void term_run_exec(const char *path, const char *args) {
    char full[256]; full[0] = 0;
    vfs_node_t *node = find_in_path(path, full);
    if (!node) {
        
        char cwdpath[256];
        term_resolve(path, cwdpath);
        node = vfs_resolve(cwdpath);
        if (node) strncpy(full, cwdpath, 255);
    }
    if (!node) {
        term_puts("Comando nao encontrado: "); term_puts(path); term_puts("\n");
        return;
    }
    if ((node->flags & 0x7) == VFS_DIRECTORY) {
        term_puts(path); term_puts(": e um diretorio\n"); return;
    }

    uint32_t size = node->size;
    if (size == 0) { term_puts("Erro: arquivo vazio\n"); return; }

    
    uint8_t *data = (uint8_t *)kmalloc(size);
    if (!data) { term_puts("Erro: sem memoria\n"); return; }
    vfs_read(node, 0, size, data);

    
    if (size < 4 || data[0] != 0x7F || data[1] != 'E' ||
        data[2] != 'L'  || data[3] != 'F') {
        kfree(data);
        term_puts("Erro: nao e um ELF valido\n");
        return;
    }

    const char *pname = full[0] ? full : path;
    const char *t = pname;
    while (*t) { if (*t == '/') pname = t + 1; t++; }

    process_t *proc = process_create(pname, 0, 2);
    if (!proc) { kfree(data); term_puts("Erro: falha ao criar processo\n"); return; }

    
    strncpy(proc->cwd, term_cwd, 255);

    elf_load_result_t res;
    if (elf_load(proc, data, size, &res) != 0) {
        kfree(data); term_puts("Erro: falha ao carregar ELF\n"); return;
    }
    kfree(data);

    proc->ctx.rip    = res.entry_point;
    proc->ctx.rsp    = res.user_stack_top;
    proc->ctx.rflags = 0x202;
    proc->heap_start = res.heap_base;
    proc->heap_end   = res.heap_base;
    proc->compat_mode = COMPAT_LINUX;

    
    

    scheduler_add(proc);

    term_puts("[EXEC] ");
    term_puts(pname);
    if (args && args[0]) { term_puts(" "); term_puts(args); }
    term_puts("\n");

    (void)args;
}


static const char *term_arg(const char *line, int n) {
    
    while (*line == ' ') line++;
    int i;
    for (i = 0; i < n; i++) {
        while (*line && *line != ' ') line++;
        while (*line == ' ') line++;
        if (!*line) return 0;
    }
    return line[0] ? line : 0;
}

static void term_handle_command(const char *cmd) {
    while (*cmd == ' ') cmd++;
    if (!cmd[0]) return;

    
    if (term_hist_count < TERM_HIST_SIZE) {
        strncpy(term_hist[term_hist_count++], cmd, 255);
    } else {
        int i;
        for (i = 0; i < TERM_HIST_SIZE - 1; i++) strcpy(term_hist[i], term_hist[i+1]);
        strncpy(term_hist[TERM_HIST_SIZE-1], cmd, 255);
    }
    term_hist_idx = -1;

    
    char verb[32]; int vi = 0;
    while (cmd[vi] && cmd[vi] != ' ' && vi < 31) { verb[vi] = cmd[vi]; vi++; }
    verb[vi] = 0;

    
    const char *rest = cmd + vi;
    while (*rest == ' ') rest++;

    if (strcmp(verb, "help")    == 0) { term_cmd_help(); }
    else if (strcmp(verb, "clear") == 0) {
        int i; for (i=0;i<TERM_LINES;i++) memset(term_buf[i],0,TERM_COLS+1);
        term_row=0; term_col=0;
    }
    else if (strcmp(verb, "version") == 0) { term_puts("Krypx v0.1.0 — bare-metal x86\n"); }
    else if (strcmp(verb, "uname")   == 0) { term_puts("Krypx 0.1.0 x86 32-bit bare-metal\n"); }
    else if (strcmp(verb, "pwd")   == 0) { term_puts(term_cwd); term_puts("\n"); }
    else if (strcmp(verb, "ls")    == 0) { term_cmd_ls(*rest ? rest : 0); }
    else if (strcmp(verb, "cd")    == 0) { term_cmd_cd(*rest ? rest : 0); }
    else if (strcmp(verb, "cat")   == 0) { term_cmd_cat(*rest ? rest : 0); }
    else if (strcmp(verb, "mkdir") == 0) { term_cmd_mkdir(*rest ? rest : 0); }
    else if (strcmp(verb, "rm")    == 0) { term_cmd_rm(*rest ? rest : 0); }
    else if (strcmp(verb, "echo")  == 0) { term_cmd_echo(*rest ? rest : 0); }
    else if (strcmp(verb, "ps")    == 0) { term_cmd_ps(); }
    else if (strcmp(verb, "kill")  == 0) { term_cmd_kill(*rest ? rest : 0); }
    else if (strcmp(verb, "ifconfig") == 0) { term_cmd_ifconfig(); }
    else if (strcmp(verb, "ping")  == 0) { term_cmd_ping(*rest ? rest : 0); }
    else if (strcmp(verb, "write") == 0) { term_cmd_write(*rest ? rest : 0); }
    else if (strcmp(verb, "meminfo") == 0) {
        extern uint32_t pmm_get_free_pages(void);
        term_puts("RAM livre: "); term_put_dec(pmm_get_free_pages() * 4); term_puts(" KB\n");
    }
    else if (strcmp(verb, "dhcp")   == 0) {
        extern bool dhcp_request(void);
        term_puts("Iniciando DHCP...\n");
        dhcp_request();
        uint32_t t0 = timer_get_ticks();
        while (!net_is_configured() && timer_get_ticks()-t0 < 3000) {
            netif_poll(); __asm__ volatile("hlt");
        }
        if (net_is_configured()) term_puts("IP obtido via DHCP\n");
        else term_puts("DHCP timeout\n");
    }
    else if (strcmp(verb, "shutdown") == 0 || strcmp(verb, "poweroff") == 0) {
        term_puts("Desligando...\n");
        extern void acpi_shutdown(void);
        acpi_shutdown();
    }
    else if (strcmp(verb, "reboot") == 0) {
        term_puts("Reiniciando...\n");
        extern void acpi_reboot(void);
        acpi_reboot();
    }
    else if (strcmp(verb, "kpkg") == 0) {
        char verb2[32]; int v2i = 0;
        while (rest[v2i] && rest[v2i] != ' ' && v2i < 31) { verb2[v2i] = rest[v2i]; v2i++; }
        verb2[v2i] = 0;
        const char *karg = rest + v2i;
        while (*karg == ' ') karg++;

        if (strcmp(verb2, "install") == 0 && karg[0]) {
            char pkgpath[256];
            if (karg[0] == '/') strncpy(pkgpath, karg, 255);
            else { strcpy(pkgpath, KPKG_PKG_DIR "/"); strncat(pkgpath, karg, 200); strncat(pkgpath, ".kpkg", 255 - strlen(pkgpath)); }
            kpkg_install(pkgpath, term_puts);
        } else if (strcmp(verb2, "list") == 0) {
            kpkg_list(term_puts);
        } else if (strcmp(verb2, "search") == 0) {
            kpkg_search(karg[0] ? karg : 0, term_puts);
        } else {
            term_puts("uso: kpkg install <nome> | kpkg list | kpkg search [nome]\n");
        }
    }
    else if (strcmp(verb, "cp") == 0) {
        const char *s2 = rest;
        while (*s2 && *s2 != ' ') s2++;
        while (*s2 == ' ') s2++;
        if (!*s2) { term_puts("uso: cp <origem> <destino>\n"); }
        else {
            char src[256], dst[256];
            int n = (int)(s2 - 1 - rest); if (n > 255) n = 255;
            int si; for (si = 0; si < n; si++) src[si] = rest[si]; src[n] = 0;
            term_resolve(src, dst); 
            char fsrc[256]; strcpy(fsrc, dst);
            term_resolve(s2, dst);
            
            vfs_node_t *snode = vfs_resolve(fsrc);
            if (!snode) { term_puts("cp: origem nao encontrada\n"); }
            else {
                uint8_t *cb = (uint8_t*)kmalloc(snode->size ? snode->size : 1);
                if (cb) {
                    vfs_read(snode, 0, snode->size, cb);
                    char dname[256];
                    vfs_node_t *ddir = vfs_resolve_parent(dst, dname);
                    if (ddir && dname[0]) {
                        vfs_node_t *dn = vfs_resolve(dst);
                        if (!dn) { vfs_create(ddir, dname, 0644); dn = vfs_resolve(dst); }
                        if (dn) { vfs_write(dn, 0, snode->size, cb); dn->size = snode->size; term_puts("OK\n"); }
                    }
                    kfree(cb);
                }
            }
        }
    }
    else if (strcmp(verb, "mv") == 0) {
        
        term_puts("mv: use cp + rm\n");
    }
    else if (strcmp(verb, "chmod") == 0 || strcmp(verb, "chown") == 0) {
        term_puts("OK\n"); 
    }
    else if (strcmp(verb, "export") == 0 || strcmp(verb, "env") == 0) {
        term_puts("DISPLAY=:0\nHOME=/home/root\nPATH=/bin:/usr/bin:/usr/local/bin\n");
    }
    else if (strcmp(verb, "which") == 0 && rest[0]) {
        char fp[256];
        vfs_node_t *n = find_in_path(rest, fp);
        if (n) { term_puts(fp); term_puts("\n"); }
        else { term_puts("which: nao encontrado: "); term_puts(rest); term_puts("\n"); }
    }
    
    else {
        char fp[256];
        vfs_node_t *n = find_in_path(verb, fp);
        if (n && (n->flags & 0x7) != VFS_DIRECTORY) {
            term_run_exec(verb, rest);
        } else if (verb[0] == '/') {
            term_run_exec(verb, rest);
        } else {
            term_puts("Comando nao encontrado: "); term_puts(verb);
            term_puts("\n(tente 'help' ou 'kpkg search')\n");
        }
    }
}

static void term_on_paint(window_t *win) {
    
    if (term_linux_mode) term_drain_shell_output();

    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    int bx = win->content_x, by = win->content_y;
    canvas_fill_rect(bx, by, win->content_w, win->content_h, 0x00080808);

    int row, col;
    for (row = 0; row < TERM_LINES; row++) {
        for (col = 0; col < TERM_COLS; col++) {
            char c = term_buf[row][col];
            if (!c) c = ' ';
            canvas_draw_char(bx + col * CHAR_WIDTH,
                             by + row * CHAR_HEIGHT,
                             c, 0x0000FF41, COLOR_TRANSPARENT);
        }
    }

    int input_y = by + TERM_LINES * CHAR_HEIGHT + 4;
    canvas_fill_rect(bx, input_y, win->content_w, CHAR_HEIGHT + 6, 0x00111111);

    
    char prompt[64];
    if (term_linux_mode) {
        prompt[0] = '>'; prompt[1] = ' '; prompt[2] = 0;
    } else {
        memcpy(prompt, "root@krypx:", 11);
        int pl = 11;
        int cl = (int)strlen(term_cwd);
        if (cl + pl < 50) { memcpy(prompt + pl, term_cwd, (uint32_t)cl); pl += cl; }
        prompt[pl++] = '$'; prompt[pl++] = ' '; prompt[pl] = 0;
    }
    canvas_draw_string(bx + 2, input_y + 3, prompt, 0x0000CEC9, COLOR_TRANSPARENT);
    int prompt_px = bx + 2 + (int)strlen(prompt) * CHAR_WIDTH;
    canvas_draw_string(prompt_px, input_y + 3, term_input, 0x00FFFFFF, COLOR_TRANSPARENT);

    if ((timer_get_ticks() / 500) % 2 == 0) {
        int cx = prompt_px + term_input_len * CHAR_WIDTH;
        canvas_fill_rect(cx, input_y + 3, 2, CHAR_HEIGHT, 0x00FFFFFF);
    }

}

static void term_on_keydown(window_t *win, char key) {
    (void)win;

    
    if (term_linux_mode) term_drain_shell_output();

    if (key == '\n') {
        term_input[term_input_len] = 0;

        if (term_linux_mode && term_shell_proc &&
            term_shell_proc->state != PROC_ZOMBIE) {
            
            int j;
            for (j = 0; j < term_input_len; j++)
                process_stdin_push(term_shell_proc, term_input[j]);
            process_stdin_push(term_shell_proc, '\n');
            
            term_puts(term_input);
            term_putchar('\n');
        } else {
            
            term_puts("> ");
            term_puts(term_input);
            term_puts("\n");
            term_handle_command(term_input);
        }
        term_input_len = 0;
        term_input[0]  = 0;

    } else if (key == '\b') {
        if (term_input_len > 0) {
            term_input_len--;
            term_input[term_input_len] = 0;
        }
    } else if (key >= 32 && key < 127 && term_input_len < 255) {
        term_input[term_input_len++] = key;
        term_input[term_input_len]   = 0;
    }
}

static void draw_wallpaper(void) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    if (wp_pixels && wp_w > 0 && wp_h > 0) {
        canvas_draw_scaled_bitmap(0, 0, (int)fb.width, (int)fb.height - TASKBAR_HEIGHT,
                                  wp_pixels, wp_w, wp_h);
    } else {
        canvas_fill_gradient(0, 0, fb.width, fb.height - TASKBAR_HEIGHT,
                             0x000C2461, 0x001A1A2E);
        int lx = (int)fb.width / 2 - 100;
        int ly = (int)fb.height / 2 - 50;
        canvas_draw_string(lx, ly, "K R Y P X", 0x00FFFFFF, COLOR_TRANSPARENT);
        canvas_draw_string(lx + 4, ly + 20, "Custom OS v0.1", 0x00636E72, COLOR_TRANSPARENT);
    }

    
    int i;
    for (i = 0; ICONS[i].name; i++) {
        int ix = ICONS[i].x, iy = ICONS[i].y;
        canvas_fill_rounded_rect(ix, iy, ICON_W, ICON_W, 8, ICONS[i].col);
        
        char ltr[2] = { ICONS[i].name[0], 0 };
        canvas_draw_string(ix + ICON_W/2 - 4, iy + ICON_W/2 - 8, ltr, 0x00FFFFFF, COLOR_TRANSPARENT);
        
        int lw = canvas_string_width(ICONS[i].name);
        canvas_draw_string(ix + ICON_W/2 - lw/2, iy + ICON_W + 2,
                           ICONS[i].name, 0x00FFFFFF, COLOR_TRANSPARENT);
    }
}

void desktop_set_wallpaper(const char *path) {
    if (!path || !path[0]) return;

    
    int plen = (int)strlen(path);
    const char *ext = path + plen;
    while (ext > path && *ext != '.') ext--;
    bool is_png  = (strcmp(ext, ".png")  == 0 || strcmp(ext, ".PNG")  == 0);
    bool is_jpg  = (strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0 ||
                    strcmp(ext, ".JPG")  == 0 || strcmp(ext, ".JPEG") == 0);
    if (!is_png && !is_jpg) return;

    vfs_node_t *node = vfs_resolve(path);
    if (!node || node->size == 0 || node->size > 4 * 1024 * 1024) return;

    uint8_t *buf = (uint8_t *)kmalloc(node->size);
    if (!buf) return;
    vfs_read(node, 0, node->size, buf);

    int nw = 0, nh = 0;
    uint32_t *pix = 0;
    if (is_png)      pix = png_decode(buf, node->size, &nw, &nh);
    else if (is_jpg) pix = jpeg_decode(buf, node->size, &nw, &nh);
    kfree(buf);

    if (pix && nw > 0 && nh > 0) {
        if (wp_pixels) kfree(wp_pixels);
        wp_pixels = pix; wp_w = nw; wp_h = nh;
    }
}

static void draw_taskbar(void) {
    int ty = TASKBAR_Y(fb.height);

    
    canvas_fill_gradient(0, ty, fb.width, TASKBAR_HEIGHT,
                         0x001E272E, 0x00141414);
    canvas_fill_rect(0, ty, fb.width, 1, KRYPX_ACCENT);  

    
    canvas_fill_rounded_rect(8, ty + 6, 70, 28, 5, KRYPX_BUTTON);
    canvas_draw_string(18, ty + 12, "Menu", COLOR_WHITE, COLOR_TRANSPARENT);

    
    bool term_min = terminal_win && (terminal_win->flags & WIN_MINIMIZED);
    uint32_t term_col = term_min ? 0x00555566 : 0x00333333;
    canvas_fill_rounded_rect(86, ty + 6, 90, 28, 5, term_col);
    canvas_draw_string(96, ty + 12, "Terminal", 0x00DFE6E9, COLOR_TRANSPARENT);

    
    wm_draw_taskbar_entries(184, ty + 6, 110, 28, fb.width - 220);

    
    uint32_t secs  = timer_get_seconds();
    uint32_t hh    = (secs / 3600) % 24;
    uint32_t mm    = (secs / 60) % 60;
    uint32_t ss    = secs % 60;
    char clock[16];
    clock[0]  = '0' + hh / 10; clock[1]  = '0' + hh % 10;
    clock[2]  = ':';
    clock[3]  = '0' + mm / 10; clock[4]  = '0' + mm % 10;
    clock[5]  = ':';
    clock[6]  = '0' + ss / 10; clock[7]  = '0' + ss % 10;
    clock[8]  = 0;
    canvas_draw_string(fb.width - 80, ty + 12, clock,
                       0x00DFE6E9, COLOR_TRANSPARENT);

    
    {
        int vol_x = (int)fb.width - 210;
        canvas_fill_rounded_rect(vol_x,      ty + 7, 22, 26, 4, 0x00333333);
        canvas_draw_string(vol_x + 7,   ty + 12, "-", 0x00DFE6E9, COLOR_TRANSPARENT);
        canvas_fill_rounded_rect(vol_x + 26, ty + 7, 22, 26, 4, 0x00333333);
        canvas_draw_string(vol_x + 32,  ty + 12, "+", 0x00DFE6E9, COLOR_TRANSPARENT);
        if (ac97_available()) {
            int vl = ac97_get_volume() * 40 / 100;
            canvas_fill_rect(vol_x - 44, ty + 15, 40, 10, 0x00222222);
            if (vl > 0) canvas_fill_rect(vol_x - 44, ty + 15, vl, 10, KRYPX_ACCENT);
        }
    }

    if (current_user) {
        canvas_draw_string(fb.width - 200, ty + 12, current_user->username,
                           0x0074B9FF, COLOR_TRANSPARENT);
    } else {
        canvas_draw_string(fb.width - 200, ty + 12, "Krypx v0.1",
                           0x00636E72, COLOR_TRANSPARENT);
    }
}


typedef struct { const char *name; void (*open)(void); } menu_item_t;
static const menu_item_t menu_items[] = {
    { "Gerenc. Arquivos", file_manager_open   },
    { "Editor de Texto",  text_editor_open    },
    { "Calculadora",      calculator_open     },
    { "Task Manager",     task_manager_open   },
    { "Rede",             network_manager_open},
    { "Configuracoes",    settings_open       },
    { "Sobre o Krypx",    about_open          },
    { NULL, NULL }
};
#define MENU_ITEM_H   28
#define MENU_W       180

static void draw_menu(void) {
    int ty = TASKBAR_Y(fb.height);
    int nitems = 0;
    while (menu_items[nitems].name) nitems++;
    int mh = nitems * MENU_ITEM_H + 8;
    int mx = 8, my = ty - mh - 4;

    canvas_fill_rounded_rect(mx, my, MENU_W, mh, 6, 0x00263545);
    canvas_draw_rounded_rect(mx, my, MENU_W, mh, 6, 0x00636E72);

    int i;
    for (i = 0; i < nitems; i++) {
        int iy = my + 4 + i * MENU_ITEM_H;
        canvas_draw_string(mx + 12, iy + 8, menu_items[i].name,
                           0x00DFE6E9, COLOR_TRANSPARENT);
        
        if (i < nitems-1)
            canvas_draw_line(mx+4, iy+MENU_ITEM_H-1, mx+MENU_W-4, iy+MENU_ITEM_H-1, 0x00333333);
    }
}


static void menu_geometry(int *mx_out, int *my_out, int *mh_out, int *nitems_out) {
    int ty = TASKBAR_Y(fb.height);
    int nitems = 0;
    while (menu_items[nitems].name) nitems++;
    int mh = nitems * MENU_ITEM_H + 8;
    *mx_out    = 8;
    *my_out    = ty - mh - 4;
    *mh_out    = mh;
    *nitems_out = nitems;
}


static bool is_in_menu(int x, int y) {
    int mx, my, mh, nitems;
    menu_geometry(&mx, &my, &mh, &nitems);
    return (x >= mx && x <= mx + MENU_W && y >= my && y <= my + mh);
}


static void menu_handle_click(int x, int y) {
    int mx, my, mh, nitems;
    menu_geometry(&mx, &my, &mh, &nitems);

    if (x < mx || x > mx + MENU_W || y < my || y > my + mh) {
        menu_open = false; return;
    }
    int idx = (y - my - 4) / MENU_ITEM_H;
    if (idx >= 0 && idx < nitems && menu_items[idx].open) {
        menu_items[idx].open();
        menu_open = false;
    }
}


static void desktop_taskbar_click(int x, int y) {
    int ty = TASKBAR_Y(fb.height);

    
    if (x >= 8 && x <= 78 && y >= ty + 6 && y <= ty + 34) {
        menu_open = !menu_open;
        return;
    }
    
    if (x >= 86 && x <= 176 && y >= ty + 6 && y <= ty + 34) {
        if (terminal_win) {
            if (terminal_win->flags & WIN_MINIMIZED) {
                terminal_win->flags &= ~WIN_MINIMIZED;
                wm_focus(terminal_win);
            } else {
                terminal_win->flags |= WIN_MINIMIZED;
            }
        }
        return;
    }
    
    
    int vol_x = (int)fb.width - 210;
    if (y >= ty + 7 && y <= ty + 33) {
        if (x >= vol_x && x <= vol_x + 22) {
            int v = ac97_get_volume() - 10;
            if (v < 0) v = 0;
            ac97_set_volume(v);
            return;
        }
        if (x >= vol_x + 26 && x <= vol_x + 48) {
            int v = ac97_get_volume() + 10;
            if (v > 100) v = 100;
            ac97_set_volume(v);
            return;
        }
    }

    wm_taskbar_entry_click(x, y, 184, ty + 6, 110, 28, (int)fb.width - 220);
}


static void desktop_icon_click(int mx, int my) {
    int i;
    for (i = 0; ICONS[i].name; i++) {
        if (mx >= ICONS[i].x && mx <= ICONS[i].x + ICON_W &&
            my >= ICONS[i].y && my <= ICONS[i].y + ICON_W + 14) {
            uint32_t now = timer_get_ticks();
            int dx = mx - last_click_x, dy = my - last_click_y;
            if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
            if (now - last_click_t < 600 && dx < 10 && dy < 10) {
                
                if (ICONS[i].open) {
                    ICONS[i].open();
                } else {
                    
                    if (terminal_win) {
                        terminal_win->flags &= ~WIN_MINIMIZED;
                        wm_focus(terminal_win);
                    }
                }
                last_click_t = 0;
            } else {
                last_click_t = now;
                last_click_x = mx;
                last_click_y = my;
            }
            return;
        }
    }
    last_click_t = 0;
}

void desktop_render(void) {
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);
    draw_wallpaper();
    draw_taskbar();
    if (menu_open) draw_menu();
    wm_render();
}

void desktop_init(void) {
    strcpy(term_cwd, "/");
    ac97_init();
    wm_init();

    
    terminal_win = wm_create("Krypx Terminal",
                             50, 50,
                             TERM_COLS * CHAR_WIDTH + 24,
                             TERM_LINES * CHAR_HEIGHT + CHAR_HEIGHT + 52,
                             WIN_RESIZABLE);
    if (terminal_win) {
        terminal_win->bg_color    = 0x00111111;
        terminal_win->on_paint    = term_on_paint;
        terminal_win->on_keydown  = term_on_keydown;
        term_puts("Krypx Terminal — Linux Subsystem\n");
        term_puts("Iniciando shell...\n\n");

        
        term_start_linux_shell();
        if (!term_linux_mode) {
            
            term_puts("Shell interno Krypx ativo.\n");
            term_puts("Digite 'help' para ver comandos.\n\n");
        }

        { process_t *p = process_create_app("Terminal", 128 * 1024);
          if (p) terminal_win->proc_pid = p->pid; }
    }

    
    window_t *about = wm_create("Sobre o Krypx",
                                fb.width/2 - 200, fb.height/2 - 120,
                                400, 240, WIN_RESIZABLE);
    if (about) {
        about->bg_color = 0x001E272E;
        about->on_paint = 0;  
    }
}


static void draw_login_screen(const char *username, uint32_t ulen,
                               const char *pass_mask, uint32_t plen,
                               bool field_user, bool error)
{
    
    canvas_init(fb.backbuf, fb.width, fb.height, fb.pitch);

    
    canvas_fill_rect(0, 0, fb.width, fb.height, 0x000C2461);

    
    int cx = (int)fb.width / 2;
    canvas_draw_string((uint32_t)(cx - 60), 80, "Krypx OS", 0x0074B9FF, COLOR_TRANSPARENT);
    canvas_draw_string((uint32_t)(cx - 80), 110, "Sistema Seguro — Ring 0/3", 0x00636E72, COLOR_TRANSPARENT);

    
    int bx = cx - 160;
    int by = (int)fb.height / 2 - 100;
    canvas_fill_rounded_rect((uint32_t)bx, (uint32_t)by, 320, 200, 8, 0x001E272E);
    canvas_draw_rounded_rect((uint32_t)bx, (uint32_t)by, 320, 200, 8, 0x00636E72);

    canvas_draw_string((uint32_t)(bx + 10), (uint32_t)(by + 12), "Login — Krypx", 0x00DFE6E9, COLOR_TRANSPARENT);

    
    uint32_t ub_col = field_user ? 0x0074B9FF : 0x00636E72;
    canvas_fill_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+40), 300, 30, 4, 0x00141E26);
    canvas_draw_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+40), 300, 30, 4, ub_col);
    canvas_draw_string((uint32_t)(bx+16), (uint32_t)(by+48), "Usuario: ", 0x00636E72, COLOR_TRANSPARENT);
    canvas_draw_string((uint32_t)(bx+90), (uint32_t)(by+48), username, 0x00DFE6E9, COLOR_TRANSPARENT);
    if (field_user && (timer_get_ticks() % 60 < 30))
        canvas_fill_rect((uint32_t)(bx+90+(int)ulen*8), (uint32_t)(by+48), 2, 14, 0x0074B9FF);

    
    uint32_t pb_col = !field_user ? 0x0074B9FF : 0x00636E72;
    canvas_fill_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+84), 300, 30, 4, 0x00141E26);
    canvas_draw_rounded_rect((uint32_t)(bx+10), (uint32_t)(by+84), 300, 30, 4, pb_col);
    canvas_draw_string((uint32_t)(bx+16), (uint32_t)(by+92), "Senha:   ", 0x00636E72, COLOR_TRANSPARENT);
    canvas_draw_string((uint32_t)(bx+90), (uint32_t)(by+92), pass_mask, 0x00DFE6E9, COLOR_TRANSPARENT);
    if (!field_user && (timer_get_ticks() % 60 < 30))
        canvas_fill_rect((uint32_t)(bx+90+(int)plen*8), (uint32_t)(by+92), 2, 14, 0x0074B9FF);

    
    canvas_fill_rounded_rect((uint32_t)(bx+90), (uint32_t)(by+130), 140, 32, 6, 0x000984E3);
    canvas_draw_string((uint32_t)(bx+126), (uint32_t)(by+142), "Entrar (Enter)", 0x00FFFFFF, COLOR_TRANSPARENT);

    
    if (error) {
        canvas_draw_string((uint32_t)(bx+10), (uint32_t)(by+174), "Credenciais invalidas!", 0x00D63031, COLOR_TRANSPARENT);
    }

    canvas_draw_string((uint32_t)(cx - 120), (uint32_t)(by + 215),
                       "Tab: trocar campo  |  Enter: confirmar",
                       0x00636E72, COLOR_TRANSPARENT);

    fb_swap();
}

static void login_screen(void) {
    char username[USERNAME_MAX]; uint32_t ulen = 0;
    char password[64];            uint32_t plen = 0;
    char pass_mask[64];           pass_mask[0] = '\0';
    bool field_user = true;
    bool error      = false;

    memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));

    while (1) {
        draw_login_screen(username, ulen, pass_mask, plen, field_user, error);

        char c = keyboard_getchar();
        if (!c) { __asm__ volatile("hlt"); continue; }

        if (c == '\t') {
            field_user = !field_user;
            error = false;
        } else if (c == '\n') {
            
            if (user_authenticate(username, password) == 0) return; 
            error = true;
            plen = 0; password[0] = '\0'; pass_mask[0] = '\0';
        } else if (c == '\b') {
            if (field_user) {
                if (ulen > 0) { ulen--; username[ulen] = '\0'; }
            } else {
                if (plen > 0) { plen--; password[plen] = '\0'; pass_mask[plen] = '\0'; }
            }
            error = false;
        } else if (c >= 0x20 && c < 0x7F) {
            if (field_user && ulen < USERNAME_MAX - 1) {
                username[ulen++] = c; username[ulen] = '\0';
            } else if (!field_user && plen < 63) {
                password[plen]   = c; plen++;
                password[plen]   = '\0';
                pass_mask[plen-1] = '*'; pass_mask[plen] = '\0';
            }
            error = false;
        }
    }
}

void desktop_run(void) {
    login_screen();

    uint32_t last_render  = 0;
    uint8_t  prev_btns    = 0;

    while (1) {
        
        uint32_t now = timer_get_ticks();
        if (now - last_render >= 33) {
            last_render = now;
            desktop_render();
        }

        
        {
            int mx = mouse_get_x();
            int my = mouse_get_y();
            uint8_t btns = mouse_get_buttons();
            int ty = (int)fb.height - TASKBAR_HEIGHT;

            
            wm_mouse_move(mx, my);

            
            if ((btns & 1) && !(prev_btns & 1)) {
                if (menu_open && is_in_menu(mx, my)) {
                    
                    menu_handle_click(mx, my);
                } else if (my >= ty) {
                    
                    if (menu_open) menu_open = false;
                    desktop_taskbar_click(mx, my);
                } else {
                    if (menu_open) { menu_open = false; }
                    
                    if (!wm_window_at(mx, my)) {
                        desktop_icon_click(mx, my);
                    }
                    wm_mouse_down(mx, my, 0);
                }
            }

            
            if (!(btns & 1) && (prev_btns & 1)) {
                wm_mouse_up(mx, my, 0);
            }

            prev_btns = btns;
        }

        
        {
            char key = keyboard_getchar();
            if (key) {
                if (key == '\x1B') {
                    menu_open = !menu_open;
                } else if (key == '1' && menu_open) {
                    file_manager_open();   menu_open = false;
                } else if (key == '2' && menu_open) {
                    text_editor_open();    menu_open = false;
                } else if (key == '3' && menu_open) {
                    calculator_open();     menu_open = false;
                } else if (key == '4' && menu_open) {
                    task_manager_open();   menu_open = false;
                } else if (key == '5' && menu_open) {
                    network_manager_open();menu_open = false;
                } else if (key == '6' && menu_open) {
                    settings_open();       menu_open = false;
                } else if (key == '7' && menu_open) {
                    about_open();          menu_open = false;
                } else {
                    wm_key_event(key);
                }
            }
        }

        netif_poll();
        usb_hid_poll();
        __asm__ volatile ("hlt");
    }
}
