#include <pthread.h>
#include <unistd.h>

#include "../src/queue.h"

int has_data(Queue *q) {
    return q->length > 0;
}

void *test_consumer(Queue *q) {
    for (;;) {
        long int v = (long int)queue_dequeue_wait(q, has_data);
        printf("get %ld\n", v);
        if (v == 0) {
            break;
        }
    }
    return NULL;
}

void *test_producer(Queue *q) {
    for (long int i = 1; i < 100; i++) {
        queue_enqueue(q, (void *)i);
        printf("put %ld\n", i);
        usleep(1000 * 20);
    }
    queue_enqueue(q, NULL);
    return NULL;
}

void test_queue() {
    Queue q;

    pthread_t t1, t2;
    queue_init(&q);
    pthread_create(&t1, NULL, (void *)test_consumer, &q);
    pthread_create(&t2, NULL, (void *)test_producer, &q);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
}
