# Makefile — Sistema de build do Krypx
# Usa i686-elf-gcc (cross-compiler) se disponível, senão gcc -m32 como fallback.
# Para instalar o cross-compiler: https://wiki.osdev.org/GCC_Cross-Compiler

# ============================================================
# Detecta toolchain disponível
# ============================================================
ifeq ($(shell which i686-elf-gcc 2>/dev/null),)
    CC      := gcc -m32
    LD      := ld -m elf_i386
    OBJCOPY := objcopy
    $(info [WARN] i686-elf-gcc nao encontrado — usando gcc -m32 como fallback)
else
    CC      := i686-elf-gcc
    LD      := i686-elf-ld
    OBJCOPY := i686-elf-objcopy
endif

AS      := nasm
ISO     := grub2-mkrescue
QEMU    := qemu-system-i386

# ============================================================
# Flags
# ============================================================
CFLAGS := -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
          -fno-exceptions -fno-stack-protector -fno-builtin \
          -fno-pie -fno-pic \
          -nostdlib -nostdinc \
          -I. -Iinclude -Ilib -Idrivers -Ifs -Imm -Iproc -Inet -Igui -Isecurity

ASFLAGS := -f elf32

LDFLAGS := -T linker.ld -nostdlib

# ============================================================
# Arquivos fonte
# ============================================================
ASM_SOURCES := boot/boot.asm \
               boot/gdt.asm \
               boot/idt.asm \
               boot/isr.asm \
               boot/switch.asm

C_SOURCES   := kernel/kernel.c \
               kernel/gdt.c \
               kernel/idt.c \
               kernel/timer.c \
               drivers/vga.c \
               drivers/keyboard.c \
               drivers/ide.c \
               mm/pmm.c \
               mm/vmm.c \
               mm/heap.c \
               lib/string.c \
               fs/vfs.c \
               fs/fat32.c \
               proc/process.c \
               proc/scheduler.c \
               kernel/syscall.c \
               drivers/framebuffer.c \
               gui/canvas.c \
               gui/window.c \
               gui/desktop.c

# Objetos gerados
ASM_OBJECTS := $(ASM_SOURCES:.asm=.o)
C_OBJECTS   := $(C_SOURCES:.c=.o)
ALL_OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)

# ============================================================
# Alvos principais
# ============================================================
.PHONY: all iso run run-debug clean

all: kernel.bin

# Linka o kernel
kernel.bin: $(ALL_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJECTS)
	@echo "[LD]  kernel.bin gerado"
	@size $@

# Gera a ISO bootável
iso: kernel.bin
	@mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	cp grub.cfg iso/boot/grub/grub.cfg
	$(ISO) -o Krypx.iso iso/
	@echo "[ISO] Krypx.iso gerada"
	@ls -lh Krypx.iso

# Roda no QEMU (modo normal)
run: iso
	$(QEMU) -cdrom Krypx.iso -m 256M \
	    -vga std \
	    -boot d \
	    -serial stdio \
	    -no-reboot \
	    -no-shutdown

# Roda com debug GDB (para debugar: gdb kernel.bin -ex "target remote :1234")
run-debug: iso
	$(QEMU) -cdrom Krypx.iso -m 256M \
	    -vga std \
	    -boot d \
	    -serial stdio \
	    -s -S \
	    -no-reboot \
	    -no-shutdown

# Roda sem ISO (direto com kernel multiboot — mais rápido para desenvolvimento)
run-kernel: kernel.bin
	$(QEMU) -kernel kernel.bin -m 256M \
	    -vga std \
	    -serial stdio \
	    -no-reboot \
	    -no-shutdown

# ============================================================
# Regras de compilação
# ============================================================

# Assembly (NASM)
%.o: %.asm
	@echo "[AS]  $<"
	$(AS) $(ASFLAGS) -o $@ $<

# C
%.o: %.c
	@echo "[CC]  $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# Limpeza
# ============================================================
clean:
	rm -f $(ALL_OBJECTS) kernel.bin Krypx.iso
	rm -f iso/boot/kernel.bin
	@echo "[CLEAN] Feito"
