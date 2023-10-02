/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered(&sema->waiters,&thread_current ()->elem,order_by_priority,NULL);
		// list_insert_donation(&sema->waiters,&thread_current ()->elem,order_by_priority,NULL);
		thread_block ();
	}
	sema->value--; 
	intr_set_level (old_level);
}

void list_insert_donation (struct list *list, struct list_elem *elem, list_less_func *less, void *aux)
{
    struct list_elem *e1, *e2, *e3;
	struct thread *insert_t;
	struct thread *wait_head = list_entry(list_begin(list),struct thread, elem);

	ASSERT (list != NULL);
	ASSERT (elem != NULL);

	//리스트가 비어있다면 맨 뒤로 넣어주고
	if(list_empty(list)){
		list_push_back(list,elem);
		return;
	}
	
	insert_t = list_entry(elem,struct thread,elem);
	// insert 할 스레드의 우선순위가 waiters의 모든 노드보다 클때
	if(insert_t->priority > wait_head->priority){
		for (e1 = list_begin (list); e1 != list_end (list); e1 = list_next (e1)){
			list_entry(e1,struct thread,elem)->priority = insert_t->priority;
		}
		list_push_back(list,elem);
		return;
	}
	// waiters에 더 큰 우선순위가 존재하면
	else{
		//waiters의 뒤부터 우선순위 비교하며 insert_t보다 큰게 아닌 것들에게 donate
		for (e2 = list_end (list); e2 != list_begin (list); e2 = list_prev (e2)){
			if (less (e2, elem, aux)){
				list_push_back(list,elem);
				break;
			}
			list_entry(e2,struct thread,elem)->priority = insert_t->priority;
		}
		return;
	}
}


/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	struct thread *t;
	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		list_sort(&sema->waiters,order_by_priority,NULL); // sema_down에서 insert order로 넣었는데 왜 또? %%%
		t = list_entry (list_pop_front (&sema->waiters),
					struct thread, elem);
		thread_unblock (t);
	}
	sema->value++;
	cmp_preempt_max(t); // cmp_preempt 로 하면 안되는 이유는 같은 우선순위끼리 fifo가 안되기 때문.
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));
	struct list_elem *e;
	int count = 0;

	if(lock->holder != NULL){ // lock->holder가 NULL이 아닐 때
		thread_current()->wait_on_lock = lock; 
		//스레드 T1 이 lock A 를 얻으려고 왔는데, 그 A는 이미 lock holder가 있었고, 그 lock holder는 락 B도 hold 하고 있는 상태다.
		// 이 때, 그 lock holder의 donation list에는 락 A의 우선순위 최대값과 락 B의 우선순위 최대값이 들어있다.
		// 만약 T1이 lock A의 우선순위 중 최고라면, lock holder의 donation list의 A에 대한 우선순위 max값을 갱신해야 한다. 아래는 이를 위한 코드다.
		for (e = list_begin (&lock->holder->donations); e != list_end (&lock->holder->donations);e = list_next(e)){
			struct thread *e_thread = list_entry(e,struct thread, d_elem);
			if(e_thread->wait_on_lock == lock){
				count++;
				if(e_thread->priority < thread_current()->priority){
					list_remove(e);
					list_insert_ordered(&lock->holder->donations, &thread_current()->d_elem, order_by_priority_donation, 0); // lock holders의 donations에 current thread 추가
					break;
				}
				else {
					break;
				}
			}
		}
		if(count == 0){
			list_insert_ordered(&lock->holder->donations, &thread_current()->d_elem, order_by_priority_donation, 0); // lock holders의 donations에 current thread 추가
		}
		cmp_priority_lock_aquire(lock, thread_current());
	}

	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
	lock->holder->wait_on_lock = NULL;
}

void cmp_priority_lock_aquire(struct lock *lock, struct thread *req_lock_thread)
{
	if(req_lock_thread->priority > lock->holder->priority){ // lock 요청한놈 priority가 lock holder보다 클 때		
		lock->holder->priority = req_lock_thread->priority; // lock holder의 우선순위를 lock 요청한 놈으로 우선순위 변경
	}
	if(lock->holder->wait_on_lock != NULL){
		cmp_priority_lock_aquire(lock->holder->wait_on_lock,lock->holder);
	}
	return;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	cmp_priority_lock_release(lock);

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

void cmp_priority_lock_release(struct lock *lock){
	struct thread *cur_t = thread_current();
	struct list_elem *e;

	for (e = list_begin (&cur_t->donations); e != list_end (&cur_t->donations);){
		if(list_entry(e,struct thread,d_elem)->wait_on_lock == lock){
			e = list_remove(e);
			
		} else 
			e = list_next (e);
	}

	if(!list_empty(&cur_t->donations)){
		int max_priority = list_entry(list_begin (&cur_t->donations),struct thread,d_elem)->priority;
		if(cur_t->priority > max_priority){
			cur_t->priority = max_priority;
		} 
	} else{
		cur_t->priority = cur_t->origin_p;
	}

}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	// list_insert_ordered(&cond->waiters,&waiter.elem,order_by_priority_condition,NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		list_sort(&cond->waiters, order_by_priority_condition,NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

bool order_by_priority_condition (const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct semaphore_elem *waiter1 = list_entry(a,struct semaphore_elem, elem);
    struct thread *st_a = list_entry(waiter1->semaphore.waiters.head.next, struct thread, elem);

	struct semaphore_elem *waiter2 = list_entry(b,struct semaphore_elem, elem);
    struct thread *st_b = list_entry(waiter2->semaphore.waiters.head.next, struct thread, elem);

    return st_a->priority > st_b->priority;
}