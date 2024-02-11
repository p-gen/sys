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
#include <string.h>
#include <regex.h>
#include <netdb.h>
#include <time.h>
#include <glob.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include "xmalloc.h"
#include "config.h"
#include "list.h"
#include "types.h"
#include "log.h"
#include "utils.h"
#include "bst.h"
#include "conf.h"
#include "user.h"
#include "ht.h"
#include "passwd.h"
#include "check.h"
#include "strarray.h"

extern long path_max;

static int
regex_match(rule_t     *rule,
            char      **argv,
            int         aind,
            int         pind,
            pattern_t **pattern,
            ht_t       *re_patterns_ht,
            char      **err);

static int
text_match(rule_t     *rule,
           char      **argv,
           int         aind,
           int         pind,
           pattern_t **pattern,
           ht_t       *re_patterns_ht,
           char      **err);

static int
pos_match(rule_t     *rule,
          char      **argv,
          int         aind,
          int         pind,
          pattern_t **pattern,
          ht_t       *re_patterns_ht,
          char      **err);

static int max_pos = 0;

/* =================================================================== */
/* If the rule has a password parameter ask the target user's password */
/* returns 1 if the password is valid else 0.                          */
/* =================================================================== */
int
check_password(char *username)
{
  int valid = 0;

  if (is_in_foreground_process_group())
  {
    printf("An authorization request for \"%s\" is required.\n", username);

#if defined(HAVE_LIBPAM) && PAM_ENABLED == 1
    valid = check_password_pam(username);
#else
    valid = check_password_files(username);
#endif
  }

  return valid;
}

/* ================================================= */
/* Try to open a plugin and lanch its main function. */
/* ================================================= */
int
check_plugin(rule_t *rule, char *plugins_dir, param_t *param)
{
  int rc = 0;

  void *handle;

  char *plugin_name; /* Plugin name, (first value of a plugin parameter. */
  char *path = NULL; /* plugin full path. */
  char *version;     /* Plugin version. */
  char *output;      /* Plugin output or NULL. */

  ll_t      *val_list = param->val_list;
  ll_node_t *node     = val_list->head;

  /* Prototypes for the plugin function. */
  /* """"""""""""""""""""""""""""""""""" */
  char *(*f_version)(void);
  int (*f_main)(int, char **, char **);

  /* the rest of the plugin parameter's arguments. */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  char **args;

  if (node)
  {
    args    = xmalloc(val_list->len * sizeof(char *));
    args[0] = NULL;

    plugin_name = (char *)node->data;
    path        = xmalloc(strlen(plugins_dir) + strlen(plugin_name) + 1 + 1);
    path        = strcpy(path, plugins_dir);
    path        = strcat(path, "/");
    path        = strcat(path, plugin_name);
    node        = node->next;

    if (val_list->len > 1)
    {
      int i = 0;
      while (node)
      {
        args[i++] = (char *)node->data;
        node      = node->next;
      }
      args[i] = NULL;
    }
  }
  else
  {
    trace(LOG_DATA, "[%s] missing plugin name.", param->name);
    return 0;
  }

  if (path == NULL)
  {
    trace(LOG_DATA, "[%s] plugin \"%s\" not found.", param->name, plugin_name);
    free(args);
    return 0;
  }

  /* Try to get and the plugin functions. */
  /* """""""""""""""""""""""""""""""""""" */
  handle = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
  if (handle == NULL)
  {
    trace(LOG_WARN, "Could not open plugin: %s.", dlerror());
    free(args);
    return 0;
  }

  f_version = dlsym(handle, "sys_plugin_version"); /* optional function. */

  f_main = dlsym(handle, "sys_plugin_main");
  if (f_main == NULL)
  {
    trace(LOG_WARN, "Could not find plugin main function: %s.", dlerror());
    free(args);
    return 0;
  }

  version = NULL;
  if (f_version)
    version = f_version();

  rc = f_main(val_list->len - 1, args, &output);
  if (output != NULL)
  {
    if (version)
      trace(LOG_INFO,
            "[%s/%s] plugin: %s (%s) output: \"%s\"",
            rule->tag,
            param->name + 1,
            plugin_name,
            version,
            output);
    else
      trace(LOG_INFO,
            "[%s/%s] plugin: %s output: \"%s\"",
            rule->tag,
            param->name + 1,
            plugin_name,
            output);

    free(output); /* clean-up. */
  }

  if (dlclose(handle) != 0)
  {
    trace(LOG_WARN, "Could not close plugin: %s.", dlerror());
    free(args); /* clean-up. */
    return 0;
  }

  free(args); /* clean-up. */
  return rc;
}

/* ================================================================== */
/* In case the rule has plugin parameter, try to manage them.         */
/* Each plugin must return 1 in case of success.                      */
/* The name of the first failing plugin in returned in failed_plugin. */
/* ================================================================== */
int
check_plugins(rule_t *rule, char *plugins_dir, char **failed_plugin)
{
  int        rc = 1;
  ll_t      *param_list;
  ll_t      *val_list;
  ll_node_t *node;
  param_t   *param;

  *failed_plugin = NULL;
  param_list     = rule->param_list;

  if (param_list)
  {
    node = param_list->head;
    while (node)
    {
      param = (param_t *)node->data;

      if (*(param->name) == '%')
#ifdef PLUGINS_ENABLED
        if (!check_plugin(rule, plugins_dir, param))
        {
          *failed_plugin = param->name + 1;
          rc             = 0;
          break;
        }
#else
        trace(LOG_WARN,
              "[%s] plugins not enabled, parameter \"%s\" ignored.",
              rule->tag,
              param->name);
#endif
      node = node->next;
    }
  }

  return rc;
}

/* ================================================================== */
/* Return 1 if at least one of the following parameter exists and has */
/* at least one value, else return 0.                                 */
/* ================================================================== */
int
has_users_groups_netgroups_param(rule_t *rule)
{

  char *params[] = { "!users",     "users",     "!groups", "groups",
                     "!netgroups", "netgroups", NULL };

  char **p = params;

  while (*p != NULL)
  {
    ll_t *val_list;

    val_list = get_param_val_list(rule, *p);
    if (val_list != NULL && val_list->len > 0)
      return 1;

    p++;
  }
  return 0;
}

/* =========================================================== */
/* Check if a rule passes the filters for users/group/netgroups */
/* returns 1 if so else 0.                                     */
/* =========================================================== */
int
check_users_groups_netgroups(rule_t *rule, user_t *user_data)
{
  time_t now = time(NULL);
  char   curr_date[13];

  int nu, ng, nnetg;
  int u, g, netg;
  int denied, allowed;

  if (!has_users_groups_netgroups_param(rule))
    return 1;

  /* Get the current date as a YYYYMMDDhhmm string. */
  /* """""""""""""""""""""""""""""""""""""""""""""" */
  strftime(curr_date, 13, "%Y%m%d%H%M", localtime(&now));

  nu    = check_users(rule, user_data, curr_date, "!users", 0);
  ng    = check_groups(rule, user_data, curr_date, "!groups");
  nnetg = check_netgroups(rule, user_data, curr_date, "!netgroups");

  denied = nu || ng || nnetg;

  u    = check_users(rule, user_data, curr_date, "users", 1);
  g    = check_groups(rule, user_data, curr_date, "groups");
  netg = check_netgroups(rule, user_data, curr_date, "netgroups");

  allowed = u || g || netg;

  if (denied)
    return 0;

  if (allowed)
    return 1;

  return 0;
}

