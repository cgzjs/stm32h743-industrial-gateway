ENTRY(Reset_Handler)
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x1000;
_Min_Stack_Size = 0x4000;
MEMORY {
 FLASH (rx) : ORIGIN = 0x08020000, LENGTH = 1920K
 RAM (xrw) : ORIGIN = 0x24000000, LENGTH = 512K
 DMA (xrw) : ORIGIN = 0x30000000, LENGTH = 256K
}
SECTIONS {
 .isr_vector : { . = ALIGN(512); KEEP(*(.isr_vector)) . = ALIGN(4); _isr_vector_end = .; } > FLASH
 .image_identity ORIGIN(FLASH)+0x400 : { KEEP(*(.image_identity)) } > FLASH
 ASSERT(_isr_vector_end <= ORIGIN(FLASH)+0x400,"Vector table overlaps identity")
 .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } > FLASH
 .ARM.extab : { *(.ARM.extab*) } > FLASH
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
 .preinit_array (READONLY) : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
 .init_array (READONLY) : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
 .fini_array (READONLY) : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH
 _sidata = LOADADDR(.data);
 .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
 _flash_image_end = LOADADDR(.data) + SIZEOF(.data);
 .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
 .dma (NOLOAD) : { . = ALIGN(32); *(.dma*) . = ALIGN(32); } > DMA
 ._user_heap_stack (NOLOAD) : { . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .); . += _Min_Heap_Size; . += _Min_Stack_Size; . = ALIGN(8); } > RAM
 /DISCARD/ : { *(.note.GNU-stack) }
}
ASSERT(_flash_image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "Firmware too large")
