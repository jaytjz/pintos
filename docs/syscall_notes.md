## Accessing user memory (Project 2, Method 1 — verify before dereferencing)

Goal: every syscall handler needs to read the syscall number + args off the user
stack, and for some syscalls (write, read, exec, create, open, ...) also touch an
arbitrary user buffer/string. Any of these can be a bad pointer supplied by a
malicious or buggy user program, and a bad pointer must kill *that process*
(exit(-1)) — never crash or panic the kernel. Chose Method 1 (software page-table
check before every access) over Method 2 (let the MMU fault and recover in
page_fault()) because it needs no assembly and no changes to exception.c, at the
cost of an extra pagedir walk per access. See the PHYS_BASE / paging_init /
pagedir_create discussion above for why is_user_vaddr alone isn't enough — an
address can be numerically below PHYS_BASE and still be unmapped in this
process's page directory, so it has to be checked with pagedir_get_page too.

Three-layer validity check for a single address `uaddr`:
1. `uaddr != NULL`
2. `is_user_vaddr(uaddr)` — below PHYS_BASE
3. `pagedir_get_page(thread_current()->pagedir, uaddr) != NULL` — actually mapped

All three must hold or the process gets killed. Wrap this in one helper
(something like `is_valid_uaddr`) so every call site does the same check the
same way instead of hand-rolling it per syscall (that's the "morass of
error-handling" p2.md's B6 question is pointing at).

Ranges (buffers, strings) need the same check for *every page the range
touches*, not just the first byte — a buffer can straddle a page boundary
where the first page is mapped and the second isn't. Plan:
- round the start address down to its page (`pg_round_down`)
- round the last byte's address down to its page
- walk page by page (`+= PGSIZE`) from the first to the last, checking each
  with pagedir_get_page
- for a NUL-terminated string (no known length up front) walk byte by byte
  checking each byte's page as you go, stop at the first `\0`, and still bound
  it against PHYS_BASE / a max length in case a malicious string is never
  terminated before running off the top of user space

Syscall handler shape:
- validate `f->esp` (+ each argument slot at f->esp + 1, +2, ... one word at a
  time) *before* dereferencing to read the syscall number and args — right now
  syscall.c only validates `f->esp` itself and reads the args unchecked, which
  is the first gap to close
- once args are read, validate any pointer/buffer arguments (e.g. SYS_WRITE's
  buffer, size) the same way before touching the data
- on any failed check: set `exit_status = -1` and call `thread_exit()`
  immediately — don't fall through to the syscall's normal logic
- resource cleanup on failure only matters once syscalls start allocating
  things (locks, open files) mid-handler — kill *after* releasing anything
  already acquired earlier in that same handler call, not before

## Functions to add

Still needed, in `src/userprog/syscall.c`:
- [ ] a NUL-terminated user string validator/copier for syscalls that take a
      `const char *` (`SYS_EXEC`, `SYS_CREATE`, `SYS_REMOVE`, `SYS_OPEN`) —
      byte-by-byte walk per the note above, since length isn't known up front
- [ ] `SYS_READ` needs the same buffer treatment as `SYS_WRITE`, but the
      buffer is being *written into* — same validation, just conceptually a
      destination rather than a source
- [ ] every other syscall taking a pointer argument, once implemented, must
      route through the same helpers before touching the data:
      `SYS_EXEC` (cmdline string), `SYS_CREATE`/`SYS_REMOVE`/`SYS_OPEN`
      (filename string), `SYS_SEEK`/`SYS_TELL`/`SYS_CLOSE`/`SYS_FILESIZE`
      (no pointer args, just need `validate_n_args`), `SYS_WAIT` (no pointer
      arg, just the pid)

## Exec / Wait — parent-child tracking (Project 2, Problem 2-2)

Goal: `exec` must block the calling process until the new process has either
finished `load()` or died trying, and must return that child's pid (== tid at
this stage) on success or -1 on any load failure. `wait` must block until a
*specific* child exits and return its exit status, but only once per child,
only for actual children, and it must work correctly even if the child already
exited before `wait` was called. Neither of these works today: `process_wait`
(`src/userprog/process.c`) is a literal infinite-loop stub, and there is no
`SYS_EXEC` case in `syscall.c` yet.

Core problem: a child's `struct thread` lives inside its own kernel-stack page
and gets freed once it exits (see `THREAD_DYING` / the note in `thread.h`
about the page layout). If `wait()` is called after the child is already gone,
there's nothing left to read an exit status from. So exit status (and the
exec load-result) can't live *only* in `struct thread` — they need a
separate, heap-allocated struct that can outlive whichever side (parent or
child) finishes first.

Design: a shared `struct child_process` (name tentative), malloc'd, holding:
- `tid_t tid`
- `bool load_success` + `struct semaphore load_sema` — child ups this after
  attempting `load()` in `start_process`; parent downs it in
  `process_execute` before returning, so exec is provably synchronous on load
  outcome
- `int exit_status` + `struct semaphore exit_sema` — child ups this in
  `process_exit`; parent downs it in `process_wait`
- `int ref_count` — starts at 2 (one ref each for parent and child); whichever
  side finishes second frees the struct
- `struct list_elem elem` — lives in the *parent's* `children` list

