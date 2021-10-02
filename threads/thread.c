#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210


// pintos project - alarm clock
static struct list sleep_list; //block 상태 스레드를 관리하기 위한 리스트
static int64_t next_tick_to_awake; 
//sleep_list에서 대기중인 스레드의 wakeup_tick 값 중 최솟값을 저장

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */

static unsigned thread_ticks;   /* # of timer ticks since last yield. */
// 새로운 커널 스레드가 running된지 얼마나 지났는지 기록하는 전역변수

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */

// 스레드 시스템 시작하기 위해 호출
// 핀토스의 첫번째 스레드 생성
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);
	// assert: 지정한 조건식이 FALSE 이면 프로그램 중단, TRUE면 프로그램 계속 실행

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list); // ready_list
	list_init (&destruction_req); // 소멸될 스레드 리스트

	// pintos project- alarm clock
	list_init (&sleep_list); //sleep_list

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
   // 스케줄러를 시작하기 위해 호출
   // ready 상태의 스레드가 없을 때, 스케줄 되는 idle thread 생성
   // (idle은 어떤 프로그램에 의해서도 사용되지 않는 유휴 상태를 의미)
   // main()이나 intr_yield_on_return()을 사용하는 인터럽트를 가능하게 만든다
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started; 
	//semaphore 구조체 (멤버 : current value, list waiters(waiting threads 리스트))

	sema_init (&idle_started, 0); // 초기값 0으로 만들어줘서 create 하는동안에 보호해주는것 

	thread_create ("idle", PRI_MIN, idle, &idle_started); //idle thread 생성
	// idle() 호출해서, &idle_started를 인자로 넣음
	// idle()에서 sema_up() 실행해줌 -> 그러고 나서, sema_down이 가능해짐!

	/* Start preemptive thread scheduling. */
	intr_enable (); //interrupt 활성화 

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started); //sema_init (&idle_started, 0) 으로 보호상태(0인 상태)이기 때문에
	// idle_started 세마포어가 1이 될때까지 실행되지 않음. 
	// thread_create()가 실행하면 idle 함수에서 sema_up을 할 때까지!

}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
// timer interrupt가 호출함
// time slice가 만료되었을 때, 스케줄러를 가동함
// 스케줄러 통계치를 가지고 있음
// 매 tick마다 thread_tick() 호출
// thread_tick()을 호출하는 다른 누군가는 kernel
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread) //current 스레드가 idle_thread인 경우, idle_ticks 증가
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else // 아닌 경우, kernel_ticks 증가
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return (); 
		// true가 되어 thread_yield() 호출
		//context switching 시작됨
}

/* Prints thread statistics. */
// 스레드가 shutdown 일 때, 호출됨
// 스레드 통계를 출력
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */

//인자 name으로 스레드를 만들고 시작함
//만들어진 스레드의 tid 반환하고, 해당 스레드는 function 함수를 실행, aux는 function의 인자를 나타냄
//thread_create()는 스레드의 페이지를 할당하고, 스레드 구조체를 초기화하며 스레드 스택을 할당함
//스레드는 blocked 상태에서 초기화 되며, 반환 직전에 unblocked 됨 --> 스레드를 스케줄하기 위해!
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority); //스레드 구조체 초기화
	tid = t->tid = allocate_tid (); //tid 할당

	/* Call the kernel_thread if it scheduled. // 스케줄 되었을 때, 실행할 첫 명령어가 kernel_thread이고
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread; // 현재 영역의 위치를 가리키는 포인터
	t->tf.R.rdi = (uint64_t) function; // kernel_thread에 넣을 1번째 인자
	t->tf.R.rsi = (uint64_t) aux; // kernel_thread에 넣을 2번째 인자
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t); //unblock 해서 ready queue에 넣기

	// pintos project - priority
	// 생성된 스레드 t의 우선순위(t->priority)와 current 스레드의 우선순위(thread_current()-> priority) 비교하여, 
	// t의 우선순위가 더 클 경우, thread_yield() 호출하여 cpu 선점
	if (t->priority > thread_current()-> priority)
		thread_yield();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ()); // 외부 interrupt 수행 여부 확인
	ASSERT (intr_get_level () == INTR_OFF); // interrupt off 상태 확인
	thread_current ()->status = THREAD_BLOCKED; //현재 스레드의 state를 block으로 변경하고,
	schedule (); // 스케줄 함수 호출 (단, thread_unblock()을 호출하기 전까지 스케줄 되지 않음)
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */

// 인자로 받은 스레드를 다시 스케줄 되도록 함(block 상태인 스레드를 unblock(ready to run)상태로 변경)
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t)); //스레드 값이 null 이 아니고, 스택오버플로우가 아닌지 확인

	old_level = intr_disable (); //intrerupt off
	ASSERT (t->status == THREAD_BLOCKED); //해당 스레드의 상태가 block 상태인지 확인

	// pintos project - priority
	// list_push_back (&ready_list, &t->elem); // 해당 스레드를 ready_list 끝에 추가
	// priority에 따라 정렬하여 ready_list에 삽입
	list_insert_ordered(&ready_list, &t->elem, cmp_thread_priority, NULL);


	t->status = THREAD_READY; // 스레드 상태를 ready로 변경
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

// 진짜 current 스레드인지 확인
/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ()); // 외부 interrupt 상태 확인

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable (); //interrupt off 처리
	do_schedule (THREAD_DYING); // 해당 스레드 dying 처리 
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
// 현재 running 중인 스레드를 비활성화 시키고, ready_list에 삽입
// 해당 과정을 수행하는 동안 들어오는 interrupt를 모두 무시하고,
// 작업이 끝나면 thread_yield()하기 직전의 인터럽트 상태로 되돌림
void
thread_yield (void) {
	struct thread *curr = thread_current (); //current 스레드
	enum intr_level old_level;

	ASSERT (!intr_context ()); 
	//intr_context() : 외부(하드웨어) 인터럽트 수행 중에는 해당 값이 true로 설정되며, 이 외에는 false를 반환
	// 외부 인터럽트 수행 중인지 확인

	old_level = intr_disable (); //old level은 interrupt off로 설정

	// pintos project - priority
	if (curr != idle_thread) //curr가 idle_thread가 아니면, ready_list의 맨 끝에 삽입
		// list_push_back (&ready_list, &curr->elem); 
		list_insert_ordered(&ready_list, &curr->elem, cmp_thread_priority, NULL);// 우선순위에 따라 정렬되어 삽입
		// cmp_thread_priority() : ready_list의 우선순위가 높으면 1, curr->elem의 우선순위가 높으면 0을 반환
	
	do_schedule (THREAD_READY); //do_schedule로 스레드 상태를 running에서 ready로 변경
	intr_set_level (old_level); // 인자로 부여한 level이 interrupt ON 상태이면 intr_enable ()/ interrupt off 상태이면 intr_disable ()
}

/* Sets the current thread's priority to NEW_PRIORITY. */
// pintos project - priority ver
// void
// thread_set_priority (int new_priority) {
// 	// thread_current ()->priority = new_priority; // new prority 값을 스레드에 적용
	
// 	// priority가 조정되는 mlfqs가 발생할 경우,
// 	// 현재 priority 값을 새로 받은 new_priority로 변경하고, 
// 	// 이 때 만약 현재 cpu에서 running 상태인 스레드의 priority가 낮아졌다면
// 	// ready_list의 스레드와 우선순위를 비교할 수 있도록 test_max_priority() 호출
// 	if(!thread_mlfqs) { 
// 		int previous_priority = thread_current() -> priority; //previous_priority = 현재 실행중인 스레드의 우선순위 값
// 		thread_current() -> priority = new_priority; // thread_current() -> priority 는 new_priority 값으로 갱신

// 		if(thread_current()->priority < previous_priority) // new_priority < previous_priority 이면
// 		// 현재 실행 중인 스레드의 우선순위 보다, ....new_priority가 더 높은 우선순위를 가지면
// 			test_max_priority(); // ready_list의 스레드와 우선순위 비교
	
// 	}
// }

