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

    // Acquire lock
    pthread_mutex_lock(&queue->queue_mutex);

    // Destroy this queues thread pool
    thread_pool_destroy(queue->thread_pool);

    // Destroy all items and their tasks
    queue_item_t *item = queue->head;
    while (item != NULL) {
        queue_item_t *temp_item = item;
        item = item->next_item;
        task_destroy(temp_item->task);
        free(item);
    }
    // Release lock
    pthread_mutex_unlock(&queue->queue_mutex);

    // Free memory
    free(queue);
}

/*
Sends the task to the queue (which could be either CONCURRENT or SERIAL). This function
returns immediately, the task will be dispatched sometime in the future.
*/
int dispatch_async(dispatch_queue_t *queue, task_t *task) {

    // Set task type
    task->type = ASYNC;

    // Add task to queue
    push(queue, task);
}

/*
Sends the task to the queue (which could be either CONCURRENT or SERIAL). This function does
not return to the calling thread until the task has been completed.
*/
int dispatch_sync(dispatch_queue_t *queue, task_t *task) {

    // Set task type
    task->type = SYNC;

    // Add task to queue
    queue_item_t *item = push(queue, task);

    // Wait upon completion of task
    sem_wait(&item->finished);

    // Free memory
    queue_item_destroy(item);
}

/*
Executes the work function number of times (in parallel if the queue is CONCURRENT). Each
iteration of the work function is passed an integer from 0 to number-1. The dispatch_for
function does not return until all iterations of the work function have completed.
*/
void dispatch_for(dispatch_queue_t *queue, long number, void (*work)(long)) {
    queue_item_t *tail;
   
    for (long count = 0; count < number; count++) {
        task_t *task = task_create((void*) work, (void *) count, "");
        dispatch_async(queue, task); 
    }
    dispatch_queue_wait(queue);
    dispatch_queue_destroy(queue);
}

/*
Waits (blocks) until all tasks on the queue have completed. If new tasks are added to the queue
after this is called they are ignored.
*/
int dispatch_queue_wait(dispatch_queue_t *queue) {
    queue_item_t *item;

    // Obtain lock on the queue
    pthread_mutex_lock(&queue->queue_mutex);
   
    // Set the our item to point to the tail of the queue
    item = queue->tail;
   
    // Unlock the queue
    pthread_mutex_unlock(&queue->queue_mutex);
    
    // Wait on the last task finishing execution
    sem_wait(&item->finished);
    
    // If a sync task, then clean memory
    if (item->task->type == SYNC) {
        queue_item_destroy(item);
    }
}

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

    // Initialise Threads
    thread_pool->threads = (dispatch_queue_thread_t*)malloc(num_threads * sizeof(dispatch_queue_thread_t));
    int t_count;   
    for (t_count = 0; t_count < num_threads; t_count++) {
        thread_init(thread_pool, &thread_pool->threads[t_count]);
            //TODO handle error
    }
   
    // Intialise thread pool mutex
    pthread_mutex_init(&thread_pool->tp_mutex, NULL);

    return thread_pool;
}

/*
Helper method for creating threads. Called by thread_pool_init() for the number of 
threads which have been requested to be created.
*/
int thread_init(thread_pool_t *thread_pool, dispatch_queue_thread_t *thread) {

    // Set pointer to its dispatch queue 
    thread->queue = thread_pool->dispatch_queue;
    
    // Create pthread   
    pthread_create(&thread->thread, NULL, (void *)thread_work, thread);
    
    return 0; 
}

/*
Helper method for queueing
*/
queue_item_t *push(dispatch_queue_t *queue, task_t *task) {
    
    queue_item_t *item;

    // Allocate memory
    item = (struct queue_item_t*)malloc(sizeof(struct queue_item_t));

    sem_init(&item->finished, 0,0);

    // Attempt to obtain lock mutex for this queue
    pthread_mutex_lock(&queue->queue_mutex);

    // Set pointers to items
    item->previous_item = queue->tail;
    if (queue->tail != NULL) {
        queue->tail->next_item = item;
    } 
    queue->tail = item;

    if (queue->head == NULL) {
        queue->head = item;
    }

    // Set task
    item->task = task;
    queue->size++;
    
    pthread_cond_signal(&queue->work_cond);

    // Unlock mutex for this queue
    pthread_mutex_unlock(&queue->queue_mutex);

    // Successful
    return item;
}

/*
Helper method for dequeueing
*/
queue_item_t *pop(dispatch_queue_t *queue) {

    if (queue->head == NULL) {
        return NULL;
    }

    // Set up return of next item pointer
    queue_item_t *current_item;

    // Attempt to obtain lock mutex for this queue
    pthread_mutex_lock(&queue->queue_mutex);

    // while(queue->size < 1) {
    //     pthread_cond_wait(&queue->work_cond, &queue->queue_mutex);
    // }

    current_item = queue->head;

    // Re arrange pointers at the head of the list
    queue->head = current_item->next_item;
    if (queue->head != NULL) {
        queue->head->previous_item = NULL; 
    }
    queue->size--;

    // Unlock mutex for this queue
    pthread_mutex_unlock(&queue->queue_mutex);

    return current_item;
} 

void thread_work(dispatch_queue_thread_t *thread) {

    // Get pointer to queue and thread pool
    dispatch_queue_t *queue = thread->queue;

    // Run indefinately whilst no destroyed
    while (threads_run) {

        // Ensures there is a task to execute from the queue
        pthread_mutex_lock(&queue->queue_mutex);
        while (queue->head == NULL) {
            pthread_cond_wait(&queue->work_cond, &queue->queue_mutex);
        }
        pthread_mutex_unlock(&queue->queue_mutex);

        // Get next task from queue and execute it
        queue_item_t *item = pop(queue);
        void (*task) (void*) = item->task->work;
        task(item->task->params);

        // Post to set state queue item to finished
        sem_post(&item->finished);

        // Clean up memory if async
        if (item->task->type == ASYNC) {
            queue_item_destroy(item);
        } 
    }
}

/*
Helper method for destroying thread pools
*/
void thread_pool_destroy(thread_pool_t *thread_pool) {

    // Acquire lock
    pthread_mutex_lock(&thread_pool->tp_mutex);

    // Destroy all threads in this thread pool
    for (int i = 0; i < thread_pool->size; i++) {
        thread_destroy(&thread_pool->threads[i]);
    }

    // Free memory of all threads in thread pool
    free(thread_pool->threads);
   
    // Release lock
    pthread_mutex_unlock(&thread_pool->tp_mutex);

    // Free memory
    free(thread_pool);
}

/*
Helper method for destroying thread semaphores
*/
void thread_destroy(dispatch_queue_thread_t *thread) {

    // Destroy the semaphore (free memory)
    sem_destroy(&thread->thread_semaphore);
}

/*
Helper method for destroying queue items
*/
void queue_item_destroy(queue_item_t *item) {

    // Destroy the semaphore (free memory)
    sem_destroy(&item->finished);

    // Free memory by destroying task
    task_destroy(item->task);

    // Free memory of item
    free(item);
}