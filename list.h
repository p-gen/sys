#ifndef LIST_H
#define LIST_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

/* ******************************* */
/* Linked list specific structures */
/* ******************************* */

/* Linked list node structure */
/* """""""""""""""""""""""""" */
struct ll_node_s
{
  void             *data;
  struct ll_node_s *next;
  struct ll_node_s *prev;
};

typedef struct ll_node_s ll_node_t;

/* Linked List structure */
/* """"""""""""""""""""" */
struct ll_s
{
  ll_node_t *head;
  ll_node_t *tail;
  size_t     len;
};

typedef struct ll_s ll_t;

ll_t *
ll_new(void);

void
ll_init(ll_t *list);

ll_node_t *
ll_new_node(void);

void
ll_append(ll_t * const list, void * const data);

void
ll_prepend(ll_t * const list, void * const data);

void
ll_insert_before(ll_t * const list, ll_node_t *node, void * const data);

void
ll_insert_after(ll_t * const list, ll_node_t *node, void * const data);

ll_t *
ll_concat(ll_t *list1, ll_t *list2);

ll_t *
ll_merge(ll_t *list1, ll_t *list2, int merge_before);

void
ll_sort(ll_t *list,
        int (*ll_comp)(const void *, const void *),
        void (*ll_swap)(void **a, void **));

int
ll_delete(ll_t * const list, ll_node_t *node);

ll_node_t *
ll_find(ll_t * const list,
        void const  *data,
        int (*cmpfunc)(void const *a, void const *b));

void
ll_free(ll_t * const list, void (*clean)(void *));

void
ll_destroy(ll_t *list, void (*clean)(void *));

#endif