int
check_users(rule_t *rule,
            user_t *ud,
            char   *date,
            char   *param_name,
            int     check_date)
{
  regex_t regex_user;
  regex_t regex_host;

  ll_t      *val_list;
  ll_node_t *node;
  int        found;

  found = 0;

  /* Get the list of regular expression containing user restrictions. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  val_list = get_param_val_list(rule, param_name);
  if (val_list && val_list->len == 0)
  {
    trace(LOG_WARN,
          "users: empty, all users will be %s.",
          (*param_name == '!') ? "accepted" : "denied");
    if (*param_name == '!')
      return 1;
    else
      return 0;
  }

  if (val_list)
  {
    char *delim1, *delim2;

    node = val_list->head;

    while (node)
    {
      char *val;
      char *user;
      char *host;
      char *expiration;
      int   rc1;
      int   rc2;

      host = expiration = NULL;
      rc1 = rc2 = 0;

      val = (char *)node->data;

      /* Extracts the user and host from the value string */
      /* """""""""""""""""""""""""""""""""""""""""""""""" */
      user = xstrdup(val);

      delim1 = strchr(user, '@');
      if (delim1)
        *delim1 = '\0';

      delim2 = strrchr(user, '/');
      if (delim2)
      {
        *delim2    = '\0';
        expiration = delim2 + 1;
      }

      if (delim1 && *(delim1 + 1) != '\0')
        host = delim1 + 1;

      /* build the regular expression to match the user */
      /* """""""""""""""""""""""""""""""""""""""""""""" */
      char *str;
      str = xmalloc(strlen(user) + 3);

      /* make sure the RE does not match a word in the */
      /* middle of a string for security reason.       */
      /* """"""""""""""""""""""""""""""""""""""""""""" */
      strcpy(str, "^");
      strcat(str, user);
      strcat(str, "$");

      rc1 = regcomp(&regex_user, str, REG_EXTENDED | REG_NOSUB);
      if (rc1 != 0)
      {
        trace(LOG_DATA,
              "[%s] user: invalid regular expression \"%s\"",
              rule->tag,
              user);
      }

      free(str);

      /* build the regular expression to match the host if any */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
      if (host)
      {
        str = xmalloc(strlen(host) + 3);

        /* make sure the RE does not match a word in the */
        /* middle of a string for security reason.       */
        /* """"""""""""""""""""""""""""""""""""""""""""" */
        strcpy(str, "^");
        strcat(str, host);
        strcat(str, "$");

        rc2 = regcomp(&regex_host, str, REG_EXTENDED | REG_NOSUB);
        if (rc2 != 0)
        {
          trace(LOG_DATA,
                "[%s] host: invalid regular expression \"%s\"",
                rule->tag,
                host);
        }

        free(str);
      }

      /* If the regular expressions were correct, then see if the real user */
      /* and hostname match them                                            */
      /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      if (rc1 == 0 && rc2 == 0)
      {
        if (regexec(&regex_user, ud->name, 0, NULL, 0) == 0)
        {
          int expired = check_date ? date_has_expired(date, expiration) : 0;

          if (expired)
          {
            regfree(&regex_user);
            node = node->next;
            continue;
          }

          if (host && regexec(&regex_host, ud->hostname, 0, NULL, 0))
          {
            regfree(&regex_user);
            regfree(&regex_host);
            node = node->next;
            continue;
          }

          found = 1;
          break;
        }
        regfree(&regex_user);
      }
      else
        trace(LOG_DATA,
              "[%s] invalid regular expression \"%s\" or \"%s\".",
              rule->tag,
              user,
              host);

      node = node->next;
    }
  }

  return found;
}

int
check_netgroups(rule_t *rule, user_t *ud, char *date, char *param_name)
{
  ll_t      *val_list;
  ll_node_t *node;
  char      *host       = NULL;
  char      *expiration = NULL;
  char      *delim1;
  char      *delim2;
  int        found;

#ifndef HAVE_INNETGR
  trace(LOG_DATA, "netgroups are not allowed in this system.") return 0
#endif

    found = 0;

  /* Get the list of regular expression containing netgroups restrictions. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  val_list = get_param_val_list(rule, param_name);
  if (val_list && val_list->len == 0)
  {
    trace(LOG_WARN,
          "netgroups: empty, all netgroups will be %s.",
          (*param_name == '!') ? "accepted" : "denied");
    if (*param_name == '!')
      return 1;
    else
      return 0;
  }

  if (val_list)
  {
    node = val_list->head;

    while (node)
    {
      char *val;
      char *netgroup;

      val = (char *)node->data;

      /* Extracts the netgroup and host from the value string */
      /* """""""""""""""""""""""""""""""""""""""""""""""""""" */
      netgroup = xstrdup(val);

      delim1 = strchr(netgroup, '@');
      if (delim1)
        *delim1 = '\0';

      delim2 = strrchr(netgroup, '/');
      if (delim2)
      {
        *delim2    = '\0';
        expiration = delim2 + 1;
      }

      if (delim1 && *(delim1 + 1) != '\0')
        host = delim1 + 1;

      /* The netgroups in this list are not regular expressions */
      /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
      if (innetgr(val, ud->hostname, ud->name, NULL))
      {
        if (host && strcmp(host, ud->hostname) != 0)
        {
          trace(LOG_WARN,
                "netgroups: \"%s\" is denied on this hostname.",
                netgroup);
          node = node->next;
          continue;
        }

        if (date_has_expired(date, expiration))
        {
          trace(LOG_WARN, "netgroups: \"%s\" validity has expired.", netgroup);
          node = node->next;
          continue;
        }

        found = 1;
        break;
      }

      node = node->next;
    }
  }

  return found;
}

