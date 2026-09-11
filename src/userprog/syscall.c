#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "../lib/kernel/stdio.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //is_user_vaddr doesn't guarantee page is mapped just that it is in user vaddr
  if (is_user_vaddr(f->esp)) {
    uint32_t syscall_number = *(uint32_t *) f->esp;
    switch (syscall_number) {
      case SYS_HALT:
        shutdown_power_off();
        break;
      case SYS_EXIT:
      {
        uint32_t status = *((uint32_t *) f->esp + 1);
        thread_current ()->exit_status = status;
        thread_exit ();
        break;
      }
      case SYS_WRITE:
      {
        int32_t fd = *((int32_t *) f->esp + 1);
        const void *buffer = (const void *) *((uint32_t *) f->esp + 2);
        uint32_t size = *((uint32_t *) f->esp + 3);
        if (fd == STDOUT_FILENO) 
        {
          if (is_user_vaddr(buffer) && is_user_vaddr(buffer + size) && (buffer <= buffer + size)) 
          {
            putbuf(buffer, size);
            f->eax = size;
          }
          else
            thread_exit ();
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
        thread_exit ();
    }
  }
  else 
    thread_exit ();
}
