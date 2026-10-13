/* Verified stock `demo` V2.43 addresses consumed by the U5 payload.
 *
 * Source of truth: R2 work/demo.dynsym + objdump of firmware/rootfs/release/bin/demo
 * (sha256 15ee04b5...a2c9). See evidence/binary-facts.md for the derivation of every value.
 *
 * Calling rule: every stock function here is PIC and recomputes its own gp from t9. The payload is
 * built -mno-abicalls -fno-pic -G0 and reaches these through thunks.S, which
 * explicitly loads t9 before entering stock PIC code. C function pointers alone
 * do not guarantee t9 under these compiler flags. Never `jal` stock PIC directly.
 * Data symbols are read/written by absolute address. */
#ifndef STOCK_ADDRS_H
#define STOCK_ADDRS_H

#include <stdint.h>

/* ---- exported AWTK widget/toolkit functions (dynsym, Ndx 14, size>0) ---- */
#define A_view_create                0x62f2d0u
#define A_label_create               0x629a70u
#define A_button_create              0x61d6bcu
#define A_scroll_view_create         0x60bfa4u
#define A_scroll_view_set_virtual_h  0x60c128u
#define A_scroll_view_set_offset     0x60c1d0u
#define A_widget_set_name            0x661660u
#define A_widget_set_text_utf8       0x66a4b0u
#define A_widget_set_visible         0x667124u
#define A_widget_set_enable          0x6640b0u
#define A_widget_on                  0x661ab0u
#define A_widget_off                 0x661b4cu
#define A_widget_destroy             0x6692d4u
#define A_widget_lookup              0x661804u
#define A_idle_add                   0x64ee30u
#define A_idle_remove                0x64ef38u
#define A_locale_info                0x653640u

/* ---- stock EQ owner callbacks (argument evidence: stock img_switch handler 0x498f08) ---- */
#define A_write_int_config           0x4d4058u   /* void(int value, const char* section, const char* key) */
#define A_set_equalizer_value        0x4e1684u   /* void(int mode); sends stock IPC only for mode 1..11 */

/* ---- stock EQ owner data symbols (dynsym size/addr verified) ---- */
#define A_equalizer_flag             0x80728cu   /* OBJECT size 1  -> uint8_t byte, 0/1 */
#define A_equalizer_mode             0x7e2c04u   /* OBJECT size 4  -> int32_t mode */

/* ---- libc/libm PLT stubs in demo (UND FUNC st_value = .plt stub address) ---- */
#define A_plt_snprintf               0x747480u
#define A_plt_vsnprintf              0x7479c0u
#define A_plt_cos                    0x7480a0u
#define A_plt_sin                    0x746860u
#define A_plt_pow                    0x746ec0u
#define A_plt_sqrt                   0x747410u
#define A_plt_atof                   0x747690u   /* atof@GLIBC_2.0, dynsym 7343; P1-validated tokens */

/* ---- verified AWTK enum values (from the binary, not modern headers) ---- */
#define STK_EVT_CLICK    268u   /* 0x10c: stock widget_on registrations incl. img_switch */
#define STK_EVT_DESTROY   12u   /* dispatched by widget_destroy_sync@0x66b37c */
#define STK_RET_OK         0u   /* AWTK RET_OK */
#define STK_RET_REPEAT     8u   /* idle_manager_dispatch keeps the idle only for this value */

/* Hook site. */
#define A_page_init_entry            0x4994b0u
#define A_page_init_body             0x4994bcu   /* re-entry, skipping the 3-word gp prologue + addu */
#define STK_CANONICAL_GP             0x8054d0u

#endif
