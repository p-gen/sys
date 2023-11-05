/* *************************************************************** */
/* Adapted from:                                                   */
/* https://cse.iitkgp.ac.in/~abhij/course/lab/Algo1/Autumn15/DSW.c */
/* *************************************************************** */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "xmalloc.h"
#include "bst.h"

extern int
bst_comp(const void *a, const void *b);

extern void
bst_action(void *a, void *arg);

/* ======================================================== */
/* Insert or change an item in the tree.                    */
/* If tree is NULL, create a new item.                      */
/* if tree != NULL, walk the tree and if the item is found, */
/*                  replace it, if not insert it.           */
/* if updated != NULL set the pointed value to 1 in case of */
/*                    update and 0 in case of insertion.    */
/* ======================================================== */
bst_node_t *
bst_insert(bst_node_t *tree,
           void       *item,
           int (*bst_comp)(const void *a, const void *b),
           unsigned *updated)
{
  int r;

  if (!tree)
  {
    /* Allocates a new node when tree == NULL. */
    /* """"""""""""""""""""""""""""""""""""""" */
    tree       = xmalloc(sizeof(bst_node_t));
    tree->data = item;
    tree->left = tree->right = NULL;

    /* An old item was not changed but a now on inserted, */
    /* set updated to 0* (no).                            */
    /* """""""""""""""""""""""""""""""""""""""""""""""""" */
    if (updated != NULL)
      *updated = 0;

    return tree;
  }

  r = bst_comp(item, tree->data);

  if (r < 0)
    tree->left = bst_insert(tree->left, item, bst_comp, updated);
  else if (r > 0)
    tree->right = bst_insert(tree->right, item, bst_comp, updated);
  else
  {
    free(tree->data);
    tree->data = item;

    /* Set the updated flag to 1 if an already present item was changed */
    /* and updated was not NULL.                                        */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (updated != NULL)
      *updated = 1;
  }
  return tree;
}

#if 0
/* TODO to be tested */
void *
bst_delete(bst_node_t ** tree, void * item,
           int (*bst_comp)(const void * a, const void * b))
{
  bst_node_t *p, *next_p, *prev_p;
  bst_node_t *m, *next_m, *prev_m;
  void *      return_item;
  int         cmp_result;

  /* Attempt to locate the item to be deleted. */
  next_p = *tree;

  p = NULL;
  for (;;)
  {
    prev_p     = p;
    p          = next_p;
    cmp_result = bst_comp(item, p->data);
    if (cmp_result < 0)
      next_p = p->left;
    else if (cmp_result > 0)
      next_p = p->right;
    else
    {
      /* Item found. */
      break;
    }
    if (!next_p)
      return NULL;
  }

  /* p points to the node to be deleted. */
  if (!p->left)
  {
    /* Right child replaces p. */
    if (!prev_p)
      *tree = p->right;
    else if (p == prev_p->left)
      prev_p->left = p->right;
    else
      prev_p->right = p->right;
  }
  else if (!p->right)
  {
    /* Left child replaces p. */
    if (!prev_p)
      *tree = p->left;
    else if (p == prev_p->left)
      prev_p->left = p->left;
    else
      prev_p->right = p->left;
  }
  else
  {
    /* Minimum child, m, in the right subtree replaces p. */
    m      = p;
    next_m = p->right;
    do
    {
      prev_m = m;
      m      = next_m;
      next_m = m->left;
    } while (next_m);

    /* Update either the left or right child pointers of prev_p. */
    if (!prev_p)
      *tree = m;
    else if (p == prev_p->left)
      prev_p->left = m;
    else
      prev_p->right = m;

    /* Update the tree part m was removed from, and assign m the child
         * pointers of p (only if m is not the right child of p).
         */
    if (prev_m != p)
    {
      prev_m->left = m->right;
      m->right     = p->right;
    }
    m->left = p->left;
  }

  /* Get return value and free space used by node p. */
  return_item = p->data;

  free(p);

  return return_item;
}
#endif

