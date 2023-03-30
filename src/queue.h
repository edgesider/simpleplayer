#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <pthread.h>

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
typedef void (*DataCleaner)(void *);

void queue_init(Queue *q);

void queue_enqueue(Queue *queue, void *data);
void *queue_dequeue(Queue *queue);

void queue_clear(Queue *queue, DataCleaner data_cleaner);

void queue_enqueue_wait(Queue *queue, void *data, QueuePrediction pred);
void *queue_dequeue_wait(Queue *queue, QueuePrediction pred);
int queue_dequeue_timedwait(Queue *queue, QueuePrediction pred,
                            int64_t microseconds, void **data_ptr);
int queue_enqueue_timedwait(Queue *queue, void *data, QueuePrediction pred,
                            int64_t microseconds);

static int queue_has_data(Queue *q) {
    return q->length > 0;
}

#endif /* ifndef _QUEUE_H_ */
