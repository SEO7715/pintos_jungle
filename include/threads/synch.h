#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */
	struct list waiters;        /* List of waiting threads. */
};

void sema_init (struct semaphore *, unsigned value); //세마포어를 주어진 value로 초기화
void sema_down (struct semaphore *); // 세마포어를 요청하고, 획득한 경우 value를 1 낮춤
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *); // 세마포어를 반환하고, value를 1 높임
void sema_self_test (void);

// pintos - priority 2
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux);

/* Lock. */
// 각 스레드는 lock을 요청하거나 반환할 수 있음
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	// 현재 lock을 가지고 있는 스레드 정보

	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init (struct lock *); //lock 자료구조 초기화
void lock_acquire (struct lock *); // lock을 요청
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *); // lock을 반환
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. *///세마포어 안에 있는 waiting 스레드
};

void cond_init (struct condition *); // condition variable 자료구조 초기화
void cond_wait (struct condition *, struct lock *); // condition variable을 통해 signal이 오는지 기다림
void cond_signal (struct condition *, struct lock *); // condition variable에서 기다리는 가장 높은 우선순위의 스레드에 signal 보냄
void cond_broadcast (struct condition *, struct lock *); // condition variable에서 기다리는 모든 스레드에 signal 보냄

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
