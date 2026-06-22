ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.init_array)
        *(.init_array.*)
    }

    . = 0x40080000;

    .text : ALIGN(4K)
    {
        _text_start = .;
        *(.text*)
        *(.text.*)
        _text_end = .;
    } :text

    .rodata : ALIGN(4K)
    {
        _rodata_start = .;
        *(.rodata*)
        *(.rodata.*)
        . = ALIGN(8);
        _expected_code_crc = .;
        QUAD(0x0000000000000000)
        _rodata_end = .;
    } :rodata

    .data : ALIGN(4K)
    {
        _data_start = .;
        *(.data*)
        *(.data.*)
        *(.got)
        *(.got.plt)
        _data_end = .;
    } :data

    .bss : ALIGN(4K)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        *(.bss.*)
        _bss_end = .;
    } :data

    kernel_phys_end = ALIGN(., 4K);
    kernel_virt_end = kernel_phys_end;
}
