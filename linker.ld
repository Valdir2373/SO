

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

SECTIONS {
    . = 0x00100000;

    
    .multiboot ALIGN(4) : {
        *(.multiboot)
    }

    
    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    }

    
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }

    
    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    
    . = ALIGN(4096);
    kernel_end = .;
}
