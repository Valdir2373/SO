/*
 * tools/test_linux.c — Executável de teste Linux para rodar no Krypx
 *
 * Usa syscalls Linux i386 (int $0x80) diretamente — zero libc, zero CRT.
 * Isso produz um binário de ~1-2 KB quando compilado com strip.
 *
 * Para compilar:
 *   gcc -m32 -static -nostdlib -nostartfiles -fno-builtin \
 *       -fno-pie -fno-pic -Os \
 *       -e _start \
 *       -o tools/test_linux tools/test_linux.c
 *   strip tools/test_linux
 *
 * Para instalar no disco do Krypx:
 *   sudo mount -o loop disk.img /mnt
 *   sudo cp tools/test_linux /mnt/
 *   sudo umount /mnt
 *
 * O Krypx detecta "/test_linux" no disco FAT32 como Linux ELF e
 * executa via ambiente de compatibilidade (int 0x80 traduzido).
 */

/* Wrapper de syscall write(4) */
static void do_write(const char *buf, int len) {
    __asm__ volatile (
        "int $0x80"
        :: "a"(4), "b"(1), "c"(buf), "d"(len)
        : "memory"
    );
}

/* Wrapper de syscall exit(1) */
static void do_exit(int code) {
    __asm__ volatile (
        "int $0x80"
        :: "a"(1), "b"(code)
        : "memory"
    );
    /* Nunca retorna — loop de segurança */
    for (;;) __asm__ volatile ("hlt");
}

/*
 * WRITE(s) — macro que usa sizeof para evitar qualquer chamada strlen.
 * Funciona apenas com literais de string estáticos.
 */
#define WRITE(s)  do_write((s), sizeof(s) - 1)

/* Ponto de entrada — sem CRT, sem libc */
void _start(void) {
    WRITE("\n");
    WRITE("====================================================\n");
    WRITE("   Linux ELF i386 rodando dentro do Krypx OS!      \n");
    WRITE("====================================================\n");
    WRITE("\n");
    WRITE("[OK] Deteccao binaria  -> Linux ELF i386\n");
    WRITE("[OK] ELF Loader        -> segmentos PT_LOAD mapeados\n");
    WRITE("[OK] Ambiente Linux    -> processo isolado criado\n");
    WRITE("[OK] Syscall write(4)  -> VGA exibindo este texto\n");
    WRITE("[OK] Syscall exit(1)   -> processo terminando...\n");
    WRITE("\n");
    WRITE("  Sem libc. Sem CRT. So int $0x80 puro.\n");
    WRITE("  O Krypx traduz syscalls Linux para seu kernel.\n");
    WRITE("\n");
    do_exit(0);
}