int
check_groups(rule_t *rule, user_t *ud, char *date, char *param_name)
{
  unsigned  r, g, regex_nb;
  regex_t **group_regex;
  regex_t **host_regex;

  ll_t      *val_list;
  ll_node_t *node;
  char      *group;
  char      *host       = NULL;
  char      *expiration = NULL;
  char      *val;
  int        found;
  int        rc1;
  int        rc2;

  found = 0;

  /* Get the list of regular expression containing groups restrictions. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  val_list = get_param_val_list(rule, param_name);
  if (val_list && val_list->len == 0)
  {
    trace(LOG_WARN,
          "groups: empty, all groups will be %s.",
          (*param_name == '!') ? "denied" : "accepted");
    if (*param_name == '!')
      return 1;
    else
      return 0;
  }

  if (val_list)
  {
    char *delim1, *delim2;

    /* Array of regex which will contain compiled regular expression */
    /* from the given groups.                                        */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    group_regex = xmalloc((val_list->len) * sizeof(regex_t *));
    host_regex  = xmalloc((val_list->len) * sizeof(regex_t *));

    /* Ensure that all the regex slot in the arrays are NULL. */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
    for (r = 0; r < val_list->len; r++)
    {
      group_regex[r] = NULL;
      host_regex[r]  = NULL;
    }

    /* Initialize the regex production counter */
    /* """"""""""""""""""""""""""""""""""""""" */
    regex_nb = 0;

    for (g = 0; g < ud->groups_nb; g++)
    {
      rc1 = 0;
      rc2 = 0;

      r = 0;

      node = val_list->head;

      while (node)
      {
        val = (char *)node->data;

        /* Extracts the user and host from the value string */
        /* """""""""""""""""""""""""""""""""""""""""""""""" */
        group = xstrdup(val);

        delim1 = strchr(group, '@');
        if (delim1)
          *delim1 = '\0';

        delim2 = strrchr(group, '/');
        if (delim2)
        {
          *delim2    = '\0';
          expiration = delim2 + 1;
        }

        if (delim1 && *(delim1 + 1) != '\0')
          host = delim1 + 1;

        /* The regex compilation if only done once          */
        /* (when its place in the array still equates NULL) */
        /* """""""""""""""""""""""""""""""""""""""""""""""" */
        char *str;
        if (group_regex[r] == NULL)
        {
          str = xmalloc(strlen(group) + 3);

          /* make sure the RE does not match a word in the */
          /* middle of a string for security reason.       */
          /* """"""""""""""""""""""""""""""""""""""""""""" */
          strcpy(str, "^");
          strcat(str, group);
          strcat(str, "$");

          group_regex[r] = xmalloc(sizeof(regex_t));
          rc1 = regcomp(group_regex[r], str, REG_EXTENDED | REG_NOSUB);
          if (rc1 != 0)
          {
            trace(LOG_DATA,
                  "[%s] group: invalid regular expression \"%s\"",
                  rule->tag,
                  group);
          }

          free(str);
        }

        if (host)
        {
          if (host_regex[r] == NULL)
          {
            str = xmalloc(strlen(host) + 3);

            /* make sure the RE does not match a word in the */
            /* middle of a string for security reason.       */
            /* """"""""""""""""""""""""""""""""""""""""""""" */
            strcpy(str, "^");
            strcat(str, host);
            strcat(str, "$");

            host_regex[r] = xmalloc(sizeof(regex_t));
            rc2 = regcomp(host_regex[r], str, REG_EXTENDED | REG_NOSUB);
            if (rc2 != 0)
            {
              trace(LOG_DATA,
                    "[%s] host: invalid regular expression \"%s\"",
                    rule->tag,
                    host);
            }

            free(str);
          }
        }

        /* Update the regex production counter if needed */
        /* """"""""""""""""""""""""""""""""""""""""""""" */
        if (r >= regex_nb)
          regex_nb++;

        if (rc1 == 0 && rc2 == 0)

        /* Check if one of the group in restrictions match the user's  */
        /* group of the current iteration or if its validity date has  */
        /* expired.                                                    */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        {
          if (regexec(group_regex[r], ud->group_names[g], 0, NULL, 0) == 0)
          {
            if (date_has_expired(date, expiration)
                || (host_regex[r]
                    && regexec(host_regex[r], ud->hostname, 0, NULL, 0) != 0))
            {
              r++;
              trace(LOG_WARN,
                    "groups: \"%s\" is denied on this hostname "
                    "or its validity has expired.",
                    ud->group_names[g]);
              node = node->next;
              continue;
            }
            found = 1;
            goto success;
          }
        }
        else
          trace(LOG_DATA,
                "[%s] invalid regular expression \"%s\".",
                rule->tag,
                ud->group_names[g]);

        r++; /* next group in restrictions. */

        node = node->next;
      }
    }

  success:

    /* Free the produced regex */
    /* """"""""""""""""""""""" */
    for (r = 0; r < regex_nb; r++)
    {
      regfree(group_regex[r]);
      free(group_regex[r]);
    }
    free(group_regex);
  }

  return found;
}

/* =================================================================== */
/* Manage the different cases when the name of the operation passed is */
/* interpreted as an executables.                                      */
/* The possible path in the executable if taken into account.          */
/* =================================================================== */
int
check_paths(rule_t *rule, char *user_command, char *param_name, char *rule_tag)

{
  ll_t      *val_list;
  ll_node_t *node;

  char *path_pattern;
  char *command_path; /* The directory part (before last /) or NULL.       */
  char *command_base; /* the base part (after the last / if any).          */
  char *slash;        /* The address of the last / if any in user_command. */
  char *command;      /* A work copy of user_command.                      */
  char *tmp_rule_tag = NULL; /* temp variable used if we must override     *
                              | the executable path when rule_tag has a    *
                              | path.                                      */

  enum
  {
    NOT_FOUND = 0,
    FOUND
  };

  /* ---------------------------------------------------------------------- */
  /* 1 command has path    + paths param present -> check coherency         */
  /* 2 command hasn't path + paths param present -> cannot check, accept if */
  /*                                                paths deny if !path     */
  /* 3 command has path    + param not present   -> cannot check, accept if */
  /*                                                paths deny if !path     */
  /* 4 command hasn't path + param not present   -> same as 3.              */
  /* 5 param present       + no paths given      -> same as 3.              */
  /* ---------------------------------------------------------------------- */

  path_max = pathconf("/", _PC_PATH_MAX);
  if (path_max == -1)
    path_max = 4096;
  else
    path_max++; /* Relative to /. */

  /* Get the executable name preceded or not by its path. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""" */
  command                          = xstrdup(rule->executable);
  command[strcspn(command, " \t")] = '\0';

  /* Extract the path from executable if any. */
  /* """""""""""""""""""""""""""""""""""""""" */
  slash = strrchr(command, '/');
  if (slash != NULL)
  {
    *slash       = '\0';
    command_path = realpath(command, NULL);
    command_base = slash + 1;

    if (command_path == NULL)
      error("%s: invalid path.", command);
  }
  else
  {
    command_path = NULL;
    command_base = command;
  }

  /* Special processing if the CLI tag parameter have a path:             */
  /* This path must match the command path is any and if so this path     */
  /* will be compared with the paths in the "paths" parameter if present. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  tmp_rule_tag = xstrdup(rule_tag);
  slash        = strrchr(tmp_rule_tag, '/');
  if (slash != NULL)
  {
    char *tmp_rule_tag_path;

    *slash            = '\0';
    tmp_rule_tag_path = realpath(tmp_rule_tag, NULL);

    /* Fails if the real path is not valid without exiting the program */
    /* as this case will be checked later.                             */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (tmp_rule_tag_path == NULL)
    {
      free(command);
      free(tmp_rule_tag);

      if (*param_name == '!') /* !paths: */
        return FOUND;
      else /* paths: */
        return NOT_FOUND;
    }

    if (command_path != NULL && strcmp(command_path, tmp_rule_tag_path) != 0)
      error("%s must be the same as the command path (%s).",
            tmp_rule_tag_path,
            command_path);
    else
      command_path = tmp_rule_tag_path;
  }

  /* Extract the list of the values of param_name. */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  val_list = get_param_val_list(rule, param_name);

  if (val_list && val_list->len == 0)
  {
    free(command); /* 5 */
    free(tmp_rule_tag);

    return FOUND;
  }

  if (val_list) /* 1 or 2 */
  {
    if (command_path != NULL) /* 1 */
    {
      node = val_list->head;
      while (node)
      {
        glob_t g;

        path_pattern = (char *)node->data;

        if (glob(path_pattern, 0, NULL, &g) == 0)
        {
          int c;

          /* Try each of the expanded names found. */
          /* ''''''''''''''''''''''''''''''''''''' */
          for (c = 0; c < g.gl_pathc; c++)
          {

            if (strcmp(command_path, g.gl_pathv[c]) == 0)
            {
              globfree(&g);
              goto found1; /* Found a match. */
            }
          }
        }

        globfree(&g);
        node = node->next;
      }

    found1:

      /* When all paths have been checked... */
      /* """"""""""""""""""""""""""""""""""" */
      free(command);
      free(tmp_rule_tag);

      if (node == NULL)
        return NOT_FOUND;
      else
        return FOUND;
    }
    else /* 2 */
    {
      size_t len       = strlen(command_base);
      char  *curr_path = calloc(1, path_max);

      node = val_list->head;
      while (node)
      {
        glob_t g;

        path_pattern = (char *)node->data;
        if (strlen(path_pattern) + len + 2 > path_max)
          continue;

        if (glob(path_pattern, 0, NULL, &g) == 0)
        {
          int c;

          for (c = 0; c < g.gl_pathc; c++)
          {

            strncpy(curr_path, g.gl_pathv[c], path_max - 2);
            curr_path = strcat(curr_path, "/");
            curr_path = strcat(curr_path, command_base);

            if (file_exists(curr_path))
            {
              globfree(&g);
              goto found2; /* Exit from the loop, if curr_path exists. */
            }
          }
        }

        globfree(&g);
        node = node->next;
      }

    found2:

      /* When all the paths have been checked... */
      /* """"""""""""""""""""""""""""""""""""""" */
      free(curr_path);
      free(command);
      free(tmp_rule_tag);

      if (node == NULL)
        return NOT_FOUND;
      else
        return FOUND;
    }
  }
  else /* 3 or 4 */
  {
    free(command);
    free(tmp_rule_tag);

    if (*param_name == '!') /* !paths: */
      return NOT_FOUND;
    else /* paths: */
      return FOUND;
  }
}

