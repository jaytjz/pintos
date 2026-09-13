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

Done (in `src/threads/vaddr.h`):
- [x] `is_valid_user_vaddr (const void *user_vaddr)` — the three-layer single
      address check
- [x] `is_valid_user_vaddr_bounds (const void *user_vaddr_start, size_t size)`
      — pages-spanned range check, with the `size == 0` short-circuit and the
      `end < start` overflow guard

Done (in `src/userprog/syscall.c`):
- [x] `kill_process (void)` — `exit_status = -1; thread_exit();` in one place,
      used by every failure path (bad syscall number, bad arg, bad buffer,
      unimplemented syscall)
- [x] `validate_n_args (struct intr_frame *f, int n)` — validates the syscall
      number's slot plus the first N argument slots (0 <= N <= 3) with
      `is_valid_user_vaddr_bounds`, one word at a time, via a deliberate
      case-3-falls-through-to-case-0 switch; kills on any failed check;
      `PANIC`s if N is out of range (a kernel-side caller bug, not user input)
- [x] wired into `syscall_handler`: `validate_n_args(f, 0)` before reading the
      syscall number (replaces the old `is_user_vaddr(f->esp)` check, which
      only proved the address was below PHYS_BASE, not that it's mapped),
      `validate_n_args(f, 1)` for `SYS_EXIT`, `validate_n_args(f, 3)` for
      `SYS_WRITE`
- [x] `SYS_WRITE`'s buffer check now uses `is_valid_user_vaddr_bounds(buffer,
      size)`, replacing the old `is_user_vaddr(buffer) && is_user_vaddr(buffer
      + size)` (unsafe `void *` arithmetic, and only checked two addresses,
      not every page the buffer spans)

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