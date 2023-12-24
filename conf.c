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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include "cachestat.h"
#include "utils.h"
#include "log.h"
#include "list.h"
#include "bst.h"
#include "types.h"
#include "cache.h"
#include "xmalloc.h"
#include "strarray.h"
#include "conf.h"
#include "comp.h"

extern bst_node_t *var_tree;
extern bst_node_t *rule_tree;

extern long path_max;

/* ---------------------------------------------- */
/* Parameters in Parameter's value list accessors */
/* ---------------------------------------------- */

/* ================================================================ */
/* For a given rule and a param name, return a pointer on the first */
/* corresponding parameter in rule or NULL if not found.            */
/*                                                                  */
/* Returns: - NULL if the parameter 'name' does not exist           */
/*          - a pointer to the named parameter just found           */
/* ================================================================ */
param_t *
get_param(rule_t *rule, char *name)
{
  ll_t      *param_list = rule->param_list;
  ll_node_t *node       = param_list->head;
  param_t   *par;

  while (node)
  {
    par = (param_t *)node->data;

    if (strcmp(par->name, name) == 0)
      return par;

    node = node->next;
  }
  return NULL;
}

/* ================================================================= */
/* For a given rule and a param name, return the list of its value   */
/* If found, the parameter param will point to the parameter found   */
/*                                                                   */
/* Returns: - NULL if the parameter 'name' does not exist            */
/*          - a pointer to the list of values of the named parameter */
/* ================================================================= */
ll_t *
get_param_val_list(rule_t *rule, char *name)
{
  param_t *par;

  par = get_param(rule, name);

  if (par == NULL)
    return NULL;

  return par->val_list;
}

/* ================================================================ */
/* Create a sorted list of all the data files to use.               */
/* Only the designated directories in the cfg file are considered.  */
/* ================================================================ */
int
create_data_file_list(const char *path, ll_t *file_list)
{
  char          *newpath;
  int            len = -1;
  int            rc  = 1;
  DIR           *dir;
  struct dirent *entry;
  struct stat    st;

  newpath = xmalloc(path_max);

  /* Try to open the given dir. */
  /* """""""""""""""""""""""""" */
  dir = opendir(path);
  if (dir == NULL)
  {
    rc = 0;
    goto out;
  }

  /* Store the directory filenames in the list. */
  /* """""""""""""""""""""""""""""""""""""""""" */
  while ((entry = readdir(dir)) != NULL)
  {
    if (strcmp(entry->d_name, ".") == 0)
      continue;

    if (strcmp(entry->d_name, "..") == 0)
      continue;

    /* The name must contain at least 5 characters as it must */
    /* be terminated by ".dat".                               */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
    len = strlen(entry->d_name);
    if (len < 5)
      continue;

    if (strcmp(entry->d_name + len - 4, ".dat") != 0)
      continue;

    /* forge the full path and add it to the list */
    /* """""""""""""""""""""""""""""""""""""""""" */
    snprintf(newpath, path_max, "%s/%s", path, entry->d_name);

    if (stat(newpath, &st) == 0)
    {
      if (!S_ISREG(st.st_mode))
      {
        trace(LOG_WARN, "%s ignored, not a regular file.", newpath);
        continue;
      }

      if ((st.st_mode & 0777) != 0600)
      {
        trace(LOG_WARN,
              "%s ignored, must have read/write rights for root only.",
              newpath);
        continue;
      }

      if (st.st_uid != 0 || st.st_gid != 0)
      {
        trace(LOG_WARN, "%s ignored, must be owned by root:root.", newpath);
        continue;
      }

      if (strcmp(entry->d_name + len - 4, ".dat") == 0)
      {
        ll_append(file_list, xstrdup(newpath));
      }
    }
  }

  /* Sort the resulting list if not empty as readdir doesn't do that. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (len < 0)
    rc = 0;
  else
    ll_sort(file_list, str_comp, swap);

  /* Done */
  closedir(dir);

out:
  free(newpath);

  return rc;
}

/* ------------------- */
/* Variable management */
/* ------------------- */

/* ================================================================== */
/* Callback function used by bst_foreach.                             */
/* Copies global variables present in the variables bst tree into the */
/* new tree whose reference is passed in arg.                         */
/* ================================================================== */
void
copy_global_var(void *node, void *arg)
{
  var_t       *var, *new_var;
  bst_node_t **tree_ptr;

  var      = node;               /* node is a var_t instance.         */
  tree_ptr = (bst_node_t **)arg; /* arg points to a bst tree pointer. */

  if (var->global)
  {
    new_var = xmalloc(sizeof(var_t));
    memcpy(new_var, var, sizeof(var_t));

    /* We do not case if new_var was inserted or replaced here. */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    *tree_ptr = bst_insert(*tree_ptr, new_var, item_comp, NULL);
  }
}

/* ========================================= */
/* Function to expand variables in a string. */
/* variables reference are given as @{...}   */
/* ========================================= */
int
expand_var_ref(char **str)
{
  var_t    var_node;
  char    *var;
  char    *expanded_var;
  char    *var_ref_name;
  char    *new_val;
  unsigned finished = 0;
  size_t   offset   = 0;
  //char * old_str = xstrdup(*str);

  /* While expansion is not finished */
  /* """"""""""""""""""""""""""""""" */
  do
  {
    /* While there remains a non expanded variable in the variable */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    while ((offset = get_next_var_reference(*str, offset, &var)) != 0)
    {
      var_t *found_var;

      /* Locate the variable reference */
      /* """"""""""""""""""""""""""""" */
      var_node.name = var;
      if ((found_var = bst_search(var_tree, &var_node, var_entry_comp)) != NULL)
        expanded_var = xstrdup(found_var->value);
      else
        expanded_var = xstrdup("");

      /* Update the variable content */
      /* """"""""""""""""""""""""""" */
      var_ref_name = xcalloc(strlen(var) + 4, 1);
      strcat(var_ref_name, "@{");
      strcat(var_ref_name, var);
      strcat(var_ref_name, "}");
      new_val = strrep(*str, var_ref_name, expanded_var);

      free(var_ref_name);
      free(expanded_var);

      free(*str);
      *str   = new_val;
      offset = 0;
    }
    if (get_next_var_reference(*str, 0, &var) != 0)
      offset = 0;
    else
      finished = 1;
  } while (!finished);
}

/* ====================================================== */
/* Extracts the next variable name from a string starting */
/* at offset start if any.                                */
/* Returns the first position after the discovered        */
/* variable on success or 0                               */
/* ====================================================== */
int
get_next_var_reference(char *str, int start, char **var)
{
  char *pstart, *pend;

  if ((pstart = strstr(str + start, "@{")) != NULL)
  {
    /* Skip \ protected variables reference */
    /* """""""""""""""""""""""""""""""""""" */
    while (pstart > str + start && *(pstart - 1) == '\\')
      pstart = strstr(pstart + 1, "@{");

    if (pstart != NULL)
    {
      pstart += 2;
      pend = pstart;
      while (*pend != '\0' && *pend != '}' && *str != '\0')
        pend++;

      if (*pend == '}')
      {
        if (pend > pstart)
        {
          *var = xcalloc(pend - pstart + 1, 1);
          memcpy(*var, pstart, pend - pstart);
          return pend - str + 2;
        }
      }
    }
  }
  return 0;
}