/* ========================================================================= */
/* Utility function to parse a pattern element for use in check_rule_options */
/*                                                                           */
/* pattern: (in/out) textual representation on the pattern ($* per ex)       */
/* type:    (out)    pattern type (enum). Defaults to TI                     */
/* pos:     (out)    pattern suffix to enable the user to set restrictions   */
/*                   on different instances of pattern with the same type    */
/*                   Defaults to -1                                          */
/* Invalid patterns are considered as type TI patters.                       */
/* ========================================================================= */
void
decode_pattern(char *pattern, pattern_type_t *type, int *pos)
{
  *pos = -1;
  char c;

  if (strcmp(pattern, "$*") == 0 || sscanf(pattern, "$*%d%c", pos, &c) == 1)
    *type = T0;
  else if (strcmp(pattern, "$+") == 0
           || (sscanf(pattern, "$+%d%c", pos, &c) == 1 && *pos > 0))
    *type = T1;
  else if (strcmp(pattern, "$,") == 0
           || (sscanf(pattern, "$,%d%c", pos, &c) == 1 && *pos > 0))
    *type = T2S;
  else if (strcmp(pattern, "$;") == 0
           || (sscanf(pattern, "$;%d%c", pos, &c) == 1 && *pos > 0))
    *type = T2M;
  else if (strcmp(pattern, "$.") == 0
           || (sscanf(pattern, "$.%d%c", pos, &c) == 1 && *pos > 0))
    *type = TS;
  else if (strcmp(pattern, "$?") == 0
           || (sscanf(pattern, "$?%d%c", pos, &c) == 1 && *pos > 0))
    *type = TO;
  else if (sscanf(pattern, "$%d%c", pos, &c) == 1 && *pos > 0)
    *type = TP;
  else if (*pattern == '^')
    *type = TT;
  else
    *type = TI;
}

/* ===================================================================== */
/* Store the parameters recognized as constraints for the command line   */
/* arguments in a hash table.                                            */
/* To do that, iterates in the parameter's list of lists in the command. */
/* rule:            (in)  the rule we are checking.                      */
/* re_patterns_ht: (out) the destination hash table.                     */
/* ===================================================================== */
static void
store_patterns_regex_list(rule_t *rule, ht_t *re_patterns_ht)
{
  ll_t          *param_list = rule->param_list;
  ll_node_t     *node       = param_list->head;
  ll_node_t     *val_node;
  ll_t          *re_pat_list;
  param_t       *par;
  regex_t       *regex;
  re_pat_t      *re;
  pattern_type_t type;
  int            pos = -1;
  char          *str;

  while (node) /* For each list of parameters associated with the command. */
  {
    char *par_name;

    /* Get the name of the parameter. */
    /* """""""""""""""""""""""""""""" */
    par      = (param_t *)node->data;
    par_name = par->name;

    /* Skip the negation part of the parameter in order to recognize it. */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (*par_name == '!')
      par_name++;

    /* Decode the pattern type and if its is a constraint for the  */
    /* command line arguments, create a list of the values of this */
    /* parameter and insert it in the hash table.                  */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    decode_pattern(par_name, &type, &pos);

    if (type == T0 || type == T1 || type == T2S || type == T2M || type == TS
        || type == TP || type == TO)
      if (par->val_list != NULL)
      {
        re_pat_list = ll_new();
        val_node    = par->val_list->head;

        while (val_node)
        {
          regex = xmalloc(sizeof(regex_t));
          str   = xmalloc(strlen((char *)val_node->data) + 3);

          /* make sure the RE does not match a word in the */
          /* middle of a string for security reason.       */
          /* """"""""""""""""""""""""""""""""""""""""""""" */
          strcpy(str, "^");
          strcat(str, (char *)val_node->data);
          strcat(str, "$");

          if (regcomp(regex, str, REG_EXTENDED | REG_NOSUB) != 0)
          {
            trace(LOG_DATA,
                  "{%s] invalid regular expression \"%s\".",
                  rule->tag,
                  (char *)val_node->data);

            free(regex);
            free(str);

            val_node = val_node->next;
            continue;
          }

          free(str);

          re        = xmalloc(sizeof(re_pat_t));
          re->name  = (char *)val_node->data;
          re->regex = regex;

          ll_append(re_pat_list, re);
          val_node = val_node->next;
        }

        if (*(par->name) == '!')
          ht_insert(re_patterns_ht, par->name, re_pat_list);
        else
          ht_insert(re_patterns_ht, par_name, re_pat_list);
      }

    node = node->next;
  }
}

/* ==================================================================== */
/* Different pattern matching function according to the current pattern */
/* type.                                                                */
/* ==================================================================== */
static int
pattern_match(rule_t     *rule,
              char      **argv,
              int         aind,
              int         pind, /* For debugging purpose. */
              pattern_t **pattern,
              ht_t       *re_patterns_ht,
              char      **err) /* For debugging purpose. */
{
  switch (pattern[pind]->type)
  {
    case T0:
    case T1:
    case T2S:
    case T2M:
    case TO:
    case TS:
      return regex_match(rule, argv, aind, pind, pattern, re_patterns_ht, err);
      break;

    case TT:
      return text_match(rule, argv, aind, pind, pattern, re_patterns_ht, err);
      break;

    case TP:
      return pos_match(rule, argv, aind, pind, pattern, re_patterns_ht, err);
      break;

    default:
      return 0;
  }
}

