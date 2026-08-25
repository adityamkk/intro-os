/* Tell the linker that we want an aarch64 ELF64 output file. */
OUTPUT_FORMAT(elf64-littleaarch64)

/* kmain is our entry point (see src/main.cpp). */
ENTRY(kmain)

/* Define the program headers we want so the bootloader gives us the right
 * MMU permissions for each segment. */
PHDRS
{
    limine_requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    /* We want to be placed in the topmost 2GiB of the address space, as
     * mandated by the Limine boot protocol. */
    . = 0xffffffff80000000;

    /* Limine's boot requests must live in their own section so the
     * bootloader can find them. */
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .note.gnu.build-id : {
        *(.note.gnu.build-id)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    /* .bss must be the last thing mapped to :data, otherwise the on-disk
     * image would need to store a lot of needless zeros. */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
