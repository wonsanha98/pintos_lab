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

/* struct thread의 magic 멤버에 대한 임의 값이다.
   stack overflow를 감지하는 데 사용된다. 
   자세한 내용은 thread.h 파일 상단의 큰 주석을 참고 */
#define THREAD_MAGIC 0xcd6abf4b //10진수는 3446325067, unsigned 정수 범위에 해당

/* 기본 스레드용 임의 값이다.
   이 값을 수정하지 마시오 */
#define THREAD_BASIC 0xd42df210	//10진수는 3559780880, unsigned 정수 범위에 해당

/* THREAD_READY 상태에 있는 프로세스들의 목록
   실행할 준비는 되었지만 실제로 실행 중은 아닌 프로세스들 */
static struct list ready_list;

static struct list sleep_list;		//추가++


/* Idle thread. */
static struct thread *idle_thread;

/* 초기 스레드, init.c 의 main() 함수를 실행하는 스레드 */
static struct thread *initial_thread;

/* allocate_tid() 함수에서 사용되는 락 */
static struct lock tid_lock;

/* 스레드 파괴(소멸) requests */
static struct list destruction_req;

/* 통계, 스레드 실행 시간? ticks 시간에 대한 부분 같아 보임 */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # 각 스레드에 타이머 틱을 제공한다. */
static unsigned thread_ticks;   /* # 마지막 yield 이후 경과한 타이머 틱 수 */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;				//참일 경우 MLFQ 스케줄러를 사용한다. 커널 명령줄 옵션 -o mlfqs에 의해 제어된다.

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

void thread_sleep (int64_t getuptick); //++ 추가
bool sleep(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED); //++추가
bool sort_list(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED); //++추가
/* 메크로, t가 유효한 스레드를 가리키면 true를 반환한다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC) //t의 magic 멤버가 THREAD_MAGIC 과 같다면

/* 실행 중인 스레드를 반환
 * CPU의 stack pointer 'rsp'를 읽은 다음 반올림한다. (왜 반올림?)
 * 페이지의 시작 지점까지 내려간다. `struct thread' 는 항상 페이지의 시작 위치에 있고
 * 스택 포인터는 중간 어딘가 있기에 이를 통해 현재 스레드를 찾아낼 수 있다.*/
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// thread_start를 위한 전역 디스크립터 테이블(GDT)
// GDT는 thread_init 이후에 설정되기 때문에, 임시 GDT를 설정해야 한다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화한다.
   일반적으로는 이런 방식이 동작하지 않지만 , 이 경우에는 loader.S가 스택의 바닥을
   페이지 경계에 정확히 맞춰 배치했기 때문에 가능하다.
   또한 실행 대기 큐(run queue)와 TID 락도 함께 초기화한다.
   이 함수를 호출한 후에는 thread_create()로 스레드를 생성하기 전에 
   반드시 페이지 할당자(page alloctor)를 초기화해야 한다.
   이 함수가 끝나기 전까지는 thread_current()호출 하는 것은 안전하지 않다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* kernel을 위한 임시 GDT를 다시 로드한다.
	 * 이 GDT에는 사용자 컨텍스트가 포함되어 있지 않다.
	 * 커널을 gdt_init() 함수에서 사용자 컨텍스트를 포함한 GDT를 다시 구축할 것 */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1, 		//1 값을 빼는 이유? 
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 전역 스레드 컨텍스트를 초기화한다. */
	lock_init (&tid_lock);			
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list);
	global_tick = INT64_MAX;			//추가++


	/* 현재 실행 중인 스레드를 위한 스레드 구조체를 설정한다 */
	initial_thread = running_thread ();					//초기 스레드를 실행중인 스레드로 한다?
	//initial_thread, 이름을 main으로, priority를 기본 값으로
	init_thread (initial_thread, "main", PRI_DEFAULT);	//init_thread는 구조체를 설정하는 함수
	initial_thread->status = THREAD_RUNNING;			//실행 중인 상태
	initial_thread->tid = allocate_tid ();				//스레드 식별자 tid를 할당?
}

