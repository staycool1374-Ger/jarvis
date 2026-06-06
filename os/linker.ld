ENTRY(_start)

PHDRS
{
    boot   PT_LOAD FLAGS(7);
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x200000;

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
    }

    .boot : ALIGN(4K)
    {
        KEEP(*(.multiboot_header))
        *(.boot.text)
        *(.boot.rodata)
        *(.boot.data)
        *(.boot.bss)
    } :boot

    . = ALIGN(., 4K);
    boot_vma_end = .;
    phys_lma = boot_vma_end;

    . = 0xFFFF800000000000 + boot_vma_end;

    .text : AT(phys_lma) ALIGN(4K)
    {
        *(.text*)
        *(.text.*)
    } :text
    phys_lma = ALIGN(phys_lma + SIZEOF(.text), 4K);

    .rodata : AT(phys_lma) ALIGN(4K)
    {
        *(.rodata*)
        *(.rodata.*)
    } :rodata
    phys_lma = ALIGN(phys_lma + SIZEOF(.rodata), 4K);

    .data : AT(phys_lma) ALIGN(4K)
    {
        *(.data*)
        *(.data.*)
        *(.got)
        *(.got.plt)
    } :data
    phys_lma = ALIGN(phys_lma + SIZEOF(.data), 4K);

    .bss : AT(phys_lma) ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
        *(.bss.*)
    } :data

    kernel_phys_end = ALIGN(phys_lma + SIZEOF(.bss), 4K);
    kernel_virt_end = ALIGN(., 4K);
}
