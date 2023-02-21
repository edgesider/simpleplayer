#ifndef _LIST_H_
#define _LIST_H_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

// 好处在于：
//   1. 不需要列表管理节点的分配和释放
//   2. 拿到数据节点可以直接操作列表
struct list_node {
    struct list_node *prev;
    struct list_node *next;
};

void list_node_init(struct list_node *node);
void list_del(struct list_node *node);
void list_add(struct list_node *node, struct list_node *to_add);
unsigned int list_length(struct list_node *node);

// clang-format off
#define list_object(node_ptr, type, member) \
    ((type *)((unsigned long)(node_ptr) - offsetof(type, member)))

#define list_foreach(ptr, node) \
    for (struct list_node *ptr = (node)->next; ptr != node; ptr = ptr->next)
// clang-format on

#endif
