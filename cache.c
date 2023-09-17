/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "list.h"
#include "xmalloc.h"
#include "utils.h"
#include "cachestat.h"
#include "types.h"
#include "cache.h"
#include "crc16.h"
#include "bst.h"

struct index_node_s
{
  long                 start;
  bst_t                bst;
  struct index_node_s *next;
};

struct tree_node_s
{
  uint32_t hash;               /* hash value.                         */
  uint8_t  first_bucket;       /* index of the first occupied bucket  *
                                | (0-255).                            */
  uint8_t  buckets;            /* number of consecutive occupied      *
                                | buckets (0-255).                    */
  uint8_t  last_bucket_length; /* length of the data occupying the    *
                                | latest bucket.                      */
  void    *data;               /* data to put in buckets.             */
  uint8_t  flags;              /* various flags.                      */

  struct tree_node_s *right, *left;
};

static char   magic[8]    = { 's', 'y', 's', 'd', 'b', '-', '0', '1' };
static size_t first_start = 32; /* Offset of the first hash table     */

static uint16_t cache_is_empty = EMPTY;

/* ===================================================================== */
/* Return n if it is a multiple of size else return the next multiple of */
/* size greater than n.                                                  */
/* size must be > 0 (assumed).                                           */
/* ===================================================================== */
static uint64_t
align(uint16_t size, uint64_t n)
{
  if (n % size != 0)
    n = size * (n / size + 1);
  return n;
}

/* ====================================== */
/* Utility function to compare two hashes */
/* (same logic as strcmp).                */
/* ====================================== */
static int
hash_node_comp(const void *a, const void *b)
{
  tree_node_t *na = (tree_node_t *)a;
  tree_node_t *nb = (tree_node_t *)b;

  if (na->hash > nb->hash)
    return 1;
  else if (na->hash < nb->hash)
    return -1;
  else
    return 0;
}

/* ======================================= */
/* Add the address of the next index block.*/
/* ======================================= */
static void
index_add(index_node_t **index, index_node_t *item)
{
  if (!(*index))
    *index = item;
  else
    (*index)->next = item;
}

/* ============================================ */
/* The aim of the two following functions is to */
/* write all the hashes contained in the        */
/* perfect binary tree in is hashes slots       */
/* sorted by level like in this illustration:   */
/*                                              */
/* 1,2,3,4,5,6,7,... represents the tree:       */
/*                                              */
/*       1                                      */
/*   2       3                                  */
/* 4   5   6   7                                */
/* ...                                          */
/* ============================================ */
static void
hashes_write_level(int fd, bst_node_t *tree, int level)
{
  if (tree == NULL)
    return;

  if (level == 1)
  {
    bst_node_t  *node;
    tree_node_t *item;

    node = (bst_node_t *)tree;
    item = (tree_node_t *)node->data;

    xwrite(fd, &(item->hash), 4);
    xwrite(fd, &(item->buckets), 1);
    xwrite(fd, &(item->first_bucket), 1);
    xwrite(fd, &(item->last_bucket_length), 1);
    xwrite(fd, &(item->flags), 1);
  }
  else
  {
    hashes_write_level(fd, tree->left, level - 1);
    hashes_write_level(fd, tree->right, level - 1);
  }
}

static void
hashes_write(int fd, uint64_t index_offset, bst_node_t *tree, int height)
{
  int d;

  lseek(fd, align(16, index_offset + 10), SEEK_SET);
  for (d = 1; d <= height; d++)
    hashes_write_level(fd, tree, d);
}

/* ===================================== */
/* Create a new index with its metadata. */
/* ===================================== */
static void
add_new_index(int fd, uint64_t offset, uint8_t hashes_nb)
{
  uint32_t zero = 0;
  uint8_t  i;

  lseek(fd, offset, SEEK_SET);

  xwrite(fd, &zero, 4); /* An uint32_t size. */
  xwrite(fd, &zero, 4); /* An uint32_t size. */

  lseek(fd, align(16, offset + INDEX_ENTRY_SIZE), SEEK_SET);

  for (i = 0; i < hashes_nb; i++) /* Clear all hashes slots. */
  {
    xwrite(fd, &zero, 4);
    xwrite(fd, &zero, 4);
  }
}