/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작한다.
   또한 idle thread를 생성한다. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started; 		//semaphore 구조체? idle_started
	sema_init (&idle_started, 0);		//semaphore 초기화? 0 값으로
	//idle thread를 생성한다. 우선순위가 가장 낮게
	thread_create ("idle", PRI_MIN, idle, &idle_started); 

	/* 선점형 스레드 스케줄링을 시작한다. */
	intr_enable ();

	/* 아이들 스레드가 idle_thread를 초기화할 때까지 대기한다. */
	sema_down (&idle_started);
}

/* 타이머 인터럽트 핸들러에 의해 각 타이머 틱마다 호출된다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행된다.  */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)		//틱 정보를 갱신?
		idle_ticks++;			//idle 스레드의 실행시간 증가?
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;			//아니면 kernel의 ticks 증가

	/* 선점을 강제한다 */
	if (++thread_ticks >= TIME_SLICE) //TIME_SLICE는 4, thread_ticks에 ++을 한 값이 4이상 이라면
		intr_yield_on_return ();	//intr_yield_return을 true로?
}

/* 스레드 통계 정보를 출력 */
void
thread_print_stats (void) {	//각각의 ticks를 print?
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 지정된 이름 NAME, 초기 PRIORITY, 그리고 인자로 AUX를 전달받아
   FUNCTION을 실행하는 새로운 커널 스레드를 생성하고, 이를 ready queue에 추가
   스레드 생성에 성공하면 해당 스레드의 식별자(TID)를 반환하며, 실패하면 TID_ERROR를 반환
   
   thread_start()가 이미 호출된 경우, 새 스레드는 thread_create()가 반환되기 전에 
   스케줄링될 수 있으며, 심지어 그 전에 종료될 수도 있다.
   반대로, 원래 스레드가 새로운 스레드가 스케줄되기 전에 계속 실행될 수도 있다.
   이러한 실행 순서를 보장하려면 세마포어(semaphore) 또는 다른 동기화 메커니즘을 사용해야 한다.
   
   제공된 코드는 새 스레드의 priority 멤버를 PRIORITY로 설정하지만
   실제 우선순위 스케줄링은 구현되어 있지 않다.
   우선순위 스케줄링은 문제 1-3의 목표이다.*/
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;			//thread를 가리키는 포인터 t
	struct thread *curr = thread_current(); //현재 실행중인 스레드 추가++
	tid_t tid;					//thread 식별자 tid

	ASSERT (function != NULL);	//function가 존재 안하면 에러??

	/* 스레드를 할당한다 */
	t = palloc_get_page (PAL_ZERO); //page alloc?? 002?
	if (t == NULL)					//t 가 NULL일 경우
		return TID_ERROR;			//TID error를 반환한다. thread.h를 봐둬야 할듯

	/* 스레드를 초기화한다 */
	init_thread (t, name, priority);//thread 초기화 함수?
	tid = t->tid = allocate_tid ();	//thread의 tid를 할당한다

	/* kernel_thread가 스케줄링되었다면 호출한다.
	 * rdi는 첫 번째 인자이고, rsi는 두 번쨰 인자이다. */
	t->tf.rip = (uintptr_t) kernel_thread;	//kernel_thread를 uintptr_t로 캐스팅후 rip에 대입
	t->tf.R.rdi = (uint64_t) function;		//function을 캐스팅 후 rdi에 대입?
	t->tf.R.rsi = (uint64_t) aux;			
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* 실행 대기 큐(run queue)에 추가한다. */
	thread_unblock (t);
	if(curr->priority < t->priority) //현재 실행 중인 스레드와 새로 삽입된 스레드의 우선순위를 비교
	{
		thread_yield();					//새로운 스레드의 우선순위가 더 높다면 CPU 양보
	}

	/* 현재 실행 중인 스레드와 새로 삽입된 스레드의 우선순위를 비교하라.  +++
   새로 도착한 스레드의 우선순위가 더 높다면 CPU를 양보하라. */

	return tid;					//thread id를 반환
}

