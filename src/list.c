#include "list.h"

inline void list_node_init(struct list_node *node) {
    node->prev = node->next = node;
}

void list_add(struct list_node *node, struct list_node *to_add) {
    node->next->prev = to_add;
    to_add->next = node->next;
    to_add->prev = node;
    node->next = to_add;
}

void list_del(struct list_node *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    list_node_init(node);
}

unsigned int list_length(struct list_node *node) {
    unsigned int count = 0;
    struct list_node *ptr;
    for (ptr = node->next; ptr != node; ptr = ptr->next, count++) {}
    return count;
}
