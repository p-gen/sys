#ifndef CHECK_H
#define CHECK_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

typedef enum
{
  T0 = 1,
  T1,
  T2S,
  T2M,
  TS,
  TO,
  TP,
  TI,
  TT,
  TL
} pattern_type_t;

typedef struct re_pat_s
{
  char    *name;
  regex_t *regex;
} re_pat_t;

typedef struct pattern_s
{
  char          *name;    /* Pattern name as used in the command line mask. */
  pattern_type_t type;    /*  F0, F1, ...                                   */
  int            pos;     /* pattern identification number.                 */
  int            matches; /* # of times this pattern was matched.           */
  ll_t          *inserts; /* List of words to be inserted in the final      *
                           | command line when this pattern matches a       *
                           | command line word.                             */
} pattern_t;

int
check_password(char *username);

int
has_users_groups_netgroups_param(rule_t *rule);

int
check_users_groups_netgroups(rule_t *rule, user_t *user_data);

int
check_users(rule_t *rule,
            user_t *ud,
            char   *date,
            char   *param_namei,
            int     check_date);
int
check_netgroups(rule_t *rule, user_t *ud, char *date, char *param_name);

int
check_groups(rule_t *rule, user_t *ud, char *date, char *param_name);

int
check_paths(rule_t *rule,
            char   *user_command,
            char   *param_name,
            char  **real_pathname);
void
decode_pattern(char *pattern, pattern_type_t *type, int *pos);

int
check_rule_options(rule_t *rule, int *argc, char ***argv, char **err);

int
date_has_expired(char *curr_date, char *date);

int
check_owners(rule_t *rule, char *name, char *path);

int
check_plugins(rule_t *rule, char *plugins_dir, char **failed_plugin);
#endif
