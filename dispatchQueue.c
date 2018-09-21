#include "dispatchQueue.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
Author: Luke Thompson
UPI: ltho948
*/

task_t *task_create(void (*work)(void *), void *params, char *name) {

    // Allocate memory
    task_t *task = (task_t *)malloc(sizeof(task_t));
    if (!task) {
        error_exit("Unable to allocate memory to task\n");
    }

    // Set variables/pointers
    task->work = work;
    task->params = params;
    strcpy(task->name, name);

    return task;
}

void task_destroy(task_t *task) { free(task); }

dispatch_queue_t *dispatch_queue_create(queue_type_t queueType) {

    // Create and set memory
    dispatch_queue_t *dispatch_queue = (malloc(sizeof(dispatch_queue_t)));

    if (!dispatch_queue) {
        error_exit("Unable to allocate memory to dispatch queue\n");
    }

    // Determine number of threads to create in thread pool
    if (queueType == SERIAL) {
        dispatch_queue->pool_size = 1;
    } else if (queueType == CONCURRENT) {
        dispatch_queue->pool_size = sysconf(_SC_NPROCESSORS_ONLN);
    } else {
        error_exit("Unknown queue type\n");
    }

    // Set initial variables
    dispatch_queue->head = NULL;
    dispatch_queue->tail = NULL;
    dispatch_queue->run = 1;
    dispatch_queue->wait = 0;

    // Intialise mutex & cond
    if (pthread_mutex_init(&dispatch_queue->queue_mutex, NULL)) {
        error_exit("Unable to intialise dispatch queue mutex\n");
    }
    if (pthread_cond_init(&dispatch_queue->work_cond, NULL)) {
        error_exit("Unable to initialise work condition variable\n");
    }

    // Allocate memory to thread pool
    dispatch_queue->threads =
        malloc(dispatch_queue->pool_size * sizeof(dispatch_queue_thread_t));
    if (!dispatch_queue->threads) {
        error_exit("Unable to allocate memory to thread pool\n");
    }

    // Intialise threads
    for (int i = 0; i < dispatch_queue->pool_size; i++) {
        dispatch_queue->threads[i].queue = dispatch_queue;
        pthread_create(&dispatch_queue->threads[i].thread, NULL,
                       (void *)thread_work, (void *)dispatch_queue);
    }

    if (!dispatch_queue->threads) {
        error_exit("Unable to initialise thread pool\n");
    }

    return dispatch_queue;
}

void dispatch_queue_destroy(dispatch_queue_t *queue) {

    // Acquire lock
    pthread_mutex_lock(&queue->queue_mutex);

    // Set quit and broadcast to wake up slept threads
    queue->run = 0;
    pthread_cond_broadcast(&queue->work_cond);

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
        queue_item_destroy(temp_item);
    }

    // Release lock
    pthread_mutex_unlock(&queue->queue_mutex);

    // Destroy mutex and condition variable
    pthread_mutex_destroy(&queue->queue_mutex);
    pthread_cond_destroy(&queue->work_cond);

    // Free memory
    free(queue);
}

int dispatch_async(dispatch_queue_t *queue, task_t *task) {

    // Only dispatch if the queue wait flag has not been set or queue set for
    // destruction
    if (!queue->wait && queue->run) {

        // Set task type
        task->type = ASYNC;

        // Obtain lock
        pthread_mutex_lock(&queue->queue_mutex);

        // Add task to queue
        push(queue, task);

        // Release lock
        pthread_mutex_unlock(&queue->queue_mutex);
    } else {

        // If wait flag set, we ignore task so clean up its held memory
        task_destroy(task);
    }

    return 0;
}

int dispatch_sync(dispatch_queue_t *queue, task_t *task) {

    // Only queue a task if queue not waiting or been set for destruction
    if (!queue->wait && queue->run) {

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
    } else {

        // If wait flag set, we ignore task so clean up its held memory
        task_destroy(task);
    }

    return 0;
}

void dispatch_for(dispatch_queue_t *queue, long number, void (*work)(long)) {

    // Create and dispatch num times
    for (long count = 0; count < number; count++) {
        task_t *task = task_create((void *)work, (void *)count, "");
        dispatch_async(queue, task);
    }

    // Wait on all threads to finish
    dispatch_queue_wait(queue);

    // Free up memory
    dispatch_queue_destroy(queue);
}

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

queue_item_t *push(dispatch_queue_t *queue, task_t *task) {

    // Allocate memory
    queue_item_t *item =
        (struct queue_item_t *)malloc(sizeof(struct queue_item_t));
    if (!item) {
        error_exit("Unable to allocate memory for queued task\n");
    }

    // Initialise semaphore
    if (sem_init(&item->finished, 0, 0)) {
        error_exit("Unable to initialise task completed semaphore\n");
    }

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

queue_item_t *pop(dispatch_queue_t *queue) {

    // If the head is null, we just return since queues empty
    while (queue->head == NULL) {
        return NULL;
    }

    // Save the pointer to head before adjusting pointers
    queue_item_t *current_item = queue->head;

    // Re-arrange pointers at the head of the list
    queue->head = current_item->next_item;

    // Clean up tail when queue is empty
    if (queue->head == NULL) {
        queue->tail = NULL;
    }

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
        if ((queue->wait && queue->head == NULL) || !queue->run) {
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
            void (*task)(void *) = item->task->work;
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

void queue_item_destroy(queue_item_t *item) {

    // Destroy the semaphore (free memory)
    sem_destroy(&item->finished);

    // Free memory by destroying task
    task_destroy(item->task);

    // Free memory of item
    free(item);
}