/* 
   현재 스레드를 스립 상태로 전환한다.
   이 스레드는 thread_unblock()에 의해 깨워질 때까지 다시 스케줄되지 않는다.
   이 함수는 인터럽트가 꺼진 상태에서만 호출되어야 하며,
   보통은 synch.h에 정의된 동기화 프리미티브들(세마포어, 락 등)을 사용하는 것이 더 바람직하다. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;	//현재 thread의 상태를 THREAD_BLOCKED로 만든다.
	schedule ();								//스케줄링을 실행한다. 현재 스레드를 블락하고 스케줄을 통해 새로운 스레드를 실행?
}

/* 차단(blocked) 상태에 있는 스레드 T를 실행 준비 상태(ready-to-run)로 전환한다.
   T가 차단 상태가 아닌 경우, 이는 오류이다.
   (현재 실행 중인 스레드를 준비 상태로 만들려면 thread_yield()를 사용하시오.)
   이 함수는 현재 실행 중인 스레드를 선점(preempt)하지 않는다.
   이 점은 중요할 수 있는데, 호출자가 인터럽트를 스스로 비활성화한 경우, 
   스레드를 원자적으로(uninterruptibly)언블록하고 다른 데이터를 수정하는 것을 기대할 수 있기 때문이다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;			//인터럽트가 켜져있는지 꺼져있는지 나타냄?

	ASSERT (is_thread (t));				

	old_level = intr_disable ();		//인터럽트를 비활성화하고, 이전 인터럽트 상태를 반환
	ASSERT (t->status == THREAD_BLOCKED);//t가 THREAD_BLOCKED 상태라면? ASSERT는 디버그용?
	list_insert_ordered(&ready_list, &t->elem, sort_list, NULL);

	t->status = THREAD_READY;			//t를 THREAD_READY 상태로 변경한다.
	intr_set_level (old_level);			//이전 인터럽드 상태로 set한다?
}

/* 현재 실행 중인 스레드의 이름을 반환한다 */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 현재 실행 중인 스레드를 반환한다.
   이는 running_thread() 함수에 몇 가지 무결성 검사(sanity check)를 추가한 것이다.
   자세한 내용은 thread.h 파일 상단의 주석을 참고 */
struct thread * 	//thread 포인터를 반환
thread_current (void) {
	struct thread *t = running_thread ();	

	/* T가 실제로 스레드인지 확인한다.
	이러한 단언문(assertion) 중 하나라도 실패했다면,
	해당 스레드가 스택 오버플로우를 일으켰을 가능성이 있다.
	각 스레드는 4KB 미만의 스택만을 가지므로, 
	몇 개의 큰 자동 배열이나 약간의 재귀 호출만으로도 스택 오버플로우가 발생할 수 있다. */
	ASSERT (is_thread (t));					//t가 thread인지 
	ASSERT (t->status == THREAD_RUNNING);	//t가 실행 중인지

	return t;
}

/* 현재 실행 중인 스레드의 TID(thread ID)를 반환한다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;	
}

/* 현재 스레드를 스케줄링에서 제외시키고 파괴(destroy)한다.
   이 함수는 호출한 곳으로 절대 반환되지 않는다. */
void
thread_exit (void) {
	ASSERT (!intr_context ()); //외부 인터럽트를 처리중인지 여부를 반환 받는다.

#ifdef USERPROG
	process_exit ();
#endif

	/* 상태를 dying으로 설정하고, 다른 프로세스를 스케줄링하기만 하면 된다.
	   현재 스레드는 schedule_tail() 호출 중에 파괴(destroy)될 것이다. */
	intr_disable ();			//인터럽트 비활성화, 이전 인터럽트 상태를 반환 받음
	do_schedule (THREAD_DYING);	//THREAD_DYING으로 스케줄을 한다?
	NOT_REACHED ();				//도달하지 않음??
}

void
thread_sleep (int64_t getupticks) 
{
	 //현재 스레드 대한 포인터
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());	//외부 인터럽트를 처리하는 중일 때는 true를 반환하고, 그 외의 모든 시간에는 false를 반환

	old_level = intr_disable (); //인터럽트 비활성화
	if (curr != idle_thread)		//현재 쓰레드가 idle_thread가 아니라면
	{
		curr->getuptick = getupticks;								//깨어날 시간을 getupick으로 저장
		list_insert_ordered(&sleep_list, &curr->elem, sleep, NULL);	//리스트에 정렬 삽입
		struct thread *target = list_entry(list_begin(&sleep_list), struct thread, elem); 
		global_tick = target->getuptick;							//가장 작은 값을 global_tick으로
		thread_block();										
	}
	intr_set_level (old_level); //인터럽트 수준을 원래 상태로 설정한다.
}

