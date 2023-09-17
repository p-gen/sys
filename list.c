/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <stdio.h>
#include <stdlib.h>

#include "list.h"
#include "xmalloc.h"

extern int
comp(void const *, void const *);

extern void
swap(void **, void **);

/* ********************* */
/* Linked List functions */
/* ********************* */

static ll_node_t *
ll_partition(ll_node_t *l,
             ll_node_t *h,
             int (*comp)(void const *, const void const *),
             void (*swap)(void **, void **));

static void
ll_quicksort(ll_node_t *l,
             ll_node_t *h,
             int (*comp)(const void const *, const void const *),
             void (*swap)(void **, void **));

/* ======================== */
/* Create a new linked list */
/* ======================== */
ll_t *
ll_new(void)
{
  ll_t *ret = xmalloc(sizeof(ll_t));
  ll_init(ret);

  return ret;
}

/* ======================== */
/* Initialize a linked list */
/* ======================== */
void
ll_init(ll_t *list)
{
  list->head = NULL;
  list->tail = NULL;
  list->len  = 0;
}

/* ==================================================== */
/* Allocate the space for a new node in the linked list */
/* ==================================================== */
ll_node_t *
ll_new_node(void)
{
  ll_node_t *ret = xmalloc(sizeof(ll_node_t));

  return ret;
}

/* ==================================================================== */
/* Append a new node filled with its data at the end of the linked list */
/* The user is responsible for the memory management of the data        */
/* ==================================================================== */
void
ll_append(ll_t * const list, void * const data)
{
  ll_node_t *node;

  node = ll_new_node(); /* ll_new_node cannot return NULL because it   *
                         | uses xmalloc which does not return if there *
                         | is an allocation error.                     */

  node->data = data;
  node->next = NULL;       /* This node will be the last. */
  node->prev = list->tail; /* NULL if it is a new list.   */

  if (list->tail)
    list->tail->next = node;
  else
    list->head = node;

  list->tail = node;

  ++list->len; /* One more node in the list. */
}

/* =================================================================== */
/* Put a new node filled with its data at the beginning of the linked  */
/* list. The user is responsible for the memory management of the data */
/* =================================================================== */
void
ll_prepend(ll_t * const list, void * const data)
{
  ll_node_t *node;

  node = ll_new_node(); /* ll_new_node cannot return NULL because it   *
                         | uses xmalloc which does not return if there *
                         | is an allocation error.                     */

  node->data = data;
  node->prev = NULL;       /* This node will be the first. */
  node->next = list->head; /* NULL if it is a new list.    */

  if (list->head)
    list->head->prev = node;
  else
    list->tail = node;

  list->head = node;

  ++list->len; /* One more node in the list. */
}

/* ======================================================= */
/* Insert a new node before the specified node in the list */
/* TODO test it                                           */
/* ======================================================= */
void
ll_insert_before(ll_t * const list, ll_node_t *node, void * const data)
{
  ll_node_t *new_node;

  if (list)
  {
    if (node->prev == NULL)
      ll_prepend(list, data);
    else
    {
      new_node = ll_new_node();
      if (new_node)
      {
        new_node->data   = data;
        new_node->next   = node;
        new_node->prev   = node->prev;
        node->prev->next = new_node;

        ++list->len;
      }
    }
  }
}

/* ====================================================== */
/* Insert a new node after the specified node in the list */
/* TODO test it                                           */
/* ====================================================== */
void
ll_insert_after(ll_t * const list, ll_node_t *node, void * const data)
{
  ll_node_t *new_node;

  if (node->next == NULL)
    ll_append(list, data);
  else
  {
    new_node = ll_new_node();
    if (new_node)
    {
      new_node->data   = data;
      new_node->prev   = node;
      new_node->next   = node->next;
      node->next->prev = new_node;

      ++list->len;
    }
  }
}

/* ================================================================= */
/* Remove a node from a linked list.                                 */
/* The memory taken by the deleted node must be freed by the caller. */
/* ================================================================= */
int
ll_delete(ll_t * const list, ll_node_t *node)
{
  if (list->head == list->tail)
  {
    /* We delete the last remaining element from the list. */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""" */
    if (list->head == NULL)
      return 0;

    list->head = list->tail = NULL;
  }
  else if (node->prev == NULL)
  {
    /* We delete the first element from the list. */
    /* """""""""""""""""""""""""""""""""""""""""" */
    list->head       = node->next;
    list->head->prev = NULL;
  }
  else if (node->next == NULL)
  {
    /* We delete the last element from the list. */
    /* """"""""""""""""""""""""""""""""""""""""" */
    list->tail       = node->prev;
    list->tail->next = NULL;
  }
  else
  {
    /* We delete an element from the list. */
    /* """"""""""""""""""""""""""""""""""" */
    node->next->prev = node->prev;
    node->prev->next = node->next;
  }

  free(node);

  --list->len; /* One less node in the list. */

  return 1;
}

