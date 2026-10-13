/* Lead-verified native header helpers. */
/* locale_info_change @653778 copies country to +0 and language to +3,
 * two bytes each. Read only those proven bytes; all other locales use English. */
static ctl_lang native_language(void) {
    const unsigned char *loc=s_locale_info();
    return loc && loc[3]=='z' && loc[4]=='h' &&
           ((loc[0]=='C' && loc[1]=='N') || (loc[0]=='c' && loc[1]=='n'))
           ? CTL_LANG_ZH : CTL_LANG_EN;
}

/* Keep the ORIGINAL gif_returnplay object, image, click handler and animation.
 * Stock status events look it up recursively on page; moving it above the pager
 * makes the same object visible on both pages without adding a timer or handler. */
static void share_playing_button(void *page,int width) {
    void *nav=s_widget_lookup(page,"navigationbar_view",1);
    void *gif=nav ? s_widget_lookup(nav,"gif_returnplay",1) : 0;
    void *prior=s_widget_lookup(page,"gif_returnplay",0);
    if (gif && prior && prior!=gif) s_widget_destroy(prior);
    if (gif && s_widget_remove_child(nav,gif)==0) {
        if (s_widget_add_child(page,gif)==0) s_widget_move(gif,width-98,0);
        else s_widget_add_child(nav,gif); /* restore ownership on failure */
    }
}

