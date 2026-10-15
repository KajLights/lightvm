ENTRY(native_entry)
SECTIONS
{
    . = 0;
    .text : ALIGN(4) {
        KEEP(*(.text.native_entry))
        *(.text .text.*)
    }
    .rodata : ALIGN(4) { *(.rodata .rodata.* .srodata .srodata.*) }
    .data : ALIGN(4) { *(.data .data.* .sdata .sdata.* .got .got.*) }
    .bss : ALIGN(4) { *(.bss .bss.* .sbss .sbss.* COMMON) }
    ASSERT(native_entry == 0, "native entry must be at payload offset zero")
    ASSERT(SIZEOF(.data) == 0, "native package cannot contain mutable data or GOT")
    ASSERT(SIZEOF(.bss) == 0, "native package cannot contain mutable globals")
    /DISCARD/ : { *(.comment .eh_frame .eh_frame_hdr .note .note.*) }
}