/* =================================== */
/* Create the cache with its metadata. */
/* =================================== */
int
cache_create(char *name)
{
  int            fd;
  cache_status_t status = UNUSABLE;

  uint8_t  hashes_nb   = HASHES_NB;
  uint8_t  buckets_nb  = BUCKETS_NB;
  uint8_t  bucket_size = BUCKET_SIZE;
  uint32_t hash_seed   = 0;

  unlink(name);
  fd = open(name, O_CREAT | O_TRUNC | O_EXCL | O_WRONLY, 0600);
  if (fd == -1)
  {
    close(fd);
    return 0;
  }

  chown(name, 0, 0);

  errno = 0;

  xwrite(fd, magic, 8);
  xwrite(fd, &hashes_nb, 1);
  xwrite(fd, &buckets_nb, 1);
  xwrite(fd, &bucket_size, 1);
  xwrite(fd, &status, 2);
  xwrite(fd, &hash_seed, 4);

  add_new_index(fd, first_start, hashes_nb);

  close(fd);

  return (errno == 0);
}

/* ======================================== */
/* Utility function to set the cache status */
/* The status can be UNUSABLE or USABLE.    */
/* ======================================== */
int
cache_set_status(char *name, cache_status_t status)
{
  int fd;

  errno = 0;

  fd = open(name, O_WRONLY | O_EXCL | O_SYNC, 0600);

  lseek(fd, CACHE_STATUS_OFFSET, SEEK_SET);
  xwrite(fd, &status, 2);

  close(fd);

  return (errno != 0) ? 0 : 1;
}

/* ======================================== */
/* Utility function to get the cache status */
/* The status can be UNUSABLE or USABLE.    */
/* ======================================== */
uint16_t
cache_get_status(char *name)
{
  int      fd;
  uint16_t status;

  errno = 0;

  fd = open(name, O_RDONLY);
  if (fd == -1)
    return UNUSABLE;

  lseek(fd, CACHE_STATUS_OFFSET, SEEK_SET);
  xread(fd, &status, 2);

  close(fd);

  if (errno != 0)
    return UNUSABLE;

  return status;
}

/* ======================================== */
/* Utility function to set the cache status */
/* The status can be UNUSABLE or USABLE.    */
/* ======================================== */
int
cache_set_hash_seed(char *name, uint32_t hash_seed)
{
  int fd;

  errno = 0;

  fd = open(name, O_WRONLY | O_EXCL | O_SYNC, 0600);

  lseek(fd, CACHE_HASH_SEED_OFFSET, SEEK_SET);
  xwrite(fd, &hash_seed, 4);

  close(fd);

  return (errno != 0) ? 0 : 1;
}

/* ======================================== */
/* Utility function to get the cache status */
/* The status can be UNUSABLE or USABLE.    */
/* ======================================== */
uint32_t
cache_get_hash_seed(char *name)
{
  int      fd;
  uint32_t seed;

  errno = 0;

  fd = open(name, O_RDONLY);
  if (fd == -1)
    return UNUSABLE;

  lseek(fd, CACHE_HASH_SEED_OFFSET, SEEK_SET);
  xread(fd, &seed, 4);

  close(fd);

  if (errno != 0)
    return UNUSABLE;

  return seed;
}

/* ================================================================== */
/* Check if the cache file is older than one of the files whose names */
/* are given in a linked list.                                        */
/* Returns 1 if it's the case or 0 if not.                            */
/* ================================================================== */
int
cache_is_outdated(char *cache_filename, ll_t *list)
{
  struct stat cache_stat, data_stat;
  ll_node_t  *node;

  /* Get the latest modification time of the cache. */
  /* """""""""""""""""""""""""""""""""""""""""""""" */
  if (stat(cache_filename, &cache_stat) < 0)
    return 1;

  node = list->head;
  while (node)
  {
    if (stat((char *)node->data, &data_stat) == 0)
      if (data_stat.st_ctime > cache_stat.st_ctime
          || data_stat.st_mtime > cache_stat.st_mtime)
        return 1;

    node = node->next;
  }
  return 0;
}

