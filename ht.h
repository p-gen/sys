/* +++Date last modified: 05-Jul-1997 */

#ifndef HASH__H
#define HASH__H

#include <stddef.h> /* For size_t     */

/*
** A hash table consists of an array of these buckets.  Each bucket
** holds a copy of the key, a pointer to the data associated with the
** key, and a pointer to the next bucket that collided with this one,
** if there was one.
*/

typedef struct bucket
{
  char          *key;
  void          *data;
  struct bucket *next;
} bucket;

/*
** This is what you actually declare an instance of to create a table.
** You then call 'construct_table' with the address of this structure,
** and a guess at the size of the table.  Note that more nodes than this
** can be inserted in the table, but performance degrades as this
** happens.  Performance should still be quite adequate until 2 or 3
** times as many nodes have been inserted as the table was created with.
*/

typedef struct ht_s
{
  size_t   size;
  bucket **table;
} ht_t;

/*
** Hashes a string to produce an unsigned short, which should be
** sufficient for most purposes.
*/

unsigned
hash(char *string);

/*
** This is used to construct the table.  If it doesn't succeed, it sets
**the table's size to 0, and the pointer to the table to NULL. */

ht_t *
ht_new(ht_t *table, size_t size);

/*
** Inserts a pointer to 'data' in the table, with a copy of 'key' as its
** key.  Note that this makes a copy of the key, but NOT of the
** associated data.
*/

void *
ht_insert(ht_t *table, char *key, void *data);

/*
** Returns a pointer to the data associated with a key.  If the key has
** not been inserted in the table, returns NULL.
*/

void *
ht_search(ht_t *table, char *key);

/*
** Deletes an entry from the table.  Returns a pointer to the data that
** was associated with the key so the calling code can dispose of it
** properly.
*/

void *
ht_delete(ht_t *table, char *key);

/*
** Goes through a hash table and calls the function passed to it
** for each node that has been inserted.  The function is passed
** a pointer to the key, and a pointer to the data associated
** with it.
*/

void
ht_foreach(ht_t *table, void (*func)(char *, void *));

/*
** Frees a hash table.  For each node that was inserted in the table,
** it calls the function whose address it was passed, with a pointer
** to the data that was in the table.  The function is expected to
** free the data.  Typical usage would be:
** free_table(&table, free);
** if the data placed in the table was dynamically allocated, or:
** free_table(&table, NULL);
** if not.  ( If the parameter passed is NULL, it knows not to call
** any function with the data. )
*/

void
ht_free(ht_t *table, void (*func)(void *));

#endif /* HASH__H */