// pintos project - priority donation ver
void
thread_set_priority (int new_priority) {
	thread_current() -> init_priority = new_priority;

	refresh_priority();
	test_max_priority();
}


/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
// 하나의 함수를 실행시키고, 종료하는 것(즉, 한 개의 커널 스레드는 컴퓨터에서 수행하는 하나의 task를 개념화한 것)
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* because The scheduler runs with interrupts off.*/ 
	function (aux);       /* Execute the thread function. 함수 실행*/
	thread_exit ();       /* If function() returns, kill the thread. 함수 종료시, kernel thread 종료*/
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
   // 커널 스레드에 들어가야 하는 정보를 가지고 있는 struct thread의 값을 init_thread() 함수를 통해 초기화 
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);

	// pintos project - priority donation
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donations);

	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
// 다음 기동할 스레드 정하기
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list)) //ready_list에 스레드가 없으면 idle_thread를 next로 지정
		return idle_thread;
	else // ready_list에 스레드가 있는 경우, list의 첫번째 스레드를 next로 지정
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
   // 새로운 스레드가 running함에 따라, context switching 수행
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf; 
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF); //interrupt off 

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			// rax : 누산기(accumulator) 레지스터 -> 사칙연산 명령어에서 자동으로 사용, 리턴 레지스터/ 시스템콜의 실질적인 번호를 가리키는 포인터
			// rbx : 베이스 레지스터 -> 메모리 주소 저장
			// rcx : 카운터 레지스터 -> ECX(Extended Counter Register)로, 반복적으로 수행되는 연산에 사용되는 카운터 값 저장
			// rdx : 데이터 레지스터 -> EAX(Extended Accumulator Register)와 같이 사용되며, 산술 연산 또는 함수의 리턴 값 저장
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			// rsp : 스택 포인터 레지스터 / rbp : 베이스 포인터 레지스터(스택 복귀 주소)/ rsi : 근원지(source) 레지스터/ rid : 목적지(destination) 레지스터
			// rsp, rbp, rsi, rdi 레지스터의 경우 포인터나 인덱스라 부르기도 함
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
// 현재 running 중인 스레드 status를 바꾸고, 새로운 스레드를 실행
// interrupt가 없고, 현재 스레드 상태가 running인 경우 실행

static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF); //interrupt off 상태인지 확인
	ASSERT (thread_current()->status == THREAD_RUNNING); //current 스레드 상태가 running인지 확인
	while (!list_empty (&destruction_req)) {  
		// destruction_req가 있는 리스트의 맨 앞을 victim으로 지정
		// 즉, 삭제 리스트 안의 첫번째 리스트를 victim으로 지정
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim); // victim 페이지 할당 해제
	}
	thread_current ()->status = status; //현재 스레드의 상태를 인자로 받은 상태(ready)로 갱신
	schedule ();
}

// running 스레드(current)를 빼내고, next 스레드를 running으로 만들어 줌
// curr = running 상태의 스레드
// next = ready_list가 있으면, ready_list의 첫번째 스레드를 가져오고, ready_list가 빈 경우, idle thread임
static void
schedule (void) {
	struct thread *curr = running_thread (); // current 스레드(running 상태)
	struct thread *next = next_thread_to_run (); //next 스레드

	ASSERT (intr_get_level () == INTR_OFF); // interrupt off 상태 확인
	ASSERT (curr->status != THREAD_RUNNING); // current 스레드 상태가 running인지 확인
	ASSERT (is_thread (next)); // next 스레드가 null이 아니고, 스택오버플로우 상태가 아니면
	/* Mark us as running. */
	next->status = THREAD_RUNNING; //next 스레드 running으로 변경

	/* Start new time slice. */
	thread_ticks = 0; //thread_ticks를 0으로 변경(새로운 스레드가 시작했으므로)

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) { // curr과 next가 다른 스레드이고,
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			// curr가 존재하면서 dying 상태이고, curr는 initial_thread가 아니라면
			// curr를 삭제 리스트의 마지막에 넣기
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next); // next를 thread_launch()함(context switching 수행)
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

