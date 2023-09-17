#ifndef SYS_H
#define SYS_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include "ht.h"

#define SYS_CFG "sys.cfg"
#define SYS_CACHE "sys.cache"

#define SYS_CFG_DIR XQUOTE(CFG_DIR)

char *
search_in_paths(rule_t *rule, char *base, char **extra_path_array);

int
str_to_user(char *str, uid_t *uid, char **username);

int
str_to_group(char *str, gid_t *gid, char **groupname);

int
exec_command(rule_t  *rule,
             int      argc,
             char   **argv,
             char   **new_environ,
             int      daemon_flag,
             user_t  *user_data,
             ht_t    *user_pw_ok_ht,
             uid_t    new_uid,
             gid_t    new_gid,
             char    *new_username,
             char    *new_groupname,
             param_t *pwd_param,
             int      filter_on_success,
             char   **extra_path_array,
             char   **error_msg);

void
add_to_environ(void *node, void *env);

void
free_env_var(void *a);

char **
process_env(rule_t *rule, char **environ, char ***new_environ);

int
read_env_from_file(rule_t *rule, char *command_line);

void
set_umask(rule_t *rule);

int
process_rule(rule_t *rule,
             int     argc,
             char  **argv,
             char ***new_environ,
             uid_t  *new_uid,
             gid_t  *new_gid,
             char  **new_username,
             char  **new_groupname,
             int     daemon_flag,
             char  **real_pathname,
             char   *req_user,
             char   *req_groups,
             user_t *user_data,
             ht_t   *user_pw_ok_ht,
             char   *plugins_dir,
             char  **extra_path_array,
             char  **error_msg);

void
get_user_data(user_t *user_data);

void
split_command_path(char *command, char **path, char **base);

void
update_params_val_lists(rule_t *rule);

void
usage(char *progname);

#endif