/* =============================================================== */
/* Return the number of hashes and the number of buckets per index */
/* and the bucket size in bytes.                                   */
/* =============================================================== */
int
cache_get_infos(int      fd,
                uint8_t *hashes_nb,
                uint8_t *buckets_nb,
                uint8_t *bucket_size)
{
  int rc;

  lseek(fd, 8, SEEK_SET);

  rc = xread(fd, hashes_nb, 1);
  if (rc != 1)
    return 0;

  rc = xread(fd, buckets_nb, 1);
  if (rc != 1)
    return 0;

  rc = xread(fd, bucket_size, 1);
  if (rc != 1)
    return 0;

  return 1;
}

/* ======================================================= */
/* Return the crc16 of the cache except for the part of it */
/* where the crc16 is stored.                              */
/* ======================================================= */
uint16_t
cache_crc16_calculate(int fd, size_t crc_offset)
{
  uint16_t      crc16 = 0;
  unsigned long block = 0;
  long          nread;
  uint8_t       buffer[4096];

  lseek(fd, 0, SEEK_SET);

  while ((nread = xread(fd, &buffer, 4096)) > 0)
  {
    if (crc_offset >= block * 4096 + nread || crc_offset < block * 4096)
    {
      crc16 = crc16_update(crc16, buffer, 0, nread);
    }
    else
    {
      crc16 = crc16_update(crc16, buffer, 0, crc_offset % 4096);
      crc16 = crc16_update(crc16, buffer, crc_offset % 4096 + 2, nread);
    }
    block++;
  }

  return crc16;
}

/* ========================================================================= */
/* Check if the crc16 stored in the cache is the same as the calculated one. */
/* ========================================================================= */
int
cache_crc16_check(char *name)
{
  int      fd;
  uint16_t crc16_ref, crc16;

  fd = open(name, O_RDONLY);

  if (lseek(fd, CRC16_OFFSET, SEEK_SET) == -1)
  {
    close(fd);
    return 0;
  }

  if (xread(fd, &crc16_ref, 2) != 2)
  {
    close(fd);
    return 0;
  }

  crc16 = cache_crc16_calculate(fd, CRC16_OFFSET);

  close(fd);
  return (crc16_ref == crc16);
}

/* ====================================================================== */
/* Search the cache to see if the hash of the string str is in one of its */
/* indexes.                                                               */
/* If it is the case, data will point to the record and data_len will be  */
/* set to the data length in bytes.                                       */
/* ====================================================================== */
int
cache_search(char *cache_file, char *str, void **data, uint16_t *data_len)
{
  int     fd;
  uint8_t hashes_nb, used_hashes;
  uint8_t buckets_nb, used_buckets;
  uint8_t bucket_size;

  uint8_t found = 0;
  size_t  str_len; /* length of the str argument. */

  uint64_t index_offset;
  uint64_t next_index_offset;

  uint32_t searched_hash;

  void *bucket;

  uint32_t hash_seed;

  fd = open(cache_file, O_RDONLY, 0600);
  if (fd == -1)
  {
    unlink(cache_file);
    close(fd);
    return 0;
  }

  hash_seed = cache_get_hash_seed(cache_file);

  lseek(fd, 8, SEEK_SET);
  if (!cache_get_infos(fd, &hashes_nb, &buckets_nb, &bucket_size))
  {
    unlink(cache_file);
    close(fd);
    return 0;
  }

  bucket       = xmalloc(bucket_size);
  index_offset = first_start;
  str_len      = strlen(str);

  do
  {
    uint8_t  h;
    uint32_t hash;
    uint8_t  hash_buckets;
    uint8_t  hash_first_bucket;
    uint8_t  hash_last_bucket_size;
    uint64_t hashes_offset;

    lseek(fd, index_offset, SEEK_SET);
    xread(fd, &next_index_offset, 8);
    xread(fd, &used_hashes, 1);
    xread(fd, &used_buckets, 1);

    hashes_offset = align(16, index_offset + 10);
    lseek(fd, hashes_offset, SEEK_SET);

    *data_len = 0;

    /* Walk through the array representation of */
    /* the perfect binary tree of hashes.       */
    /* """""""""""""""""""""""""""""""""""""""" */
    h = 0;
    while (!found && h < used_hashes)
    {
      xread(fd, &hash, 4);
      xread(fd, &hash_buckets, 1);
      xread(fd, &hash_first_bucket, 1);
      xread(fd, &hash_last_bucket_size, 1);

      searched_hash = hash_data(str, str_len, hash_seed);

      if (hash == searched_hash)
      {
        uint8_t  b;
        uint64_t buckets_offset = hashes_offset + hashes_nb * INDEX_ENTRY_SIZE;

        found = 1;

        *data = xmalloc(bucket_size * (hash_buckets - 1)
                        + hash_last_bucket_size);

        lseek(fd, buckets_offset + hash_first_bucket * bucket_size, SEEK_SET);

        for (b = 0; b < hash_buckets - 1; b++)
        {
          xread(fd, bucket, bucket_size);
          *data_len += bucket_size;
          memcpy((char *)*data + b * bucket_size, bucket, bucket_size);
        }

        xread(fd, bucket, hash_last_bucket_size);
        memcpy((char *)*data + b * bucket_size, bucket, hash_last_bucket_size);
        *data_len += hash_last_bucket_size;

        break;
      }
      if (searched_hash < hash)
        h = 2 * h + 1;
      else
        h = 2 * h + 2;

      lseek(fd, hashes_offset + h * INDEX_ENTRY_SIZE, SEEK_SET);
    }
    index_offset = next_index_offset;
  } while (!found && next_index_offset != 0);

  free(bucket);

  close(fd);

  return found;
}