void wakeup()
{
	if(list_empty(&sleep_list))
	{
		return;
	}

	enum intr_level old_level;
	old_level = intr_disable (); //인터럽트 비활성화	
	
	struct list_elem *start = list_begin(&sleep_list);				//sleep_list의 가장 앞의 값을 start로 

	while(start != list_end(&sleep_list))							//start가 list의 끝이 아니면
	{
		struct thread *start_thread = list_entry(start, struct thread, elem);//start로 start_thread를 반환
		if(start_thread->getuptick <= global_tick)							 //global_tick보다 작은 값을 찾는다.
		{
			struct list_elem *next = start->next;							//start의 다음을 next로 저장
			list_pop_front (&sleep_list);									//최소 값을 삭제
			thread_unblock(start_thread);									//현재 start_thread를 unblock
			start = next;													//start에 다음 값을 저장
		}
		else
		{
			break;
		}
	}	
	//global_tick 갱신
	if(!list_empty(&sleep_list))
	{
		global_tick = list_entry(start, struct thread, elem)->getuptick;
	}	
	else
	{
		global_tick = INT64_MAX;
	}

	intr_set_level (old_level); //인터럽트 수준을 원래 상태로 설정한다.
}

/* CPU를 양보한다.
   현재 스레드는 슬립 상태로 전환되지 않으며, 
   스케줄러의 판단에 따라 즉시 다시 스케줄될 수도 있다. */

void
thread_yield (void) {
	struct thread *curr = thread_current (); //현재 스레드 대한 포인터
	enum intr_level old_level;

	ASSERT (!intr_context ());	//외부 인터럽트를 처리하는 중일 때는 true를 반환하고, 그 외의 모든 시간에는 false를 반환

	old_level = intr_disable (); //인터럽트 비활성화
	if (curr != idle_thread)
	{
		list_insert_ordered(&ready_list, &curr->elem, sort_list, NULL);
	}
	do_schedule (THREAD_READY); //현재 실행 중인 스레드의 상태를 준비상태로
	intr_set_level (old_level); //인터럽트 수준을 원래 상태로 설정한다.
}

/* 현재 스레드의 우선순위를 NEW_PRIORITY로 설정한다. */
void	//수정 ++
thread_set_priority (int new_priority) {
	
	struct thread *curr_thread = thread_current ();
 	curr_thread->priority = new_priority;
	if(!list_empty(&ready_list))
	{
		struct thread *forst_thread = list_entry(list_begin(&ready_list), struct thread, elem);
		if(curr_thread->priority < forst_thread->priority)
		{
			thread_yield();
		}
	}
}

/* 현재 스레드의 우선순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* 현재 스레드의 nice 값을 NICE로 설정한다 */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* 현재 스레드의 nice 값을 반환한다 */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* 시스템 부하 평균(load average)에 100을 곱한 값을 반환한다. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* recent_cpu 값에 100을 곱한 값을 반환한다. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  다른 어떤 스레드도 실행 준비가 되어 있지 않을 때 실행된다.

   idle thread는 처음 thread_start()에 의해 ready 리스트에 추가된다.
   이 스레드는 처음에 한 번 스케줄된 후, idle_thread를 초기화하고, 
   thread_strat()가 계속 진행할 수 있다도록 세마포어를 up 시킨 뒤, 즉시 block 상태로 전환된다.allocate_tid
   그 이후에는 idle thread는 ready 리스트에 다시 나타나지 않으며
   ready 리스트가 비어 있을 때만 특별한 경우로 
   next_thread_to_run()에 의해 반환된다.
 */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_; //semaphore 구조체

	idle_thread = thread_current ();		//현재 실행중인 구조체
	sema_up (idle_started);

	for (;;) {
		/* 다른 스레드가 실행하도록 양보한다. */
		intr_disable ();					//인터럽트 비활성화
		thread_block ();					//현재 스레드를 block한다.

		/* 인터럽트를 다시 활성화하고, 다음 인터럽트를 기다린다.

		   sti 명령어는 다음 명령어가 완료될 때까지 인터럽트를 비활성화하기 때문에,
		   이 두 명령어(sti, hlt)는 원자적으로(atomic) 실행된다.
		   이러한 원자성은 매우 중요하다.
		   그렇지 않으면 인터럽트가 인터럽트를 다시 활성화한 직후와 대기 명령어 사이에 처리될 수 있으며,
		   이로 인해 최대 한 틱(tick)만큼 시간을 낭비할 수 있다.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */

		//원자적 : 어떤 연산이 더 이상 쪼갤 수 없는 단일 단위로 실행되어,
		//중간에 다른 작업(인터럽트나 스레드 전환 등)이 끼어들 수 없는 상태를 말한다.
		// 원자적으로 실행되기 위해 어셈블리로? 
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* 커널 스레드의 기반이 되는 함수이다. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* 스케줄러는 인터럽트가 꺼진 상태에서 실행된다. */
	function (aux);       /* 스레드 함수를 실행한다. , aux는 보통 보조인자의 의미로 쓰인다. */
	thread_exit ();       /* function()이 반환되면, 해당 스레드를 종료시킨다. */
}


