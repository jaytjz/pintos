The approach:

Track wake time, not remaining duration. When a thread calls timer_sleep(ticks), compute wake_tick = timer_ticks() + ticks and store it somewhere associated with the thread (a new field in struct thread, e.g. int64_t wakeup_time).
Block the thread instead of looping. Add the thread to a sleeping-threads list (or a priority queue ordered by wake time), then call thread_block(). This removes it from the ready queue — the scheduler won't touch it again until something explicitly unblocks it. Disable interrupts around the list insertion + block, since you're touching shared thread-list state.
Check the sleep list in the timer interrupt handler. timer_interrupt() already fires every tick. On each tick, walk the sleeping list and check whether timer_ticks() >= wakeup_time for any thread. If so, remove it from the list and call thread_unblock(), which puts it back on the ready queue.

6. Timer wheel (bucketed by tick modulo)
   Instead of one sorted list, keep an array of N lists, and file each sleeping thread into bucket wakeup_time % N. Each tick you only ever scan the one bucket for the current tick — O(1) work per tick regardless of how many threads are sleeping total, at the cost of some memory and the complexity of handling wraparound when N is smaller than the longest sleep duration. This is genuinely how Linux's classic timer wheel works, scaled up with multiple wheel levels for different time granularities. Way beyond what Pintos needs, but it's the design that answers "what if there are thousands of sleepers."

7. Sleep via semaphore instead of raw thread_block/thread_unblock
   Give each sleeping thread its own semaphore (initialized to 0) instead of blocking it directly. timer_sleep becomes sema_down(&t->sleep_sema), and the timer interrupt calls sema_up() on the semaphores of due threads instead of touching thread state directly. Functionally equivalent to direct blocking, but it reuses Pintos's existing synchronization primitive rather than reimplementing block/unblock bookkeeping — and it matters more once you add priority scheduling, since sema_up already integrates with the priority-ordered waiter list Pintos expects you to build for the priority scheduler assignment.

When sleep, 
record wakeuptime
up on thread semaphore 
write to priority queue wakeup time and thread ID
during timer_interrupt
check time and wakeuptime if time is bigger then wake up thread by down on semaphore
then thread will be unblocked and continue