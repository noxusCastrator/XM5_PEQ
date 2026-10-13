/* Idle translation pool: bridges U3's host-shaped idle callback to a native AWTK idle manager
 * WITHOUT depending on the native idle_info_t layout.
 *
 * U3 registers `pv_ret on_idle(const pv_idle_info*)` and reads only `info->ctx` (offset 0 in the host
 * struct). The native idle_info_t puts ctx at an unverified offset, so we never let the native manager
 * call U3 directly. Instead each pool slot owns a fixed trampoline (tramp0..N) that knows its slot
 * index at compile time, ignores the native idle_info, and calls U3 with a synthetic pv_idle_info
 * carrying the stored ctx. The trampoline returns RET_OK(0); since the native idle_manager_dispatch
 * keeps an idle only when the callback returns RET_REPEAT(8) (verified @0x705034), returning 0
 * correctly makes every idle one-shot. */
#ifndef IDLEPOOL_H
#define IDLEPOOL_H

#include "peq_awtk_view.h"

/* Native seam. add() registers a native idle for trampoline `t` (ret_t(*)(const idle_info_t*)) and
 * returns its id (0 on failure); remove() removes a native idle by id. */
typedef uint32_t (*u5_nat_idle_add_fn)(int (*t)(const void *), void *ctx);
typedef pv_ret (*u5_nat_idle_remove_fn)(uint32_t id);

void u5_idle_init(u5_nat_idle_add_fn add, u5_nat_idle_remove_fn remove);

/* pv_toolkit-shaped entry points. */
uint32_t u5_idle_add(pv_idle_fn fn, void *ctx);
pv_ret u5_idle_remove(uint32_t id);

/* Test introspection: number of slots currently in use. */
int u5_idle_inflight(void);

#endif
