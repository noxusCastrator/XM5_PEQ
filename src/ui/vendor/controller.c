/* Portable PEQ preset browser / import controller. See controller.h and ../README.md. */
#include "controller.h"

#include <stdio.h>
#include <string.h>

static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }

static int has_txt_suffix(const char *s, size_t n) {
    return n > 4 && s[n - 4] == '.' && lower((unsigned char)s[n - 3]) == 't' &&
           lower((unsigned char)s[n - 2]) == 'x' && lower((unsigned char)s[n - 1]) == 't';
}

/* Well-formed UTF-8, no overlongs, surrogates or code points above U+10FFFF. */
static int utf8_ok(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned ch = s[i], more, min, cp;
        if (ch < 0x80) { ++i; continue; }
        if ((ch & 0xE0) == 0xC0) { more = 1; min = 0x80; cp = ch & 0x1F; }
        else if ((ch & 0xF0) == 0xE0) { more = 2; min = 0x800; cp = ch & 0x0F; }
        else if ((ch & 0xF8) == 0xF0) { more = 3; min = 0x10000; cp = ch & 0x07; }
        else return 0;
        if (n - i <= more) return 0;
        for (unsigned k = 1; k <= more; ++k) {
            if ((s[i + k] & 0xC0) != 0x80) return 0;
            cp = cp << 6 | (s[i + k] & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
        i += more + 1;
    }
    return 1;
}

int ctl_name_safe(const char *name) {
    size_t n = name ? strlen(name) : 0;
    if (n == 0 || n >= CTL_NAME_MAX || name[0] == '.' || !has_txt_suffix(name, n)) return 0;
    if (strstr(name, "..")) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20 || ch == 0x7F || ch == '/' || ch == '\\') return 0;
    }
    return utf8_ok((const unsigned char *)name, n);
}

/* Deterministic order: ASCII case-folded, then bytewise. */
static int name_cmp(const char *a, const char *b) {
    for (size_t i = 0;; ++i) {
        int x = lower((unsigned char)a[i]), y = lower((unsigned char)b[i]);
        if (x != y) return x - y;
        if (!x) break;
    }
    return strcmp(a, b);
}

static void set_status(ctl_model *m, ctl_status st, unsigned detail, const char *name, const char *reason) {
    m->status = st;
    m->detail = detail;
    m->reason = reason;
    m->status_name[0] = 0;
    if (name) snprintf(m->status_name, sizeof m->status_name, "%s", name);
}

static void find_selected(ctl_model *m) {
    m->selected_row = -1;
    if (!m->active[0]) return;
    for (int i = 0; i < m->count; ++i)
        if (!strcmp(m->names[i], m->active)) { m->selected_row = i; return; }
}

/* Import failure: keep the stored file, explicitly turn PEQ off and show why. */
static void fail_off(ctl_controller *c, ctl_status st, unsigned detail, const char *name, const char *reason) {
    c->p->set_enabled(c->p->ctx, 0);
    c->m.enabled = c->p->get_enabled(c->p->ctx) ? 1 : 0;
    if (c->m.enabled) st = CTL_ERR_SWITCH;
    set_status(&c->m, st, detail, name, reason);
}

void ctl_init(ctl_controller *c, const ctl_platform *p, ctl_parse_fn parse, void *parse_ctx) {
    memset(c, 0, sizeof *c);
    c->p = p;
    c->parse = parse;
    c->parse_ctx = parse_ctx;
    c->m.selected_row = -1;
}

const ctl_model *ctl_get_model(const ctl_controller *c) { return &c->m; }

static void emit_entry(void *arg, const char *name) {
    ctl_model *m = arg;
    size_t n = strlen(name);
    if (!has_txt_suffix(name, n)) return; /* not a preset, silently ignored */
    if (!ctl_name_safe(name)) { ++m->rejected; return; }
    ++m->total;
    int pos = 0;
    while (pos < m->count && name_cmp(m->names[pos], name) < 0) ++pos;
    if (pos >= CTL_MAX_ENTRIES) return; /* sorts after every kept row */
    int last = m->count < CTL_MAX_ENTRIES ? m->count : CTL_MAX_ENTRIES - 1;
    memmove(m->names[pos + 1], m->names[pos], (size_t)(last - pos) * CTL_NAME_MAX);
    memcpy(m->names[pos], name, n + 1);
    if (m->count < CTL_MAX_ENTRIES) ++m->count;
}

