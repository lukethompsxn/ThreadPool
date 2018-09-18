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
    sem_init(&task->completed, 0, 1);
    return task;
}

/*
Destroys the task. Call this function as soon as a task has completed. All memory allocated to the
task should be returned.
*/
void task_destroy(task_t *task) {
    sem_destroy(&task->completed);
    free(task);
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
    dispatch_queue->size = 0;
  
    // Intialise mutex & cond
    pthread_mutex_init(&dispatch_queue->queue_mutex, NULL);
    pthread_cond_init(&dispatch_queue->work_cond, NULL);
   
    // Intialise threads and thread pool
    dispatch_queue->thread_pool = thread_pool_init(num_threads, dispatch_queue);
    
    //TODO add error checking and freeup space if failed
    
    return dispatch_queue;
} 

/*
Destroys the dispatch queue queue. All allocated memory and resources such as semaphores are
released and returned.
*/
void dispatch_queue_destroy(dispatch_queue_t *queue) {
    printf("xxz\n");
    // Acquire lock
    // pthread_mutex_lock(&queue->queue_mutex);
    printf("m\n");
    // Destroy this queues thread pool
    thread_pool_destroy(queue->thread_pool);
    printf("x\n");
    // Destroy all items and their tasks
    queue_item_t *item = queue->head;
    while (item != NULL) {
        queue_item_t *temp_item = item;
        item = item->next_item;
        task_destroy(temp_item->task);
        free(item);
    }
    printf("y\n");
    // Release lock
    // pthread_mutex_unlock(&queue->queue_mutex);

    // Free memory
    free(queue);
}

/*
Sends the task to the queue (which could be either CONCURRENT or SERIAL). This function
returns immediately, the task will be dispatched sometime in the future.
*/
int dispatch_async(dispatch_queue_t *queue, task_t *task) {
    
    // Add task to queue
    push(queue, task);
}

/*
Sends the task to the queue (which could be either CONCURRENT or SERIAL). This function does
not return to the calling thread until the task has been completed.
*/
int dispatch_sync(dispatch_queue_t *queue, task_t *task) {
   
    // Add task to queue
    push(queue, task);

    // Wait upon completion of task
    sem_wait(&task->completed);

    // Free memory
    task_destroy(task);
}

/*
Executes the work function number of times (in parallel if the queue is CONCURRENT). Each
iteration of the work function is passed an integer from 0 to number-1. The dispatch_for
function does not return until all iterations of the work function have completed.
*/
void dispatch_for(dispatch_queue_t *queue, long number, void (*work)(long)) {}

