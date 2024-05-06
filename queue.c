#include <stdlib.h> /* provides functions for memory management, process control, conversions, and others. */
#include <string.h>  /* provides functions for manipulating arrays of characters (strings). */
#include <stdbool.h> /* provides a Boolean data type. */
#include <stddef.h> /* provides several types and macros. */
#include <stdint.h> /* provides a set of typedefs that specify exact-width integer types. */

#include <stdatomic.h> /* provides support for atomic operations. */
#include <threads.h> /* provides functions for creating and managing threads. */
#include "queue.h" /* library function that we will implement as part of the assignment.  */

/*
 * This file contains the implementation of a queue data structure that is used to store data in a first-in, first-out (FIFO) order.
*/
/* Waiting list node */
typedef struct WaitingNode {
    thrd_t thread; /* The thread itself. */
    struct WaitingNode* next; /* Pointer to the next node.*/
    cnd_t* cond; /* Condition variable used to wake up the thread. */
} WaitingNode;

/* Waiting threads struct */
typedef struct WaitingThreads {
    WaitingNode* head; /* Pointer to the head of the waiting list. */
    WaitingNode* tail; /* Pointer to the tail of the waiting list. */
} WaitingThreads;

/* Node structure for the queue. */
typedef struct Node {
    void* data; /* Pointer to the data. */
    struct Node* next; /* Pointer to the next node. */
} Node;

/* Queue structure. */
typedef struct Queue {
    Node* head; /* Pointer to the head of the queue. */
    Node* tail; /* Pointer to the tail of the queue. */

    atomic_size_t currently_in_queue; /* Number of elements currently in the queue. */
    atomic_size_t num_of_waiting_threads; /* Number of threads waiting for an item from the queue. */
    atomic_size_t visited_and_gone; /* Number of items that have been inserted and removed from the queue. */

    struct WaitingThreads* waitingThreads; /* Pointer to the waiting list. */
    mtx_t mutex; /* Mutex lock used to synchronize access to shared queue data. */
} Queue;

/* Global */
struct Queue* queue; /* Pointer to the queue. */
mtx_t mtx_init_and_delet; /* use to lock the init and delete functions */

/*Help functions*/

/*Function to get the thread that currently waiting in x place*/
WaitingNode* get_waiting_thread(Queue* queue, int place) {
    WaitingThreads* waiting_threads; 
    WaitingNode* thread_node;
    int i;
    waiting_threads = queue->waitingThreads;
    i = 0;
    if (waiting_threads->head == NULL  || place < 0 || place >= queue->num_of_waiting_threads)
    {
        /* If the waiting list is empty, the place is negative, or the place is greater than the number of threads waiting, return NULL */
        return NULL;
    }
    thread_node = waiting_threads->head; /* set the thread to the head of the waiting list */
    for (i = 0; i < place; i++)
    { 
        /* Loop through the waiting list */
        thread_node = thread_node->next;
    }
    return thread_node;
}

/* Function to create a new waiting list. */
WaitingThreads* create_waiting_threads(void) {
    WaitingThreads* waiting_threads = (WaitingThreads*)malloc(sizeof(WaitingThreads)); /* Allocate memory for the waiting list */
    /* Initialize the waiting list */
    waiting_threads->head = NULL;
    waiting_threads->tail = NULL;
    return waiting_threads;
}

/* Function to destroy the waiting list. */
void destroy_waiting_threads(WaitingThreads* waiting_threads) {
    WaitingNode* temp;
    WaitingNode* prev;
    /*If the waiting list is not empty, free it's nodes*/
    temp = waiting_threads->head;
    while(waiting_threads->head != NULL){
        prev = temp; 
        temp = temp->next;
        free(prev);
    }
    /* Free the waiting list */
    free(waiting_threads);
}

/* Function to add a thread to the waiting list. */
void add_waiting_thread(Queue* queue, WaitingNode* new_thread) {
    WaitingThreads* waiting_threads = queue->waitingThreads;

    /* Add the node to the waiting list */
    if (waiting_threads->head == NULL) {
        waiting_threads->head = new_thread;
        waiting_threads->tail = new_thread;
    } else {
        (waiting_threads->tail)->next = new_thread;
        waiting_threads->tail = new_thread;
    }
    queue->num_of_waiting_threads++;
}