// pintos project - alarm clock
void
thread_sleep (int64_t ticks) {
	struct thread *this;
	this = thread_current();

	if (this == idle_thread) //idle_thread는 sleep 되어서는 안되므로
	{
		ASSERT(0);
	}
	else
	{
		enum intr_level old_level;
		old_level = intr_disable(); //interrupt off

		update_next_tick_to_awake(this->wakeup_tick = ticks); // 일어날 시간(ticks) 저장
		list_push_back(&sleep_list, &this->elem); //push to sleep_list
		thread_block(); // block 상태로 만들어주기
		intr_set_level(old_level); //interrupt on

		// intr_enable(); // 이렇게 하면 안되나..?
	}
}

// pintos project - alarm clock
// wakeup_tick 값이 ticks 보다 작거나 같은 스레드를 깨움
// 현재 대기중인 스레드의 wakeup_tick 변수 중 가장 작은 값을
// next_tick_to_awake 전역 변수에 저장
void
thread_awake (int64_t wakeup_tick) {
	next_tick_to_awake = INT64_MAX; // next_tick_to_awake 변수 초기화 // 왜 여기있을까..?

	struct list_elem *sleeping;
	sleeping = list_begin(&sleep_list); // sleep_list의 head에 있는 스레드를 sleeping 변수로 설정

	// for all sleeping threads
	// sleep_list의 모든 entry를 순회하면서
	// 현재 tick이 깨워야 할 tick 보다 크거나 같을 경우, 리스트에서 제거하고 unblock
	// 작을 경우, update_next_tick_to_awake() 호출
	while(sleeping != list_end(&sleep_list)) 
	//sleeping list의 마지막 위치에 있는 스레드가 sleeping 변수가 될 때까지
	{	
		struct thread *th = list_entry(sleeping, struct thread, elem); //list_entry 를 쓰는 이유..?

		if (wakeup_tick >= th->wakeup_tick) { //스레드가 일어날 시간이 되었는지 확인
			sleeping = list_remove(&th -> elem); //스레드 삭제 
			thread_unblock(th); //스레드 unblock 으로 설정
		}
		else {
			sleeping = list_next(sleeping);
			update_next_tick_to_awake(th -> wakeup_tick);
		}
	}
}

// pintos project - alarm clock
// 다음으로 깨어나야 할 스레드의 tick 값을 최소값으로 갱신하도록 하는 함수
// 현재 ticks 값과 비교하여 더 작은 값을 가질 수 있도록 함
// 즉, next_tick_to_awake 변수를 업데이트
// next_tick_to_awake가 깨워야 할 스레드 중 가장 작은 tick을 가지도록 업데이트
void
update_next_tick_to_awake(int64_t ticks) {
	next_tick_to_awake = (next_tick_to_awake > ticks) ? ticks : 
	next_tick_to_awake;
}

// pintos project - alarm clock
// 현재 next_tick_to_awake 값을 리턴
int64_t
get_next_tick_to_awake(void) {
	return next_tick_to_awake;
}

// pintos project - priority
// running 스레드와, ready_list의 가장 앞의 스레드의 priority 비교
void
test_max_priority(void) {
	struct thread *cp = thread_current(); // 현재 실행중인 스레드
	struct thread *first_thread; 

	if(list_empty(&ready_list)) return;

	// ready_list의 첫번째 스레드(즉, list 내 가장 높은 우선순위를 가진 스레드)
	// ready_list의 삽입 방식을 priority 값에 따라 정렬하여 넣는 것으로 수정했으므로
	first_thread = list_entry(list_front(&ready_list), struct thread, elem);

	// 현재 실행중인 스레드의 우선순위가, ready_list의 첫번째 스레드의 우선순위 보다 낮은 우선순위를 가지면
	// thread_yield()를 통해 cpu 양보
	if(cp->priority < first_thread -> priority)
		thread_yield();
}

