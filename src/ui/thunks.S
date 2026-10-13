/* t9-setting tail-call thunks for every stock function the payload calls.
 *
 * WHY: the payload is built -mno-abicalls, under which gcc emits indirect calls as `jalr <any reg>`
 * and does NOT place the target in t9. But the stock exported AWTK functions are PIC and compute their
 * own gp from t9 (lui gp,..; addu gp,gp,t9); calling them without t9 set gives a garbage gp. Each thunk
 * loads t9 = the real address and `jr t9`, leaving a0-a3 / stack / FP argument registers untouched, so
 * it works for any signature. The libc/libm entries point at the demo's .plt stubs; entering a stub
 * with t9 = stub address is the canonical PIC call and preserves lazy resolution.
 *
 * C code calls these thunks (by name or via the toolkit table); the thunk fixes t9 before the real
 * function runs. This is the MIPS t9/gp convention the lead brief requires. */
    .set noreorder
    .set nomips16
    .text

    .macro STHUNK name, addr
    .globl \name
    .type \name,@function
    .ent \name
\name:
    lui  $25, (\addr >> 16) & 0xffff
    ori  $25, $25, (\addr) & 0xffff
    jr   $25
    nop
    .end \name
    .endm

    /* exported AWTK / stock-EQ functions (PIC: must have t9 = own address) */
    STHUNK s_view_create,              0x62f2d0
    STHUNK s_label_create,             0x629a70
    STHUNK s_button_create,            0x61d6bc
    STHUNK s_scroll_view_create,       0x60bfa4
    STHUNK s_scroll_view_set_virtual_h,0x60c128
    STHUNK s_scroll_view_set_offset,   0x60c1d0
    STHUNK s_widget_set_name,          0x661660
    STHUNK s_widget_set_text_utf8,     0x66a4b0
    STHUNK s_widget_set_visible,       0x667124
    STHUNK s_widget_set_enable,        0x6640b0
    STHUNK s_widget_on,                0x661ab0
    STHUNK s_widget_off,               0x661b4c
    STHUNK s_widget_destroy,           0x6692d4
    STHUNK s_widget_lookup,            0x661804
    STHUNK s_idle_add,                 0x64ee30
    STHUNK s_idle_remove,              0x64ef38
    STHUNK s_write_int_config,         0x4d4058
    STHUNK s_set_equalizer_value,      0x4e1684
    STHUNK s_mclGetPlayStatus,         0x5ccbac
    STHUNK s_mclSetEq,                 0x5cc094
    STHUNK s_widget_use_style,         0x6657f4
    STHUNK s_widget_set_style,         0x6671d8
    STHUNK s_value_set_uint32,         0x7161f8
    STHUNK s_value_set_str,            0x716510
    STHUNK s_image_base_set_image,     0x64f6a0
    STHUNK s_widget_set_prop_int,      0x66accc
    STHUNK s_slide_view_set_active,    0x614718
    STHUNK s_hscroll_label_create,     0x6052e0

    STHUNK s_widget_get_prop_int,      0x663488
    STHUNK s_widget_layout,            0x651aec
    STHUNK s_widget_remove_child,      0x66921c
    STHUNK s_widget_add_child,         0x668434
    STHUNK s_widget_move,              0x667d50
    STHUNK s_widget_move_resize,       0x668198
    STHUNK s_locale_info,              0x653640

    /* libc / libm .plt stubs (t9 = stub address; preserves lazy resolution) */
    STHUNK s_vsnprintf,                0x7479c0
    STHUNK s_cos,                      0x7480a0
    STHUNK s_sin,                      0x746860
    STHUNK s_sqrt,                     0x747410
    STHUNK s_pow,                      0x746ec0
    STHUNK s_atof,                     0x747690
