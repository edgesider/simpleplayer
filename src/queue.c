#include "queue.h"

#include <pthread.h>

static inline void lock(Queue *queue) {
    if (pthread_mutex_lock(&queue->lock) != 0) {
        error("pthread_mutex_lock");
    }
}

static inline void broadcast_and_unlock(Queue *queue) {
    if (pthread_cond_broadcast(&queue->on_changed) != 0) {
        error("pthread_cond_broadcast");
    }
    if (pthread_mutex_unlock(&queue->lock) != 0) {
        error("pthread_mutex_unlock");
    }
}

void queue_init(Queue *q) {
    pthread_condattr_t cond_attr;

    q->length = 0;
    list_node_init(&q->nodes.queue);
    q->nodes.data = NULL;

    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        error("queue_init: mutex initialize failed");
    }

    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    if (pthread_cond_init(&q->on_changed, &cond_attr) != 0) {
        error("queue_init: cond initialize failed");
    }
}

static void queue_enqueue_locked(Queue *queue, void *data) {
    QueueNode *q_node = malloc(sizeof(QueueNode));
    q_node->data = data;
    list_add(queue->nodes.queue.prev, &q_node->queue);
    queue->length++;
}

static void *queue_dequeue_locked(Queue *queue) {
    if (queue->length == 0) {
        return NULL;
    }
    QueueNode *q_node = list_object(queue->nodes.queue.next, QueueNode, queue);
    list_del(&q_node->queue);
    void *data = q_node->data;
    queue->length--;
    free(q_node);
    return data;
}

void queue_enqueue(Queue *queue, void *data) {
    lock(queue);
    queue_enqueue_locked(queue, data);
    broadcast_and_unlock(queue);
}

void *queue_dequeue(Queue *queue) {
    lock(queue);
    void *v = queue_dequeue_locked(queue);
    broadcast_and_unlock(queue);
    return v;
}

static void queue_wait_locked(Queue *queue, QueuePrediction pred) {
    while (!pred(queue)) {
        int ret;
        if ((ret = pthread_cond_wait(&queue->on_changed, &queue->lock)) != 0) {
            error("pthread_cond_wait failed\n");
        }
    }
}

static int queue_timedwait_locked(Queue *queue, QueuePrediction pred,
                                  int64_t microseconds) {
    int ret = 0;
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t nano = microseconds * 1000 + ts.tv_nsec;
    ts.tv_nsec = nano % (1000 * 1000 * 1000);
    ts.tv_sec += nano / (1000 * 1000 * 1000);
    while (!pred(queue) && ret == 0) {
        ret = pthread_cond_timedwait(&queue->on_changed, &queue->lock, &ts);
    }
    if (ret == 0) {
        return 1;
    } else if (ret == ETIMEDOUT) {
        return 0;
    } else {
        error("pthread_cond_wait failed\n");
    }
}

void queue_enqueue_wait(Queue *queue, void *data, QueuePrediction pred) {
    lock(queue);
    queue_wait_locked(queue, pred);
    queue_enqueue_locked(queue, data);
    broadcast_and_unlock(queue);
}

void *queue_dequeue_wait(Queue *queue, QueuePrediction pred) {
    lock(queue);
    queue_wait_locked(queue, pred);
    void *v = queue_dequeue_locked(queue);
    broadcast_and_unlock(queue);
    return v;
}

int queue_enqueue_timedwait(Queue *queue, void *data, QueuePrediction pred,
                            int64_t microseconds) {
    lock(queue);
    int pred_succ = queue_timedwait_locked(queue, pred, microseconds);
    if (pred_succ) {
        queue_enqueue_locked(queue, data);
    }
    broadcast_and_unlock(queue);
    return pred_succ;
}

int queue_dequeue_timedwait(Queue *queue, QueuePrediction pred,
                            int64_t microseconds, void **data_ptr) {
    lock(queue);
    int pred_succ = queue_timedwait_locked(queue, pred, microseconds);
    if (pred_succ) {
        *data_ptr = queue_dequeue_locked(queue);
    }
    broadcast_and_unlock(queue);
    return pred_succ;
}

void queue_clear(Queue *queue, DataCleaner data_cleaner) {
    lock(queue);
    while (queue->length > 0) {
        void *data = queue_dequeue_locked(queue);
        // TODO 是否需要在释放锁之后再清理数据，避免锁定事件太长
        data_cleaner(data);
    }
    broadcast_and_unlock(queue);
}
