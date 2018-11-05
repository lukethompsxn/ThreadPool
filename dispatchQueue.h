/* 
 * File:   dispatchQueue.h
 * Author: robert
 *
 * Modified by: ltho948 
 */

#ifndef DISPATCHQUEUE_H
#define	DISPATCHQUEUE_H

#include <pthread.h>
#include <semaphore.h>
    
#define error_exit(MESSAGE)     perror(MESSAGE), exit(EXIT_FAILURE)

    typedef enum { // whether dispatching a task synchronously or asynchronously
        ASYNC, SYNC
    } task_dispatch_type_t;
    
    typedef enum { // The type of dispatch queue.
        CONCURRENT, SERIAL
    } queue_type_t;

    typedef struct task {
        char name[64];              // to identify it when debugging
        void (*work)(void *);       // the function to perform
        void *params;               // parameters to pass to the function
        task_dispatch_type_t type;  // asynchronous or synchronous  
    } task_t;
    
    typedef struct dispatch_queue_t dispatch_queue_t;                   // the dispatch queue type
    typedef struct dispatch_queue_thread_t dispatch_queue_thread_t;     // the dispatch queue thread type
    typedef struct queue_item_t queue_item_t;                           // the queue item

    struct dispatch_queue_thread_t {
        dispatch_queue_t *queue;        // the queue this thread is associated with
        pthread_t thread;               // the thread which runs the task
        sem_t thread_semaphore;         // the semaphore the thread waits on until a task is allocated
        task_t *task;                   // the current task for this tread
    };

    struct dispatch_queue_t {
        queue_type_t queue_type;            // the type of queue - serial or concurrent
        queue_item_t *head;                 // the next item to be executed in the queue  
        queue_item_t *tail;                 // the last item in the queue
        pthread_mutex_t queue_mutex;        // mutex for queue   
        pthread_cond_t work_cond;           // condition variable for work in the queue signalling
        dispatch_queue_thread_t* threads;   // array of threads
        volatile int pool_size;             // number of threads in the pool
        volatile int wait;                  // flag for wait condition         
        volatile int run;                   // flag for running
    };

    struct queue_item_t {
        queue_item_t *next_item;        // pointer to next item in LinkedList
        task_t *task;                   // current item task   
        sem_t finished;                 // semaphore for thread execution finished
    };
    

    /*
    Creates a task. work is the function to be called when the task is executed, param is a pointer to
    either a structure which holds all of the parameters for the work function to execute with or a single
    parameter which the work function uses. If it is a single parameter it must either be a pointer or
    something which can be cast to or from a pointer. The name is a string of up to 63 characters. This
    is useful for debugging purposes.
    Returns: A pointer to the created task.
    */
    task_t *task_create(void (*)(void *), void *, char*);
    
    /*
    Destroys the task . Call this function as soon as a task has completed. All memory allocated to the
    task should be returned.
    */
    void task_destroy(task_t *);

    /*
    Creates a dispatch queue, probably setting up any associated threads and a linked list to be used by
    the added tasks. The queueType is either CONCURRENT or SERIAL.
    Returns: A pointer to the created dispatch queue.
    */
    dispatch_queue_t *dispatch_queue_create(queue_type_t);
    
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
    Helper method for queueing. Updates tail and adjusts pointers
    */
    queue_item_t *push(dispatch_queue_t *queue, task_t *task);

    /*
    Helper method for dequeueing. Returns head and updates pointers
    */
    queue_item_t *pop(dispatch_queue_t *queue);

    /*
    The work function which is run by the threads in the thread pool
    */
    void thread_work(void *thread);

    /*
    Helper method for destroying queue items
    */
    void queue_item_destroy(queue_item_t *item);

#endif	/* DISPATCHQUEUE_H */