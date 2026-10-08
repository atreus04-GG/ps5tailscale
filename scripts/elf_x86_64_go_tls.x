OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

PHDRS {
    ph_text   PT_LOAD FLAGS (0x7);
    ph_rodata PT_LOAD FLAGS (0x6);
    ph_data   PT_LOAD FLAGS (0x6);
    ph_tls    PT_TLS FLAGS (0x4);
    ph_dyn    PT_DYNAMIC;
}

SECTIONS {
    PROVIDE_HIDDEN (__image_start = .);

    .text : ALIGN(CONSTANT(MAXPAGESIZE)) {
        PROVIDE_HIDDEN (__text_start = .);
        *(.text .text.*)
        PROVIDE_HIDDEN (__text_end = .);
    } : ph_text

    .plt : { *(.plt) }

    .eh_frame_hdr : ALIGN(CONSTANT(MAXPAGESIZE)) {
        PROVIDE_HIDDEN(__eh_frame_hdr_start = .);
        KEEP(*(.eh_frame_hdr))
        PROVIDE_HIDDEN(__eh_frame_hdr_end = .);
    } : ph_rodata
    .eh_frame : {
        PROVIDE_HIDDEN(__eh_frame_start = .);
        KEEP(*(.eh_frame))
        PROVIDE_HIDDEN(__eh_frame_end = .);
    }

    .gcc_except_table : { *(.gcc_except_table*) }
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*); }
    .got : { *(.got) }
    .got.plt : { *(.got.plt) }

    .rodata : { *(.rodata .rodata.*) }

    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_start = .);
    }
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    }
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }

    .dynsym : { *(.dynsym) } : ph_rodata
    .hash : { *(.hash) } : ph_rodata
    .gnu.hash : { *(.gnu.hash) } : ph_rodata
    .dynstr : { *(.dynstr) } : ph_rodata
    .rela.dyn : { *(.rela.dyn .rela.dyn.*) } : ph_rodata
    .rela.plt : { *(.rela.plt .rela.plt.*) } : ph_rodata
    .note : { *(.note .note.*) } : ph_rodata

    .dynamic : ALIGN(CONSTANT(MAXPAGESIZE)) {
        PROVIDE_HIDDEN (_DYNAMIC = .);
        *(.dynamic)
    } : ph_rodata : ph_dyn

    .tdata : ALIGN(16) {
        *(.tdata .tdata.*)
    } : ph_data : ph_tls

    .tbss : ALIGN(16) {
        *(.tbss .tbss.*)
    } : ph_data : ph_tls

    .data : ALIGN(CONSTANT(MAXPAGESIZE)) {
        *(.data .data.*)
    } : ph_data
    .bss : {
        PROVIDE_HIDDEN (__bss_start = .);
        *(.bss .bss.*);
        *(COMMON)
        PROVIDE_HIDDEN (__bss_end = .);
    }

    PROVIDE_HIDDEN (__image_end = .);
}
