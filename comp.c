/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <stdint.h>
#include <string.h>
#include "list.h"
#include "types.h"
#include "comp.h"

int
str_comp(const void *a, const void *b)
{
  return strcmp((char *)a, (char *)b);
}

int
rule_entry_comp(const void *a, const void *b)
{
  return strcmp(((rule_t *)a)->tag, ((rule_t *)b)->tag);
}

int
var_entry_comp(const void *a, const void *b)
{
  return strcmp(((var_t *)a)->name, ((var_t *)b)->name);
}

/* ======================================================================== */
/* Generic comparison function. The only requirement is that each structure */
/* to be compared has a char * field names "name"                           */
/* ======================================================================== */
int
item_comp(const void *a, const void *b)
{
  typedef struct x_s
  {
    char *name;
  } x_t;

  x_t *xa = (x_t *)a;
  x_t *xb = (x_t *)b;

  return strcmp(xa->name, xb->name);
}
