/* U8: stock resources, visible-area sizing and shared native playing GIF. */
#include <string.h>

#include "controller.h"
#include "native_files.h"
#include "peq_awtk_view.h"
#include "peq_adapter.h"
#include "idlepool.h"
#include "stock_addrs.h"
#include "thunks.h"
#include "layer_owner.h"

/* Original stock widgets are retained inside stock_eq_host by resource.py. */
#define U5_NAVBAR_H 0
#define U5_WIN_W 720
#define U5_WIN_H 720
static int own_get_enabled(void *ctx) { (void)ctx; return u6_peq_enabled(); }
static void own_set_enabled(void *ctx,int on) { (void)ctx; (void)u6_peq_set(on); }
static void own_request_reload(void *ctx) {
    (void)ctx; u6_send_equalizer(*(volatile int*)A_equalizer_mode);
}
static void own_navigate_back(void *ctx) {
    (void)ctx;   /* back is the stock navbar button; the view has no back control */
}

/* --------------------------------------------------------- native idle seam -- */
static uint32_t nat_idle_add(int (*t)(const void *), void *ctx) {
    (void)ctx;
    return s_idle_add((void *)t, (void *)0);
}
static pv_ret nat_idle_remove(uint32_t id) {
    return (pv_ret)s_idle_remove(id);
}

/* ---------------------------------------------------------------- toolkit -- */
#include "materials.h"
static pv_ret back_to_stock(void *ctx,pv_event *e) {
    (void)e; s_slide_view_set_active(ctx,0); return PV_RET_OK;
}
static pv_toolkit g_tk;
static void build_toolkit(void) {
    memset(&g_tk, 0, sizeof g_tk);
    g_tk.view_create        = (pv_create_fn)s_view_create;
    g_tk.label_create       = (pv_create_fn)s_hscroll_label_create;
    g_tk.button_create      = (pv_create_fn)s_button_create;
    g_tk.scroll_view_create = (pv_create_fn)s_scroll_view_create;
    g_tk.scroll_view_set_virtual_h = (pv_ret (*)(pv_widget *, pv_wh))s_scroll_view_set_virtual_h;
    g_tk.scroll_view_set_offset    = (pv_ret (*)(pv_widget *, int32_t, int32_t))s_scroll_view_set_offset;
    g_tk.widget_set_name     = (pv_ret (*)(pv_widget *, const char *))s_widget_set_name;
    g_tk.widget_set_text_utf8= (pv_ret (*)(pv_widget *, const char *))s_widget_set_text_utf8;
    g_tk.widget_set_visible  = (pv_ret (*)(pv_widget *, pv_bool))s_widget_set_visible;
    g_tk.widget_set_enable   = (pv_ret (*)(pv_widget *, pv_bool))s_widget_set_enable;
    g_tk.widget_set_style_color = style_color;
    g_tk.widget_on   = (uint32_t (*)(pv_widget *, uint32_t, pv_event_fn, void *))s_widget_on;
    g_tk.widget_off  = (pv_ret (*)(pv_widget *, uint32_t))s_widget_off;
    g_tk.widget_destroy = (pv_ret (*)(pv_widget *))s_widget_destroy;
    g_tk.widget_move_resize = (pv_ret (*)(pv_widget *,pv_xy,pv_xy,pv_wh,pv_wh))s_widget_move_resize;
    g_tk.idle_add    = u5_idle_add;     /* wrapped: synthetic idle_info + value translation */
    g_tk.idle_remove = u5_idle_remove;
    g_tk.evt_click   = STK_EVT_CLICK;
    g_tk.evt_destroy = STK_EVT_DESTROY;
    g_tk.text_normal=0xffb0b0b0u; g_tk.text_error=0xff6060ffu;
    g_tk.bg_row=0xff000000u; g_tk.bg_row_selected=0xff242424u;
    g_tk.decorate=decorate; g_tk.toggle_image=toggle_image;
}

/* ------------------------------------------------------------- attach / state -- */
static ctl_controller g_ctl;
static nf_backend g_nf;
static ctl_platform g_plat;
static pv_view g_view;
static ctl_peq_options g_opt = { 0 };   /* rate 0: validate by parse only (no peq_compile at runtime) */
static int g_open;

#include "native_header.h"

void u5_attach(void *page) {
    if (!page) return;
    if (g_open) { pv_close(&g_view); g_open = 0; }  /* reopen: drop any prior instance first */

    void *host=s_widget_lookup(page,"peq_host",1);
    void *pager=s_widget_lookup(page,"xm5_eq_pager",1);
    if (!host || !pager) return;

    /* Resolve wrapper percentages before reading client geometry. */
    s_widget_layout(page);
    int width=s_widget_get_prop_int(page,"w",0);
    int height=s_widget_get_prop_int(page,"h",0);
    if(width<PV_MIN_W || height<PV_MIN_H) return;
    share_playing_button(page,width);
    u5_idle_init(nat_idle_add, nat_idle_remove);

    nf_owner owner;
    memset(&owner, 0, sizeof owner);
    owner.get_enabled = own_get_enabled;
    owner.set_enabled = own_set_enabled;
    owner.request_reload = own_request_reload;
    owner.navigate_back = own_navigate_back;
    if (nf_init(&g_nf, &g_plat, NF_TARGET_IMPORT_DIR, NF_TARGET_ACTIVE_DIR, &owner) != 0) return;

    ctl_init(&g_ctl, &g_plat, ctl_peq_parse, &g_opt);

    build_toolkit();
    pv_rect area;
    area.x=0; area.y=0; area.w=width; area.h=height;
    if (pv_open(&g_view,&g_tk,&g_ctl,(pv_widget*)host,area,native_language())==0) {
        g_open=1;
        void *back=s_button_create(g_view.root,0,0,108,82);
        if (back) {
            s_widget_set_name(back,"peq_back");
            s_widget_use_style(back,"navbar_btnreturn_style");
            s_widget_on(back,STK_EVT_CLICK,(void*)back_to_stock,pager);
        }
        void *title=s_hscroll_label_create(g_view.root,118,0,width-346,82);
        if (title) {
            s_widget_set_name(title,"peq_title");
            s_widget_use_style(title,"msg_leftwhite36_style");
            s_widget_set_text_utf8(title,"PEQ");
        }
    }
}