/* ============================================================== */
/* Utility function to manage a pattern of type T0, T1, T2S, T2M. */
/* RC: 0 if argv[aind] matches a deny pattern of does not match   */
/*       an accept pattern.                                       */
/*     1 if argv[aind] does not match a deny pattern and matches  */
/*       an accept pattern.                                       */
/* ============================================================== */
static int
regex_match(rule_t     *rule,
            char      **argv,
            int         aind,
            int         pind, /* For debugging purpose. */
            pattern_t **pattern,
            ht_t       *re_patterns_ht,
            char      **err) /* For debugging purpose. */
{
  ll_node_t *re_pat_ll_node;
  ll_t      *pattern_list;
  ll_t      *arg_re_deny_list;
  ll_t      *arg_re_accept_list;
  regex_t   *regex;
  int        accept;
  char      *pattern_name; /* Temporary variable to hold search a *
                            | pattern in the accept or deny list. */

  /* Look for deny/accept regex series for the current pattern */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  arg_re_deny_list   = NULL;
  arg_re_accept_list = NULL;

  pattern_name = xmalloc(strlen(pattern[pind]->name) + 2);
  strcpy(pattern_name, "!");
  strcat(pattern_name, pattern[pind]->name);

  arg_re_deny_list = ht_search(re_patterns_ht, pattern_name);

  /* The "+ 1" point after the first ! in the following expression. */
  arg_re_accept_list = ht_search(re_patterns_ht, pattern_name + 1);

  free(pattern_name);

  /* Follow the regex deny lists and exit with a fatal error if we have. */
  /* a matching argument.                                                */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  pattern_list = arg_re_deny_list;

  if (pattern_list && pattern_list->len)
  {
    re_pat_ll_node = pattern_list->head;
    while (re_pat_ll_node)
    {
      regex = ((re_pat_t *)(ll_node_t *)re_pat_ll_node->data)->regex;

      if (regex && regexec(regex, argv[aind], 0, NULL, 0) == 0)
        return 0;

      re_pat_ll_node = re_pat_ll_node->next;
    }
  }

  /* If the argument did not match any deny regex, then follow the accept  */
  /* regex lists to see if it match at least one.                          */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  pattern_list = arg_re_accept_list;

  if (pattern_list && pattern_list->len)
  {
    accept = 0;

    re_pat_ll_node = pattern_list->head;
    while (re_pat_ll_node)
    {
      regex = ((re_pat_t *)(ll_node_t *)re_pat_ll_node->data)->regex;

      if (regex && regexec(regex, argv[aind], 0, NULL, 0) == 0)
      {
        accept = 1;
        break; /* we have a match */
      }

      re_pat_ll_node = re_pat_ll_node->next;
    }
    if (re_pat_ll_node != NULL)
      accept = 1;
  }
  else /* no or empty accept list => accept everything */
    accept = 1;

  /* Increment the matching score of this pattern. */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  if (accept)
    pattern[pind]->matches++;

  /* Returns true if the argument passed the regex filters */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
  return accept;
}

/* ============================================================== */
/* Utility function to manage a pattern of type TP.               */
/* RC: 0 if argv[aind] matches a deny pattern of does not match   */
/*       an accept pattern.                                       */
/*     1 if argv[aind] does not match a deny pattern and matches  */
/*       an accept pattern.                                       */
/* ============================================================== */
static int
pos_match(rule_t     *rule,
          char      **argv,
          int         aind,
          int         pind, /* For debugging purpose. */
          pattern_t **pattern,
          ht_t       *re_patterns_ht,
          char      **err) /* For debugging purpose. */
{

  int pos    = 0;
  int accept = 0;

  sscanf(pattern[pind]->name, "$%d", &pos);

  /* The positional argument must be ordered in ascending order */
  /* $1 designates the first argument after the rule            */
  /* $n designates the argument of index n after the rule       */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (aind == pos + optind)
  {
    if (pos <= max_pos)
    {
      trace(LOG_DATA,
            "[%s] positional arguments must be in ascending order.",
            rule->tag);
      *err = xstrdup("invalid command specification, contact your sys admin.");
    }
    else
      /* trivial checks */
      /* """""""""""""" */
      if (pos + optind < aind - 1)
        *err = mkerr("$%d has already be used.", pos);
      else
      {
        accept =
          regex_match(rule, argv, aind, pind, pattern, re_patterns_ht, err);
        if (!accept)
          *err = mkerr("%s does not allow the argument \"%s\".",
                       pattern[pind]->name,
                       argv[aind]);
      }
  }

  if (accept)
    pattern[pind]->matches = 1;

  return accept;
}

/* ============================================================== */
/* Utility function to manage a pattern of type TT.               */
/* RC: 0 if argv[aind] matches a deny pattern of does not match   */
/*       an accept pattern.                                       */
/*     1 if argv[aind] does not match a deny pattern and matches  */
/*       an accept pattern.                                       */
/* ============================================================== */
static int
text_match(rule_t     *rule,
           char      **argv,
           int         aind,
           int         pind, /* For debugging purpose. */
           pattern_t **pattern,
           ht_t       *re_patterns_ht,
           char      **err) /* For debugging purpose. */
{
  int accept;

  accept = (strcmp(pattern[pind]->name + 1, argv[aind]) == 0);

  if (accept)
    pattern[pind]->matches = 1;

  return accept;
}

/* ================================================================ */
/* Insert the accumulated inserts stored in the inserts list on the */
/* current pattern.                                                 */
/* The process is destructive ans leaves the list empty.            */
/* ================================================================ */
int
do_inserts(int *argc, char ***argv, int aind, int pind, pattern_t **pattern)
{
  if (pattern[pind]->inserts != NULL)
  {
    ll_node_t *node = pattern[pind]->inserts->head;
    ll_node_t *nodenext;

    while (node)
    {
      *argv = xrealloc(*argv, (*argc + 1) * sizeof(char *));
      memmove(*argv + aind + 1, *argv + aind, (*argc - aind) * sizeof(char *));
      (*argc)++;
      (*argv)[aind] = node->data;
      aind++;
      nodenext = node->next;
      ll_delete(pattern[pind]->inserts, node);

      node = nodenext;
    }
  }
  return aind;
}

