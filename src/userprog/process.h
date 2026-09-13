#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct child_process {
	tid_t tid;
	bool load_success;
	struct semaphore load_sema; /* child ups after load() attempt */
	int exit_status;
	struct semaphore exit_sema; /* child ups in process_exit */
	int ref_count; /* 2: freed when both sides are done with it */
	struct list_elem elem; /* lives in parent's children list */

};

struct fd_entry {
	int fd;
	struct file *file;
	struct list_elem elem;
};

#endif /**< userprog/process.h */
