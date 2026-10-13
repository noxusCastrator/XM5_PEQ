/* Thin view adapter over ctl_model. */
#include "view.h"

#include <stdio.h>

void view_render(const ctl_model *m, ctl_lang lang, view_state *v) {
    v->title = ctl_text(lang, CTL_TXT_TITLE);
    v->rescan_label = ctl_text(lang, CTL_TXT_RESCAN);
    v->toggle_label = ctl_text(lang, m->enabled ? CTL_TXT_DISABLE : CTL_TXT_ENABLE);
    v->back_label = ctl_text(lang, CTL_TXT_BACK);
    snprintf(v->selected_line, sizeof v->selected_line, "%s %s", ctl_text(lang, CTL_TXT_SELECTED),
             m->active[0] ? m->active : ctl_text(lang, CTL_TXT_NONE));
    v->state_line = ctl_text(lang, m->enabled ? CTL_TXT_STATE_ON : CTL_TXT_STATE_OFF);
    ctl_status_message(m, lang, v->status_line, sizeof v->status_line);
    v->status_is_error = m->status >= CTL_WARN_TRUNCATED;
    v->row_count = m->count;
    for (int i = 0; i < m->count; ++i) v->rows[i] = m->names[i];
    v->highlighted_row = m->selected_row;
    v->generation = m->generation;
}

void view_press(ctl_controller *c, const view_state *v, view_button button, int row) {
    switch (button) {
    case VIEW_BTN_ROW: ctl_select(c, v->generation, row); break;
    case VIEW_BTN_RESCAN: ctl_rescan(c); break;
    case VIEW_BTN_TOGGLE: ctl_set_enabled(c, !ctl_get_model(c)->enabled); break;
    case VIEW_BTN_BACK: ctl_back(c); break;
    }
}
