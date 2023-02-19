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

#ifdef TEST
void test_list() {
    struct list_node head;
    struct TestObj obj = { .name = '0' };
    struct TestObj obj1 = { .name = '1' };
    struct TestObj obj2 = { .name = '2' };

    list_node_init(&head);
    assert(list_length(&head) == 0);

    list_node_init(&obj.list);
    assert(&obj == list_object(&obj.list, struct TestObj, list));
    assert(obj.list.prev == &obj.list);
    assert(obj.list.next == &obj.list);
    list_add(&head, &obj.list);
    assert(list_length(&head) == 1);

    list_add(&obj.list, &obj1.list);
    assert(obj.list.next == &obj1.list);
    assert(obj1.list.prev = &obj.list);
    assert(list_length(&head) == 2);

    list_add(&obj1.list, &obj2.list);
    assert(list_length(&head) == 3);
    assert(list_length(&obj.list) == 3);
    assert(list_length(&obj1.list) == 3);
    assert(list_length(&obj2.list) == 3);

    char str[8] = { 0 };
    int i = 0;
    list_foreach(ptr, &head) {
        str[i++] = list_object(ptr, struct TestObj, list)->name;
    }
    assert(!strcmp("012", str));

    list_del(&obj1.list);
    assert(list_length(&obj.list) == 2);
    list_del(&obj2.list);
    assert(list_length(&obj.list) == 1);
}
#endif
