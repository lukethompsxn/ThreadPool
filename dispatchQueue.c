#include "dispatchQueue.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

/*
Creates a task. work is the function to be called when the task is executed, param is a pointer to
either a structure which holds all of the parameters for the work function to execute with or a single
parameter which the work function uses. If it is a single parameter it must either be a pointer or
something which can be cast to or from a pointer. The name is a string of up to 63 characters. This
is useful for debugging purposes.
Returns: A pointer to the created task.
*/
task_t *task_create(void (*work)(void *), void *params, char* name) {
    task_t *task = (task_t *)malloc(sizeof (task_t));
    task->work = work;
    task->params = params;
    strcpy(task->name, name);
    return task;
}

/*
Destroys the task. Call this function as soon as a task has completed. All memory allocated to the
task should be returned.
*/
void task_destroy(task_t *task) {
    free(task);
    task = NULL;
}

/*
Creates a dispatch queue, probably setting up any associated threads and a linked list to be used by
the added tasks. The queueType is either CONCURRENT or SERIAL.
Returns: A pointer to the created dispatch queue.
*/
dispatch_queue_t *dispatch_queue_create(queue_type_t queueType) {
    dispatch_queue_thread_t *dispatch_queue;
    dispatch_queue = (struct dispatch_queue_thread_t*)malloc(sizeof(struct dispatch_queue_thread_t));

    // Determine number of threads to create in thread pool
    long int num_threads;
    if (queueType == SERIAL) {
        num_threads = 1;
    } else {
        num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    }

    // Intialise threads and thread pool
    thread_pool_init(num_threads, dispatch_queue);
    
    //intialise linkedlist struct node 
} 

/*
Destroys the dispatch queue queue. All allocated memory and resources such as semaphores are
released and returned.
*/
void dispatch_queue_destroy(dispatch_queue_t *);

/*
Sends the task to the queue (which could be either CONCURRENT or SERIAL). This function
returns immediately, the task will be dispatched sometime in the future.
*/
int dispatch_async(dispatch_queue_t *, task_t *);

/*
Sends the task to the queue (which could be either CONCURRENT or SERIAL). This function does
not return to the calling thread until the task has been completed.
*/
int dispatch_sync(dispatch_queue_t *, task_t *);

/*
Executes the work function number of times (in parallel if the queue is CONCURRENT). Each
iteration of the work function is passed an integer from 0 to number-1. The dispatch_for
function does not return until all iterations of the work function have completed.
*/
void dispatch_for(dispatch_queue_t *, long, void (*)(long));

/*
Waits (blocks) until all tasks on the queue have completed. If new tasks are added to the queue
after this is called they are ignored.
*/
int dispatch_queue_wait(dispatch_queue_t *);

/*
Helper method for generating the thread pool based on a specified number of threads. This 
handles the creates and allocation of memory to both the threads and the thread pool then
returns a pointer to the thread pool.
*/
thread_pool *thread_pool_init(int num_threads, dispatch_queue_t *dispatch_queue) {
    // Create the new thread pool
    thread_pool *th_pool

    th_pool = (struct thread_pool*)malloc(sizeof(struct thread_pool));
    th_pool->dispatch_queue = (struct dispatch_queue_t*)malloc(sizeof(struct dispatch_queue_t*));
    th_pool->dispatch_queue = dispatch_queue;

    //set ints for working and active //TODO

    // Initialise Threads
    th_pool->threads = (struct dispatch_queue_thread_t**)malloc(num_threads * sizeof(struct dispatch_queue_thread_t *));
    int t_count;
    for (t_count = 0; t_count < num_threads; t_count++) {
        thread_init(); //add params here
    }

    // handle thread pool locking //TODO

    //may need to wait here
    return th_pool;
}

/*
Helper method for creating threads. Called by thread_pool_init() for the number of 
threads which have been requested to be created.
*/
int thread_init(thread_pool *th_pool, struct dispatch_queue_thread_t** th) {
    *th = (struct thread*)malloc(sizeof(struct dispatch_queue_thread_t));

    (*th)->thread_pool = th_pool;

    
    
}