/* =========================================================== */
/* Creates a new list formed by the concatenation of two lists */
/* =========================================================== */
ll_t *
ll_concat(ll_t *list1, ll_t *list2)
{
  ll_t      *new_list = ll_new();
  ll_node_t *node;

  if (list1)
  {
    node = list1->head;
    while (node)
    {
      ll_append(new_list, node->data);
      node = node->next;
    }
  }

  if (list2)
  {
    node = list2->head;
    while (node)
    {
      ll_append(new_list, node->data);
      node = node->next;
    }
  }
  return new_list;
}

/* ====================================================== */
/* Partition code for the quicksort function              */
/* Based on code found here:                              */
/* http://www.geeksforgeeks.org/quicksort-for-linked-list */
/* ====================================================== */
static ll_node_t *
ll_partition(ll_node_t *l,
             ll_node_t *h,
             int (*comp)(void const *, void const *),
             void (*swap)(void **, void **))
{
  /* Considers last element as pivot, places the pivot element at its       */
  /* correct position in sorted array, and places all smaller (smaller than */
  /* pivot) to left of pivot and all greater elements to right of pivot     */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  /* set pivot as h element */
  /* """""""""""""""""""""" */
  void *x = h->data;

  ll_node_t *i = l->prev;
  ll_node_t *j;

  for (j = l; j != h; j = j->next)
  {
    if (comp(j->data, x) < 1)
    {
      i = (i == NULL) ? l : i->next;

      swap(&(i->data), &(j->data));
    }
  }

  i = (i == NULL) ? l : i->next;
  swap(&(i->data), &(h->data));

  return i;
}

/* ======================================================== */
/* A recursive implementation of quicksort for linked list. */
/* Based on code found here:                                */
/* http://www.geeksforgeeks.org/quicksort-for-linked-list   */
/* ======================================================== */
static void
ll_quicksort(ll_node_t *l,
             ll_node_t *h,
             int (*comp)(void const *, void const *),
             void (*swap)(void **, void **))
{
  if (h != NULL && l != h && l != h->next)
  {
    ll_node_t *p = ll_partition(l, h, comp, swap);
    ll_quicksort(l, p->prev, comp, swap);
    ll_quicksort(p->next, h, comp, swap);
  }
}

/* =========================== */
/* A linked list sort function */
/* =========================== */
void
ll_sort(ll_t *list,
        int (*comp)(void const *, void const *),
        void (*swap)(void **, void **))
{
  /* Call the recursive ll_quicksort function */
  /* """""""""""""""""""""""""""""""""""""""" */
  ll_quicksort(list->head, list->tail, comp, swap);
}

/* ======================================================================== */
/* Find a node in the list containing data. Return the node pointer or NULL */
/* if not found.                                                            */
/* A comparison function must be provided to compare a and b (strcmp like). */
/* ======================================================================== */
ll_node_t *
ll_find(ll_t * const list,
        void const  *data,
        int (*cmpfunc)(void const *a, void const *b))
{
  ll_node_t *node;

  if (NULL == (node = list->head))
    return NULL;

  do
  {
    if (0 == cmpfunc(node->data, data))
      return node;
  } while (NULL != (node = node->next));

  return NULL;
}

/* =============================================== */
/* Free all the elements of a list (make it empty) */
/* NULL or a custom function may be used to free   */
/* the sub components of the elements.             */
/* =============================================== */
void
ll_free(ll_t * const list, void (*clean)(void *))
{
  if (list)
  {
    ll_node_t *node = list->head;

    while (node)
    {
      /* Apply a custom cleaner if not NULL. */
      /* """"""""""""""""""""""""""""""""""" */
      if (clean)
        clean(node->data);

      ll_delete(list, node);

      node = list->head;
    }
  }
}

/* ==================================== */
/* Destroy a list and all its elements. */
/* ==================================== */
void
ll_destroy(ll_t *list, void (*clean)(void *))
{
  if (list)
  {
    ll_free(list, clean);
    free(list);
  }
}