void ctl_rescan(ctl_controller *c) {
    ctl_model *m = &c->m;
    m->count = m->total = m->rejected = 0;
    ++m->generation; /* invalidates every row reference handed out before */
    int r = c->p->list_dir(c->p->ctx, emit_entry, m);
    if (r != CTL_IO_OK) m->count = m->total = m->rejected = 0;
    find_selected(m);
    if (r == CTL_IO_MISSING) set_status(m, CTL_ERR_DIR_MISSING, 0, NULL, NULL);
    else if (r != CTL_IO_OK) set_status(m, CTL_ERR_DIR_UNREADABLE, 0, NULL, NULL);
    else if (!m->count) set_status(m, CTL_ERR_DIR_EMPTY, (unsigned)m->rejected, NULL, NULL);
    else if (m->count < m->total) set_status(m, CTL_WARN_TRUNCATED, (unsigned)m->total, NULL, NULL);
    else set_status(m, CTL_ST_LISTED, (unsigned)m->count, NULL, NULL);
}

/* Validate the stored active file and extract its source name into `name`. Returns ctl_status. */
static ctl_status check_active(ctl_controller *c, char name[CTL_NAME_MAX]) {
    unsigned len = 0;
    size_t pre = sizeof CTL_HEADER_PREFIX - 1;
    name[0] = 0;
    int r = c->p->read_active(c->p->ctx, c->out, CTL_FILE_LIMIT, &len);
    if (r == CTL_IO_MISSING) return CTL_ST_NO_ACTIVE;
    if (r != CTL_IO_OK || len > CTL_FILE_LIMIT || len < pre || memcmp(c->out, CTL_HEADER_PREFIX, pre))
        return CTL_ERR_ACTIVE_INVALID;
    const char *s = c->out + pre, *end = memchr(s, '\n', len - pre);
    if (!end || (size_t)(end - s) >= CTL_NAME_MAX || memchr(s, 0, (size_t)(end - s))) return CTL_ERR_ACTIVE_INVALID;
    char tmp[CTL_NAME_MAX];
    memcpy(tmp, s, (size_t)(end - s));
    tmp[end - s] = 0;
    if (!ctl_name_safe(tmp)) return CTL_ERR_ACTIVE_INVALID;
    unsigned line = 0;
    const char *reason = NULL;
    if (!c->parse(c->parse_ctx, c->out, len, &line, &reason)) return CTL_ERR_ACTIVE_INVALID;
    memcpy(name, tmp, sizeof tmp);
    return CTL_ST_RESTORED;
}

void ctl_open(ctl_controller *c) {
    ctl_model *m = &c->m;
    char name[CTL_NAME_MAX];
    m->enabled = c->p->get_enabled(c->p->ctx) ? 1 : 0;
    ctl_status st = check_active(c, name);
    memcpy(m->active, name, sizeof name);
    ctl_rescan(c);
    if (st == CTL_ERR_ACTIVE_INVALID) {
        /* Never report a damaged file as the running preset; the player treats it as identity anyway. */
        fail_off(c, st, 0, NULL, NULL);
    } else if (m->status == CTL_ST_LISTED) {
        set_status(m, st, 0, st == CTL_ST_RESTORED ? m->active : NULL, NULL);
    }
}

static int commit(ctl_controller *c, unsigned len) {
    const ctl_platform *p = c->p;
    int step = 0;
    if (p->tmp_open(p->ctx)) step = CTL_STEP_OPEN;
    else if (p->tmp_write(p->ctx, c->out, len)) step = CTL_STEP_WRITE;
    else if (p->tmp_flush(p->ctx)) step = CTL_STEP_FLUSH;
    else if (p->tmp_fsync(p->ctx)) step = CTL_STEP_FSYNC;
    else if (p->tmp_close(p->ctx)) step = CTL_STEP_CLOSE;
    else if (p->tmp_rename(p->ctx)) step = CTL_STEP_RENAME;
    if (step) p->tmp_discard(p->ctx);
    return step;
}

