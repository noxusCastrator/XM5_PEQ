/* Portable PEQ preset browser / import controller.
 * No UI toolkit, firmware or device knowledge: every side effect goes through ctl_platform,
 * the parser is injected (ctl_parse_fn). Single-threaded, no allocation; call from the UI thread only. */
#ifndef CONTROLLER_H
#define CONTROLLER_H

#define CTL_MAX_ENTRIES 64
#define CTL_NAME_MAX 64          /* bytes including NUL: names are 1..63 bytes */
#define CTL_FILE_LIMIT 16384     /* final stored file including the header line */
#define CTL_HEADER_PREFIX "# xm5-peq 1 source="
#define CTL_RAW_LIMIT (CTL_FILE_LIMIT + 3) /* import file may carry a 3 byte UTF-8 BOM on top */

/* Platform I/O results. */
enum { CTL_IO_OK = 0, CTL_IO_MISSING, CTL_IO_NOT_REGULAR, CTL_IO_TOO_LARGE, CTL_IO_ERROR };

/* Commit steps, in order. Reported in status detail on save failure. */
typedef enum { CTL_STEP_OPEN = 1, CTL_STEP_WRITE, CTL_STEP_FLUSH, CTL_STEP_FSYNC, CTL_STEP_CLOSE, CTL_STEP_RENAME } ctl_step;

typedef enum {
    CTL_ST_NONE = 0,
    CTL_ST_LISTED,          /* rescan found entries */
    CTL_ST_SAVED,           /* import validated and saved; PEQ was enabled so reload requested */
    CTL_ST_SAVED_OFF,       /* import saved while off; stays off until enable */
    CTL_ST_RESTORED,        /* open found a valid active file */
    CTL_ST_NO_ACTIVE,       /* open: no active file */
    CTL_ST_ENABLED,
    CTL_ST_DISABLED,
    CTL_WARN_TRUNCATED,     /* list shows first CTL_MAX_ENTRIES of total */
    CTL_ERR_DIR_MISSING,    /* import directory missing (card removed / not inserted) */
    CTL_ERR_DIR_EMPTY,      /* directory present, no usable .txt presets */
    CTL_ERR_DIR_UNREADABLE,
    CTL_ERR_STALE_ROW,      /* row reference from an older list generation or out of range */
    CTL_ERR_BAD_NAME,
    CTL_ERR_FILE_MISSING,
    CTL_ERR_FILE_UNREADABLE,
    CTL_ERR_NOT_REGULAR,
    CTL_ERR_TOO_LARGE,
    CTL_ERR_INVALID,        /* parser rejected text: detail = source line */
    CTL_ERR_SAVE,           /* commit failed: detail = ctl_step */
    CTL_ERR_ACTIVE_INVALID, /* stored active file unreadable/corrupt */
    CTL_ERR_NO_ACTIVE,      /* enable requested without a valid stored preset */
    CTL_ERR_SWITCH,
    CTL_ST_COUNT
} ctl_status;

typedef enum { CTL_LANG_EN = 0, CTL_LANG_ZH, CTL_LANG_COUNT } ctl_lang;

typedef enum {
    CTL_TXT_TITLE = 0, CTL_TXT_RESCAN, CTL_TXT_ENABLE, CTL_TXT_DISABLE, CTL_TXT_STATE_ON, CTL_TXT_STATE_OFF,
    CTL_TXT_SELECTED, CTL_TXT_NONE, CTL_TXT_BACK, CTL_TXT_COUNT
} ctl_text_id;

/* Returns nonzero when text is a valid preset. On failure sets *line (1-based, 0 unknown) and *reason
 * (static string, may be NULL). See peq_adapter.c for the P1 peq_parse binding. */
typedef int (*ctl_parse_fn)(void *ctx, const char *text, unsigned size, unsigned *line, const char **reason);

typedef struct ctl_platform {
    void *ctx;
    /* Enumerate the import directory: call emit(arg, name) for each regular, non-symlink entry.
     * Returns CTL_IO_OK, CTL_IO_MISSING (no directory) or CTL_IO_ERROR. */
    int (*list_dir)(void *ctx, void (*emit)(void *arg, const char *name), void *arg);
    /* Read import-directory file `name` (already validated) refusing symlinks/non-regular files.
     * Returns CTL_IO_* ; CTL_IO_TOO_LARGE when the file exceeds cap. */
    int (*read_import)(void *ctx, const char *name, char *buf, unsigned cap, unsigned *len);
    /* Read the stored active file. CTL_IO_MISSING when absent. */
    int (*read_active)(void *ctx, char *buf, unsigned cap, unsigned *len);
    /* Atomic commit of the active file; each returns 0 on success. discard removes any temp file. */
    int (*tmp_open)(void *ctx);
    int (*tmp_write)(void *ctx, const char *data, unsigned len);
    int (*tmp_flush)(void *ctx);
    int (*tmp_fsync)(void *ctx);
    int (*tmp_close)(void *ctx);
    int (*tmp_rename)(void *ctx);
    void (*tmp_discard)(void *ctx);
    /* On/off owner (stock EQ flag on the target). */
    int (*get_enabled)(void *ctx);
    void (*set_enabled)(void *ctx, int on);
    /* Ask the player to re-read the active file. No acknowledgement exists. */
    void (*request_reload)(void *ctx);
    void (*navigate_back)(void *ctx);
} ctl_platform;

typedef struct {
    char names[CTL_MAX_ENTRIES][CTL_NAME_MAX];
    int count;          /* rows shown */
    int total;          /* usable .txt entries found (count < total means truncated) */
    int rejected;       /* entries skipped for unsafe/oversized names */
    unsigned generation;/* bumped by every rescan; rows are only valid with the matching generation */
    char active[CTL_NAME_MAX]; /* selected/saved preset name from the stored header, "" none */
    int selected_row;   /* row of active in names[], -1 if not listed */
    int enabled;
    ctl_status status;
    unsigned detail;    /* line number or ctl_step */
    char status_name[CTL_NAME_MAX]; /* file the status refers to */
    const char *reason; /* parser reason (static), may be NULL */
} ctl_model;

typedef struct {
    ctl_model m;
    const ctl_platform *p;
    ctl_parse_fn parse;
    void *parse_ctx;
    char raw[CTL_RAW_LIMIT + 1];
    char out[CTL_FILE_LIMIT + 1];
} ctl_controller;

void ctl_init(ctl_controller *c, const ctl_platform *p, ctl_parse_fn parse, void *parse_ctx);
/* Page open: restore selected name from the stored header, read on/off, then rescan. */
void ctl_open(ctl_controller *c);
void ctl_rescan(ctl_controller *c);
/* Import row `row` of list `generation`. Returns 1 on saved. Stale references change nothing. */
int ctl_select(ctl_controller *c, unsigned generation, int row);
/* Explicit on/off. Enabling requires a valid stored preset. Returns resulting state. */
int ctl_set_enabled(ctl_controller *c, int on);
void ctl_back(ctl_controller *c);

const ctl_model *ctl_get_model(const ctl_controller *c);
int ctl_name_safe(const char *name);
const char *ctl_text(ctl_lang lang, ctl_text_id id);
/* Human-readable status line; always NUL terminated. Returns buf. */
const char *ctl_status_message(const ctl_model *m, ctl_lang lang, char *buf, unsigned cap);

#endif
