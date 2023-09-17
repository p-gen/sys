#include <string.h>
#include <stdlib.h>

#include "ht.h"
#include "xmalloc.h"

/* **************************************************************** */
/* public domain code by Jerry Coffin, with improvements by HenkJan */
/* Wolthuias, Ken Murchison and Pierre Gentile (xmalloc cannot      */
/* return NULL).                                                    */
/* **************************************************************** */

/* ================================================================= */
/* Initialize the hash table to the size asked for.  Allocates space */
/* for the correct number of pointers and sets them to NULL.         */
/* ================================================================= */
ht_t *
ht_new(ht_t *table, size_t size)
{
  size_t   i;
  bucket **temp;

  table->size  = size;
  table->table = (bucket **)xmalloc(sizeof(bucket *) * size);
  temp         = table->table;

  for (i = 0; i < size; i++)
    temp[i] = NULL;

  return table;
}

/* ============================================================= */
/* Hashes a string to produce an unsigned short, which should be */
/* sufficient for most purposes.                                 */
/* ============================================================= */
unsigned
hash(char *string)
{
  unsigned ret_val = 0;

  while (*string)
  {
    int i;

    i = (int)*string;
    ret_val ^= i;
    ret_val <<= 1;
    string++;
  }
  return ret_val;
}

/* =============================================================== */
/* Insert 'key' into hash table.                                   */
/* Returns pointer to old data associated with the key, if any, or */
/* NULL if the key wasn't in the table previously.                 */
/* =============================================================== */
void *
ht_insert(ht_t *table, char *key, void *data)
{
  unsigned val = hash(key) % table->size;
  bucket  *ptr;

  /* NULL means this bucket hasn't been used yet.  We'll simply     */
  /* allocate space for our new bucket and put our data there, with */
  /* the table pointing at it.                                      */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  if (NULL == (table->table)[val])
  {
    (table->table)[val] = (bucket *)xmalloc(sizeof(bucket));

    (table->table)[val]->key  = xstrdup(key);
    (table->table)[val]->next = NULL;
    (table->table)[val]->data = data;

    return (table->table)[val]->data;
  }

  /* This spot in the table is already in use.  See if the current string */
  /* has already been inserted, and if so, increment its count.           */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  for (ptr = (table->table)[val]; NULL != ptr; ptr = ptr->next)
    if (0 == strcmp(key, ptr->key))
    {
      void *old_data;

      old_data  = ptr->data;
      ptr->data = data;
      return old_data;
    }

  /* This key must not be in the table yet.  We'll add it to the head of   */
  /* the list at this spot in the hash table.  Speed would be              */
  /* slightly improved if the list was kept sorted instead.  In this case, */
  /* this code would be moved into the loop above, and the insertion would */
  /* take place as soon as it was determined that the present key in the   */
  /* list was larger than this one.                                        */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  ptr = (bucket *)xmalloc(sizeof(bucket));

  ptr->key            = xstrdup(key);
  ptr->data           = data;
  ptr->next           = (table->table)[val];
  (table->table)[val] = ptr;

  return data;
}

/* ============================================================== */
/* Look up a key and return the associated data.  Returns NULL if */
/* the key is not in the table.                                   */
/* ============================================================== */
void *
ht_search(ht_t *table, char *key)
{
  unsigned val = hash(key) % table->size;
  bucket  *ptr;

  if (NULL == (table->table)[val])
    return NULL;

  for (ptr = (table->table)[val]; NULL != ptr; ptr = ptr->next)
  {
    if (0 == strcmp(key, ptr->key))
      return ptr->data;
  }

  return NULL;
}

/* ====================================================== */
/* Delete a key from the hash table and return associated */
/* data, or NULL if not present.                          */
/* ====================================================== */
void *
ht_delete(ht_t *table, char *key)
{
  unsigned val = hash(key) % table->size;
  void    *data;
  bucket  *ptr, *last = NULL;

  if (NULL == (table->table)[val])
    return NULL;

  /* Traverse the list, keeping track of the previous node in the list.  */
  /* When we find the node to delete, we set the previous node's next    */
  /* pointer to point to the node after ourself instead.  We then delete */
  /* the key from the present node, and return a pointer to the data it  */
  /* contains.                                                           */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  for (last = NULL, ptr = (table->table)[val]; NULL != ptr;
       last = ptr, ptr = ptr->next)
  {
    if (0 == strcmp(key, ptr->key))
    {
      if (last != NULL)
      {
        data       = ptr->data;
        last->next = ptr->next;
        free(ptr->key);
        free(ptr);
        return data;
      }

      /* If 'last' still equals NULL, it means that we need to   */
      /* delete the first node in the list. This simply consists */
      /* of putting our own 'next' pointer in the array holding  */
      /* the head of the list.  We then dispose of the current   */
      /* node as above.                                          */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""""" */

      else
      {
        data                = ptr->data;
        (table->table)[val] = ptr->next;
        free(ptr->key);
        free(ptr);
        return data;
      }
    }
  }

  /* If we get here, it means we didn't find the item in the table. */
  /* Signal this by returning NULL.                                 */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  return NULL;
}

/* ===================================================================== */
/* Frees a complete table by iterating over it and freeing each node.    */
/* the second parameter is the address of a function it will call with a */
/* pointer to the data associated with each node.  This function is      */
/* responsible for freeing the data, or doing whatever is needed with    */
/* it.                                                                   */
/* ===================================================================== */
void
ht_free(ht_t *table, void (*func)(void *))
{
  unsigned i;
  bucket  *ptr, *temp;

  for (i = 0; i < table->size; i++)
  {
    ptr = (table->table)[i];
    while (ptr)
    {
      temp = ptr;
      ptr  = ptr->next;
      free(temp->key);
      if (func)
        func(temp->data);
      free(temp);
    }
  }

  free(table->table);
  table->table = NULL;
  table->size  = 0;
}

/* ================================================================== */
/* Simply invokes the function given as the second parameter for each */
/* node in the table, passing it the key and the associated data.     */
/* ================================================================== */
void
ht_foreach(ht_t *table, void (*func)(char *, void *))
{
  unsigned i;
  bucket  *temp;

  for (i = 0; i < table->size; i++)
  {
    if ((table->table)[i] != NULL)
    {
      for (temp = (table->table)[i]; NULL != temp; temp = temp->next)
      {
        func(temp->key, temp->data);
      }
    }
  }
}