int ctl_select(ctl_controller *c, unsigned generation, int row) {
    ctl_model *m = &c->m;
    if (generation != m->generation || row < 0 || row >= m->count) {
        set_status(m, CTL_ERR_STALE_ROW, 0, NULL, NULL);
        return 0;
    }
    char name[CTL_NAME_MAX];
    memcpy(name, m->names[row], CTL_NAME_MAX); /* own copy: nothing below refers back into the list */
    if (!ctl_name_safe(name)) { fail_off(c, CTL_ERR_BAD_NAME, 0, NULL, NULL); return 0; }

    unsigned raw_len = 0;
    int r = c->p->read_import(c->p->ctx, name, c->raw, CTL_RAW_LIMIT, &raw_len);
    if (r == CTL_IO_MISSING) { fail_off(c, CTL_ERR_FILE_MISSING, 0, name, NULL); return 0; }
    if (r == CTL_IO_NOT_REGULAR) { fail_off(c, CTL_ERR_NOT_REGULAR, 0, name, NULL); return 0; }
    if (r == CTL_IO_TOO_LARGE || (r == CTL_IO_OK && raw_len > CTL_RAW_LIMIT)) {
        fail_off(c, CTL_ERR_TOO_LARGE, 0, name, NULL); return 0;
    }
    if (r != CTL_IO_OK) { fail_off(c, CTL_ERR_FILE_UNREADABLE, 0, name, NULL); return 0; }

    const char *body = c->raw;
    unsigned body_len = raw_len;
    if (body_len >= 3 && !memcmp(body, "\xEF\xBB\xBF", 3)) { body += 3; body_len -= 3; }
    int hlen = snprintf(c->out, sizeof c->out, "%s%s\n", CTL_HEADER_PREFIX, name);
    if (hlen < 0 || (unsigned)hlen + body_len > CTL_FILE_LIMIT) { fail_off(c, CTL_ERR_TOO_LARGE, 0, name, NULL); return 0; }
    memcpy(c->out + hlen, body, body_len);
    unsigned len = (unsigned)hlen + body_len;
    c->out[len] = 0;

    /* Validate exactly the bytes that will be stored; report source-file line numbers. */
    unsigned line = 0;
    const char *reason = NULL;
    if (!c->parse(c->parse_ctx, c->out, len, &line, &reason)) {
        fail_off(c, CTL_ERR_INVALID, line > 1 ? line - 1 : 0, name, reason);
        return 0;
    }
    int step = commit(c, len);
    if (step) { fail_off(c, CTL_ERR_SAVE, (unsigned)step, name, NULL); return 0; }

    memcpy(m->active, name, CTL_NAME_MAX);
    find_selected(m);
    if (m->enabled) {
        c->p->request_reload(c->p->ctx);
        set_status(m, CTL_ST_SAVED, 0, name, NULL);
    } else {
        set_status(m, CTL_ST_SAVED_OFF, 0, name, NULL); /* selection while off stays off */
    }
    return 1;
}

int ctl_set_enabled(ctl_controller *c, int on) {
    ctl_model *m = &c->m;
    if (!on) {
        c->p->set_enabled(c->p->ctx, 0);
        m->enabled = c->p->get_enabled(c->p->ctx) ? 1 : 0;
        set_status(m, m->enabled ? CTL_ERR_SWITCH : CTL_ST_DISABLED, 0, NULL, NULL);
        return m->enabled;
    }
    char name[CTL_NAME_MAX];
    ctl_status st = check_active(c, name);
    memcpy(m->active, name, sizeof name);
    find_selected(m);
    if (st != CTL_ST_RESTORED) {
        fail_off(c, st == CTL_ST_NO_ACTIVE ? CTL_ERR_NO_ACTIVE : CTL_ERR_ACTIVE_INVALID, 0, NULL, NULL);
        return 0;
    }
    c->p->set_enabled(c->p->ctx, 1);
    m->enabled = c->p->get_enabled(c->p->ctx) ? 1 : 0;
    if (!m->enabled) { set_status(m,CTL_ERR_SWITCH,0,NULL,NULL); return 0; }
    set_status(m, CTL_ST_ENABLED, 0, m->active, NULL);
    return 1;
}

void ctl_back(ctl_controller *c) { c->p->navigate_back(c->p->ctx); }

/* ---- text ---- */

static const char *const texts[CTL_LANG_COUNT][CTL_TXT_COUNT] = {
    { "PEQ", "Rescan", "Enable", "Disable", "On", "Off (bypass)", "Selected:", "None", "Back" },
    { "PEQ", "重新扫描", "开启", "关闭", "已开启", "已关闭（旁通）", "已选择：", "无", "返回" },
};

const char *ctl_text(ctl_lang lang, ctl_text_id id) {
    if ((unsigned)id >= CTL_TXT_COUNT) return "";
    if ((unsigned)lang >= CTL_LANG_COUNT) lang = CTL_LANG_EN;
    return texts[lang][id];
}

static const char *const step_names[CTL_LANG_COUNT][7] = {
    { "unknown", "create temp", "write", "flush", "fsync", "close", "rename" },
    { "未知", "创建临时文件", "写入", "刷新", "同步", "关闭", "重命名" },
};

