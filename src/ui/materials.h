/* value_t is opaque here: original value_set_* fills its verified layout. */
#ifndef PEQ_VALUE_STORAGE
#define PEQ_VALUE_STORAGE(name) unsigned long long name[4]={0}
#endif
static pv_ret style_color(pv_widget *w,const char *name,uint32_t x) {
    PEQ_VALUE_STORAGE(value);
    s_value_set_uint32(value,x); return (pv_ret)s_widget_set_style(w,name,value);
}
static void style_str(pv_widget *w,const char *name,const char *text) {
    PEQ_VALUE_STORAGE(value);
    s_value_set_str(value,text); s_widget_set_style(w,name,value);
}
static void decorate(pv_widget *w,const char *role) {
    s_widget_use_style(w,"msg_leftwhite36_style");
    style_color(w,"normal:font_size",32);
    style_color(w,"normal:text_color",0xffffffffu);
    style_color(w,"normal:bg_color",0xff000000u);
    style_color(w,"normal:border_width",0);
    style_color(w,"normal:text_align_h",2); /* AWTK ALIGN_H_LEFT */
    if (!strcmp(role,"peq_view") || !strcmp(role,"peq_list")) {
        s_widget_set_prop_int(w,"xslidable",0);
        s_widget_set_prop_int(w,"yslidable",1);
    } else if (!strcmp(role,"peq_off_icon")) {
        style_str(w,"normal:bg_image","eq_off");
        style_color(w,"normal:bg_image_draw_type",1);
    } else if (!strcmp(role,"peq_off_label") || !strcmp(role,"peq_off_note")) {
        style_color(w,"normal:text_align_h",1);
        style_color(w,"normal:text_color",0xffaaaaaau);
        style_color(w,"normal:font_size",!strcmp(role,"peq_off_label") ? 32:28);
    } else if (!strcmp(role,"peq_rescan")) {
        style_color(w,"normal:bg_color",0xff84b5cau);
        style_color(w,"normal:round_radius",36);
        style_color(w,"normal:text_align_h",1); /* AWTK ALIGN_H_CENTER */
        style_color(w,"pressed:bg_color",0xff708ea0u);
    } else if (!strcmp(role,"peq_status") || !strcmp(role,"peq_coverage")) {
        style_color(w,"normal:font_size",26);
        style_color(w,"normal:text_color",0xffb0b0b0u);
    } else if (!strncmp(role,"peq_row_",8)) {
        style_color(w,"normal:margin_left",16);
        style_color(w,"normal:border_color",0xff303030u);
        style_color(w,"normal:border",8);
        style_color(w,"normal:border_width",1);
        style_color(w,"pressed:bg_color",0xff202020u);
    }
}
static void toggle_image(pv_widget *w,int on) {
    s_widget_set_text_utf8(w,"");
    style_str(w,"normal:bg_image",on ? "switch_on":"switch_off");
    style_str(w,"pressed:bg_image",on ? "switch_on":"switch_off");
    style_color(w,"normal:bg_image_draw_type",1);
    style_color(w,"pressed:bg_image_draw_type",1);
    style_color(w,"normal:border_width",0);
    style_color(w,"pressed:border_width",0);
}
