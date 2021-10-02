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
	list_init (&sema->waiters); //스레드 wait 리스트 생성
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
		// pintos - priority 2
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema-> waiters, &thread_current() -> elem, cmp_thread_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
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

	old_level = intr_disable ();
	
	//기존
	// if (!list_empty (&sema->waiters))
	// 	thread_unblock (list_entry (list_pop_front (&sema->waiters),
	// 				struct thread, elem));

	// pintos - priority 2
	// waiter 리스트 우선순위 정렬
	// 점유하고 있던 세마포어 반환
	if (!list_empty(&sema -> waiters)) {
		list_sort(&sema->waiters, cmp_thread_priority, NULL); 
		thread_unblock(list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}
	
	sema->value++; 

	// pintos - priority 2
	test_max_priority(); // cpu 점유중인 스레드와 ready_list에 있는 스레드의 우선순위 비교
	// ready list에 들어갈 때, 우선순위가 가장 높아서 맨 앞에 들어가있을 수 있으니깐

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
// Initializes lock as a new lock. The lock is not initially owned by any thread.
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL; //초기값 NULL 설정
	sema_init (&lock->semaphore, 1); // lock 안에 세마포어 만들어줌 (세마포어 초기값은 1로 설정)
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
	ASSERT (!lock_held_by_current_thread (lock)); // lock hold한 스레드가 current 스레드가 아닌지 확인

	// pintos project - priority donation
	struct thread *cur = thread_current();
	if (lock->holder) { // lock의 holder가 존재할 경우, holder 스레드에게 priority를 넘겨주기
		cur -> wait_on_lock = lock; // 스레드가 현재 얻기 위해 기다리고 있는 lock은, 현재 lock
		// list_insert_ordered(&lock->holder->donations, &cur->donation_elem, thread_compare_donate_priority, 0);
		
		list_push_front (&lock->holder->donations, &cur->donation_elem); 
		// 우선순위가 더 높은 스레드가 lock_acquire를 한 것이므로 
		// &lock->holder->donations 내 가장 앞부분에(가장 우선순위가 높은 위치) insert 해주기

		donate_priority();
	}

	// 현재 lock을 소유하고 있는 스레드가 없을 경우, lock hold하기
	sema_down (&lock->semaphore);

	// pintos project - priority donation
	cur->wait_on_lock = NULL;
	lock->holder = cur;

	// lock->holder = thread_current ();
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
	ASSERT (lock_held_by_current_thread (lock)); // lock hold한 스레드가 current 스레드인지 확인

	// pintos project - priority donation
	remove_with_lock(lock); 
	// 현재 lock을 사용하기 위해 나에게 priority donate한 스레드들을 donations 리스트에서 제거 
	refresh_priority(); //priority 재설정

	lock->holder = NULL;
	sema_up (&lock->semaphore); // sema_up하여 lock 점유 반환

}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current (); // 같으면 true 반환(lock hold한 스레드가 current 스레드인지 확인)
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

// pintos - priority 2
// 두 세마포어 우선순위 비교 (인자로 세마포어 list_elem을 받음)
// a의 우선순위가 높으면 1, b의 우선순위가 높으면 0을 리턴
bool
cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux) {
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	struct list *la = &(sa -> semaphore.waiters);
	struct list *lb = &(sb -> semaphore.waiters);

	struct list_elem *lea = list_begin(la);
	struct list_elem *leb = list_begin(lb);

	struct thread *thread_a = list_entry(lea, struct thread, elem);
	struct thread *thread_b = list_entry(leb, struct thread, elem);

	if(thread_a -> priority > thread_b -> priority)
		return true;
	else
		return false;

	// **** 주어진 list_elem의 주소 a로 이것이 포함된 진정한 semaphore_elem의 주소를 구하는 매크로 함수
	// 구조체 구조 파악: 
    // struct semaphore_elem { struct list_elem elem{
    //  											struct list_elem *prev,
    //											    struct list_elem *next },
    //						  struct semaphore semaphore{
    //  											unsigned value,
    //											    struct list waiters{
    //														struct list_elem head,	
    //														struct list_elem tail }}
 
    // elem.next 멤버만큼 offset 왼쪽으로 이동하면 struct semaphore_elem의 주소 --> 이후 캐스팅
    //    (struct semaphore_elem *)       & elem->next
    //                        |<-offset()->|     
    // in memory...    		  V            V
    // sturct semaphore_elem: || elem.prev | elem.next || semaphore.value | semaphore.waiters...|| 
	//						  ^            ^                              |
	//	    a_sema, b_sema 여기		     여기 a.next, b.next              ^
	//										waiter_a_sema, waiter_b_sema 여기

	// l_sema.semaphore의 waiting_list의 첫번째 원소의 priority와
	// s_sema.semaphore의 waiting_list의 첫번째 원소의 priority 대소 비교
	// 즉, 세마포어가 가지는 waiting_list의 첫번째 스레드들의 priority를 비교하기 위함
}


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters); //세마포어 wait list 초기화
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
	
	// if(list_empty(&waiter.semaphore.waiters)) {
	// 	printf("********************************************\n");
	// }; // 생성한 세마포어 내 스레드 여부 확인
	
	// pintos - priority 2
	list_push_back (&cond->waiters, &waiter.elem);
	// list_insert_ordered(&cond -> waiters, &waiter.elem, cmp_sem_priority, NULL); 안해도 되지 않나..

	lock_release (lock); // priority_condvar_thread()에서 lock_acquire()를 해줬으므로 release 해주기
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

	if (!list_empty (&cond->waiters))
		// pintos - priority 2
		// wait 도중에 priority가 바꿔었을 수 있으므로, list_sort()로 내림차순 정렬
		list_sort(&cond ->waiters, cmp_sem_priority, NULL);

		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
		// cond이 관리하는 세마포어 wait list의 가장 맨 앞에 위치한(우선순위가 가장 높은) 세마포어를 인자로 넣어줌
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


