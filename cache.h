#ifndef CACHE_H
#define CACHE_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

/* Offset of the status (good/bad) of the cache. */
#define CACHE_STATUS_OFFSET 11

/* Offset of the hash seed used to calculate hashes. */
#define CACHE_HASH_SEED_OFFSET 13

/* Offset of the CRC of the cache. */
#define CRC16_OFFSET 17

/* 255 max, # of hashes before the need to create a new index. */
#define HASHES_NB 128

/* 255 max, # of bucket before the need to create a new index.  */
#define BUCKETS_NB 160

/* 255 max, number of bytes in a bucket before the need to allocate */
/* a new bucket.                                                    */
#define BUCKET_SIZE 128

#define INDEX_ENTRY_SIZE 8

#define SYS_CACHE "sys.cache"

#define SYS_CACHE_DIR XQUOTE(CACHE_DIR)

/*
               0                   1                   2                   3  
               0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
              +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
              |     Magic     |H|B|S|ST |H Seed |CRC|       Reserved          |
 Index   ---> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |    Next_IDX   |H|B|                Reserved                   |
 |Hashes ---> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |  Hash |N|F|L|R|  Hash |N|F|L|R| ...                           |
 |            |                                                               |
 |            |                                                               |
 Buckets ---> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |                                                               |
 |            |                             Bucket                            |
 |            |                                                               |
 |            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |                                                               |
 |            |                             Bucket                            |
 |            |                                                               |
 |            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |                              ...                              |
 Index   ---> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |    Next_IDX   |H|B|                  Reserved                 |
 |Hashes ---> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |  Hash |N|F|L|R|  Hash |N|F|L|R| ...                           |
 Buckets ---> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |            |                              ...                              |

 After Magic  H       : Maximum number of hashes by index.
              B       : Maximum number of buckets by index.
              S       : Maximum number of bytes per bucket.
              ST      : Status.
              H Seed  : Seed used to generate the hashes.
              CRC     : cache CRC.

 In index     Next_IDX: location of the next index in file.
              H       : Number of hashes.
              B       : Number of buckets in this index.

 In hashes:   N       : Number of used buckets
              F       : first bucker index
              L       : Number of bytes in the last bucket
              R       : Reserved
*/

typedef struct index_node_s index_node_t;
typedef struct tree_node_s  tree_node_t;

int
cache_create(char *name);

int
cache_set_status(char *name, cache_status_t status);

uint16_t
cache_get_status(char *name);

int
cache_is_outdated(char *cache_filename, ll_t *data_filename_list);

int
cache_get_infos(int      fd,
                uint8_t *hashes_nb,
                uint8_t *buckets_nb,
                uint8_t *bucket_size);

uint16_t
cache_crc16_calculate(int fd, size_t crc_offset);

int
cache_crc16_check(char *name);

int
cache_search(char *cache_file, char *str, void **data, uint16_t *data_len);

int
cache_build(char *cache_file, ll_t *input_list, int32_t hash_seed);

#endif
