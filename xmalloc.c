#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "xmalloc.h"

/* *************************** */
/* Memory allocation functions */
/* *************************** */

/* Created by Kevin Locke (from numerous canonical examples)         */
/*                                                                   */
/* I hereby place this file in the public domain.  It may be freely  */
/* reproduced, distributed, used, modified, built upon, or otherwise */
/* employed by anyone for any purpose without restriction.           */
/* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

/* ================= */
/* Customized malloc */
/* ================= */
void *
xmalloc(size_t size)
{
  void  *allocated;
  size_t real_size;

  real_size = (size > 0) ? size : 1;
  allocated = malloc(real_size);
  if (allocated == NULL)
  {
    fprintf(stderr,
            "Error:  Insufficient memory "
            "(attempt to malloc %lu bytes)\n",
            (unsigned long int)size);

    exit(EXIT_FAILURE);
  }

  return allocated;
}

/* ================= */
/* Customized calloc */
/* ================= */
void *
xcalloc(size_t n, size_t size)
{
  void *allocated;

  n         = (n > 0) ? n : 1;
  size      = (size > 0) ? size : 1;
  allocated = calloc(n, size);
  if (allocated == NULL)
  {
    fprintf(stderr,
            "Error:  Insufficient memory "
            "(attempt to calloc %lu bytes)\n",
            (unsigned long int)size);

    exit(EXIT_FAILURE);
  }

  return allocated;
}

/* ================== */
/* Customized realloc */
/* ================== */
void *
xrealloc(void *p, size_t size)
{
  void *allocated;

  allocated = realloc(p, size);
  if (allocated == NULL && size > 0)
  {
    fprintf(stderr,
            "Error:  Insufficient memory "
            "(attempt to xrealloc %lu bytes)\n",
            (unsigned long int)size);

    exit(EXIT_FAILURE);
  }

  return allocated;
}

/* ==================================== */
/* strdup implementation ussing xmalloc */
/* ==================================== */
char *
xstrdup(const char *str)
{
  char *p = xmalloc(strlen(str) + 1);
  strcpy(p, str);
  return p;
}

/* ===================================== */
/* strndup implementation ussing xmalloc */
/* ===================================== */
char *
xstrndup(const char *str, unsigned len)
{
  char *p = xmalloc(len + 1);
  strncpy(p, str, len);
  p[len] = '\0';
  return p;
}
