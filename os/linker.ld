ENTRY(_start)

PHDRS
{
    boot_text PT_LOAD FLAGS(5);   /* R+X, .boot.text */
    boot_data PT_LOAD FLAGS(6);   /* R+W, .boot.data/.boot.bss */
    text      PT_LOAD FLAGS(5);   /* R+X, kernel .text */
    rodata    PT_LOAD FLAGS(4);   /* R,   kernel .rodata */
    data      PT_LOAD FLAGS(6);   /* R+W, kernel .data/.bss */
}

SECTIONS
{
    . = 0x200000;

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
    }

    .boot_text : ALIGN(4K)
    {
        KEEP(*(.multiboot_header))
        *(.boot.text)
        *(.boot.rodata)
    } :boot_text

    .boot_data : ALIGN(4K)
    {
        *(.boot.data)
        *(.boot.bss)
    } :boot_data

    . = ALIGN(., 4K);
    boot_vma_end = .;
    phys_lma = boot_vma_end;

    . = 0xFFFF800000000000 + boot_vma_end;

    .text : AT(phys_lma) ALIGN(4K)
    {
        *(.text*)
        *(.text.*)
        *(.ltext*)
        *(.ltext.*)
    } :text
    phys_lma = ALIGN(phys_lma + SIZEOF(.text), 4K);

    .rodata : AT(phys_lma) ALIGN(4K)
    {
        *(.rodata*)
        *(.rodata.*)
        *(.lrodata*)
        *(.lrodata.*)
    } :rodata
    phys_lma = ALIGN(phys_lma + SIZEOF(.rodata), 4K);

    .data : AT(phys_lma) ALIGN(4K)
    {
        *(.data*)
        *(.data.*)
        *(.ldata*)
        *(.ldata.*)
        *(.got)
        *(.got.plt)
    } :data
    phys_lma = ALIGN(phys_lma + SIZEOF(.data), 4K);

    .bss : AT(phys_lma) ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
        *(.bss.*)
        *(.lbss*)
        *(.lbss.*)
    } :data

    kernel_phys_end = ALIGN(phys_lma + SIZEOF(.bss), 4K);
    kernel_virt_end = ALIGN(., 4K);
}