/*Function that remove the thread in place x */
void remove_waiting_thread(Queue* queue, int place) {
    WaitingThreads* waiting_threads; 
    WaitingNode* thread_node;
    WaitingNode* prev_thread_node;
    int i;
    waiting_threads = queue->waitingThreads;
    i = 0;

    if (waiting_threads->head == NULL || place < 0 || place >= queue->num_of_waiting_threads){
        /* If the waiting list is empty, the place is negative, or the place is greater than the number of threads waiting, return NULL */
        return;
    }
    thread_node = waiting_threads->head; /* set the thread to the head of the waiting list */
    if (queue->num_of_waiting_threads == 1){
        /* If there is only one thread waiting, set the head of the waiting list to NULL */
        waiting_threads->head = NULL;
        waiting_threads->tail = NULL;
    }
    else if (place == 0){
        /* If the place is 0, set the head of the waiting list to the next node */
        waiting_threads->head = thread_node->next;  
    }
    else {
        for (i = 0; i < place; i++){ 
            /* Loop through the waiting list */
            prev_thread_node = thread_node;
            thread_node = thread_node->next;
        } if (place == queue->num_of_waiting_threads - 1){
            /* If the place is the last place, set the tail of the waiting list to the previous node */
            waiting_threads->tail = prev_thread_node;
            prev_thread_node->next = NULL; /* set the next node of the previous node to NULL */
        } else {
            prev_thread_node->next = thread_node->next; /* set the next node of the previous node to the next node of the current node */
        }
    }
    
    queue->num_of_waiting_threads--; /* decrease the number of threads waiting for an item from the queue */
    if (thread_node->cond != NULL){
        /* If the condition variable is not NULL, destroy it */
        cnd_destroy(thread_node->cond);
    } 
    free(thread_node); /* free the thread */
}

/*Function that remove and return the data in place x*/
void* remove_and_return_data(Queue* queue, int place){
    Node* temp;
    Node* prev;
    void* data = NULL;
    int i;
    if (queue->head == NULL || place < 0 || place >= queue->currently_in_queue){
        /* If the queue is empty, the place is negative, or the place is greater than the number of elements currently in the queue, return NULL */
        return NULL;
    }
    temp = queue->head; /* set the temp to the head of the queue */
    if (queue->currently_in_queue == 1){
        /* If there is only one element in the queue, set the head and the tail of the queue to NULL */
        queue->head = NULL;
        queue->tail = NULL;
    }
    else if (place == 0){
        /* If the place is 0, set the head of the queue to the next node */
        queue->head = temp->next;
    }
    else if (place == queue->currently_in_queue - 1){
        /* If the place is the last place, set the tail of the queue to the previous node */
        data = (queue->tail)->data; /* set the data to the data of the tail of the queue */
        for (i = 0; i < place; i++){
            /* Loop through the queue */
            prev = temp;
            temp = temp->next;
        }
        queue->tail = prev; /* set the tail of the queue to the previous node */
        (queue->tail)->next = NULL; /* set the next node of the tail of the queue to NULL */
    }
    else {
        /* If the place is between 0 and the number of elements currently in the queue - 1, set the next node of the previous node to the next node of the current node */
        for (i = 0; i < place; i++){
            /* Loop through the queue */
            prev = temp;
            temp = temp->next;
        }
        if (place == queue->currently_in_queue - 1){
            /* If the place is the last place, set the tail of the queue to the previous node */
            queue->tail = prev;
            prev->next = NULL; /* set the next node of the previous node to NULL */
        }
        else {
            prev->next = temp->next; /* set the next node of the previous node to the next node of the current node */
        }
    }
    data = temp->data; /* set the data to the data of the temp */
    queue->currently_in_queue--; /* decrease the number of elements currently in the queue */
    free(temp); /* free the temp */
    return data; /* return the data */
}

/*Main part*/

/* Function to create a new queue. */
void initQueue(void) {
    mtx_init(&mtx_init_and_delet, mtx_plain); /* Initialize mutex for init and delete the queue*/
    mtx_lock(&mtx_init_and_delet); /* Lock the mutex */
    queue = (Queue*)malloc(sizeof(struct Queue)); /* allocate queue*/
    /* Initialize head/tail */
    queue->head = NULL;
    queue->tail = NULL;

    mtx_init(&queue->mutex, mtx_plain); /* Initialize queue mutex */

    /* Initialize counts */
    queue->currently_in_queue = 0;
    queue->num_of_waiting_threads = 0;
    queue->visited_and_gone = 0;

    /* Create a new waiting list */
    queue-> waitingThreads = create_waiting_threads();
    mtx_unlock(&mtx_init_and_delet);
}

/* Function to destroy the queue. */
void destroyQueue(void) {
    mtx_lock(&mtx_init_and_delet); /* Lock the mutex */
    Node* temp;
    Node* prev;
    /*If the queue is not empty, free it's nodes*/
    temp = queue->head; 
    while(temp != NULL){
        prev = temp;
        temp = temp->next;
        free(prev); /* Free the node */
    }
    
    destroy_waiting_threads(queue->waitingThreads);/* Free the waiting threads */
    mtx_destroy(&queue->mutex);/* Destroy queue mutex */
    free(queue);/* Free the queue */
    mtx_unlock(&mtx_init_and_delet); /* Unlock the mutex */
    mtx_destroy(&mtx_init_and_delet); /* Destroy the mutex */
}

