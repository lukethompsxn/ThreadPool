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
    
    typedef struct dispatch_queue_t dispatch_queue_t; // the dispatch queue type
    typedef struct dispatch_queue_thread_t dispatch_queue_thread_t; // the dispatch queue thread type
    typedef struct queue_item_t queue_item_t;
    typedef struct thread_pool_t thread_pool_t;

    struct dispatch_queue_thread_t {
        dispatch_queue_t *queue;// the queue this thread is associated with
        pthread_t thread;       // the thread which runs the task
        sem_t thread_semaphore; // the semaphore the thread waits on until a task is allocated
        task_t *task;           // the current task for this tread
    };

    struct dispatch_queue_t {
        queue_type_t queue_type;            // the type of queue - serial or concurrent
        queue_item_t *head;                 // the next item to be executed in the queue  
        queue_item_t *tail;                 // the last item in the queue
        volatile int size;                           // the number of items currently in the queue
        thread_pool_t *thread_pool;         // the pool of threads for this dispatch queue    
        pthread_mutex_t queue_mutex;        // mutex for queue      
    };

    struct queue_item_t {
        queue_item_t *previous_item;    // pointer to previous item in LinkedList
        queue_item_t *next_item;        // pointer to next item in LinkedList
        task_t *task;                   // current item task
    };

    struct thread_pool_t {
        dispatch_queue_thread_t** threads;
        volatile int num_threads_alive;
        volatile int num_threads_working;
        dispatch_queue_t *dispatch_queue;
        pthread_mutex_t tp_mutex;
    };
    
    task_t *task_create(void (*)(void *), void *, char*);
    
    void task_destroy(task_t *);

    dispatch_queue_t *dispatch_queue_create(queue_type_t);
    
    void dispatch_queue_destroy(dispatch_queue_t *);
    
    int dispatch_async(dispatch_queue_t *, task_t *);
    
    int dispatch_sync(dispatch_queue_t *, task_t *);
    
    void dispatch_for(dispatch_queue_t *, long, void (*)(long));
    
    int dispatch_queue_wait(dispatch_queue_t *);

    thread_pool_t *thread_pool_init(int num_threads, dispatch_queue_t *dispatch_queue);

    int thread_init(thread_pool_t *thread_pool, struct dispatch_queue_thread_t** thread, int i);

    int queue(dispatch_queue_t *queue, task_t *task);

    queue_item_t *dequeue(dispatch_queue_t *queue);

    void thread_work();

#endif	/* DISPATCHQUEUE_H */