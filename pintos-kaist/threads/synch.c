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

/* SEMA를 VALUE로 초기화한다.
   세마포어는 음이 아닌 정수와 이를 조작하기 위한 두 개의 원자적 연산으로 구성된다.
   down 또는 "p": 값이 양수가 될 때까지 기다린 후, 값을 감소시킨다.
   up 또는 "v": 값을 증가시키고 (대기 중인 스레드가 있다면 그 중 하나를 깨운다) */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "p"연산이다.
   SEMA의 값이 양수가 될 때까지 기다린 후, 원자적으로 값을 감소시킨다.
   
   이 함수는 슬립(sleep)할 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 된다.
   인터럽트를 비활성화한 상태에서 호출할 수는 있지만, 슬립이 발생하면 다음에 스케줄된 스레드가 
   인터럽트를 다시 활성화할 가능성이 높다.
   sema_down 함수 */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered(&sema->waiters, &thread_current ()->elem, sort_list, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 down 또는 "p" 연산이지만, 세마포어의 값이 0이 아닐 떄만 수행된다.
   세마포어 값이 감소되면 true를 반환하고, 그렇지 않으면 false를 반환한다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있다. */
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

/* 세마포어에 대한 up 또는 "v" 연산이다.
   SEMA의 값을 증가시키고, SEMA를 기다리는 스레드가 있다면 그 중 하나를 깨운다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	
	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
	{
		struct thread *unblock_thread = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
		thread_unblock (unblock_thread);
		sema->value++;
		if(thread_current()->priority < unblock_thread->priority)
		{
			thread_yield();
		}	
	}
	else sema->value++;

	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 세마포어의 자체 테스트로, 두 개의 스레드 사이에서 제어가 "핑퐁"되도록 만든다.
   진행 상황을 확인하려면 중간에 printf() 호출을 삽입하라. */
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

/* sema_self_test()에서 사용되는 스레드 함수 */
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

/* LOCK을 초기화한다.
   LOCK은 어느 시점이든 오직 하나의 스레드만 보유할 수 있다.
   이 LOCK은 "재귀적(recursive)"이지 않기 때문에, 
   현재 LOCK을 보유한 스레드가 다시 그 락을 획득하려고 하면 오류가 발생한다.

   LOCK은 초기 값이 1인 세마포의 특수한 형태이다.
   락과 이러한 세마포어의 차이점은 두 가지이다.
   
   첫째, 세마포어는 값이 1보다 클 수 있지만, 락은 동시에 하나의 스레드만 소유할 수 있다.
   둘째, 세마포어는 소유자(owner) 개념이 없기 때문에 한 스레드가 down을 호출하고, 
   다른 스레드가 up을 호출하는 것이 가능하다.
   하지만 락은 동일한 스레드가 락을 획득하고 해제해야 한다.
   
   이러한 제약이 불편하게 느껴질 경우에는, 락 대신 세마포어를 사용하는 것이 더 적절하다는 신호일 수 있다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* 필요한 경우 잠들면서 LOCK을 획득한다
   이 락은 현재 스레드가 이미 보유하고 있어서는 안된다.
   
   이 함수는 잠들 수 있기 때문에, 
   인터럽트 핸들러 내부에서 호출해서는 안된다.
   인터럽트가 비활성된 상태에서 호출될 수는 있지만,
   슬립이 필요하면 인터럽트는 다시 활성화된다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* LOCK을 획득하려 시도하며, 성공하면 true를 반환하고 실패하면 false를 반환한다.
   이 락은 현재 스레드가 이미 보유하고 있어서는 안된다.
   이 함수는 슬립하지 않으므로, 인터럽트 핸들러 내에서 호출될 수 있다. */
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

/* LOCK을 해제한다. 이 락은 현재 스레드가 보유하고 있어야 한다.
   이 함수는 lock_release 함수이다.
   
   인터럽트 핸들러는 락을 획득할 수 없기 때문에,
   인터럽트 핸들러 내에서 락을 해제하려는 시도는 의미가 없다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있으면 true를, 그렇지 않으면 false를 반환한다.
   (참고 : 다른 스레드가 락을 보유하고 있는지를 검사하는 것은 경쟁 상태(race condition)
   가 발생할 수 있다.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트에 있는 하나의 세마포어 */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* 조건 변수 COND를 초기화한다.
   조건 변수는 하나의 코드가 어떤 조건을 시그널(signal)로 알리고
   협력하는 다른 코드가 그 시그널을 받아 동작할 수 있도록 해준다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 원자적으로 해제하고, 다른 코드에서 COND가 시그널될 때까지 기다린다.
   COND가 시그널되면, 반환하기 전에 LOCK을 다시 획득한다.
   이 함수를 호출하기 전에 LOCK을 보유하고 있어야 한다.

   이 함수로 구현된 모니터는 "Hoare" 스타일이 아닌 "Mesa"스타일이다.
   즉, 시그널을 보내는 것과 받는 것은 원자적(atomic)연산이 아니다.
   따라서 일반적으로 호출자는 대기가 끝난 후 조건을 다시 확인하고, 필요하다면 다시 대기해야 한다.

   하나의 조건 변수는 오직 하나의 락과만 연결되지만, 하나의 락은 여러 개의 조건 변수와 연결될 수 있다.
   즉, 락과 조건 변수 사이에는 일대다(one-to-many)관계가 존재한다.

   이 함수는 슬립할 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안된다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 슬립이 필요한 경우 인터럽트는 다시 활성화된다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* COND에서 (LOCK에 의해 보호된 상태로) 대기 중인 스레드가 하나라도 있다면,
   이 함수는 그 중 하나에게 시그널을 보내어 대기에서 깨어나도록 한다.
   이 함수는 호출하기 전에 LOCK을 보유하고 있어야 한다.
   
   인터럽트 핸들러는 락을 획득할 수 없기 때문에, 인터럽트 핸들러 내에서 
   조건 변수에 시그널을 보내는 것은 적절하지 않다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* COND에서 (LOCK에 의해 보호된 상태로) 대기 중인 모든 스레드가 있다면, 
   이 함수는 그들 모두를 깨운다.
   이 함수를 호출하기 전에 LOCK을 보유하고 있어야 한다.
   
   인터럽트 핸들러는 락을 획득할 수 없기 때문에, 인터럽트 핸들러 내에서 
   조건 변수에 시그널을 보내는 것은 적절하지 않다.ㅏㅏㅡ */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
