ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text :
    {
        /* O cabeçalho multiboot PRECISA vir primeiro */
        *(.multiboot)
        *(.text)
    }

    .data : { *(.data) }
    .bss  : { *(.bss) }
}