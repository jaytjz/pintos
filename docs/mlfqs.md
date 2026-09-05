# MLFQS Implementation Plan

Based on the current `thread.c`/`thread.h` (priority donation already implemented), this is the plan for the Advanced Scheduler (MLFQS) task.

## 1. Fixed-point arithmetic (`threads/fixed-point.h`) — DONE

Already implemented and committed (`82c6c5c`), with a passing standalone test in `src/tests/fixed-point/`. 17.14 fixed-point, `typedef int32_t fixed_point_t`, `FIXED_F (1 << 14)`. API actually provided (use these names, not `fp_*`):

- `int_to_fixed`, `fixed_to_int_trunc`, `fixed_to_int_round` (round to nearest, ties away from zero)
- `fixed_add`, `fixed_sub`, `fixed_add_int`, `fixed_sub_int`
- `fixed_mul`, `fixed_mul_int`, `fixed_div`, `fixed_div_int` (`fixed_mul`/`fixed_div` cast through `int64_t`)

## 2. Thread struct additions (`thread.h`) — DONE

```c
int nice;                 /* Niceness, -20 to 20 */
fixed_point_t recent_cpu; /* Fixed-point recent CPU usage */
```

`thread.h` now `#include`s `threads/fixed-point.h` (needed for `fixed_point_t` in the struct). `init_thread` already `memset`s the whole struct to 0, so `nice`/`recent_cpu` start at 0 with no extra code (initial thread also starts at 0/0 per spec). Section 6 handles inheriting these from the parent in `thread_create` under MLFQS.

## 3. Global state (`thread.c`) — DONE

```c
static fixed_point_t load_avg;   /* System-wide load average */
```

Declared with the other file-scope statics (after `thread_mlfqs`), not among the function prototypes. `thread.c` `#include`s `threads/fixed-point.h` explicitly. Set to 0 in `thread_init` (a static is already zero-initialized, so this is belt-and-suspenders).

## 4. Core formulas (run only when `thread_mlfqs` is true) — DONE

Implemented as `static` helpers at the bottom of `thread.c`, forward-declared near line 79:

- `mlfqs_priority(t)` → `PRI_MAX - fixed_to_int_trunc(fixed_div_int(recent_cpu, 4)) - nice*2`, clamped to `[PRI_MIN, PRI_MAX]`. Returns `int`.
- `mlfqs_recent_cpu(t)` → coefficient `(2*load_avg)/(2*load_avg + 1)` computed first (overflow-safe), then `coeff*recent_cpu + nice`. Returns `fixed_point_t`.
- `mlfqs_update_load_avg()` → `load_avg = (59/60)*load_avg + (1/60)*ready_threads`, where `ready_threads = list_size(&ready_list) + (thread_current() != idle_thread ? 1 : 0)`. Mutates the global `load_avg`.
- `mlfqs_refresh_thread(t, aux)` → `thread_foreach` adapter: skips idle, sets `t->recent_cpu = mlfqs_recent_cpu(t)` then `t->priority = mlfqs_priority(t)`.

## 5. Where to hook updates — DONE

