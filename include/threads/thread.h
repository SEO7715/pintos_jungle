#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	// 기동(running)중, 오직 한 스레드가 주어진 시간동안 기동하고 있음
	// thread_current()는 현재 기동중인 스레드를 반환함

	THREAD_READY,       /* Not running but ready to run. */
	// 스레드가 기동할 준비가 되었지만 기동하지 않는 상태
	// 스케줄러에 의해 기동될 수 있음
	// ready thread는 ready_list라는 이중연결리스트 안에 보관됨

	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	// 대기(또는 일시정지)중인 상태(ready와 다름)
	// 이 스레드는 thread_unblock()으로 상태가 thread_ready로 바뀌기 전에는 스케줄에 할당되지 않음
	// 원시적 synchronization 방법
	// blocked thread가 무엇을 기다리고 있는지는 알 수 없음

	THREAD_DYING        /* About to be destroyed. */
	// 다음 스레드가 오면, 스케줄러에 의해 사라질 스레드라는 것을 나타냄
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	//스레드의 상태는 thread_running, thread_ready, thread_blocked, thread_dying 중 하나

	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. 스레드 우선순위(0이 가장 낮은 우선순위) */

	int64_t wakeup_tick;                // alarm clock
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	// 스레드를 이중연결 리스트에 넣기 위해 사용
	// 이중연결리스트란, ready_list(run을 위해 ready 중인 스레드의 리스트),
	// sema_down()에서 세마포어에서 waiting 중인 스레드 리스트를 말함
	// 세마포어에 있는 스레드는 ready 상태가 될 수 없고, ready 상태인 스레드는 세마포어일 수 없으므로 
	// 두 리스트에 대해 같은 list_elem을 사용할 수 있음

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	// 레지스터 및 스택 포인터를 포함하는 context switching에 대한 정보 저장

	unsigned magic;                     /* Detects stack overflow. */
	// 스택 오버플로우 탐지 위해 사용하며, 스택 오버플로우가 발생할 시 해당 숫자가 바뀌는 성질을 이용
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

// pintos project - alarm clock
void thread_sleep(int64_t ticks); //running 상태인 스레드를 sleep으로 만들어줌(block)
void thread_awake(int64_t ticks); // 깨워야 할 스레드를 awake 
void update_next_tick_to_awake(int64_t ticks); // 최소 tick을 가진 스레드 저장
int64_t get_next_tick_to_awake(void); // thread.c의 next_tick_to_awake 반환

// pintos project - priority
void test_max_priority (void);
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

void thread_init (void); // 스레드 시스템 초기화
void thread_start (void); // idle 스레드 생성, thread_create(), interrupt 활성화

void thread_tick (void); 
void thread_print_stats (void);

typedef void thread_func (void *aux); // 스레드로 실행되는 함수의 type, aux는 함수 인자 
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void); 
//running 상태의 스레드를 blocked 상태로 변경
//해당 스레드는 호출이 없으면 다시 기동하지 않음
// low-level 방식의 synchronization

void thread_unblock (struct thread *); // blocked -> ready 상태로 변경

struct thread *thread_current (void); // running 상태 스레드 반환
tid_t thread_tid (void); // running 상태 스레드의 tid 반환 (thread_current() -> name과 같음)
const char *thread_name (void);

void thread_exit (void) NO_RETURN; // 현재 스레드를 나가게 함

void thread_yield (void); 
// 스레드가 새로운 스레드를 기동할 수 있도록 스케줄러에 CPU 할당
// 새로운 스레드가 current thread로 바뀜
// 다만, 스레드를 특정 시간동안만 running 상태에서 벗어나게 하기 위해 해당 함수 쓰면 안 됨

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