/* Function to add an item to the queue. */
void enqueue(void* data) {
    WaitingNode* thread_woken_up = NULL;
    cnd_t* cond = NULL;
    /* Create a new node */
    Node* node = (Node*)malloc(sizeof(Node));
    node->data = data;
    node->next = NULL;

    /* Lock the mutex */
    mtx_lock(&queue->mutex);

    /* Add the node to the queue */
    if (queue->head == NULL) {
        queue->head = node;
        queue->tail = node;
    } else {
        (queue->tail)->next = node;
        queue->tail = node;
    }
    /* Update num of elements currently in the queuent */
    queue->currently_in_queue++;
    /*
    Wake up sleeping thread, if exists.
    Have to be done in FIFO order
    */
    if (queue->num_of_waiting_threads >= queue->currently_in_queue) {
        thread_woken_up = NULL;
        thread_woken_up = (WaitingNode*)get_waiting_thread(queue, queue->currently_in_queue - 1); /* get the thread that currently waiting in the place of the number of elements currently in the queue */
        cond = thread_woken_up->cond; /* set the condition variable to the condition variable of the thread */
        cnd_signal(cond); /* signal the thread */
    }
    /* Unlock the mutex */
    mtx_unlock(&queue->mutex);
}

/* Function to remove an item from the queue. Will block if empty  */
void* dequeue(void) {
    /* Lock the mutex */
    mtx_lock(&queue->mutex);
    void* data = NULL;
    cnd_t new_cont;
    WaitingNode* thread_arrived = NULL;
    WaitingNode* temp = NULL;
    int i = 0;
    WaitingThreads* waiting_threads = queue->waitingThreads;
    thrd_t current_thread = thrd_current(); /* get the current thread */

    if(queue->currently_in_queue <= queue->num_of_waiting_threads){
    /*
        There are more threads that waiting to wake up than number of iteams is the queue, 
        so the new thread don't have a "job" and need to go to sleep until we will have a "job" for it
    */
        cnd_init(&new_cont);
        thread_arrived = (WaitingNode*)malloc(sizeof(WaitingNode));/* allocate memory for the thread that arrived */
        /* set the thread to the current thread */
        thread_arrived->thread = current_thread;
        thread_arrived->cond = &new_cont;
        thread_arrived->next = NULL;
        add_waiting_thread(queue, thread_arrived);
        cnd_wait(&new_cont, &queue->mutex);

    /* Return from waiting, the thread is woken up and now it's time to remove it from the waiting list and return the data from the queue. */
        temp = waiting_threads->head;
        for (i = 0; i < queue->num_of_waiting_threads; i++){
            /* Loop through the waiting list */
            if (thrd_equal(temp->thread, thread_arrived->thread)){
                /* If the thread is the current thread, break */
                break;
            }
            temp = temp->next;
        }
        remove_waiting_thread(queue, i); /* remove the thread from the waiting list */
        data = remove_and_return_data(queue, i); /* remove and return the data from the queue */
        cnd_destroy(&new_cont); /* destroy the condition variable */

    } else {
    /* This thread can "work" now and there is a "job" that can be done by it, we will find it and "make the job done".*/
        data = remove_and_return_data(queue, queue->num_of_waiting_threads); /* remove and return the data from the queue */
            
    }
    /* Unlock the mutex */
    queue->visited_and_gone++;
    mtx_unlock(&queue->mutex);
    return data;
}

/* 
Function that try to remove an item from the queue. If succeeded, return it via the argument and return true.
If the queue is empty, return false, and leave the pointer unchanged.  
*/
bool tryDequeue(void** data) {
    /* Lock the mutex */
    mtx_lock(&queue->mutex);
    void* return_data = NULL;

    if (queue->currently_in_queue <= queue->num_of_waiting_threads){
        /* 
        If the number of elements currently in the queue is less than or equal to the number of threads waiting for
        an item from the queue, return false. Because we don't have a job for the thread. 
        */
        mtx_unlock(&queue->mutex); /* Unlock the mutex */
        return false; /* Return false */
    }

    /* If the queue is not empty, remove an item */
    if (queue->currently_in_queue > 0) {
        return_data = remove_and_return_data(queue, queue->num_of_waiting_threads);
        queue->visited_and_gone++;
        *data = return_data; /* set the data to the data of the queue */
        mtx_unlock(&queue->mutex); /* Unlock the mutex */
        return true; 
    }
    /* Unlock the mutex */
    mtx_unlock(&queue->mutex);
    return false; 
}

/* Function to return the amount of items currently in the queue. */
size_t size(void) {
    return (size_t)(queue->currently_in_queue); /* Return the number of elements currently in the queue */
}

/* Function to return the current amount of threads  waiting for  the queue to fill. */
size_t waiting(void) {
    return (size_t)(queue->num_of_waiting_threads); /* Return the number of threads waiting for an item from the queue */
}

/* Function to return the amount of items that have passed inside the queue (i.e. inserted and then removed). */
size_t visited(void) {
    return (size_t)(queue->visited_and_gone); /* Return the number of items that have been inserted and removed from the queue */
}
