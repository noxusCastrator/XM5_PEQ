/* Thin, toolkit-independent view adapter: turns the controller model into display strings and
 * maps button presses back to controller actions. A real widget layer copies these strings. */
#ifndef VIEW_H
#define VIEW_H

#include "controller.h"

typedef enum { VIEW_BTN_ROW = 0, VIEW_BTN_RESCAN, VIEW_BTN_TOGGLE, VIEW_BTN_BACK } view_button;

typedef struct {
    const char *title, *rescan_label, *toggle_label, *back_label;
    char selected_line[CTL_NAME_MAX + 32];
    const char *state_line;
    char status_line[256];
    int status_is_error;
    int row_count;
    const char *rows[CTL_MAX_ENTRIES]; /* point into the model; valid until the next controller call */
    int highlighted_row;               /* -1 none */
    unsigned generation;               /* hand back with VIEW_BTN_ROW */
} view_state;

void view_render(const ctl_model *m, ctl_lang lang, view_state *v);
/* Dispatch a press. row is used only for VIEW_BTN_ROW together with v->generation. */
void view_press(ctl_controller *c, const view_state *v, view_button button, int row);

#endif