void *
bst_search(bst_node_t *tree,
           void       *searched_item,
           int (*bst_comp)(const void *a, const void *b))
{
  int r;

  if (!tree)
    return NULL;

  r = bst_comp(searched_item, tree->data);
  if (r == 0)
    return tree->data;
  else if (r < 0)
    return bst_search(tree->left, searched_item, bst_comp);
  else
    return bst_search(tree->right, searched_item, bst_comp);
}

bst_t
bst_right_rotate(bst_t bst)
{
  bst_node_t *L, *LR;

  if ((bst == NULL) || (bst->left == NULL))
    return bst;
  L         = bst->left;
  LR        = L->right;
  L->right  = bst;
  bst->left = LR;
  return L;
}

bst_t
bst_left_rotate(bst_t bst)
{
  bst_node_t *R, *RL;

  if ((bst == NULL) || (bst->right == NULL))
    return bst;
  R          = bst->right;
  RL         = R->left;
  R->left    = bst;
  bst->right = RL;
  return R;
}

bst_t
bst_make_skew(bst_t bst)
{
  bst_node_t *p, *r;

  /* Create a dummy root to make the rest of the implementation clean */
  r        = (bst_node_t *)xmalloc(sizeof(bst_node_t));
  r->data  = NULL; /* this hashue is not necessary.               */
  r->left  = NULL; /* the dummy root has no left subtree.         */
  r->right = bst;  /* the original tree bst is the right subtree. *
                    | of the dummy root.                          */

  p = r;
  while (p->right)
  {
    /* Let q = p -> left. If q has left child, right rotate at q, else *
     | advance down the linked list.                                   */
    if (p->right->left == NULL)
      p = p->right;
    else
      p->right = bst_right_rotate(p->right);
  }

  return r;
}

/* ======================================================================= */
/* this function keeps on left rotating alternate nodes on the rightmost   */
/* path from the root. The total number of rotations is supplied as count. */
/* ======================================================================= */
bst_t
bst_compress(bst_t bst, int count)
{
  bst_node_t *p;

  p = bst;
  while (count)
  {
    p->right = bst_left_rotate(p->right);
    p        = p->right;
    --count;
  }

  return bst;
}

bst_t
bst_rebalance(bst_t bst, int *h)
{
  int n, t, l, lc;

  /* n is the number of nodes in the original tree */
  n  = bst_height(bst);
  *h = n;

  /* Compute l = log2(n+1) */
  t = n + 1;
  l = 0;
  while (t > 1)
  {
    ++l;
    t /= 2;
  }

  /* Create the deepest leaves */
  lc = n + 1 - (1 << l);
  if (lc == 0)
    lc = (1 << (l - 1));
  bst = bst_compress(bst, lc);

  n -= lc;
  while (n > 1)
  { /* Sequence of left rotations */
    n /= 2;
    bst = bst_compress(bst, n);
  }

  return bst;
}

/* Standard height-computation function. */
int
bst_height(bst_t bst)
{
  int lht, rht;

  if (bst == NULL)
    return -1;
  lht = bst_height(bst->left);
  rht = bst_height(bst->right);
  return 1 + ((lht >= rht) ? lht : rht);
}

void
bst_foreach(bst_node_t *item, void (*bst_action)(void *, void *), void *arg)
{
  if (!item)
    return;

  bst_foreach(item->left, bst_action, arg);
  bst_action(item->data, arg);
  bst_foreach(item->right, bst_action, arg);
}

void
bst_delete_tree(bst_node_t *item, void (*item_free)(void *))
{
  if (item == NULL)
    return;

  /* first delete both subtrees */
  bst_delete_tree(item->left, item_free);
  bst_delete_tree(item->right, item_free);

  /* then delete the node if item_free is not NULL */
  if (item_free != NULL)
    item_free(item->data);

  free(item);
}

/* Return NULL or the direct successor of item in the tree. */
bst_t
bst_next(bst_node_t *root,
         bst_node_t *item,
         int (*bst_comp)(const void *a, const void *b))
{
  bst_node_t *next = NULL;

  while (root)
  {
    if (bst_comp(item->data, root->data) > 0)
      root = root->right;
    else
    {
      next = root;
      root = root->left;
    }
  }
  return next;
}
