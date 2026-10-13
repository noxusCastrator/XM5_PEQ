/* Prototypes for the t9-setting stock thunks in thunks.S. Generic C types (void pointer, int, double);
 * binder.c casts the widget thunks to the pv_toolkit function-pointer types. Calling any of these
 * guarantees the real stock function runs with t9 = its own address (see thunks.S). */
#ifndef THUNKS_H
#define THUNKS_H

#include <stddef.h>

void *s_view_create(void *, int, int, int, int);
void *s_label_create(void *, int, int, int, int);
void *s_button_create(void *, int, int, int, int);
void *s_scroll_view_create(void *, int, int, int, int);
int   s_scroll_view_set_virtual_h(void *, int);
int   s_scroll_view_set_offset(void *, int, int);
int   s_widget_set_name(void *, const char *);
int   s_widget_set_text_utf8(void *, const char *);
int   s_widget_set_visible(void *, int);
int   s_widget_set_enable(void *, int);
unsigned s_widget_on(void *, unsigned, void *, void *);
int   s_widget_off(void *, unsigned);
int   s_widget_destroy(void *);
void *s_widget_lookup(void *, const char *, int);
unsigned s_idle_add(void *, void *);
int   s_idle_remove(unsigned);
void  s_write_int_config(int, const char *, const char *);
void  s_set_equalizer_value(int);
int s_mclGetPlayStatus(void);
int s_mclSetEq(float,float,float,float,float,float,float,float,float,float,int);
int s_widget_use_style(void *, const char *);
int s_widget_set_style(void *, const char *, const void *);
void *s_value_set_uint32(void *, unsigned);
void *s_value_set_str(void *, const char *);
int s_image_base_set_image(void *, const char *);
int s_widget_set_prop_int(void *, const char *, int);
int s_slide_view_set_active(void *, unsigned);
void *s_hscroll_label_create(void *,int,int,int,int);
int s_widget_get_prop_int(void *,const char *,int);
int s_widget_layout(void *);
int s_widget_remove_child(void *,void *);
int s_widget_add_child(void *,void *);
int s_widget_move(void *,int,int);
int s_widget_move_resize(void *,int,int,int,int);
void *s_locale_info(void);

int    s_vsnprintf(char *, size_t, const char *, void *);  /* va_list passed as void* (ABI-identical) */
double s_cos(double);
double s_sin(double);
double s_sqrt(double);
double s_pow(double, double);
double s_atof(const char *);

#endif
