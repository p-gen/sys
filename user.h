#ifndef USER_H
#define USER_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

typedef struct user_s
{
  uid_t  uid;
  gid_t  gid;
  char  *name;
  char  *shell;
  size_t groups_nb;
  gid_t *groups;
  char **group_names;
  char  *hostname;
} user_t;

void
get_user_data(user_t *user_data);

#endif
