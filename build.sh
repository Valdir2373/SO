#!/bin/bash

nasm -f elf32 boot.asm -o boot.o

gcc -m32 -c kernel.c -o kernel.o -ffreestanding -O2 -nostdlib -fno-stack-protector

ld -m elf_i386 -T linker.ld -o kernel.bin boot.o kernel.o

if grub2-file --is-x86-multiboot kernel.bin; then
    echo "boooot do so"
    qemu-system-i386 -kernel kernel.bin
else
    echo "Erro: O binário não é um Multiboot válido."
fi