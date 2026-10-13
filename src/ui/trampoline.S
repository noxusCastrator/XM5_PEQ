/* U5 hook trampoline (non-PIC, -mno-abicalls).
 *
 * Reached by the patched `j u5_payload_entry` at playset_equalizer_page_init (0x4994b0). On entry:
 *   a0 = page widget, a1 = context, ra = page-factory return, t9 = 0x4994b0 (factory's jalr).
 * It runs the ORIGINAL body as a subroutine with the canonical gp restored, re-entering at 0x4994bc
 * (skipping the 3 gp-prologue words we overwrote + the `addu gp,gp,t9`), preserves the body's return
 * value, then calls u5_attach(page) to build the UI, and finally returns to the factory with ra and
 * the original return value intact. Callee-saved registers are preserved by the body itself; this
 * stub only clobbers at/t1/t9 and its own frame. */
    .set noreorder
    .set nomips16
    .text
    .section .text.entry,"ax",@progbits
    .align 2
    .globl u5_payload_entry
    .ent u5_payload_entry
    .type u5_payload_entry,@function
u5_payload_entry:
    addiu   $sp, $sp, -32
    sw      $ra, 28($sp)
    sw      $a0, 24($sp)            /* page widget */
    sw      $a1, 20($sp)            /* context */

    /* gp = 0x8054d0 (canonical), required by the stock body's gp-relative GOT loads */
    lui     $gp, 0x80
    ori     $gp, $gp, 0x54d0
    /* t9 = 0x4994b0 (original entry); harmless, the body does not use t9 after its prologue */
    lui     $t9, 0x0049
    ori     $t9, $t9, 0x94b0
    /* restore the body's arguments */
    lw      $a0, 24($sp)
    lw      $a1, 20($sp)
    /* call the stock body at 0x4994bc */
    lui     $t1, 0x0049
    ori     $t1, $t1, 0x94bc
    jalr    $t1
    nop
    sw      $v0, 16($sp)            /* save the body's return value */

    /* attach the U5 UI on top of the freshly-built stock page */
    lw      $a0, 24($sp)
    jal     u5_attach
    nop

    lw      $v0, 16($sp)            /* restore original return value */
    lw      $ra, 28($sp)
    jr      $ra
    addiu   $sp, $sp, 32            /* delay slot */
    .end u5_payload_entry
