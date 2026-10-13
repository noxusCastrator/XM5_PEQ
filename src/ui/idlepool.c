#include "idlepool.h"

#define U5_IDLE_SLOTS 4

static struct {
    pv_idle_fn fn;
    void *ctx;
    uint32_t id;
    int used;
} g_slots[U5_IDLE_SLOTS];

static u5_nat_idle_add_fn g_add;
static u5_nat_idle_remove_fn g_remove;

void u5_idle_init(u5_nat_idle_add_fn add, u5_nat_idle_remove_fn remove) {
    g_add = add;
    g_remove = remove;
    for (int i = 0; i < U5_IDLE_SLOTS; i++) { g_slots[i].used = 0; g_slots[i].fn = 0; g_slots[i].ctx = 0; g_slots[i].id = 0; }
}

static pv_ret fire(int i) {
    pv_idle_info syn;
    syn.ctx = g_slots[i].ctx;
    syn.id = g_slots[i].id;
    pv_idle_fn fn = g_slots[i].fn;
    g_slots[i].used = 0;            /* one-shot: native removes it after we return (!= RET_REPEAT) */
    g_slots[i].fn = 0;
    if (fn) return fn(&syn);
    return (pv_ret)0;
}

/* Native idle_func_t is ret_t(*)(const idle_info_t*); the native struct is ignored on purpose. */
static int tramp0(const void *x) { (void)x; return (int)fire(0); }
static int tramp1(const void *x) { (void)x; return (int)fire(1); }
static int tramp2(const void *x) { (void)x; return (int)fire(2); }
static int tramp3(const void *x) { (void)x; return (int)fire(3); }
static int (*const g_tramp[U5_IDLE_SLOTS])(const void *) = { tramp0, tramp1, tramp2, tramp3 };

uint32_t u5_idle_add(pv_idle_fn fn, void *ctx) {
    if (!g_add) return 0;
    for (int i = 0; i < U5_IDLE_SLOTS; i++) {
        if (g_slots[i].used) continue;
        g_slots[i].fn = fn;
        g_slots[i].ctx = ctx;
        g_slots[i].id = 0;
        g_slots[i].used = 1;
        uint32_t id = g_add(g_tramp[i], (void *)0);
        if (!id) { g_slots[i].used = 0; g_slots[i].fn = 0; return 0; }
        g_slots[i].id = id;
        return id;
    }
    return 0; /* pool exhausted: the view falls back to running the work synchronously */
}

pv_ret u5_idle_remove(uint32_t id) {
    for (int i = 0; i < U5_IDLE_SLOTS; i++) {
        if (g_slots[i].used && g_slots[i].id == id) { g_slots[i].used = 0; g_slots[i].fn = 0; break; }
    }
    return g_remove ? g_remove(id) : (pv_ret)0;
}

int u5_idle_inflight(void) {
    int n = 0;
    for (int i = 0; i < U5_IDLE_SLOTS; i++) if (g_slots[i].used) n++;
    return n;
}
