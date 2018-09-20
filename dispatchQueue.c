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

/*
Creates a task. work is the function to be called when the task is executed, param is a pointer to
either a structure which holds all of the parameters for the work function to execute with or a single
parameter which the work function uses. If it is a single parameter it must either be a pointer or
something which can be cast to or from a pointer. The name is a string of up to 63 characters. This
is useful for debugging purposes.
Returns: A pointer to the created task.
*/
task_t *task_create(void (*work)(void *), void *params, char* name) {

    // Allocate memory 
    task_t *task = (task_t *)malloc(sizeof (task_t));

    // Set variables
    task->work = work;
    task->params = params;
    strcpy(task->name, name);

    return task;
}

void task_destroy(task_t *task) {
    free(task);
}


/*
Creates a dispatch queue, probably setting up any associated threads and a linked list to be used by
the added tasks. The queueType is either CONCURRENT or SERIAL.
Returns: A pointer to the created dispatch queue.
*/
dispatch_queue_t *dispatch_queue_create(queue_type_t queueType) {
 
    // Create and set memory
    dispatch_queue_t *dispatch_queue = (malloc(sizeof(dispatch_queue_t)));
  
    // Determine number of threads to create in thread pool
    if (queueType == SERIAL) {
        dispatch_queue->pool_size = 1;
    } else {
        dispatch_queue->pool_size = sysconf(_SC_NPROCESSORS_ONLN);
    }
  
    // Set initial variables
    dispatch_queue->head = NULL;
    dispatch_queue->tail = NULL;
    dispatch_queue->run = 1;
    dispatch_queue->wait = 0;
  
    // Intialise mutex & cond
    pthread_mutex_init(&dispatch_queue->queue_mutex, NULL);
    pthread_cond_init(&dispatch_queue->work_cond, NULL);

    // Intialise threads and thread pool
    dispatch_queue->threads = malloc(dispatch_queue->pool_size * sizeof(dispatch_queue_thread_t));
    for (int i = 0; i < dispatch_queue->pool_size; i++) {
        dispatch_queue->threads[i].queue = dispatch_queue;
        pthread_create(&dispatch_queue->threads[i].thread, NULL, (void *)thread_work, (void *) dispatch_queue);
    }   
    
    if (!dispatch_queue->threads) {
        error_exit("Unable to initialise thread pool");
    }
    
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
    for (int i = 0; i < queue->pool_size; i++) {
        sem_destroy(&queue->threads[i].thread_semaphore);
    }
    free(queue->threads);
    
    // Destroy all items and their tasks
    queue_item_t *item = queue->head;
    while (item != NULL) {
        queue_item_t *temp_item = item;
        item = item->next_item;
        free(temp_item->task);
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

    // Attempt to obtain lock mutex for this queue
    pthread_mutex_lock(&queue->queue_mutex);

    // Add task to queue
    queue_item_t *item = push(queue, task);

    // Unlock mutex for this queue
    pthread_mutex_unlock(&queue->queue_mutex);

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
   
    // Create and dispatch num times 
    for (long count = 0; count < number; count++) {
        task_t *task = task_create((void*) work, (void *) count, "");
        dispatch_async(queue, task); 
    }

    // Wait on all threads to finish
    dispatch_queue_wait(queue);

    // Free up memory
    dispatch_queue_destroy(queue);
}

/*
Waits (blocks) until all tasks on the queue have completed. If new tasks are added to the queue
after this is called they are ignored.
*/
int dispatch_queue_wait(dispatch_queue_t *queue) {

    // Obtain lock on the queue
    pthread_mutex_lock(&queue->queue_mutex);

    // Set wait flag = true
    queue->wait = 1;

    // Wake up all threads waiting on work_cond
    pthread_cond_broadcast(&queue->work_cond);

    // Unlock queue
    pthread_mutex_unlock(&queue->queue_mutex);

    // Ensure completion of all threads before termination
    for (int i = 0; i < queue->pool_size; i++) {
        pthread_join(queue->threads[i].thread, NULL);
    }
    
    return 0;
}


/*
Helper method for queueing
*/
queue_item_t *push(dispatch_queue_t *queue, task_t *task) {

    // Allocate memory
    queue_item_t *item = (struct queue_item_t*)malloc(sizeof(struct queue_item_t));

    // Initialise semaphore
    sem_init(&item->finished, 0,0);

    // Set pointers to items
    item->next_item = NULL;
    if (queue->tail != NULL) {
        queue->tail->next_item = item;
    } 
    queue->tail = item;

    // If this is the only item in the queue, it should be head too
    if (queue->head == NULL) {
        queue->head = item;
    }

    // Set task
    item->task = task;
    
    // Signal work has been added to the queue
    pthread_cond_signal(&queue->work_cond);

    return item;
}

/*
Helper method for dequeueing
*/
queue_item_t *pop(dispatch_queue_t *queue) {

    // If the head is null, we just return since queues empty
    while (queue->head == NULL) {
        return NULL; 
    }

    // Save the pointer to head before adjusting pointers
    queue_item_t *current_item = queue->head;

    // Re-arrange pointers at the head of the list
    queue->head = current_item->next_item;

    return current_item;
} 

void thread_work(void *param) {

    // Cast parameter (the dispatch queue)
    dispatch_queue_t *queue = (dispatch_queue_t *)param;

    while (queue->run) {
    
        // Waits until there is a task to execute from queue
        pthread_mutex_lock(&queue->queue_mutex);
        while (queue->head == NULL && !queue->wait && queue->run) {
            pthread_cond_wait(&queue->work_cond, &queue->queue_mutex);
        }
     
        // If wait flag has been set and queue is empty, then return
        if (queue->wait && queue->head == NULL) {
            pthread_mutex_unlock(&queue->queue_mutex);
            return;
        }
       
        // Get next item from queue
        queue_item_t *item = pop(queue);

        // Release lock
        pthread_mutex_unlock(&queue->queue_mutex);

        // If the item is null (i.e. queue was empty) then skip
        if (item != NULL) {

            // Get task and execute it
            void (*task) (void*) = item->task->work;
            task(item->task->params);

            // Clean up memory if ASYNC, post if SYNC 
            if (item->task->type == ASYNC) {
                queue_item_destroy(item);
            } else {
                sem_post(&item->finished);
            }
        }
    }
}

/*
Helper method for destroying queue items
*/
void queue_item_destroy(queue_item_t *item) {

    // Destroy the semaphore (free memory)
    sem_destroy(&item->finished);

    // Free memory by destroying task
    free(item->task);

    // Free memory of item
    free(item);
}



/*
ISSUES
- wait function: it says to ignore subsequent tasks added to the queue, however based on your termination condition in the thread
  loop, and the fact you didnt have a condition in push to stop further tasks being added when wait flag is true, then further tasks
  could be added which would prevent the threads from finishing?


*/