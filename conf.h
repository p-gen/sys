#ifndef CONF_H
#define CONF_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#define MASK CHAR_BIT - 1
#define SHIFT ((CHAR_BIT == 8) ? 3 : (CHAR_BIT == 16) ? 4 : 8)

#define BitOff(a, x) ((void)((a)[(x) >> SHIFT] &= ~(1 << ((x)&MASK))))
#define BitOn(a, x) ((void)((a)[(x) >> SHIFT] |= (1 << ((x)&MASK))))
#define BitFlip(a, x) ((void)((a)[(x) >> SHIFT] ^= (1 << ((x)&MASK))))
#define IsBit(a, x) ((a)[(x) >> SHIFT] & (1 << ((x)&MASK)))

param_t *
get_param(rule_t *rule, char *name);

ll_t *
get_param_val_list(rule_t *rule, char *name);

int
create_data_file_list(const char *path, ll_t *file_list);

int
get_next_var_reference(char *str, int start, char **var);

void
copy_global_var(void *node, void *arg);

int
expand_var_ref(char **str);

extern ll_t  *data_list;
extern ll_t  *complete_data_list;
extern ll_t **partial_data_list;
extern int    partial_data_nb;
extern ll_t  *data_q_by_prefix;
extern char   multi_del_kw_allowed;

extern int verb_level; /* verbosity bevel */
extern int opt_quiet;  /* quiet mode toggle */

#endif