`struct thread` (`src/threads/thread.h`) needs two new fields inside the
`#ifdef USERPROG` block: `struct list children;` (init'd in `init_thread`)
and `struct child_process *cp;` — a self-pointer the child sets on itself
early in `start_process` so `process_exit` can find its own status struct.

Why `wait` removing the child from the list is enough (no extra "already
waited" bool needed): once `process_wait` finds, downs, reads, and frees a
`child_process`, it's no longer in `thread_current()->children`, so a second
`wait()` on the same tid naturally fails the lookup and returns -1.

Why exec needs the `load_sema` handshake at all, unlike real `fork()`+`exec()`:
Pintos fuses fork+exec into one syscall, so the parent genuinely doesn't know
if the new process is viable until `load()` inside the child has run. Real
Unix `fork()` returns immediately in both processes with no such handshake
(it's just duplicating already-valid state); the load-failure case here is
unique to Pintos's single-syscall exec.

`struct child_process` (in `process.h`), and the `children` list + `cp`
self-pointer fields on `struct thread` (`#ifdef USERPROG` block, with
`list_init (&t->children)` added to `init_thread`) are in place. What's left
is the actual `process_execute` / `start_process` / `process_wait` /
`process_exit` logic.

### `process_execute` (`process.c:29`)

1. `thread_create`'s single `aux` slot currently carries just `fn_copy`. It
   now needs to carry `fn_copy` *and* the new `child_process *`, so add a
   small carrier struct (e.g. `struct start_process_args { char *file_name;
   struct child_process *cp; }`), heap-allocated (not a `process_execute`
   stack local — same reasoning as why `fn_copy` is `palloc_get_page`'d
   rather than a stack buffer: the child thread can run and read it after
   `process_execute`'s frame is gone).
2. Allocate `cp` and fully initialize it — `sema_init` both semaphores to 0,
   `ref_count = 2`, `load_success = false` — *before* calling `thread_create`.
   This ordering matters: the moment `thread_create` runs, the child is
   schedulable and could touch `cp` before `process_execute` even returns
   from the call, so nothing about `cp` may be half-initialized at that
   point. `cp->tid` is the one field that can wait, since nothing reads it
   until `process_wait` looks it up later, which can only happen after this
   call returns.
3. If `thread_create` fails: the child never ran, so only the parent ever
   held a reference — just `free (cp)` directly (no ref-count dance needed,
   nothing to race with) plus free the carrier struct and `fn_copy` as today,
   return `TID_ERROR`.
4. On success: `cp->tid = tid`, then `list_push_back (&thread_current
   ()->children, &cp->elem)`.
5. `sema_down (&cp->load_sema)` — blocks until the child reports load
   outcome.
6. Check `cp->load_success`: if false, the child is already exiting on its
   own; remove `cp` from `children` and drop the parent's ref (freeing if it
   hits 0 — usually it won't yet, the child's own `process_exit` will be the
   one to actually free it), return `TID_ERROR`. If true, return `cp->tid`.

### `start_process` (`process.c:69`)

1. Unpack the carrier struct into locals (`file_name`, `cp`), free the
   carrier, then set `thread_current ()->cp = cp` — and this has to happen
   **before** the `argc == 0` check at `process.c:86-91`, since that branch
   calls `thread_exit ()` too, and `process_exit` will dereference
   `cur->cp`.
2. That `argc == 0` branch is itself a currently-missed load-failure case:
   right now it calls `thread_exit ()` without ever signaling `load_sema`,
   which would leave the parent blocked in `sema_down` forever. It needs
   `cp->load_success = false; sema_up (&cp->load_sema);` added before its
   `thread_exit ()`, exactly like the real load-failure path.
3. Existing failure path (`process.c:102-105`, `if (!success) thread_exit
   ()`): add `cp->load_success = false; sema_up (&cp->load_sema);` right
   before `thread_exit ()`. (`push_arguments` failing already funnels into
   this same branch, so it's covered too.)
4. Success path: `cp->load_success = true; sema_up (&cp->load_sema);` must
   happen *before* the `asm volatile (... jmp intr_exit ...)` at
   `process.c:113` — that jump is one-way into user mode and never returns
   to this C function, so it's the last chance to signal.

### `process_wait` (`process.c:126`)

Replace the infinite-loop stub: look up `child_tid` in `thread_current
()->children` (walk the list comparing `cp->tid`), return -1 if not found.
Otherwise `sema_down (&cp->exit_sema)`, read `cp->exit_status`, `list_remove
(&cp->elem)`, drop this side's ref (freeing if 0), return the status.
Removing it from the list is what makes a second `wait()` on the same tid
correctly fail the lookup and return -1 — no separate "already waited" flag
needed.

### `process_exit` (`process.c:138`)

After `exit_status` is set: `sema_up (&cur->cp->exit_sema)`, then drop this
side's ref on `cur->cp` (freeing if 0). Also walk `cur->children` and drop
the parent's ref on each remaining entry (freeing any that hit 0) — this is
what reclaims a child's status struct if the parent exits without ever
calling `wait` on it.

**Race to watch for when writing the ref-count decrement:** both sides
(`process_wait` and `process_exit`) do an unprotected `cp->ref_count--` on a
struct shared across two threads. That's a non-atomic read-modify-write; a
timer interrupt landing mid-decrement on one side while the other side is
also mid-decrement can leave the count permanently wrong (leak) or have both
sides independently compute 0 and both `free (cp)` (double free, heap
corruption). Do the decrement-and-maybe-free with interrupts disabled
(`intr_disable`/`intr_set_level`, same pattern used elsewhere for `all_list`
in `thread.c`), not as a bare `cp->ref_count--`.

Still needed, in `src/userprog/syscall.c`:
- [ ] `SYS_EXEC` case: `validate_n_args (f, 1)`, validate the `const char *`
      argument with the (still-unwritten) NUL-terminated string validator,
      then `f->eax = process_execute (cmd_line)`
- [ ] `SYS_WAIT` case: `validate_n_args (f, 1)`, `f->eax = process_wait
      ((tid_t) *((uint32_t *) f->esp + 1))` — no pointer validation needed,
      just the pid