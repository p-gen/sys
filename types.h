#ifndef TYPES_H
#define TYPES_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

typedef struct elem_s
{
  char    *str;
  void    *data;
  uint16_t data_len;
} elem_t;

/* ---------------------------------------------------- */
/* a sub structure describing the variable part.        */
/* ---------------------------------------------------- */
typedef struct var_s
{
  char *name;   /* The part before the first ':'               */
  char *value;  /* The part after the first ':'                *
                  * including the trailing blanks               */
  int   global; /* 1 if the variable can be used in all conf   *
                  | file or 0 if its scope is the file in which *
                  | is is defined                               */
} var_t;

/* ------------------------------------------- */
/* a sub structure describing the command part */
/* ------------------------------------------- */
typedef struct rule_s
{
  char *tag;        /* The name of the command.         */
  int   is_valid;   /* 1: valid rule, 0:invalid rule.   */
  char *command;    /* The full content of the command. */
  char *executable; /* Just the executable part of the  *
                      | command.                         */
  ll_t *param_list; /* A linked list of parameters      */
} rule_t;

/* ------------------------------------------------------------------- */
/* a sub structure describing an element of the params_q ll_t in rule_t */
/* ------------------------------------------------------------------- */
typedef struct param_s
{
  char *name;     /* The part before the first ':'    */
  ll_t *val_list; /* A linked list of values (char *) */
} param_t;

/* ---------------------------------------------- */
/* A structure describing an environment variable */
/* ---------------------------------------------- */
typedef struct env_var_s
{
  char *name;  /* The part before the first ':' */
  char *value; /* The part after the first ':'  *
                 | including the trailing blanks */
} env_var_t;

/* --------------------------- */
/* Definition of the bool type */
/* --------------------------- */
#ifndef __bool_true_false_are_defined
#ifdef _Bool
#define bool _Bool
#else
#define bool char
#endif
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif

#endif