/* t를 이름이 NAME인 차단된 스레드(blocked thread)로 기본적으로 초기화한다. */
static void	//thread, namem priority를 인자로 받는다.
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);									//t가 NULL이 아닐 때
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);//priority가 해당 범위 안에 있어야함
	ASSERT (name != NULL);								//name이 NULL이 아님

	memset (t, 0, sizeof *t);//DST(destination, 목적지)의 메모리 공간 SIZE bytes를 VALUE 값으로 설정한다.
	t->status = THREAD_BLOCKED;//t의 status를 BLOCKED로 만듬
	strlcpy (t->name, name, sizeof t->name); //t->name에 name을 복사한다.
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *); //??? 뭐를 하려고 하는가..
	t->priority = priority;	// 인자로 받은 priority를 t의 priority에 대입한다.
	t->magic = THREAD_MAGIC;// t의 magic을 THREAD_MAGIC을 대입
	t->getuptick = 0;
}

/* 다음에 스케줄될 스레드를 선택하여 반환한다.
   실행 대기 큐(run queue)에서 스레드를 반환해야 하며,
   대기 큐가 비어 있는 경우를 제외하고는 반드시 그렇다.
   (현재 실행 중인 스레드가 계속 실행 가능하다면, 
   그 스레드도 실행 대기 큐에 포함되어 있을 것이다.)
   실행 대기 큐가 비어 있다면, idle_thread를 반환한다. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))	//list가 비었는지 확인
		return idle_thread;			//비었으면 idle_thread 반환
	else	//리스트 요소, 외부 구조체 이름, 리스트 요소의 멤버이름을 받아서 요소가 포함 되어있는 구조체의 포인터를 반환
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* iretq 명령어를 사용하여 스레드를 실행한다. */
//cpu 레지스터와 스택 상태를 모두 복원하고 마치 인터럽트에서 복귀하듯이 스레드의 첫 명령어를 실행하는 방식
//완전한 context switch를 수행하는 방식 중 하나
void
do_iret (struct intr_frame *tf) {	//넘겨줘야 하는 tf를 thread_launch에서 넘겨준다?
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

/* 새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고,
   이전 스레드가 dying 상태라면 해당 스레드르 파괴(destroy)한다.

   이 함수가 호출될 시점에는, 이미 PREV 스레드로부터 전환된 상태이며, 
   새 스레드는 실행 중이고, 인터럽트는 여전히 비활성화되어 있다.

   이 시점에서 printf()를 호출하는 것은 안전하지 않다.
   실제로는, printf()는 함수의 끝부분에 추가하는 것이 바람직하다. */
static void
thread_launch (struct thread *th) {		
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf; //현재 실행중인 thread의 tf의 값
	uint64_t tf = (uint64_t) &th->tf;					 //인자로 받은 th의 tf의 값
	ASSERT (intr_get_level () == INTR_OFF);				 //현재 인터럽트가 비활성 상태

	/* 스레드 전환의 핵심 로직이다.
	   먼저 전체 실행 컨텍스트를 intr_frame에 복원한 다음,
       do_iret을 호출하여 다음 스레드로 전환한다.
	   주의할 점은, 스위칭이 완료될 때까지 이 지점에서는 스택을 절대 사용해서는 안 된다. */
	__asm __volatile (
			/* 사용될 레지스터드들을 저장한다. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* 입력을 한 번만 가져온다. */
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
			"call do_iret\n"			//컨텍스트 스위칭을 실행한다?
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* 새로운 프로세스를 스케줄링한다.
 * 함수 진입 시에는 인터럽트가 꺼져 있어야 한다.
 * 이 함수는 현재 스레드의 상태를 지정된 status로 변경한 후
 * 다음에 실행할 스레드를 찾아 전환(switch)한다.
 * schedule() 함수 내부에서는 printf()를 호출하는 것은 안전하지 않다. */
static void
do_schedule(int status) { 					//상태를 받아서 schedule한다?
	ASSERT (intr_get_level () == INTR_OFF);	//인터럽트가 꺼졌는지
	ASSERT (thread_current()->status == THREAD_RUNNING); //현재 실행중인 thread가 RUNNING으로 동작하는지 
	while (!list_empty (&destruction_req)) {		//소멸 thread list가 비어있지 않으면
		struct thread *victim =						//list_entry 리스트가 포함된 구조체의 포인터 반환
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);	//페이지의 할당해제
	}
	thread_current ()->status = status;		//현재 스레드의 상태를 인자로 받은 status로 변경
	schedule ();							//schedule 함수 호출
}

static void
schedule (void) {
	struct thread *curr = running_thread ();	//현재 실행중인 thread
	struct thread *next = next_thread_to_run ();//다음의 스케줄 대기중인 thread

	ASSERT (intr_get_level () == INTR_OFF);		//OFF상태 인지
	ASSERT (curr->status != THREAD_RUNNING);	//curr의 상태가 실행중이 아닌지
	ASSERT (is_thread (next));					//next가 thread인지
	/* 다음 스레드를 실행 중 상태로 표시한다. */
	next->status = THREAD_RUNNING;

	/* 새로운 타임 슬라이스를 시작 */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {		//curr이 next가 아니라면??
		/* 우리가 전환한 이전 스레드가 dying 상태라면, 
		   해당 스레드의 struct thread를 파괴(destroy)해야 한다.
		   이 작업은 너무 일찍 수행하면 안 되며,
		   그 이유는 thread_exit()이 자기 자신이 사용 중인 구조체를 먼저 지워버릴 위험이 있기 때문

		   현재는 스택에서 여전히 사용 중이기 때문에,
		   페이지 해제 요청만 큐에 넣어두고,
		   실제 파괴 로직은 다음번 schedule() 함수의 시작 시점에 수행된다.
		*/
		//curr 과 curr->status가 어떻게 &&연산을 할 수 있는지?
		//THREAD_RUNNING = 0?
		//THREAD_READY = 1?    
		//THREAD_BLOCKED = 10?
		//THREAD_DYING = 11?
		//initial_thread 초기의 main 함수를 실행하는 스레드
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);	//curr의 elem을 소멸 thread list에 마지막에 넣는다?
		}

		/* 스레드를 전환하기 전에
		 * 먼저 현재 실행 중인 스레드의 정보를 저장한다. */
		//next thread로 전환?
		thread_launch (next);
	}
}

/* 새 스레드에 사용할 TID(thread ID)를 반환한다. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;	//next_tid에 1을 대입
	tid_t tid;					//tid를 선언

	lock_acquire (&tid_lock);	//잠들면서 lock을 획득
	tid = next_tid++;			//tid는 next_tid에서 ++
	lock_release (&tid_lock);	//LOCK을 해제한다. lock은 현재 스레드가 보유해야 함
	
	//의문점은 tid는 숫자 하나씩 증가를 해야하는 거아닌가?
	//이 상태로 진행된다면 무조건 1서 하나 증가한 값으로 할당되는 것으로 보이는데...
	return tid;					
}


bool sleep(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *a_thread = list_entry(a, struct thread, elem);
	struct thread *b_thread = list_entry(b, struct thread, elem);
	
	return 	a_thread->getuptick < b_thread->getuptick;
}

bool sort_list(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	struct thread *a_thread = list_entry(a, struct thread, elem);
	struct thread *b_thread = list_entry(b, struct thread, elem);

	return a_thread->priority > b_thread->priority;
}