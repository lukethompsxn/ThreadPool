#include "dispatchQueue.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

/*
Author: Luke Thompson
UPI: ltho948
*/

static volatile int threads_run;
static volatile int threads_suspend;

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
    dispatch_queue_t *dispatch_queue;
    dispatch_queue = (dispatch_queue_t*)malloc(sizeof(dispatch_queue_t));

    // Determine number of threads to create in thread pool
    long int num_threads;
    if (queueType == SERIAL) {
        num_threads = 1;
    } else {
        num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    }

    // Set initial tasks and queue size
    dispatch_queue->head = NULL;
    dispatch_queue->tail = NULL;
    dispatch_queue->size = (int*)malloc(sizeof(int)); //TODO may not need this line
    dispatch_queue->size = 0;

    // Intialise mutex
    pthread_mutex_init(&dispatch_queue->queue_mutex, NULL);

    // Intialise threads and thread pool
    dispatch_queue->thread_pool = thread_pool_init(num_threads, dispatch_queue);

    //TODO add error checking and freeup space if failed
    
    return dispatch_queue;
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
thread_pool_t *thread_pool_init(int num_threads, dispatch_queue_t *dispatch_queue) {

    // Set control fields
    threads_run = 1;
    threads_suspend = 0;

    // Create the new thread pool
    struct thread_pool_t *thread_pool;

    // Allocate memory and set dispatch queue pointer
    thread_pool = (struct thread_pool_t*)malloc(sizeof(struct thread_pool_t));
    thread_pool->dispatch_queue = dispatch_queue;   

    // Set initial values for alive & working thread counts
    thread_pool->num_threads_alive = 0;
    thread_pool->num_threads_working = 0;

    // Initialise Threads
    thread_pool->threads = (dispatch_queue_thread_t**)malloc(num_threads * sizeof(dispatch_queue_thread_t *));
    int t_count;
    for (t_count = 0; t_count < num_threads; t_count++) {
        if (thread_init(thread_pool, &thread_pool->threads[t_count], t_count)) {
            //TODO handle error
        }
    }

    // Intialise thread pool mutex
    pthread_mutex_init(&thread_pool->tp_mutex, NULL);

    //may need to wait here
    return thread_pool;
}

/*
Helper method for creating threads. Called by thread_pool_init() for the number of 
threads which have been requested to be created.
*/
int thread_init(thread_pool_t *thread_pool, struct dispatch_queue_thread_t** thread, int i) {

    // Allocate memory to thread
    *thread = (dispatch_queue_thread_t*)malloc(sizeof(dispatch_queue_thread_t));

    // Set pointer to its dispatch queue //TODO this may be better to just point to the thread pool?
    (*thread)->queue = thread_pool->dispatch_queue;

    // Set up thread wait semaphore
    sem_init(&(*thread)->thread_semaphore, 0, 1);
    sem_wait(&(*thread)->thread_semaphore);

    // Create pthread   
    pthread_create(&(*thread)->thread, NULL, (void *)thread_work, i);

    // Succesful
    return 0; 
}

/*
Helper method for queueing
*/
int queue(dispatch_queue_t *queue, task_t *task) {
    
    queue_item_t *item;

    // Allocate memory
    item = (struct queue_item_t*)malloc(sizeof(struct queue_item_t));

    // Attempt to obtain lock mutex for this queue
    pthread_mutex_lock(&queue->queue_mutex);

    // Set pointers to items
    item->previous_item = queue->tail;
    queue->tail->next_item = item;
    queue->tail = item;

    // Set task
    item->task = task;
    queue->size++;

    // Unlock mutex for this queue
    pthread_mutex_unlock(&queue->queue_mutex);

    // Successful
    return 0;
}

/*
Helper method for dequeueing
*/
queue_item_t *dequeue(dispatch_queue_t *queue) {

    // Set up return of next item pointer
    queue_item_t *current_item;

    // Attempt to obtain lock mutex for this queue
    pthread_mutex_lock(&queue->queue_mutex);

    current_item = queue->head;

    // Re arrange pointers at the head of the list
    queue->head = current_item->next_item;
    queue->head->previous_item = NULL; 
    free(queue->head->previous_item);
    queue->size--;

    // Unlock mutex for this queue
    pthread_mutex_unlock(&queue->queue_mutex);

    return current_item;
} 

void thread_work(dispatch_queue_thread_t *thread) {

    // Get pointer to queue and thread pool
    dispatch_queue_t *q = thread->queue;
    thread_pool_t *tp = thread->queue->thread_pool;

    // Increase num active threads
    pthread_mutex_lock(&tp->tp_mutex);
    tp->num_threads_alive++;
    pthread_mutex_unlock(&tp->tp_mutex);

    // Run indefinately whilst no destroyed
    while (threads_run) {
        //TODO maybe check if there are any jobs?

        // Increase num working threads
        pthread_mutex_lock(&tp->tp_mutex);
        tp->num_threads_working++;
        pthread_mutex_unlock(&tp->tp_mutex);

        // Get next task from queue and execute it
        queue_item_t *item = dequeue(q);
        void (*task) (void*) = item->task->work;
        task(item->task->params);
        free(task);

        // Decrease num working threads
        pthread_mutex_lock(&tp->tp_mutex);
        tp->num_threads_working--;
        pthread_mutex_unlock(&tp->tp_mutex);
    }

    // Decrement num active threads
    pthread_mutex_lock(&tp->tp_mutex);
    tp->num_threads_alive--;
    pthread_mutex_unlock(&tp->tp_mutex);
}