/* ========================================================= */
/* Feed the cache with element from a linked list.           */
/* Each input_list elements must be a pointer to a structure */
/* containing three fields:                                  */
/* - a field of type char *   (str)                          */
/* - a field of type void *   (data)                         */
/* - a field of type uint16_t (data length)                  */
/* return 1 if the cache is created (it may be invalid).     */
/* ========================================================= */
int
cache_build(char *cache_file, ll_t *input_list, int32_t seed)
{
  int           fd;
  tree_node_t  *curr;
  bst_node_t   *root;
  index_node_t *index;

  uint8_t hashes_nb   = HASHES_NB;
  uint8_t buckets_nb  = BUCKETS_NB;
  uint8_t bucket_size = BUCKET_SIZE;

  ll_node_t *elem;

  int height;

  uint8_t b;
  uint8_t hashes = 0;
  uint8_t buckets;

  uint16_t crc16;

  index_node_t *first_index = NULL;

  index = xmalloc(sizeof(index_node_t));
  index_add(&first_index, index);
  index->start = first_start;

  uint8_t next_bucket = 0;

  errno = 0;

  fd = open(cache_file, O_EXCL | O_RDWR, 0600);
  if (fd == -1)
    return 0;

  /* Store the seed used to calculate the hashes */
  /* in the cache file.                          */
  /* """"""""""""""""""""""""""""""""""""""""""" */
  cache_set_hash_seed(cache_file, seed);

  root = NULL;

  if (input_list->len == 0)
  {
    lseek(fd, 12, SEEK_SET);
    xwrite(fd, &cache_is_empty, 2);
    close(fd);
    return 0;
  }

  /* Go to the start of the first bucket zone. */
  /* """"""""""""""""""""""""""""""""""""""""" */
  lseek(fd,
        align(16, index->start + 16 + hashes_nb * INDEX_ENTRY_SIZE),
        SEEK_SET);

  /* Walk though the linked list. */
  /* """""""""""""""""""""""""""" */
  elem = input_list->head;
  while (elem != NULL)
  {
    char    *str;
    char    *data;
    uint16_t data_len;

    str      = (char *)((elem_t *)((ll_node_t *)(elem))->data)->str;
    data     = (char *)((elem_t *)((ll_node_t *)(elem))->data)->data;
    data_len = ((elem_t *)((ll_node_t *)(elem))->data)->data_len;

    if (data_len < bucket_size)
      buckets = 1;
    else if (data_len % bucket_size != 0)
      buckets = data_len / bucket_size + 1;
    else
      buckets = data_len / bucket_size;

    /* Look if we are needing for a new index. */
    /* """"""""""""""""""""""""""""""""""""""" */
    if (hashes >= hashes_nb || next_bucket + buckets > buckets_nb)
    {
      index_node_t *old_index;
      uint64_t      new_start;

      new_start = align(16,
                        index->start + 16 + hashes_nb * INDEX_ENTRY_SIZE
                          + bucket_size * buckets_nb);

      lseek(fd, index->start, SEEK_SET);
      xwrite(fd, &new_start, 8);

      old_index = index;
      index_add(&old_index, index);

      /* Make the hashes binary tree optimal and write it. */
      /* """"""""""""""""""""""""""""""""""""""""""""""""" */
      root = bst_make_skew(root);
      root = bst_rebalance(root, &height);

      hashes_write(fd, index->start, root->right, height);
      lseek(fd, index->start + 8, SEEK_SET);
      xwrite(fd, &hashes, 1);
      xwrite(fd, &next_bucket, 1);

      /* Create a new hash table. */
      /* """""""""""""""""""""""" */
      index->start = new_start;
      add_new_index(fd, new_start, hashes_nb);

      /* And reinitialize some variables. */
      /* """""""""""""""""""""""""""""""" */
      root        = NULL;
      hashes      = 0;
      next_bucket = 0;
    }

    /* Write the buckets contents now. */
    /* """"""""""""""""""""""""""""""" */
    for (b = 1; b < buckets; b++)
      xwrite(fd, data + (b - 1) * bucket_size, bucket_size);
    xwrite(fd, data + (b - 1) * bucket_size, data_len - (b - 1) * bucket_size);

    lseek(fd, b * bucket_size - data_len, SEEK_CUR);

    /* And insert the new element in the binary tree. */
    /* """""""""""""""""""""""""""""""""""""""""""""" */
    curr = (tree_node_t *)xmalloc(sizeof(tree_node_t));

    curr->left = curr->right = NULL;
    curr->buckets            = buckets;
    curr->first_bucket       = next_bucket;

    curr->last_bucket_length = data_len % bucket_size;
    if (curr->last_bucket_length == 0)
      curr->last_bucket_length = bucket_size;

    curr->data = xmalloc(data_len);
    memcpy(curr->data, data, data_len);

    /* Retry with an increased seed while there is a collisions   */
    /* or a hash equal to 0 (considered as an uninitialized value */
    /* in cache_search).                                          */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    curr->flags = 0; /* TODO will hold flags in the future. */
    curr->hash  = hash_data(str, strlen(str), seed);

    if (bst_search(root, curr, hash_node_comp) != NULL)
    {
      /* We have a collision or a hash equal to 0, */
      /* retry with the next seed.                 */
      /* """"""""""""""""""""""""""""""""""""""""" */
      cache_set_status(cache_file, INVALID);
      goto invalid;
    }

    root = bst_insert(root, curr, hash_node_comp, NULL);

    next_bucket += buckets;
    hashes++;

    elem = elem->next;
  }

  /* No need to flush the partial hash table now */
  /* """"""""""""""""""""""""""""""""""""""""""" */
  if (root != NULL)
  {
    /* Make the hashes binary tree optimal and write it. */
    /* """"""""""""""""""""""""""""""""""""""""""""""""" */
    root = bst_make_skew(root);
    root = bst_rebalance(root, &height);

    hashes_write(fd, index->start, root->right, height);
    lseek(fd, index->start + 8, SEEK_SET);
    xwrite(fd, &hashes, 1);
    xwrite(fd, &next_bucket, 1);
  }

  cache_set_status(cache_file, USABLE);

  /* calculate and store the CRC16 of the cache (without CRC). */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if ((crc16 = cache_crc16_calculate(fd, CRC16_OFFSET)) != 0)
  {
    lseek(fd, CRC16_OFFSET, SEEK_SET);
    xwrite(fd, &crc16, 2);
  }

invalid:

  close(fd);

  return 1;
}