const char *ctl_status_message(const ctl_model *m, ctl_lang lang, char *buf, unsigned cap) {
    int zh = lang == CTL_LANG_ZH;
    const char *n = m->status_name;
    const char *step = step_names[zh][m->detail <= CTL_STEP_RENAME ? m->detail : 0];
    if (!cap) return buf;
    buf[0] = 0;
    switch (m->status) {
    case CTL_ST_NONE: break;
    case CTL_ST_LISTED: snprintf(buf, cap, zh ? "共 %d 个预设" : "%d presets", m->count); break;
    case CTL_ST_SAVED: snprintf(buf, cap, zh ? "已选择并保存：%s（已请求重新加载）" : "Selected and saved: %s (reload requested)", n); break;
    case CTL_ST_SAVED_OFF: snprintf(buf, cap, zh ? "已保存：%s。PEQ 处于关闭状态，开启后生效" : "Saved: %s. PEQ is off; enable to use it", n); break;
    case CTL_ST_RESTORED: snprintf(buf, cap, zh ? "已选择：%s" : "Selected: %s", n); break;
    case CTL_ST_NO_ACTIVE: snprintf(buf, cap, zh ? "尚未保存预设" : "No preset saved yet"); break;
    case CTL_ST_ENABLED: snprintf(buf, cap, zh ? "PEQ 已开启：%s" : "PEQ on: %s", n); break;
    case CTL_ST_DISABLED: snprintf(buf, cap, zh ? "PEQ 已关闭" : "PEQ off"); break;
    case CTL_WARN_TRUNCATED: snprintf(buf, cap, zh ? "仅显示前 %d 个预设（共 %u 个）" : "Showing first %d of %u presets", m->count, m->detail); break;
    case CTL_ERR_DIR_MISSING: snprintf(buf, cap, zh ? "未找到预设文件夹（存储卡已移除？）。已保存的预设保留" : "Preset folder not found (card removed?). Saved preset is kept"); break;
    case CTL_ERR_DIR_EMPTY: snprintf(buf, cap, zh ? "预设文件夹中没有可用的 .txt 文件（跳过 %u 个不安全文件名）" : "No usable .txt presets in folder (%u unsafe names skipped)", m->detail); break;
    case CTL_ERR_DIR_UNREADABLE: snprintf(buf, cap, zh ? "无法读取预设文件夹" : "Cannot read preset folder"); break;
    case CTL_ERR_STALE_ROW: snprintf(buf, cap, zh ? "列表已变化，请重新选择" : "List changed, please choose again"); break;
    case CTL_ERR_BAD_NAME: snprintf(buf, cap, zh ? "文件名不安全。PEQ 已关闭" : "Unsafe file name. PEQ off"); break;
    case CTL_ERR_FILE_MISSING: snprintf(buf, cap, zh ? "%s：文件已不存在。PEQ 已关闭" : "%s: file no longer exists. PEQ off", n); break;
    case CTL_ERR_FILE_UNREADABLE: snprintf(buf, cap, zh ? "%s：无法读取文件。PEQ 已关闭" : "%s: cannot read file. PEQ off", n); break;
    case CTL_ERR_NOT_REGULAR: snprintf(buf, cap, zh ? "%s：不是普通文件。PEQ 已关闭" : "%s: not a regular file. PEQ off", n); break;
    case CTL_ERR_TOO_LARGE: snprintf(buf, cap, zh ? "%s：文件过大（含文件头最多 %d 字节）。PEQ 已关闭" : "%s: too large (max %d bytes with header). PEQ off", n, CTL_FILE_LIMIT); break;
    case CTL_ERR_INVALID:
        /* detail 0: whole-file error (summed preamp, rate check), no line to point at */
        if (m->detail) snprintf(buf, cap, zh ? "%s 第 %u 行无效：%s。PEQ 已关闭" : "%s line %u invalid: %s. PEQ off", n, m->detail, m->reason ? m->reason : "?");
        else snprintf(buf, cap, zh ? "%s 无效：%s。PEQ 已关闭" : "%s invalid: %s. PEQ off", n, m->reason ? m->reason : "?");
        break;
    case CTL_ERR_SAVE: snprintf(buf, cap, zh ? "保存失败（%s）。保留原文件，PEQ 已关闭" : "Save failed (%s). Previous file kept, PEQ off", step); break;
    case CTL_ERR_ACTIVE_INVALID: snprintf(buf, cap, zh ? "已保存的预设已损坏。PEQ 已关闭" : "Saved preset is damaged. PEQ off"); break;
    case CTL_ERR_NO_ACTIVE: snprintf(buf, cap, zh ? "没有有效的已保存预设，请先选择" : "No valid saved preset; select one first"); break;
    case CTL_ERR_SWITCH: snprintf(buf,cap,zh ? "开关保存失败，保持原状态" : "Switch save failed; previous state kept"); break;
    default: break;
    }
    return buf;
}
