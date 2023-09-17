/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#ifndef BST_H
#define BST_H

struct bst_node_s
{
  void              *data;
  struct bst_node_s *right, *left;
};

typedef struct bst_node_s  bst_node_t;
typedef struct bst_node_s *bst_t;

bst_node_t *
bst_insert(bst_node_t *tree,
           void       *item,
           int (*bst_comp)(const void *a, const void *b),
           unsigned *updated);

void *
bst_delete(bst_node_t **tree,
           void        *item,
           int (*bst_comp)(const void *a, const void *b));

void *
bst_search(bst_node_t *tree,
           void       *searched_item,
           int (*bst_comp)(const void *a, const void *b));

bst_t
bst_right_rotate(bst_t bst);

bst_t
bst_left_rotate(bst_t bst);

bst_t
bst_make_skew(bst_t bst);

bst_t
bst_compress(bst_t bst, int count);

bst_t
bst_rebalance(bst_t bst, int *h);

int
bst_height(bst_t bst);

void
bst_foreach(bst_node_t *item, void (*bst_action)(void *, void *), void *arg);

void
bst_delete_tree(bst_node_t *item, void (*item_free)(void *));

bst_t
bst_next(bst_node_t *root,
         bst_node_t *item,
         int (*bst_comp)(const void *a, const void *b));

#endif