// pintos project - priority
// 스레드 2개를 인자로 받아, 각각의 우선순위를 비교하는 함수
// list_insert_ordered()에 사용됨
// a의 우선순위가 높으면 1(true), b의 우선순위가 높으면 0(false)을 리턴
// UNUSED 는, unused error 피하기 위한 설정
bool
cmp_thread_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct thread *thread_a = list_entry(a, struct thread, elem);
	struct thread *thread_b = list_entry(b, struct thread, elem);

	if(thread_a != NULL && thread_b != NULL) {
		if(thread_a->priority > thread_b->priority) return true;
		else return false;
	}
	return false;
}

bool
thread_compare_donate_priority(const struct list_elem *l, const struct list_elem *s, void *aux UNUSED) {
	struct thread *thread_l = list_entry(l, struct thread, donation_elem);
	struct thread *thread_s = list_entry(s, struct thread, donation_elem);

	if(thread_l != NULL && thread_s != NULL) {
		if(thread_l->priority > thread_s->priority) return true;
		else return false;
	}
	return false;
}

// pintos project - priority donation
// priority donation 수행하는 함수
// 현재 스레드가 기다리고 있는 lock과 연결된 모든 스레드를 순회하며
// 현재 스레드의 우선순위를 lock을 보유하고 있는 스레드에게 donate
void donate_priority(void) {
	int depth;
// depth는 nested의 최대 깊이를 지정해주기 위해 사용(max_depth = 8)

	struct thread *cur = thread_current();

	for(depth=0; depth < 8; depth++) {
	// 스레드의 wait_on_lock이 NULL이 아니라면, 스레드가 lock에 걸려있다는 의미
	// lock을 점유하고 있는 holder 스레드에게 priority를 넘겨주는 방식을 깊이 8의 스레드까지 반복
	// wait_on_lock == NULL이라면, 더이상 donation을 진행할 필요가 없으므로 break
		if(!cur->wait_on_lock) break;
			struct thread *holder = cur->wait_on_lock->holder;
			// 현재 스레드가 기다리는 lock의 holder를 holder로 설정
			
			holder->priority = cur->priority;
			// 현재 스레드의 priority를 holder에게 donate
			cur = holder;
			// holder 스레드가 current 스레드가 됨
	}
}

// pintos project - priority donation
// cur->donations 리스트를 순회하면서 리스트에 있는 스레드가 priority를 빌려준 이유
// 즉, wait_on_lock이 이번에 release하는 lock이라면, 해당 스레드를 리스트에서 삭제
void 
remove_with_lock(struct lock *lock) {
	struct list_elem *e;
	struct thread *cur = thread_current();

	// donations 리스트 전체를 순회
	for (e=list_begin(&cur->donations); e!=list_end(&cur->donations); e=list_next(e)) {
		struct thread *t = list_entry(e, struct thread, donation_elem);
		// 해당 스레드가 기다리고 있던 lock이 현재 lock이라면(더이상 donate로 받은 priority가 필요없으므로)
		// donations 리스트에서 해당 스레드 삭제해주기
		if (t->wait_on_lock == lock)
			list_remove(&t->donation_elem);
	}
}

// pintos project - priority donation
// 스레드의 priority가 변경되었을 경우, donation을 고려하여 priority 재설정
// donations 리스트가 빈 경우, init_priority로 설정
// donations 리스트에 스레드가 있는 경우, 남아있는 스레드 중 가장 높은 priority 가져오기
// priority가 가장 높은 스레드를 고르기 위해, list_sort()로 내림차순 정렬
// 그리고, 맨 앞의 스레드(priority가 가장 큰 스레드)를 뽑아서 init_priority와 비교하여 더 큰 priority 적용

void 
refresh_priority(void) {
	struct thread *cur = thread_current();

	cur->priority = cur->init_priority; //현재 스레드의 init_priority를 priority 값으로 설정

	// donations 리스트 내 스레드가 있는 경우, 우선순위 재정렬
	if(!list_empty(&cur->donations)) {
		list_sort(&cur->donations, thread_compare_donate_priority, 0);

		// donations 리스트 내 우선순위가 가장 높은 스레드(front)의 priority와 현재 스레드의 priority 비교
		struct thread *front = list_entry(list_front(&cur->donations), struct thread, donation_elem);
		if (front->priority > cur->priority)
			cur->priority = front->priority;
	}
}

