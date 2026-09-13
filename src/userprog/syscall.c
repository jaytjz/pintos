#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>

#include "../lib/kernel/list.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "../lib/kernel/stdio.h"
#include "filesys/filesys.h"

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

static bool
is_valid_user_string (const char *str)
{
  for (;;str++) {
    if (!is_valid_user_vaddr((str)))
      return false;
    if (*str == '\0')
      return true;
  }
}

static struct fd_entry * get_fd_entry (int fd) {
  struct list *fd_list = &thread_current()->fd_list;
  for (struct list_elem *e = list_begin(fd_list); e != list_end(fd_list); e = list_next(e)) {
    struct fd_entry *entry = list_entry(e, struct fd_entry, elem);
    if (entry->fd == fd)
      return entry;
  }
  return NULL;
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
    case SYS_EXEC:
    {
      validate_n_args (f, 1);
      const char* cmd_line = (const char *) *((uint32_t *) f->esp + 1);
      if (!is_valid_user_string(cmd_line))
        kill_process();
      f->eax = process_execute(cmd_line);
      break;
    }
    case SYS_WAIT:
    {
      validate_n_args (f, 1);
      tid_t pid = (tid_t) *((uint32_t *) f->esp + 1);
      f->eax = process_wait(pid);
      break;
    }
    case SYS_CREATE:
    {
      validate_n_args (f, 2);
      const char *file = (const char *) *((uint32_t *) f->esp + 1);
      unsigned initial_size = (unsigned) *((uint32_t *) f->esp + 2);
      if (!is_valid_user_string(file))
        kill_process();
      lock_acquire(&filesys_lock);
      f->eax = filesys_create(file, initial_size);
      lock_release(&filesys_lock);
      break;
    }
    case SYS_REMOVE:
    {
      validate_n_args (f, 1);
      const char *file = (const char *) *((uint32_t *) f->esp + 1);
      if (!is_valid_user_string(file))
        kill_process();
      lock_acquire(&filesys_lock);
      f->eax = filesys_remove(file);
      lock_release(&filesys_lock);
      break;
    }
    case SYS_OPEN:
    {
      validate_n_args (f, 1);
      const char *file = (const char *) *((uint32_t *) f->esp + 1);
      if (!is_valid_user_string(file))
        kill_process();
      struct fd_entry *entry = malloc (sizeof (struct fd_entry));
      if (entry == NULL) {
        f->eax = -1;
        break;
      }
      lock_acquire(&filesys_lock);
      struct file *opened_file = filesys_open(file);
      lock_release(&filesys_lock);
      if (opened_file == NULL) {
        free(entry);
        f->eax = -1;
        break;
      }
      entry->fd = thread_current()->next_fd++;
      entry->file = opened_file;
      list_push_back(&thread_current()->fd_list, &entry->elem);
      f->eax = entry->fd;
      break;
    }
    case SYS_FILESIZE:
    {
      validate_n_args (f, 1);
      int fd = *((uint32_t *) f->esp + 1);
      struct fd_entry *entry = get_fd_entry(fd);
      if (entry == NULL)
        kill_process();
      struct file *file = entry->file;
      lock_acquire(&filesys_lock);
      f->eax = file_length(entry->file);
      lock_release(&filesys_lock);
      break;
    }
    case SYS_READ:
    {
      validate_n_args (f, 3);
      int32_t fd = *((int32_t *) f->esp + 1);
      void *buffer = (void *) *((uint32_t *) f->esp + 2);
      uint32_t size = *((uint32_t *) f->esp + 3);
      if (!is_valid_user_vaddr_bounds (buffer, size))
        kill_process();

      if (fd == STDIN_FILENO) {
        for (uint32_t i = 0; i < size; i++)
          ((uint8_t *) buffer)[i] = input_getc();
      }
      else if (fd == STDOUT_FILENO) {
        kill_process();   // reading from stdout is invalid
      }
      else {
        struct fd_entry *entry = get_fd_entry(fd);
        if (entry == NULL)
          kill_process();
        lock_acquire(&filesys_lock);
        f->eax = file_read(entry->file, buffer, size);
        lock_release(&filesys_lock);
      }
      break;
    }
    case SYS_WRITE:
    {
      validate_n_args (f, 3);
      int32_t fd = *((int32_t *) f->esp + 1);
      const void *buffer = (const void *) *((uint32_t *) f->esp + 2);
      uint32_t size = *((uint32_t *) f->esp + 3);
      if (!is_valid_user_vaddr_bounds (buffer, size))
        kill_process ();
      if (fd == STDOUT_FILENO)
      {
        putbuf(buffer, size);
        f->eax = size;
      }
      else if (fd == STDIN_FILENO)
      {
        kill_process();   // writing to stdin is invalid
      }
      else {
        struct fd_entry *entry = get_fd_entry(fd);
        if (entry == NULL)
          kill_process();
        lock_acquire(&filesys_lock);
        f->eax = file_write(entry->file, buffer, size);
        lock_release(&filesys_lock);
      }
      break;
    }
    case SYS_SEEK: {
      validate_n_args (f, 2);
      int fd = *((uint32_t *) f->esp + 1);
      unsigned position = *((uint32_t *) f->esp + 2);
      struct fd_entry *entry = get_fd_entry(fd);
      if (entry == NULL)
        kill_process();
      lock_acquire(&filesys_lock);
      file_seek(entry->file, position);
      lock_release(&filesys_lock);
      break;
    }
    case SYS_TELL: {
      validate_n_args (f, 1);
      int fd = *((uint32_t *) f->esp + 1);
      struct fd_entry *entry = get_fd_entry(fd);
      if (entry == NULL)
        kill_process();
      lock_acquire(&filesys_lock);
      f->eax = file_tell(entry->file);
      lock_release(&filesys_lock);
      break;
    }
    case SYS_CLOSE:
    {
      validate_n_args (f, 1);
      int fd = *((uint32_t *) f->esp + 1);
      struct fd_entry *entry = get_fd_entry(fd);
      if (entry == NULL)
        kill_process();
      list_remove(&entry->elem);
      lock_acquire(&filesys_lock);
      file_close(entry->file);
      lock_release(&filesys_lock);
      free(entry);
      break;
    }
    default:
      printf("Syscall not implemented yet");
      kill_process ();
  }
}
