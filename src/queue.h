#ifndef _QUEUE_H_
#define _QUEUE_H_

#include "list.h"

typedef struct {
    struct list_node queue;
    void *data;
} QueueNode;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t on_changed;
    int length;
    QueueNode nodes;
} Queue;

typedef int (*QueuePrediction)(Queue *queue);

void queue_init(Queue *q);

void queue_enqueue_nolock(Queue *queue, void *data);
void *queue_dequeue_nolock(Queue *queue);
void queue_wait_nolock(Queue *queue, QueuePrediction pred);

void queue_enqueue(Queue *queue, void *data);
void *queue_dequeue(Queue *queue);

void queue_enqueue_wait(Queue *queue, void *data, QueuePrediction pred);
void *queue_dequeue_wait(Queue *queue, QueuePrediction pred);

#endif /* ifndef _QUEUE_H_ */
