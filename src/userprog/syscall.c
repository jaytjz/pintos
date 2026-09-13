#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "../lib/kernel/stdio.h"

static void syscall_handler (struct intr_frame *);

/* Single-address check: non-NULL, below PHYS_BASE, and mapped in the
   current process's page directory. */
static bool
is_valid_user_vaddr (const void *user_vaddr) {
  if (user_vaddr == NULL)
    return false;
  if (!is_user_vaddr((user_vaddr)))
    return false;
  if (pagedir_get_page(thread_current()->pagedir, user_vaddr) == NULL)
    return false;
  return true;
}

/* Range check: every page touched by the SIZE bytes starting at
   USER_VADDR_START must be valid. */
static bool
is_valid_user_vaddr_bounds (const void *user_vaddr_start, size_t size) {
  if (size == 0)
    return true;
  const uint8_t *start = user_vaddr_start;
  const uint8_t *end = start + size - 1;

  if (end < start) //overflow
    return false;

  uint8_t *pg_start = pg_round_down(start);
  uint8_t *pg_end = pg_round_down(end);
  for (uint8_t *page = pg_start; page <= pg_end; page += PGSIZE) {
    if (!is_valid_user_vaddr(page))
      return false;
  }
  return true;
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void kill_process() {
  thread_current()->exit_status= -1;
  thread_exit ();
}

/* Validates that the syscall number and its first N argument slots
   (0 <= N <= 3) are all readable user addresses, killing the current
   process if any of them isn't. */
void validate_n_args (struct intr_frame *f, int n) {
  const void* arg0_addr = f->esp;
  const void* arg1_addr = (const void*)((uint32_t *) f->esp + 1);
  const void* arg2_addr = (const void*)((uint32_t *) f->esp + 2);
  const void* arg3_addr = (const void*)((uint32_t *) f->esp + 3);
  switch (n) {
    case 3:
      if (!is_valid_user_vaddr_bounds (arg3_addr, sizeof (uint32_t)))
        kill_process();
      /* fall through */
    case 2:
      if (!is_valid_user_vaddr_bounds (arg2_addr, sizeof (uint32_t)))
        kill_process();
      /* fall through */
    case 1:
      if (!is_valid_user_vaddr_bounds (arg1_addr, sizeof (uint32_t)))
        kill_process();
      /* fall through */
    case 0:
      if (!is_valid_user_vaddr_bounds (arg0_addr, sizeof (uint32_t)))
        kill_process();
      break;
    default:
      PANIC ("n shouldn't be bigger than 3 or less than 0");
  }
}

static void
syscall_handler (struct intr_frame *f)
{
  validate_n_args (f, 0);
  uint32_t syscall_number = *(uint32_t *) f->esp;

  switch (syscall_number) {
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_EXIT:
    {
      validate_n_args (f, 1);
      uint32_t status = *((uint32_t *) f->esp + 1);
      thread_current ()->exit_status = status;
      thread_exit ();
      break;
    }
    case SYS_WRITE:
    {
      validate_n_args (f, 3);
      int32_t fd = *((int32_t *) f->esp + 1);
      const void *buffer = (const void *) *((uint32_t *) f->esp + 2);
      uint32_t size = *((uint32_t *) f->esp + 3);
      if (fd == STDOUT_FILENO)
      {
        if (!is_valid_user_vaddr_bounds (buffer, size))
          kill_process ();
        putbuf(buffer, size);
        f->eax = size;
      }
      else
      {
        //TODO: Implement after fd table
        f->eax = -1;
      }
      break;
    }
    default:
      printf("Syscall not implemented yet");
      kill_process ();
  }
}
