#include "queue.h"

#include <pthread.h>

void queue_init(Queue *q) {
    q->length = 0;
    list_node_init(&q->nodes.queue);
    q->nodes.data = NULL;

    /* pthread_mutexattr_t attr; */
    /* pthread_mutexattr_init(&attr); */
    /* pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE); */
    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        error("queue_init: mutex initialize failed");
    }
    if (pthread_cond_init(&q->on_changed, NULL) != 0) {
        error("queue_init: cond initialize failed");
    }
}

void queue_enqueue_nolock(Queue *queue, void *data) {
    QueueNode *q_node = malloc(sizeof(QueueNode));
    q_node->data = data;
    list_add(queue->nodes.queue.prev, &q_node->queue);
    queue->length++;
    if (pthread_cond_broadcast(&queue->on_changed) != 0) {
        error("pthread_cond_broadcast");
    }
}

void *queue_dequeue_nolock(Queue *queue) {
    if (queue->length == 0) {
        return NULL;
    }
    QueueNode *q_node = list_object(queue->nodes.queue.next, QueueNode, queue);
    list_del(&q_node->queue);
    void *data = q_node->data;
    queue->length--;
    free(q_node);
    if (pthread_cond_broadcast(&queue->on_changed) != 0) {
        error("pthread_cond_broadcast");
    }
    return data;
}

void queue_enqueue(Queue *queue, void *data) {
    pthread_mutex_lock(&queue->lock);
    queue_enqueue_nolock(queue, data);
    pthread_mutex_unlock(&queue->lock);
}

void *queue_dequeue(Queue *queue) {
    void *v;
    pthread_mutex_lock(&queue->lock);
    v = queue_dequeue_nolock(queue);
    pthread_mutex_unlock(&queue->lock);
    return v;
}

void queue_wait_nolock(Queue *queue, QueuePrediction pred) {
    while (!pred(queue)) {
        int ret;
        if ((ret = pthread_cond_wait(&queue->on_changed, &queue->lock)) != 0) {
            error("pthread_cond_wait failed\n");
        }
    }
}

void queue_enqueue_wait(Queue *queue, void *data, QueuePrediction pred) {
    if (pthread_mutex_lock(&queue->lock) != 0) {
        error("pthread_mutex_lock");
    }
    queue_wait_nolock(queue, pred);
    queue_enqueue_nolock(queue, data);
    if (pthread_mutex_unlock(&queue->lock) != 0) {
        error("pthread_mutex_unlock");
    }
}

void *queue_dequeue_wait(Queue *queue, QueuePrediction pred) {
    void *v;
    if (pthread_mutex_lock(&queue->lock) != 0) {
        error("pthread_mutex_lock");
    }
    queue_wait_nolock(queue, pred);
    v = queue_dequeue_nolock(queue);
    if (pthread_mutex_unlock(&queue->lock) != 0) {
        error("pthread_mutex_unlock");
    }
    return v;
}
