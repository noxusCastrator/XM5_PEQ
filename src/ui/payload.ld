/* Link script for the U5 payload: one fixed-base, position-dependent image placed ABOVE the stock
 * demo's PT_LOAD range (data+bss end at vaddr 0x812fd8). Base 0x820000 is 0x1000-aligned, well clear
 * of the stock segments, and reachable by a `j` from 0x4994b0 (same 256 MB region). The flat binary
 * (objcopy -O binary) holds .text/.rodata/.data; .bss is zero-filled by the loader via p_memsz. */
OUTPUT_ARCH(mips)
ENTRY(u5_payload_entry)

SECTIONS
{
  . = 0x00820000;

  .text : {
    *(.text.entry)          /* u5_payload_entry must be first: j-target == payload base */
    *(.text .text.* .gnu.linkonce.t.*)
  }
  .rodata : {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)
  }
  .data : {
    *(.data .data.* .gnu.linkonce.d.*)
    *(.data1)
    *(.sdata .sdata.*)      /* -G0 should keep these empty, but place them for safety */
  }

  . = ALIGN(16);
  __u5_file_end = .;         /* end of file-backed image (p_filesz boundary) */

  .bss (NOLOAD) : {
    __u5_bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    __u5_bss_end = .;
  }
  __u5_mem_end = .;          /* end of memory image (p_memsz boundary) */

  /DISCARD/ : {
    *(.MIPS.abiflags)
    *(.reginfo)
    *(.pdr)
    *(.comment)
    *(.gnu.attributes)
    *(.note .note.*)
    *(.eh_frame .eh_frame_hdr)
    *(.mdebug.* .gptab.*)
  }
}