All inside `thread_tick()`, in a single `if (thread_mlfqs) { ... }` block after the stats update and before the `TIME_SLICE` preemption check. `thread_tick()` runs in external-interrupt context, so interrupts are already off (`thread_foreach`'s assertion is satisfied). Uses a local `int64_t ticks = timer_ticks()` — needs `#include "devices/timer.h"` (added).

- **Every tick**: `if (t != idle_thread) t->recent_cpu = fixed_add_int(t->recent_cpu, 1);`
- **Every second** (`ticks % TIMER_FREQ == 0`): `mlfqs_update_load_avg()`, then `thread_foreach(mlfqs_refresh_thread, NULL)`, then `list_sort(&ready_list, thread_priority_greater, NULL)`.
- **Else, every 4th tick** (`else if (ticks % 4 == 0 && t != idle_thread)`): `t->priority = mlfqs_priority(t)`. `else if` so the per-second tick doesn't redo it. Valid optimization: between per-second passes only the running thread's `recent_cpu` moves, so no other thread's priority can change on a 4-tick boundary — provided `thread_set_nice()` recalculates on its own (section 7).
- **Preempt-on-return**: after the recalc, still inside the `thread_mlfqs` block:
  ```c
  if (!list_empty (&ready_list)
      && list_entry (list_front (&ready_list), struct thread, elem)->priority > t->priority)
    intr_yield_on_return ();
  ```
  `intr_yield_on_return()`, not `thread_yield()`, because it's interrupt context. Relies on `ready_list` being sorted highest-first (maintained by `thread_unblock`/`thread_yield`, re-sorted in the per-second branch). The stock unconditional `if (++thread_ticks >= TIME_SLICE) intr_yield_on_return();` stays for round-robin among equal priorities.

## 6. Disable manual priority donation path when MLFQS is on — DONE

- `thread_set_priority()`: if `thread_mlfqs`, do nothing (calls should have no effect per spec).
- Priority donation logic (locks_held/waiting_on_lock) should be skipped entirely under MLFQS — donation is not used with the advanced scheduler.

### 6a. `nice` / `recent_cpu` inheritance in `thread_create()` — DONE

Spec: *"The initial thread starts with a nice value of zero. Other threads start with a nice value inherited from their parent thread."* Pintos also inherits `recent_cpu` from the parent.

Current state:
- Initial thread: OK by side effect — `init_thread()` does `memset(t, 0, ...)`, so `main` gets `nice = 0` / `recent_cpu = 0`.
- Every other thread: **wrong** — `thread_create()` never copies from `thread_current()`, so all new threads also get `0 / 0` regardless of the parent.

Fix in `thread_create()`, right after `init_thread (t, name, priority)`:

```c
t->nice = thread_current ()->nice;
t->recent_cpu = thread_current ()->recent_cpu;
if (thread_mlfqs)
  {
    /* Priority is derived from the formula, not the passed-in arg. */
    enum intr_level old = intr_disable ();
    t->priority = mlfqs_priority (t);   /* PRI_MAX - recent_cpu/4 - nice*2, clamped */
    intr_set_level (old);
  }
```

- Inherit unconditionally (harmless when `thread_mlfqs` is false, since the fields are unused then).
- Do it *before* `thread_unblock (t)` so the thread enters `ready_list` with the correct priority and lands in the right sorted position.
- The `priority` argument to `thread_create()` is ignored under MLFQS — that's expected.

## 7. Implement the nice/load_avg/recent_cpu accessors

```c
void thread_set_nice(int nice) {
  thread_current()->nice = nice;
  recalc priority for current thread;
  thread_yield_to_higher_priority();
}
int thread_get_nice(void) { return thread_current()->nice; }
int thread_get_load_avg(void) { return fixed_to_int_round(fixed_mul_int(load_avg, 100)); }
int thread_get_recent_cpu(void) { return fixed_to_int_round(fixed_mul_int(thread_current()->recent_cpu, 100)); }
```

## 8. Ready list ordering under MLFQS — DONE

`next_thread_to_run()` (`list_pop_front`) relies on `ready_list` staying sorted highest-first via `list_insert_ordered` in `thread_unblock`/`thread_yield`. The only bulk shift is the once-per-second pass, and it re-sorts `ready_list` with `list_sort(&ready_list, thread_priority_greater, NULL)` right after `thread_foreach`. The every-4-tick branch only touches the *running* thread (not on the list), so no resort is needed there — its new priority is applied when it next hits `thread_yield`/`thread_unblock`.

## 9. Tests to target

`mlfqs-load-1`, `mlfqs-load-60`, `mlfqs-load-avg`, `mlfqs-recent-1`, `mlfqs-fair`, `mlfqs-block` — these check load_avg/recent_cpu/priority formulas precisely and fairness of CPU distribution across nice levels. Run via `pintos -v -k -o mlfqs ... run mlfqs-fair` etc.

## Key gotchas

- Fixed-point rounding must match spec exactly (round to nearest, ties away from zero) or `mlfqs-load-avg`/`mlfqs-recent-1` fail on exact value checks.
- Don't recalc all threads' priority every tick — only every 4 ticks per spec (though incrementing recent_cpu is every tick).
- `idle_thread` should never be counted in `ready_threads` for load_avg, and its recent_cpu/priority shouldn't be touched.
- Must guard all this behind `if (thread_mlfqs)` so existing priority-donation tests still pass unaffected.