/* ======================================================================== */
/* Parse the command line patterns to check is the user's passed arguments  */
/* match then                                                               */
/* Patterns can be:                                                         */
/* word which match a word whose first letter is not a '$'                  */
/*      If a word pattern is not present in the command line it will be     */
/*      inserted in it and will be processed as soon as possible.           */
/* ^word match the word after the ^ exactly.                                */
/* $n   (TP pattern) which match the argument at the position n             */
/*      (n starting at 1)                                                   */
/* $*   match a consecutive suite of arguments until one of these condition */
/*      is true:                                                            */
/*      - there is no more arguments                                        */
/*      - The next pattern (-if any) matches the same argument.             */
/*      - the position in the command line has been reached given by a      */
/* $+   Same as $* by must macth at least one argument.                     */
/* $*n  is the same as $* but can occur multiple times (with a same or      */
/*      different n)                                                        */
/* $+n  similar to $*n with the same restriction.                           */
/*                                                                          */
/* Example:                                                                 */
/* if these pattern list:  $*1 ^c $*2 $6 $*                                 */
/* will match: a b c d e f g h i                                            */
/* with:                                                                    */
/* $*1:a,b                                                                  */
/* $*2:d,e                                                                  */
/* $6:f                                                                     */
/* $*:g,h,i                                                                 */
/*                                                                          */
/* rule   (in)  rule to check                                               */
/* argc  (out) will contain the resulting number of arguments.              */
/* argv  (out) will contain the resulting arguments.                        */
/*                                                                          */
/* Returns 1 if OK else 0                                                   */
/* ======================================================================== */
int
check_rule_options(rule_t *rule, int *argc, char ***argv, char **err)
{
  char **pattern_a; /* Array of pattern strings containing each  *
                      | words in the rule parameter.               */
  int    count;     /* number of strings in the previous array.  */

  pattern_t **pattern;    /* Array of significant patterns found in  *
                           | pattern_a.                              */
  int         pattern_nb; /* number of patters in the pattern array. */

  int i;          /* Generic integer index. */
  int aind, pind; /* Command line word index and pattern index. */

  unsigned accept  = 0;    /* 1: the checking can continue.           */
  unsigned matched = 0;    /* # of matches for a current pattern.     */
  ht_t     re_patterns_ht; /* mapping pattern string -> RE list.      */

  int same_pattern = 0;

  pattern_type_t pattern_type;
  int            pos;

  ll_t *inserts_list = NULL;

  *err = NULL;

  /* Split the pattern line to build an array of patterns     */
  /* "pattern1 pattern2 ..." -> pattern_a[1] pattern_a[2] ... */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (strarray(rule->command, &pattern_a, &count, " \t", TRIM | MERGESEPS) != 0)
  {
    *err = xstrdup("syntax error in pattern strings");
    return 0;
  }

  pattern    = xmalloc((count) * sizeof(pattern_t *));
  pattern_nb = 0;

  /* Build the pattern array of structures from the array of pattern_a */
  /* strings, removing consecutive redundancies.                       */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  for (i = 0; i < count - 1; i++)
  {
    /* Extract the array type and id from the string. */
    /* '''''''''''''''''''''''''''''''''''''''''''''' */
    decode_pattern(pattern_a[i + 1], &pattern_type, &pos);

    if (pattern_type == TI)
    {
      /* creates or appends the insert pattern to a list of inserts. */
      /* This list will be stored in the next non insert pattern or  */
      /* in the last dummy pattern.                                  */
      /*                                                             */
      /* Note that the TI patterns will not be stored stored in the  */
      /* pattern array directly but in the insert_list of then as    */
      /* needed.                                                     */
      /* ''''''''''''''''''''''''''''''''''''''''''''''''''''''''''' */
      if (inserts_list == NULL)
        inserts_list = ll_new();
      ll_append(inserts_list, xstrdup(pattern_a[i + 1]));
    }
    else
    {
      /* Skip duplicated consecutive multiples words patterns. */
      /* ''''''''''''''''''''''''''''''''''''''''''''''''''''' */
      if (pattern_nb > 0
          && (pattern_type == T0 || pattern_type == T1 || pattern_type == T2S
              || pattern_type == T2M)
          && (pattern_type == pattern[pattern_nb - 1]->type
              && pos == pattern[pattern_nb - 1]->pos))
        continue;

      /* Store the newly crated pattern */
      /* '''''''''''''''''''''''''''''' */
      pattern[pattern_nb]          = xmalloc(sizeof(pattern_t));
      pattern[pattern_nb]->name    = pattern_a[i + 1];
      pattern[pattern_nb]->pos     = pos;
      pattern[pattern_nb]->type    = pattern_type;
      pattern[pattern_nb]->matches = 0;
      pattern[pattern_nb]->inserts = inserts_list;
      inserts_list = NULL; /* Forces the allocation of a new inserts list. *
                            | See above.                                   */

      pattern_nb++;
    }
  }

  /* Initialize the dummy last pattern.                        */
  /* This pattern exists mainly to hold the last inserts_list. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  pattern[pattern_nb]          = xmalloc(sizeof(pattern_t));
  pattern[pattern_nb]->name    = NULL;
  pattern[pattern_nb]->pos     = 0;
  pattern[pattern_nb]->type    = TL;
  pattern[pattern_nb]->matches = 0;
  pattern[pattern_nb]->inserts = inserts_list;

  aind = optind + 1; /* First command line argument to consider. */

  /* Manage the case when there is no pattern.        */
  /* No argument must be allowed in the command line. */
  /* """""""""""""""""""""""""""""""""""""""""""""""" */
  if (count == 1)
  {
    accept = 1;
    goto last;
  }

  /* For each pattern constraint defined as parameter in the rule,     */
  /* store its regex list in a hash table indexed by the pattern name. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  ht_new(&re_patterns_ht, 7);
  store_patterns_regex_list(rule, &re_patterns_ht);

  /* Insert the first word in the first list of inserts. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""" */
  aind = do_inserts(argc, argv, aind, 0, pattern);

  /* Do not accept if the first pattern is mandatory and no argument */
  /* are provided.                                                   */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (aind == *argc && pattern_nb > 0 && pattern[0]->type != T0
      && pattern[0]->type != TO)
  {
    *err = xstrdup("at least one argument is required.");

    return 0;
  }

  if (aind == *argc && pattern_nb > 0
      && (pattern[0]->type == T0 || pattern[0]->type == TO))
  {
    accept = 1; /* accept. */
    goto last;
  }

  /* For each pattern elements, identify their matching arguments if any. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  pind = 0;
  while (pind < pattern_nb)
  {
    /* TODO free pattern and pattern_a at the end of thins function. */

    pattern_type = pattern[pind]->type;
    pos          = pattern[pind]->pos;

    switch (pattern_type)
    {
      case TL: /* Dummy last pattern. */

#if 0
        accept = 1;
        if (aind < *(argc))
        {
          switch (pattern[pind - 1]->type)
          {
            case T0:
            case T1:
            case T2S:
            case T2M:
              pind -= 2;

            case TP:
            case TS:
              if (aind < *argc)
              {
                size_t s;

                s    = snprintf(NULL, 0, "command line: Too many arguments.");
                *err = xmalloc(s + 1);
                sprintf(*err, "command line: Too many arguments.");

                accept = 0;
              }
            default:
              aind++;
          }
        }
        else
          do_inserts(argc, argv, *argc, pind, pattern);
#endif

        break;

        /* ------------------------------------------------------------- */

      case T0:  /* $* */
      case T1:  /* $+ */
      case T2S: /* $, */
      case T2M: /* $; */

        /* a filtering rule exists for the current pattern */
        /* """"""""""""""""""""""""""""""""""""""""""""""" */
        if (aind < *argc)
        {
          /* Check if three is a regex match. */
          /* """""""""""""""""""""""""""""""" */
          accept =
            regex_match(rule, *argv, aind, pind, pattern, &re_patterns_ht, err);

          if (accept) /* pattern[pind] matches (*argv)[aind]. */
          {
            matched = 0;
            if (pind == pattern_nb - 1) /* Last pattern. */
            {
              accept  = 1;      /* accept by default.                         */
              matched = 1;      /* The pattern matches at least one argument. */
              aind++;           /* Next argument. */
              same_pattern = 1; /* As it is the last multiple pattern, we nee *
                                | to make sure it can potentially match other *
                                | arguments, so do not increment the pattern  *
                                | index.                                      */
            }
            else /* There is is at least one more pattern to examine. */
            {
              matched++; /* we got another match. */
              int start; /* start index in the following loop. */

              /* Look further to see if the next arguments are matched by  */
              /* the same pattern. Stop if the next pattern matches one of */
              /* these words,                                              */
              /* Do not stop in case of mismatch if the current pattern    */
              /* is T2S/M.                                                 */
              /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
              if (pattern[pind]->type == T0)
                start = aind;
              else
                start = aind + 1;

              for (aind = start; aind < (*argc); aind++)
              {
                if (pattern_match(rule,
                                  *argv,
                                  aind,
                                  pind + 1,
                                  pattern,
                                  &re_patterns_ht,
                                  err))
                {
                  pattern[pind + 1]->matches = 0; /* Cancel the matches count *
                                                   | as it be evaluated for   *
                                                   | the same pattern in the  *
                                                   | next pattern iteration.  */
                  aind = do_inserts(argc, argv, aind, pind + 1, pattern);
                  break;
                }

                /* Do not continue if an error has been triggered. */
                /* """"""""""""""""""""""""""""""""""""""""""""""" */
                if (*err)
                  break;

                /* If the next pattern does not match the next argument */
                /* then lok if it is matchched by the current pattern.  */
                /* """""""""""""""""""""""""""""""""""""""""""""""""""" */
                if (regex_match(rule,
                                *argv,
                                aind,
                                pind,
                                pattern,
                                &re_patterns_ht,
                                err))
                  matched++; /* we got another match. */
                else
                  /* Mismatch, ignore the mismatch if the pattern is T2S/M. */
                  /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
                  if (pattern[pind]->type != T2S && pattern[pind]->type != T2M)
                  {
                    if (pattern[pind]->type == T0)
                    {
                      if (matched > 0)
                        accept = 0;
                    }
                    else
                      accept = 0;

                    break;
                  }
              }
            }
          }
          else /* pattern[pind] does not match (*argv)[aind]. */
          {
            if (pind == pattern_nb - 1) /* Last pattern. */
            {
              /* Optional multi-pattern (T0/T2S/R2M) has arguments but */
              /* doesn't match them (filtering).                       */
              /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
              if (pattern[pind]->type == T0 && pattern[pind]->matches == 0)
              {
                if (aind < (*argc) - 1)
                  aind++; /* Next word; */

                accept       = 1;
                same_pattern = 1; /* Ask to stay on the same pattern. */
              }
              else if (pattern[pind]->type == T2S || pattern[pind]->type == T2M)
              {
                if (aind < (*argc) - 1)
                  aind++; /* Next word; */
                accept       = 1;
                same_pattern = 1; /* Ask to stay on the same pattern. */
              }
            }
            else /* The current pattern is not the last one. */
            {
              /* Manage special T0/2S and 2M cases which may lead to      */
              /* accept the argument even if it has failed to be matched. */
              /* """""""""""""""""""""""""""""""""""""""""""""""""""""""" */
              if (pattern[pind]->type == T0 || pattern[pind]->type == T2S
                  || pattern[pind]->type == T2M)
              {

                if (pattern_match(rule,
                                  *argv,
                                  aind,
                                  pind + 1,
                                  pattern,
                                  &re_patterns_ht,
                                  err))
                {
                  /* The next pattern matches the current argument. */
                  /* """""""""""""""""""""""""""""""""""""""""""""" */

                  /* Stay on the current pattern until if has matched       */
                  /* one argument if the pattern is T2S/M as these types of */
                  /* pattern tolerates mismatches.                          */
                  /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
                  aind = do_inserts(argc, argv, aind, pind + 1, pattern);
                  if ((pattern[pind]->type == T2S || pattern[pind]->type == T2M)
                      && pattern[pind]->matches > 0)
                  {
                    accept       = 1;
                    same_pattern = 1; /* Stay on the same pattern until it   *
                                       | matches a future commend line word. */
                  }
                  else
                  {
                    aind++; /* We can proceed with the next word. */
                    accept = 1;
                  }
                }
                else
                {
                  /* The next pattern does not match the current argument. */
                  /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */

                  /* Accept and stay on the current pattern if the pattern */
                  /* is T2S/M as these types of  pattern tolerates         */
                  /* mismatches.                                           */
                  /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
                  if (pattern[pind]->type == T2S || pattern[pind]->type == T2M)
                  {
                    aind++; /* We can proceed with the next word. */
                    accept       = 1;
                    same_pattern = 1; /* Stay on the same pattern until it   *
                                       | matches a future commend line word. */
                  }
                }
              }
            }
          }
        }

        /* TODO see if it is needed. */
        if (accept && *err != NULL)
        {
          free(*err);
          *err = NULL;
        }

        /* Do not accept if an error has already occurred. */
        /* """"""""""""""""""""""""""""""""""""""""""""""" */
        if (*err)
        {
          accept = 0;
          break;
        }

        /* Produce the right error message. */
        /* """""""""""""""""""""""""""""""" */
        if (accept == 0)
        {
          if (aind < *argc)
            *err = mkerr("%s does not match the argument \"%s\".",
                         pattern[pind]->name,
                         (*argv)[aind]);
          else
            *err = mkerr("%s needs to match at least one arguments.",
                         pattern[pind]->name);
        }

        if (pattern[pind]->type == T0 && pattern[pind]->matches == 0
            && pind == pattern_nb - 1)
        {
          *err   = mkerr("%s needs to match the argument \"%s\".",
                       pattern[pind]->name,
                       (*argv)[aind]);
          accept = 0;
        }

        break;

        /* ------------------------------------------------------------- */

      case TT: /* ^text */
        /* when a TT pattern is expected, check its validity */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""" */
        accept =
          text_match(rule, *argv, aind, pind, pattern, &re_patterns_ht, err);

        if (accept)
          aind = do_inserts(argc, argv, aind, pind, pattern);
        else
        {
          *err = mkerr("%s does not match \"%s\".",
                       pattern[pind]->name,
                       (*argv)[aind]);

          break;
        }

        aind++;

        break;

        /* ------------------------------------------------------------- */

      case TP: /* $n */
        /* when a TP pattern is expected, check its validity */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        accept =
          pos_match(rule, *argv, aind, pind, pattern, &re_patterns_ht, err);

        if (accept)
        {
          aind    = do_inserts(argc, argv, aind, pind, pattern);
          max_pos = pos;
        }
        else
          *err = mkerr("\"%s\" doesn't match any permitted "
                       "regular expressions for \"%s\".",
                       (*argv)[aind],
                       pattern[pind]->name);

        aind++;

        break;

        /* ------------------------------------------------------------- */

      case TS: /* $. */
      case TO: /* $? */
        /* Check if the current argument match the optional regex */
        /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
        accept =
          regex_match(rule, *argv, aind, pind, pattern, &re_patterns_ht, err);

        if (accept)
        {
          aind = do_inserts(argc, argv, aind, pind, pattern);
          aind++; /* Consume an argument on success. */
          pattern[pind]->matches = 1;
        }
        else /* Optional single any pattern. */
        {
          if (pattern[pind]->type == TS)
            *err = mkerr("\"%s\" is not matching the "
                         "rules given or implied by the presence of \"%s\".",
                         (*argv)[aind],
                         pattern[pind]->name);
          else
          {
            if (pind == pattern_nb - 1) /* Last pattern. */
            {
              *err   = mkerr("\"%s\" is not matching the rules given or "
                             "implied by the presence of \"%s\".",
                           (*argv)[aind],
                           pattern[pind]->name);
              accept = 0;
            }
            else
            {
              aind   = do_inserts(argc, argv, aind, pind, pattern);
              accept = 1; /* else accept anyway but do not *
                           | consume an argument.          */
            }
          }
        }

        break;
    }

    if (*err)
      return accept;

    if (aind == *argc)
      goto last;

    if (!same_pattern)
      pind++;
    else
      same_pattern = 0;
  }

last:

  /* Do remaining actions re-analyzing data stored in patterns. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  for (pind = 0; pind < pattern_nb; pind++)
  {
    /* Do remaining inserts. */
    /* ''''''''''''''''''''' */
    aind = do_inserts(argc, argv, aind, pind, pattern);

    if ((pattern[pind]->type == T1 || pattern[pind]->type == TP
         || pattern[pind]->type == TS)
        && pattern[pind]->matches == 0)
    {
      *err = mkerr("The mandatory command line pattern \"%s\" "
                   "did not match anything.",
                   pattern[pind]->name);

      accept = 0;
      break;
    }

    /* Abort on missing required matches. */
    /* '''''''''''''''''''''''''''''''''' */
    else if (pattern[pind]->type == T2S && pattern[pind]->matches != 1)
    {
      *err = mkerr("the command line pattern \"%s\" "
                   "must match exactly one command line argument.",
                   pattern[pind]->name);

      accept = 0;
      break;
    }
    else if (pattern[pind]->type == T2M && pattern[pind]->matches < 1)
    {
      *err = mkerr("the command line pattern \"%s\" "
                   "did not match anything.",
                   pattern[pind]->name);

      accept = 0;
      break;
    }
    else if (pattern[pind]->type == TT && pattern[pind]->matches == 0)
    {
      *err = mkerr("the required argument \"%s\" "
                   "was not found on the command line.",
                   pattern[pind]->name + 1);

      accept = 0;
      break;
    }
  }

  if (aind < *argc)
  {
    /* All arguments haven't been used. */
    /* """""""""""""""""""""""""""""""" */
    if (!*err)
      *err = xstrdup("Too many arguments.");

    accept = 0;
  }
  else if (accept)
  {
    /* In case of success, append the remaining last insert to the */
    /* command line.                                               */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    ll_t      *list;
    ll_node_t *node;

    list = pattern[pattern_nb]->inserts;

    if (list)
    {
      int s;

      node = list->head;
      s    = list->len;

      *argv = xrealloc(*argv, (*argc + s) * sizeof(char *));

      while (node)
      {
        (*argv)[(*argc)++] = node->data;

        node = node->next;
      }
    }
  }

  return accept;
}

/* ============================================== */
/* Check if a date is later than the current date */
/* returns 1 if yes and 0 if not.                 */
/* ============================================== */
int
date_has_expired(char *curr_date, char *date)
{
  char mask[13] = "000000000000";

  if (date == NULL)
    return 0;

  memcpy(mask, date, strlen(date));

  if (strcmp(curr_date, mask) > 0)
    return 1;
  else
    return 0;
}

/* -------------------------------------------------- */
/* owners constraint must respect the syntax :        */
/* owners=user:group,...                              */
/* Notice : user and/or group are regular expressions */
/* -------------------------------------------------- */
int
check_owners(rule_t *rule, char *param_name, char *path)
{
  ll_t      *val_list;
  ll_node_t *node;

  struct passwd *pwbuf;
  struct group  *grbuf;
  struct stat    buf;
  char          *ptr;
  char          *val;

  int rc = 1;

  /* Get user and group name of the owner of the file. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""" */
  errno = 0;

  if (stat(path, &buf) == -1)
  {
    switch (errno)
    {
      case ENOENT:
        error("%s: \"%s\" does not exist.", rule->tag, path);

      case ENOTDIR:
        error("%s: \"%s\" invalid path.", rule->tag, path);
    }

    trace(LOG_DATA,
          "[%s] unable to get owner's information for \"%s\".",
          rule->tag,
          path);

    return 0;
  }
  pwbuf = getpwuid(buf.st_uid);

  if (pwbuf == NULL)
    error("%s: no identified user for uid '%d'", rule->tag, buf.st_uid);

  grbuf = getgrgid(buf.st_gid);

  if (grbuf == NULL)
    error("%s: no identified group for gid '%d'", rule->tag, buf.st_gid);

  /* check users,groups candidates. */
  /* """""""""""""""""""""""""""""" */
  val_list = get_param_val_list(rule, param_name);
  if (val_list && val_list->len == 0)
  {
    trace(LOG_WARN,
          "[%s] owners: no constraints found.%s.",
          rule->tag,
          (*param_name == '!') ? "denied" : "accepted");

    if (*param_name != '!')
      return 1;
    else
      return 0;
  }

  if (val_list)
  {
    node = val_list->head;
    while (node)
    {
      char    user_regex[258]  = { 0 };
      char    group_regex[258] = { 0 };
      regex_t re1, re2;
      char    c;
      int     n;

      val = (char *)node->data;
      n   = sscanf(val,
                 "%255[^-]-%255[^ \t]%c",
                 user_regex + 1,
                 group_regex + 1,
                 &c);
      if (n != 3 || c != '\0')
        continue;

      user_regex[0]                  = '^';
      user_regex[strlen(user_regex)] = '$';

      group_regex[0]                   = '^';
      group_regex[strlen(group_regex)] = '$';

      if (regcomp(&re1, user_regex, REG_EXTENDED | REG_NOSUB) != 0)
      {
        trace(LOG_WARN,
              "[%s] owners: invalid regular expression \"%s\".",
              rule->tag,
              user_regex);
        break;
      }

      if (regcomp(&re2, group_regex, REG_EXTENDED | REG_NOSUB) != 0)
      {
        trace(LOG_WARN,
              "[%s] owners: invalid regular expression \"%s\".",
              rule->tag,
              group_regex);
        break;
      }

      if ((regexec(&re1, pwbuf->pw_name, 0, NULL, 0) == 0)
          && regexec(&re2, grbuf->gr_name, 0, NULL, 0) == 0)
      {
        if (*param_name != '!')
          rc = 1;
        else
          rc = 0;

        regfree(&re1);
        regfree(&re2);

        break;
      }

      regfree(&re1);
      regfree(&re2);

      node = node->next;
    }

    if (node == NULL)
    {
      if (*param_name != '!')
        rc = 0;
      else
        rc = 1;
    }
  }

  return rc;
}

#if 0
/* ===================================================================== */
/* modes constraint must respect the syntax :                            */
/* modes=NNNN,MMMM,... where NNNN and MMMM are an octal representation   */
/* or a ls "-l like" form of the target requested authorised             */
/* permissions.                                                          */
/* Notice : NNNN and MMMM can be regular expressions.                    */
/*                                                                       */
/* rule (in) The whole rule structure containing command and parameters  */
/* name (in) "mode" or "!modes"                                          */
/* path (in) The name of the object for witch we want to check the modes */
/* ===================================================================== */
int
check_modes(rule_t * rule, char * name, char * path)
{
  ll_t *      val_list;
  ll_node_t * node;
  struct stat buf;
  regex_t     regex;
  int         rc = 1;

  val_list = get_param_val_list(rule, name);
  if (val_list && val_list->len == 0)
    error("%s: the list of modes is not given.", name);

  if (val_list)
  {
    node = val_list->head;

    if (stat(path, &buf) == -1)
    {
      trace(LOG_DATA, "[%s] modes: unable to get the permissions of \"%s\".",
            rule->tag, path);
      rc = 1;
    }
    else
    {
      char foctmode[5];
      char frwxmode[10];
      char regstr[13];

      sprintf(foctmode, "%04o", buf.st_mode & 07777);
      octal_to_rwx(foctmode, frwxmode);

      while (node)
      {
        size_t mode_len;
        char * mode;

        mode     = (char *)node->data;
        mode_len = strlen(mode);
        if (mode_len != 3 && mode_len != 4 && mode_len != 9 && mode_len != 10)
        {
          trace(LOG_DATA,
                "[%s] modes: invalid access mode or permissions string \"%s\"",
                rule->tag, mode);
          node = node->next;
          continue;
        }

        /* Regular expression building */
        /* """"""""""""""""""""""""""" */

        /* make sure the RE does not match a word in the */
        /* middle of a string for security reason.       */
        /* """"""""""""""""""""""""""""""""""""""""""""" */
        strcpy(regstr, "^");
        if (mode_len == 10)
          strcat(regstr, mode + 1);
        else
        {
          if (mode_len == 3)
            strcat(regstr, "0");
          strcat(regstr, mode);
        }
        strcat(regstr, "$");

        /* Regular expression compilation */
        /* """""""""""""""""""""""""""""" */
        if (regcomp(&regex, regstr, REG_EXTENDED | REG_NOSUB) != 0)
        {
          trace(LOG_DATA, "[%s] modes: invalid regular expression \"%s\"",
                rule->tag, mode);
          continue;
        }

        /* Regular expression evaluation */
        /* """"""""""""""""""""""""""""" */
        if (mode_len == 3 || mode_len == 4)
        {
          if (regexec(&regex, foctmode, 0, NULL, 0) == 0)
          {
            regfree(&regex);
            if (*name == '!')
              rc = 0;
            break;
          }
        }
        else
        {
          if (regexec(&regex, frwxmode, 0, NULL, 0) == 0)
          {
            regfree(&regex);
            if (*name == '!')
              rc = 0;
            break;
          }
        }

        regfree(&regex);

        node = node->next;
      }

      if (node == NULL)
        if (*name != '!')
          rc = 0;
    }
  }

  return rc;
}
#endif