/*
Waits (blocks) until all tasks on the queue have completed. If new tasks are added to the queue
after this is called they are ignored.
*/
int dispatch_queue_wait(dispatch_queue_t *queue) {}

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
    thread_pool_t *thread_pool;
    
    // Allocate memory and set dispatch queue pointer
    thread_pool = (struct thread_pool_t*)malloc(sizeof(struct thread_pool_t));
    thread_pool->dispatch_queue = dispatch_queue;   
    
    // Set initial values for working thread and size
    thread_pool->size = num_threads;
    thread_pool->num_threads_working = 0;

    // Initialise Threads
    thread_pool->threads = (dispatch_queue_thread_t*)malloc(num_threads * sizeof(dispatch_queue_thread_t));
    int t_count;
    for (t_count = 0; t_count < num_threads; t_count++) {
        thread_init(thread_pool, &thread_pool->threads[t_count]);
            //TODO handle error
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
int thread_init(thread_pool_t *thread_pool, dispatch_queue_thread_t *thread) {

    // Set pointer to its dispatch queue //TODO this may be better to just point to the thread pool?
    thread->queue = thread_pool->dispatch_queue;
    
    // Set up thread wait semaphore
    sem_init(&thread->thread_semaphore, 0, 1);
    
    // Create pthread   
    pthread_create(&thread->thread, NULL, (void *)thread_work, thread);
    
    // Succesful
    sem_post(&thread->thread_semaphore);
    return 0; 
}

/*
Helper method for queueing
*/
int push(dispatch_queue_t *queue, task_t *task) {
    
    queue_item_t *item;

    // Allocate memory
    item = (struct queue_item_t*)malloc(sizeof(struct queue_item_t));

    // Attempt to obtain lock mutex for this queue
    pthread_mutex_lock(&queue->queue_mutex);

    // Set pointers to items
    item->previous_item = queue->tail;
    if (queue->tail != NULL) {
        queue->tail->next_item = item;
    } else {
        queue->head = item;
    }
    queue->tail = item;

    // Set task
    item->task = task;
    queue->size++;

    pthread_cond_signal(&queue->work_cond); //TODO should this be in an if statement if == 1 then do

    // Unlock mutex for this queue
    pthread_mutex_unlock(&queue->queue_mutex);

    // Successful
    return 0;
}

/*
Helper method for dequeueing
*/
queue_item_t *pop(dispatch_queue_t *queue) {

    // Set up return of next item pointer
    queue_item_t *current_item;

    // Attempt to obtain lock mutex for this queue
    pthread_mutex_lock(&queue->queue_mutex);

    current_item = queue->head;

    // Re arrange pointers at the head of the list
    queue->head = current_item->next_item;
    queue->head->previous_item = NULL; 
    queue->size--;
    
    if (queue->size < 1) {
        printf("LOL"); //TODO set condition for work_cond to be no
    }

    // Unlock mutex for this queue
    pthread_mutex_unlock(&queue->queue_mutex);

    return current_item;
} 

void thread_work(dispatch_queue_thread_t *thread) {

    sem_wait(&thread->thread_semaphore);
    // Get pointer to queue and thread pool
    dispatch_queue_t *q = thread->queue;
    thread_pool_t *tp = thread->queue->thread_pool;

    // Run indefinately whilst no destroyed
    while (threads_run) {
        // Ensures there is a task to execute from the queue
        pthread_mutex_lock(&q->queue_mutex);
        while (q->size == 0) {
            pthread_cond_wait(&q->work_cond, &q->queue_mutex);
        }
        pthread_mutex_unlock(&q->queue_mutex);
        
        // Get next task from queue and execute it
        queue_item_t *item = pop(q);
        void (*task) (void*) = item->task->work;
        task(item->task->params);

        // Increase num working threads
        pthread_mutex_lock(&tp->tp_mutex);
        tp->num_threads_working++;
        pthread_mutex_unlock(&tp->tp_mutex);

        // Clean up memory if async, post if sync
        if (item->task->type == ASYNC) {
            task_destroy(item->task);
        } else {
            sem_post(&item->task->completed);
        }

        // Clean up memory
        queue_item_destroy(item);

        // Decrease num working threads
        pthread_mutex_lock(&tp->tp_mutex);
        tp->num_threads_working--;
        pthread_mutex_unlock(&tp->tp_mutex);
    }
}

/*
Helper method for destroying thread pools
*/
void thread_pool_destroy(thread_pool_t *thread_pool) {

    // Acquire lock
    // pthread_mutex_lock(&thread_pool->tp_mutex);

    // Destroy all threads in this thread pool
    for (int i = 0; i < thread_pool->size; i++) {
        thread_destroy(&thread_pool->threads[i]);
    }
    printf("out\n");
    free(thread_pool->threads);
    printf("ou1111t\n");

    // Release lock
    // pthread_mutex_unlock(&thread_pool->tp_mutex);

    // Free memory
    free(thread_pool);
    printf("after freeing thread pool\n");
}

/*
Helper method for destroying threads
*/
void thread_destroy(dispatch_queue_thread_t *thread) {
    printf("before\n");
    sem_destroy(&thread->thread_semaphore);
    printf("after\n");
    // free(thread);
}

/*
Helper method for destroying queue items
*/
void queue_item_destroy(queue_item_t *item) {
    free(item);
}
