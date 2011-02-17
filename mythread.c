#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <signal.h>

#include "mythread.h"
#include "futex.h"

#define STACK_SIZE (64 *1024)
#define THREADS 101

enum mythread_state {	// thread state. 
	RUNNING,
	READY,
	BLOCKED,
	EXITED				// thread state is changed to EXITED after thread's
					// function has finished running, which is caused by
					// calling mythread_exit().
};

struct func_t {
	void *(*func)(void *arg);
	void *arg;
};

struct queue_entry {		// queue entry struct.
	mythread_t id;			// id of thread.
	struct futex fut;
	// mythread_attr_t *attr;
	enum mythread_state state;		// thread's state.
	struct func_t func;				// function of thread.
	struct queue_entry *next;		// pointer to next entry in the queue.
	struct queue_entry *prev;		// pointer to previous entry in the queue.
	char *stack[STACK_SIZE];		
	int retval;
};

static struct queue_entry threads[THREADS];	// queue of threads.

static struct queue_entry *head = NULL;	// head of the queue.

static struct futex queue_futex;	// queue futex.

static void insert_at_tail(struct queue_entry *thread)	// inserts new thread to 
{									// the queue.
	static int init;
	if (init == 0) {
		init = 1;
		futex_init(&queue_futex, 1);		// initialize futex for the queue.
	}
	futex_down(&queue_futex);		// block thread

	if (head == NULL) {		// if the queue is empty, assign new thread to the
		head = thread;			// head of the queue.
		head->next = head;
		head->prev = head;
	} else {						// otherwise, insert the new thread to the tail of the queue.
		thread->prev = head->prev;	// have new thread's prev point to the current tail of the queue.
		head->prev->next = thread;	// have previous tail's next point to new thread.
		head->prev = thread;	// have the head of the tail's prev point to the new tail of the queue,
									// the new thread.
		thread->next = head;	// have the new tail's next point to the head of the queue.
	}

	futex_up(&queue_futex);		// unblock thread
}

static void move_head(void)	// assign head to point to the next thread in the queue.
{
	futex_down(&queue_futex);	// block thread while head is reassigned.

	if (head != NULL) {			// if the queue is not empty,
		head = head->next;		// assign head pointer to the next item in the queue.
	}

	futex_up(&queue_futex);		// unblock thread.
}

static void remove_from_head(void)		// remove thread from head of the queue.
{
	futex_down(&queue_futex);	// block thread.

	if (head == NULL) {			// if the queue is empty, unblock thread and
		futex_up(&queue_futex);		// return.
		return;
	}
	if (head->next == head) {		// if only one item exists in the queue, 
		head = NULL;					// assign head to NULL, emptying queue.
		futex_up(&queue_futex);		// unblock for future thread to execute.
		return;
	}
	
	head->prev->next = head->next;	// have tail's next point to new head of queue.
	head->next->prev = head->prev;	// have head's prev point to tail of the queue.
	head = head->next;					// assign head pointer to next thread in queue.

	futex_up(&queue_futex);		// unblock thread.
}

static int thread_wrapper(void *arg)	// called from clone, passing function which
								// thread will execute along with function
								// argument.
{
	struct queue_entry *thread = (struct queue_entry *)arg;	
	struct queue_entry *cur;

	futex_down(&thread->fut);	// blocks thread
	cur = head;

	thread->retval = (int)(thread->func.func)(thread->func.arg);	// run function, passing in the argument.

	mythread_exit(NULL);	// once function is finished, exit thread.
	return thread->id;	// return thread id.
}

static int idle_thread(void *arg)	// called with clone() to create idle thread
								// while the first thread in the queue is being
								// created.
{
	struct queue_entry *thread = (struct queue_entry *)arg;
	static struct timespec timeout = {
		.tv_sec = 1,
		.tv_sec = 0
	};

	while (1) {		// continue to sleep and yield for other threads while 
			// this idle thread still exists.
		futex_down_timeout(&thread->fut, &timeout);	
		usleep(10);
		mythread_yield();
	}
	return 0;
}

mythread_t mythread_self(void)	// returns the id of thread in the head of the queue.
{
	return head->id;
}

int mythread_create(mythread_t *new_thread_ID,		// create new thread, passing in thread id,
					mythread_attr_t *attr,			// attributes of thread, thread function, along
					void * (*start_func)(void *),			// function argument.
					void *arg)
{
	static unsigned thread_num;
	int ret;

	if (thread_num == 0) {	// if there are no threads in the queue, 
		futex_init(&threads[thread_num].fut, 1);	// initialize futex for thread.
		threads[thread_num].id = thread_num;	
		insert_at_tail(&threads[thread_num]);	// insert thread to the tail of the queue.

		// create idle thread using clone(), assigning thread to execute idle_thread 
		// function.
		clone(idle_thread, threads[thread_num].stack + STACK_SIZE - 1,		
			  CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_VM | CLONE_THREAD,
			  (void *)&threads[thread_num]);

		thread_num++;	// increment thread_num to 1.
	}

	futex_init(&threads[thread_num].fut, 0);		// intialize futex for new thread.
	threads[thread_num].id = *new_thread_ID = thread_num;	
	threads[thread_num].func.func = start_func;
	threads[thread_num].func.arg = arg;		
	insert_at_tail(&threads[thread_num]);	// add new thread to the end of the
									//  tail of queue.

	// create new thread, which will run the thread_wrapper function, passing in the function and 
	// argument of the function thread will be executing.
	ret = clone(thread_wrapper, threads[thread_num].stack + STACK_SIZE - 1,
				CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_VM,
				(void *)&threads[thread_num]);
	thread_num++;	// increment thread_num

	return 0;
}

int mythread_yield(void){			// yield to next thread in queue.
	struct queue_entry *thread = head;	

	move_head();	// move head pointer to next thread in queue.

	futex_up(&head->fut);		// unblock next thread.
	futex_down(&thread->fut);	// block previous head of queue.

	return 0;
}

int mythread_join(mythread_t target_thread, void **status) {
	if (threads[target_thread].id == target_thread) {	// if thread exists, 
		while (threads[target_thread].state != EXITED);	// continue while() loop until
	} else {
		return -1;
	}
											// target_thread has completed its
											// function.
	if (status) {
		*((int *)status) = threads[target_thread].retval;
	}
	return 0;
}

void mythread_exit(void *retval) {		// exit thread
	struct queue_entry *thread = head;

	remove_from_head();		// remove thread from queue
	futex_up(&thread->next->fut);		// free futex for next thread
	thread->state = EXITED;		// change state to EXITED
	if (retval) {
		*((int *)retval) = thread->retval;
	}
	exit(thread->retval);			// exit thread
}
