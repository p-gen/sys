/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <glob.h>
#include <grp.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define XQUOTE(s) QUOTE(s)
#define QUOTE(s) #s

#include "config.h"
#include "xmalloc.h"
#include "list.h"
#include "bst.h"
#include "cachestat.h"
#include "user.h"
#include "types.h"
#include "cache.h"
#include "check.h"
#include "comp.h"
#include "conf.h"
#include "ini.h"
#include "log.h"
#include "parser.h"
#include "sys.h"
#include "strarray.h"
#include "utils.h"

int
egetopt(int nargc, char **nargv, char *ostr);

extern char **environ;

/* External variables used by egetopt */
/* """""""""""""""""""""""""""""""""" */
extern int   optneed;  /* Character used for mandatory arguments  */
extern int   optmaybe; /* Character used for optional arguments   */
extern int   optchar;  /* Character which begins a givenargument  */
extern int   optbad;   /* What egetopt() returns for a bad option */
extern int   opterrfd; /* Where egetopt() error messages go       */
extern char *optstart; /* String which contains valid option      *
                        | start chars                             */
extern int   optind;   /* Index of current argv[]                 */
extern int   optopt;   /* The actual option pointed to            */
extern int   opterr;   /* Set to 0 to suppress egetopt's error    *
                        | messages                                */
extern char *optarg;   /* The argument of the option              */

long path_max; /* Global variable which will contail the maximal   *
                | path length. This valum will be given by sysconf *
                | or set to an arbitrary usual value.              */

FILE *log_fh = NULL;

bst_node_t *rule_tree; /* A binary tree of all the successfully parsed ops *
                        | the tree only contains the latest seen ops       */
bst_node_t *var_tree;  /* The same for the correctly identified variables. *
                        | This tree is only used when reading the .dat     *
                        | files to create the final version of the ops so  *
                        | we will be able to put then in the cache         */
bst_node_t *env_tree;
size_t      env_count;

bst_node_t *new_var_tree;

static ll_t *sys_item_list; /* Item list read from the rule_tree. The list *
                               | will be used to feed the cache.            */

static void
insert_in_cache(void *node, void *arg);

static rule_t *
build_rule_from_cache(char    *rule_tag,
                      char    *orig_rule_tag,
                      char    *data,
                      uint16_t data_len);

/* ===================================================== */
/* This function is made to be called in bst_foreach     */
/* The node argument will take all the values in the bst */
/* ----------------------------------------------------- */
/* Appends one element of type elem_t to a linked list   */
/*                                                       */
/* node (in/out) a node in the bst tree                  */
/* arg  (in/out) a generic pointer not used here but     */
/*               needed by the callback prototype.       */
/* ===================================================== */
static void
insert_in_cache(void *node, void *arg)
{
  rule_t *rule;
  elem_t *elem;

  ll_node_t *param_list_elem;
  ll_node_t *val_list_elem;

  rule = (rule_t *)node;

  if (rule->command == NULL)
    return;

  /* Allocate some space for the new element to be added in the cache */
  /* and start to put data in it.                                     */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  elem           = xmalloc(sizeof(elem_t));
  elem->str      = rule->tag;
  elem->data     = xstrdup(rule->command);
  elem->data_len = strlen(rule->command) + 1;

  /* Add the optional list of parameters */
  /* """""""""""""""""""""""""""""""""""" */
  param_list_elem = rule->param_list->head;
  while (param_list_elem)
  {
    param_t *data = (param_t *)(param_list_elem->data);
    char    *name = (char *)data->name;
    size_t   len  = strlen(name) + 1;

    elem->data = xrealloc(elem->data, elem->data_len + len + 1);
    strcpy((char *)(elem->data) + elem->data_len, name);
    elem->data_len += len;
    *((char *)(elem->data) + elem->data_len - 1) = ':';

    /* Process the optional parameter's value list */
    /* """"""""""""""""""""""""""""""""""""""""""" */
    val_list_elem = data->val_list->head;
    if (val_list_elem == NULL)
    {
      /* We separate the parameter from its list if it has no value */
      /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      elem->data = xrealloc(elem->data, elem->data_len + 1);
      *((char *)(elem->data) + elem->data_len) = '\0';
      elem->data_len++;
    }
    else
      while (val_list_elem)
      {
        name       = (char *)val_list_elem->data;
        len        = strlen(name);
        elem->data = xrealloc(elem->data, elem->data_len + len + 1);
        strcpy((char *)(elem->data) + elem->data_len, name);
        elem->data_len += len + 1;

        /* The latest comma is to be replaced by a NUL */
        /* """"""""""""""""""""""""""""""""""""""""""" */
        if (val_list_elem->next != NULL)
          *((char *)(elem->data) + elem->data_len - 1) = ',';
        else
          *((char *)(elem->data) + elem->data_len - 1) = '\0';

        val_list_elem = val_list_elem->next;
      }
    param_list_elem = param_list_elem->next;
  }

  /* And finally append the new element to the list */
  /* """""""""""""""""""""""""""""""""""""""""""""" */
  ll_append(sys_item_list, elem);
}

/* ===================================================== */
/* This function is made to be called in bst_foreach     */
/* The node argument will take all the values in the bst */
/* ----------------------------------------------------- */
/* Prints the command name followed by its patterns for  */
/* an element in the command tree.                       */
/* ===================================================== */
static void
print_rule_tag_header(void *node, void *arg)
{
  ll_t      *val_list;
  ll_node_t *ll_node;

  user_t *user_data = (user_t *)arg;

  rule_t *rule;

  rule = (rule_t *)node;

  if (rule->command == NULL)
    return;

  if (check_users_groups_netgroups(rule, user_data))
  {
    size_t offset = 0;

    /* Print the command name and the command line patters if any. */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    offset = strcspn(rule->command, " \t");
    if (rule->command[offset] == '\0')
      printf("%s\n", rule->tag);
    else
      printf("%s %s\n", rule->tag, rule->command + offset + 1);

    /* If the help parameter exists, display its content. */
    /* """""""""""""""""""""""""""""""""""""""""""""""""" */
    val_list = get_param_val_list(rule, "help");
    if (val_list && val_list->len == 0)
      trace(LOG_DATA, "[%S] the help message is missing.", rule->tag);
    else if (val_list)
    {
      /* Display a prefix followed by the concatenation of all the   */
      /* help values separated by a comma.                           */
      /* All occurrences of a newline character trigger the printing */
      /* of a new prefix.                                            */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      char *tmp = xstrdup("| ");

      ll_node = val_list->head;
      while (ll_node)
      {
        tmp = strappend(tmp, (char *)ll_node->data, (char *)0);
        if (ll_node->next)
          tmp = strappend(tmp, ",", (char *)0);
        ll_node = ll_node->next;
      }

      printf("%s\n", strrep(tmp, "\n", "\n| "));
      free(tmp);
    }
  }
}

/* =========================================================== */
/* Search the first permitted path in which a command is valid */
/* Returns NULL if not found or the path found if found.       */
/* =========================================================== */
char *
search_in_paths(rule_t *rule, char *base, char **extra_path_array)
{
  ll_t    *accept_list, *deny_list;
  param_t *accept_param, *deny_param;
  char    *fullpath;
  char    *path;
  int      found         = 0;
  char    *accept_buffer = NULL;
  char    *deny_buffer   = NULL;
  glob_t   globbuf;
  char   **path_array;
  int      n;

  /* Get accepted path list. */
  /* """"""""""""""""""""""" */
  accept_param = get_param(rule, "paths");
  if (accept_param == NULL)
  {

    /* Try to set a default paths. */
    /* """"""""""""""""""""""""""" */
    accept_list = ll_new();

    /* Test for the existence of the _CS_PATH configuration string. */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    n = confstr(_CS_PATH, NULL, (size_t)0);
    if (n > 0)
    {
      /* It exists, so read it */
      /* """"""""""""""""""""" */
      path = xmalloc(n);
      confstr(_CS_PATH, path, n);

      /* And add is components to the list of allowed paths.        */
      /* Also add the extra default directories from the .cfg file. */
      /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      if (strarray(path, &path_array, &n, ":", MERGESEPS | KEEPQUOTES) == 0)
      {
        int i;

        for (i = 0; i < n; i++)
          ll_append(accept_list, path_array[i]);

        if (extra_path_array != NULL)
        {
          i = 0;
          while (extra_path_array[i] != NULL)
            ll_append(accept_list, extra_path_array[i++]);
        }
      }
      else
      {
        int i;

        /* Set an arbitrary default path.                             */
        /* Also add the extra default directories from the .cfg file. */
        /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        ll_append(accept_list, "/bin");
        ll_append(accept_list, "/usr/bin");

        if (extra_path_array != NULL)
        {
          i = 0;
          while (extra_path_array[i] != NULL)
            ll_append(accept_list, extra_path_array[i++]);
        }
      }
      free(path);
    }
    else
    {
      int i;

      /* Set an arbitrary default path and the extra default directories */
      /* from the .cfg file.                                             */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      ll_append(accept_list, "/bin");
      ll_append(accept_list, "/usr/bin");

      if (extra_path_array != NULL)
      {
        i = 0;
        while (extra_path_array[i] != NULL)
          ll_append(accept_list, extra_path_array[i++]);
      }
    }
  }
  else
    accept_list = accept_param->val_list;

  /* Get denied path list. */
  /* """"""""""""""""""""" */
  deny_param = get_param(rule, "!paths");
  if (deny_param == NULL)
    deny_list = ll_new();
  else
    deny_list = deny_param->val_list;

  /* Check if a full binary path can be built with one of */
  /* the accepted path and if its path is not denied.     */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (accept_list && accept_list->len > 0)
  {
    size_t     i;
    ll_node_t *node;
    FILE      *fp;

    /* For each path in the "paths" list, try to concatenate the */
    /* binary name to it and check if that form a pathname to an */
    /* existing file.                                            */
    /* If such a pathname is found, proceed directly with the    */
    /* next part of the code.                                    */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    accept_buffer = xmalloc(path_max);
    node          = accept_list->head;
    while (node && !found)
    {
      strcpy(accept_buffer, (char *)node->data);

      if (glob(accept_buffer, 0, NULL, &globbuf) == 0)
      {
        for (i = 0; i < globbuf.gl_pathc; i++)
        {
          strcpy(accept_buffer, globbuf.gl_pathv[i]);
          strcat(accept_buffer, "/");
          strcat(accept_buffer, base);

          if ((fp = fopen(accept_buffer, "r")) != NULL)
          {
            fclose(fp);
            found = 1;
            path  = xstrdup(globbuf.gl_pathv[i]);
            break;
          }
        }
      }
      globfree(&globbuf);
      node = node->next;
    }

    /* If a potential pathname was found, check if its path is in */
    /* the list of forbidden paths regex. If not, returns it else */
    /* return NULL                                                */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (found)
    {
      deny_buffer = xmalloc(path_max);
      node        = deny_list->head;
      while (node && found)
      {
        strcpy(deny_buffer, (char *)node->data);

        if (glob(deny_buffer, 0, NULL, &globbuf) == 0)
        {
          for (i = 0; i < globbuf.gl_pathc; i++)
          {
            strcpy(deny_buffer, globbuf.gl_pathv[i]);

            if (strcmp(path, globbuf.gl_pathv[i]) == 0)
            {
              /* A pathname was found in the deny paths lists, mark it */
              /* as rejected and exits the loop.                       */
              /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
              found = 0;
              free(path);
              break;
            }
          }
        }
        globfree(&globbuf);
        node = node->next;
      }
    }
  }

  if (!found)
    fullpath = NULL;
  else
    fullpath = xstrdup(accept_buffer);

  globfree(&globbuf);
  free(accept_buffer);
  free(deny_buffer);

  return fullpath;
}

/* ============================================================ */
/* Sets the uid/username couple from a string containing either */
/* the user id or the user name.                                */
/* in case of success *username will have to be freed.          */
/* returns 0 if OK or -1 if str doesn't contain a valid user.   */
/* ============================================================ */
int
str_to_user(char *str, uid_t *uid, char **username)
{
  struct passwd *pw;

  if (!is_number((int *)uid, str))
  {
    if ((pw = getpwnam(str)) == NULL)
      return -1;

    *uid      = pw->pw_uid;
    *username = xstrdup(str);
  }
  else
  {
    if ((pw = getpwuid(atoi(str))) == NULL)
      return -1;

    *username = xstrdup(pw->pw_name);
  }
  return 0;
}

/* ============================================================= */
/* Sets the gid/groupname couple from a string containing either */
/* the group id or the group name.                               */
/* returns 0 if OK or 1 if str doesn't contain a valid group.    */
/* ============================================================= */
int
str_to_group(char *str, gid_t *gid, char **groupname)
{
  struct group *gr;

  if (!is_number((int *)gid, str))
  {
    if ((gr = getgrnam(str)) == NULL)
      return -1;

    *gid       = gr->gr_gid;
    *groupname = xstrdup(str);
  }
  else
  {
    if ((gr = getgrgid(atoi(str))) == NULL)
      return -1;

    *groupname = xstrdup(gr->gr_name);
  }
  return 0;
}

/* =================================================================== */
/* Ask for password for one of the users in pwd_param using a cache of */
/* already validated users in user_pw_ok_ht.                           */
/* This function puts a potential error message in error_msg.          */
/* Returns 1 if the access has been granted, 0 if not.                 */
/* =================================================================== */
int
ask_password(rule_t  *rule,
             param_t *pwd_param,
             ht_t    *user_pw_ok_ht,
             char   **error_msg)
{
  ll_t      *val_list        = pwd_param->val_list;
  ll_node_t *param_list_node = val_list->head;
  char      *name;
  uid_t      uid;
  int        rc = 0;

  while (param_list_node)
  {
    if (str_to_user((char *)(param_list_node->data), &uid, &name) == -1)
    {
      trace(LOG_WARN, "[%s] uid: unknown user \"%s\".", rule->tag, name);
      goto next;
    }

    /* If the password  for this user has already be successfully entered */
    /* do not check it anymore.                                           */
    /* '''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''' */
    if (ht_search(user_pw_ok_ht, name) != NULL)
    {
      rc = 1;
      break;
    }

    trace(LOG_INFO, "password request for user \"%s\".", name);

    if (check_password(name))
    {
      rc = 1;
      break;
    }

  next:
    free(name);
    param_list_node = param_list_node->next;
  }

  if (rc)
  {
    if (*(rule->tag) == '@')
      trace(LOG_INFO, "[%s] access granted.", rule->tag + 1);
    else
      trace(LOG_INFO, "[%s] access granted.", rule->tag);
    ht_insert(user_pw_ok_ht, name, "ok"); /* Here "ok" is a dummy value. */
  }
  else
  {
    if (*(rule->tag) == '@')
      my_asprintf(error_msg, "[%s] invalid password.", rule->tag + 1);
    else
      my_asprintf(error_msg, "[%s] invalid password.", rule->tag);
  }

  return rc;
}

/* ================================ */
/* Daemonize the current process.   */
/* returns 1 on success 0 on error. */
/* ================================ */
static int
become_daemon()
{
  int maxfd, fd;

  /* First fork to change the pid pid but sid and pgid will be */
  /* the same as the calling process.                          */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  switch (fork()) /* become background process. */
  {
    case -1:
      return 0;
    case 0:
      break; /* Child. */
    default:
      _exit(EXIT_SUCCESS); /* parent terminates. */
  }

  /* Run the process in a new session without a controlling  */
  /* terminal. The process group ID will be the process ID   */
  /* and thus, the process will be the process group leader. */
  /* After this call the process will be in a new session,   */
  /* and it will be the progress group leader in a new       */
  /* process group.                                          */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (setsid() == -1) /* Become leader of new session. */
    return 0;

  /* Fork again.  This second fork orphans the process because the         */
  /* parent will exit.  The child process will be adopted by the init      */
  /* process with process ID 1.  The process will be in it's own session   */
  /* and process group and will have no controlling terminal. Furthermore, */
  /* the process will not be the process group leader and thus, cannot     */
  /* have the controlling terminal if there was one.                       */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  switch (fork())
  {
    case -1:
      return 0;
    case 0:
      break; /* Child. */
    default:
      _exit(EXIT_SUCCESS); /* parent terminates. */
  }

  chdir("/"); /* Change to root directory. */

  maxfd = sysconf(_SC_OPEN_MAX);

  if (maxfd == -1)
    maxfd = 1024; /* Sane value. */

  /* Close potentially open files ignoring errors. */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  for (fd = 0; fd < maxfd; fd++)
    close(fd);

  close(STDIN_FILENO);

  /* Redirect stdin,stdout and stderr to /dev/null. */
  /* """""""""""""""""""""""""""""""""""""""""""""" */
  if (freopen("/dev/null", "r", stdin) == NULL)
    return 0;

  if (freopen("/dev/null", "r", stdout) == NULL)
    return 0;

  if (freopen("/dev/null", "r", stderr) == NULL)
    return 0;

  return 1;
}

/* ========================================================================= */
/* Try to execute the command in the new environment.                        */
/* The process will to fork here:                                            */
/* The parent will wait for the child to exist and log this event and        */
/* return 1 on success and 0 on error.                                       */
/* The child will try to execute the target executable in the selected rule  */
/* and does not return on success or returns 0 on error.                     */
/* ========================================================================= */
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
             int      identity_failure,
             char   **extra_path_array,
             char   **error_msg)
{
  char          *tag;
  char          *path;
  char          *base;
  char         **args;
  int            i;
  int            status;
  int            sup_gid_nb;
  char          *str;
  pid_t          pid;
  gid_t         *groups;
  gid_t          gid;
  struct passwd *pwd;
  int            exit_status;
  int            sig_status;

  glob_t g;

  *error_msg = NULL;

  exit_status = -1;
  sig_status  = -1;

  if (rule->tag[0] == '@')
    my_asprintf(&tag, "External command %s", rule->tag + 1);
  else
    tag = xstrdup(rule->tag);

  /* Fork to create an asynchronous child process. */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  switch (pid = fork())
  {
    int rc;

    case -1: /* Error. */
      error("fork has failed.");
      break;

    case 0: /* Child. */

      /* Set the new user's uid, gid and supplementary gids */
      /* """""""""""""""""""""""""""""""""""""""""""""""""" */
      setgroups(0, NULL); /* clear the supplementary groups. */

      /* Get the main group of new_username */
      /* '''''''''''''''''''''''''''''''''' */
      pwd = getpwnam(new_username);
      if (pwd == NULL)
      {
        my_asprintf(error_msg,
                    "[%s] unable to get the groups "
                    "for the user \"%s\".",
                    tag,
                    new_username);

        goto error;
      }

      gid = pwd->pw_gid;

      if (initgroups(new_username, gid) == -1)
      {
        my_asprintf(error_msg,
                    "[%s] unable to initialize the supplementary groups "
                    "for the user \"%s\".",
                    tag,
                    new_username);

        goto error;
      }

      if ((sup_gid_nb = getgroups(0, groups)) == -1)
      {
        my_asprintf(error_msg,
                    "[%s] unable to get the number of supplementary groups.",
                    tag);

        goto error;
      }

      groups = (gid_t *)xmalloc(sup_gid_nb * sizeof(gid_t));

      if (getgroups(sup_gid_nb, groups) == -1)
      {
        my_asprintf(error_msg,
                    "[%s] unable to get the supplementary groups.",
                    tag);

        goto error;
      }

      /* If the new user is not root, check if the asked new group is */
      /* one of its groups or supplementary groups.                   */
      /* '''''''''''''''''''''''''''''''''''''''''''''''''''''''''''' */
      if (new_uid == 0)
      {
        rc = setgid(new_gid);
        if (rc != 0)
        {
          my_asprintf(error_msg,
                      "[%s] unable to set new group to \"%s\".",
                      tag,
                      new_groupname);

          goto error;
        }
      }
      else
      {
        int is_in_sg = 0;
        for (int i = 0; i < sup_gid_nb; i++)
          if (groups[i] == new_gid)
          {
            if (setgid(new_gid) == -1)
            {
              my_asprintf(error_msg,
                          "[%s] unable to set new group to \"%s\".",
                          tag,
                          new_groupname);
              free(groups);

              goto error;
            }

            is_in_sg = 1;
            break;
          }

        if (!is_in_sg)
        {
          my_asprintf(error_msg,
                      "[%s] the new group \"%s\" in not one of the groups "
                      "to which \"%s\" belongs.",
                      tag,
                      new_groupname,
                      new_username);
          free(groups);

          goto error;
        }
      }

      rc = setuid(new_uid);
      if (rc != 0)
      {
        my_asprintf(error_msg,
                    "[%s] unable to set new users to \"%s\".",
                    tag,
                    new_username);

        goto error;
      }

      /* If the umask parameter exists and has a legal octal value, then use */
      /* it as the new umask else, use the current inherited umask           */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      set_umask(rule);

      /* Build the new arguments array */
      /* """"""""""""""""""""""""""""" */
      split_command_path(rule->executable, &path, &base);
      args = xmalloc((argc - optind + 1) * sizeof(char *));

      args[0] = rule->executable;

      args[argc - optind] = NULL;

      for (i = 1; i < argc - optind; i++)
        args[i] = argv[i + optind];

      /* Try to execute the command */
      /* """""""""""""""""""""""""" */
      if (path != NULL)
      {
        /* Check if the executable has a valid owner */
        /* """"""""""""""""""""""""""""""""""""""""" */
        if (!check_owners(rule, "!owners", rule->executable))
        {
          my_asprintf(error_msg,
                      "[%s] owners: the owner of %s is in "
                      "forbidden ones.",
                      tag,
                      rule->executable);

          goto error;
        }

        /* Check if the mode of the executable is in the */
        /* list of authorized modes.                     */
        /* """"""""""""""""""""""""""""""""""""""""""""" */
        if (!check_owners(rule, "owners", rule->executable))
        {
          my_asprintf(error_msg,
                      "[%s] owners: the owner of %s are not "
                      "in authorized ones.",
                      tag,
                      rule->executable);
          goto error;
        }

        if (glob(rule->executable, 0, NULL, &g) == 0)
        {
          if (daemon_flag)
            if (!become_daemon())
              exit(1);

          if (pwd_param && identity_failure)
          {
            if (ask_password(rule, pwd_param, user_pw_ok_ht, error_msg) == 0)
              goto error;
          }

          execve(rule->executable, args, new_environ);
          _exit(1); /* Normally newer executed. */
        }
        else
        {
          globfree(&g);
          my_asprintf(
            error_msg,
            "[%s] could not locate the executable in the default paths",
            tag,
            rule->executable);
          goto error;
        }
      }
      else
      {
        char *expanded_path;

        /* When the name of the executable if prefixed by an absolute     */
        /* path we do not have to prefix it with one of it permitted path */
        /* if it is found in one of them.                                 */
        /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        if (*args[0] != '/')
          expanded_path = search_in_paths(rule, args[0], extra_path_array);
        else
          expanded_path = rule->executable;

        if (expanded_path)
        {
          /* Check if the executable has no forbidden owners */
          /* """"""""""""""""""""""""""""""""""""""""""""""" */
          if (!check_owners(rule, "!owners", expanded_path))
          {
            my_asprintf(error_msg,
                        "[%s] owners: the owner of %s is in "
                        "forbidden ones.",
                        tag,
                        expanded_path);

            goto error;
          }

          /* Check if the owner of the executable is in the */
          /* list of authorized ones.                       */
          /* """""""""""""""""""""""""""""""""""""""""""""" */
          if (!check_owners(rule, "owners", expanded_path))
          {
            my_asprintf(error_msg,
                        "[%s] owners: the owner of %s are not "
                        "in authorized ones.",
                        tag,
                        expanded_path);

            goto error;
          }

          if (glob(expanded_path, 0, NULL, &g) == 0)
          {
            if (daemon_flag)
              if (!become_daemon())
                exit(1);

            if (pwd_param && identity_failure)
            {
              if (ask_password(rule, pwd_param, user_pw_ok_ht, error_msg) == 0)
                goto error;
            }
            execve(expanded_path, args, new_environ);
            _exit(1); /* Normally newer executed. */
          }
          else
          {
            globfree(&g);
            my_asprintf(
              error_msg,
              "[%s] could not locate the executable in the default paths",
              tag,
              expanded_path);
            goto error;
          }
        }
        else
        {
          my_asprintf(
            error_msg,
            "[%s] could not locate the executable in the default paths",
            tag,
            rule->executable);
          goto error;
        }
      }

      /* Normally unreachable code */
      /* """"""""""""""""""""""""" */
      error("[%s] exec has failed (%s).", tag, strerror(errno));

    default: /* Parent. */

      /* TODO Free var_tree, rule_tree env_tree and other dynamically */
      /* allocated objects.                                           */
      /* ************************************************************ */

      if (daemon_flag)
        exit(0);
      else
      {
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
          exit_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
          sig_status = WTERMSIG(status);
      }

      /* And log its termination. */
      /* """""""""""""""""""""""" */
      str = argvtostr(argc - 1, argv + 1);

      if (exit_status >= 0)
      {
        trace(LOG_INFO,
              "%s: sys %s <= %d/%d/%d (%d)",
              user_data->name,
              str,
              getpid(),
              pid,
              getppid(),
              exit_status);
      }
      else
        trace(LOG_INFO,
              "%s: sys %s <= %d/%d/%d (killed by signal %d)",
              user_data->name,
              str,
              getpid(),
              pid,
              getppid(),
              sig_status);

      free(str);
  }

  free(tag);

  return 1;

error:

  free(tag);

  return 0;
}

/* ================================================================ */
/* This function will be called for each node of the env_var tree   */
/* by the bst_foreach function.                                     */
/* The argument env (which stands for a char *** array) will be fed */
/* by the name/value couple in the node's data. The array index if  */
/* defined as a static variable as its value must be kept through   */
/* each call.                                                       */
/* ================================================================ */
void
add_to_environ(void *node, void *env)
{
  static unsigned n = 0;

  char *env_var;
  char *name;
  char *value;

  name  = ((env_var_t *)node)->name;
  value = ((env_var_t *)node)->value;

  if (*value != '\0')
  {
    env_var = xmalloc(strlen(name) + strlen(value) + 2);
    strcpy(env_var, name);
    strcat(env_var, "=");
    strcat(env_var, value);
    //hexdump(value , stderr, name, strlen(value));
    (*(char ***)env)[n++] = env_var;
  }
}

/* ===================================================== */
/* This function is made to be called in bst_foreach     */
/* The node argument will take all the values in the bst */
/* ----------------------------------------------------- */
/* Free one element of type env_var_t in a linked list   */
/* ===================================================== */
void
free_env_var(void *a)
{
  env_var_t *var;

  var = (env_var_t *)a;

  free(var->name);
  free(var->value);
  free(var);
}

/* ================================================================== */
/* This function is called by the process_rule function.              */
/*                                                                    */
/* It gets the existing environment and update it with the content of */
/* the variables defined in the command definition.                   */
/* new_environ will be created and filled.                            */
/* if the parameter environment is                                    */
/* - not present           -> an empty environment is passed to the   */
/*                            command                                 */
/* - present without value -> the full existing environment is passed */
/* - present with values   -> the full existing environment is        */
/*                            amended by then :                       */
/*   - if a value is the name of an executable , try to execute it    */
/*     then read the resulting output to get the environment values   */
/*   - if not, try to parse it as file containing environment         */
/*     definitions (one environment definition per line)              */
/*                                                                    */
/* Finally the parameters starting by a $ and not describing argument */
/* pattern can be used to modify the passed environment               */
/* ================================================================== */
char **
process_env(rule_t *rule, char **environ, char ***new_environ)
{
  ll_t      *env_list = NULL;
  ll_t      *val_list = NULL;
  ll_node_t *par_node;
  ll_node_t *val_node;
  param_t   *param;

  env_var_t *env_var;
  char     **penv = environ;
  char      *param_name;
  char      *val_name;
  char      *p;
  unsigned   updated;

  param = get_param(rule, "environment");
  if (param != NULL)
  {
    /* Record the whole existing environment */
    /* """"""""""""""""""""""""""""""""""""" */
    while (*penv != NULL)
    {
      env_var = xmalloc(sizeof(env_var_t));

      p              = strchr(*penv, '=');
      env_var->name  = xstrndup(*penv, p - *penv);
      env_var->value = xstrdup(p + 1);

      env_tree = bst_insert(env_tree, env_var, item_comp, &updated);
      if (!updated)
        env_count++;

      penv++;
    }

    /* The command environment can be modified by the "environment" */
    /* parameters values in order                                   */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    env_list = param->val_list;
    if (env_list != NULL && env_list->len > 0)
    {
      val_node = env_list->head;
      while (val_node)
      {
        val_name = (char *)val_node->data;

        if (env_tree == NULL && strcmp(val_name, "-") == 0)
        {
          if (val_node == env_list->head)
          {
            bst_delete_tree(env_tree, free_env_var);
            env_tree  = NULL;
            env_count = 0;
          }
          else
            trace(LOG_DATA,
                  "[%s] "
                  "the special \"-\" (clear environment item) "
                  "can only be at the first position",
                  rule->tag);

          val_node = val_node->next;

          continue;
        }

        read_env_from_file(rule, val_name);

        val_node = val_node->next;
      }
    }
  }

  /* Environment variables can be overloaded or removed from   */
  /* the environment with $PARAMETER placed as arguments in    */
  /* the command parameters.                                   */
  /* The name must be follow by 0 or 1 value, a variable       */
  /* without value will be removed from the final environment. */
  /* A previously existing variable will be replaced.          */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  par_node = rule->param_list->head;
  while (par_node)
  {
    param      = (param_t *)par_node->data;
    param_name = param->name;

    if (param_name[0] == '$')
    {
      if (param_name[1] == '_' || isalpha(param_name[1]))
      {
        val_list = param->val_list;
        if (val_list->len <= 1)
        {
          unsigned i;

          for (i = 2; param_name[i] != '\0'; i++)
            if (param_name[i] != '_' && !isalnum(param_name[i]))
            {
              trace(LOG_DATA,
                    "[%s] \"%s\": invalid environment variable name.",
                    rule->tag,
                    param_name);
              goto ignore;
            }

          env_var = xmalloc(sizeof(env_var_t));

          env_var->name = xstrdup(param_name + 1);

          if (val_list->len == 1)
            env_var->value = xstrdup((char *)val_list->head->data);
          else
            env_var->value = xstrdup("");

          env_tree = bst_insert(env_tree, env_var, item_comp, &updated);

          if (!updated)
            env_count++;
        }
        else
          trace(LOG_DATA,
                "[%s] \"%s\": invalid environment variable definition.",
                rule->tag,
                param_name);
      }
      else
      {
        char c = param_name[1];

        /* Ignore filtering parameters. */
        /* """""""""""""""""""""""""""" */
        if (c != '*' && c != '+' && c != ',' && c != ';' && c != '.' && c != '?'
            && !isdigit(c))
          trace(LOG_DATA,
                "[%s] \"%s\": invalid environment variable name.",
                rule->tag,
                param_name);
      }
    }
  ignore:

    par_node = par_node->next;
  }

  /* Create a new array of strings to receive the variables defined in the */
  /* bst tree after possible modifications as seen in the command          */
  /* parameters. The last element will be NULL.                            */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  *new_environ = xcalloc(sizeof(char *), env_count + 1);
  bst_foreach(env_tree, add_to_environ, new_environ);
  bst_delete_tree(env_tree, free_env_var);
  env_tree = NULL;

  (*new_environ)[env_count] = NULL;

  return *new_environ;
}

/* ====================================================================== */
/* This function is called by the main and the process_env function.      */
/*                                                                        */
/* It is an utility function able to load environment variables from a    */
/* file.                                                                  */
/*                                                                        */
/* The first argument must be NULL when this function is called when      */
/* processing the "Environ Generator" variable in sys.cfg file as no rule */
/* has been parsed yet.                                                   */
/* The second argument must be a command line.                            */
/* The first word of this command line must name an existing executable   */
/* file belonging to root.                                                */
/*                                                                        */
/* Return 1 on success, 0 on failure.                                     */
/* ====================================================================== */
int
read_env_from_file(rule_t *rule, char *command_line)
{
  enum
  {
    OPEN,
    POPEN
  } mode;

  env_var_t *env_var;

  char **command;
  int    count;

  pid_t       pid;
  struct stat st;
  FILE       *f;
  char       *buf;
  char       *p;
  unsigned    updated;
  ssize_t     read;

  int rc = 1;

  /* Split the value into words */
  /* """""""""""""""""""""""""" */
  strarray(command_line, &command, &count, " \t", KEEPQUOTES);

  errno = 0;
  if (stat(command[0], &st) == -1)
  {
    trace(LOG_WARN,
          "[%s] invalid command %s (%s).",
          (rule == NULL) ? "cfg" : rule->tag,
          command[0],
          strerror(errno));

    rc = 0;
    goto clean;
  }

  /* Only consider regular or link files */
  /* """"""""""""""""""""""""""""""""""" */
  if (!S_ISREG(st.st_mode))
  {
    trace(LOG_INFO,
          "environment: %s is not a regular file nor a link.",
          command[0]);

    rc = 0;
    goto clean;
  }

  /* Only consider wommand with a full path name. */
  /* """""""""""""""""""""""""""""""""""""""""""" */
  if (*command[0] != '/')
  {
    trace(LOG_INFO, "environment: %s must include a full path.", command[0]);

    rc = 0;
    goto clean;
  }

  if ((st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) > 0)
  {
    /* Only executable files can be executed to get environment variables. */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */

    /* The file must belong to root. */
    /* """"""""""""""""""""""""""""" */
    if (st.st_uid != 0 || st.st_gid != 0)
    {
      trace(LOG_WARN,
            "environment: %s must be owned by root:root.",
            command[0]);

      rc = 0;
      goto clean;
    }

    /* The file must not be writable by others. */
    /* """""""""""""""""""""""""""""""""""""""" */
    if ((st.st_mode & S_IWOTH) > 0)
    {
      trace(LOG_WARN, "environment: %s may be written by anybody.", command[0]);

      rc = 0;
      goto clean;
    }

    mode = POPEN;
    f    = popen_exec(command, "r", &pid);
  }
  else
  {
    /* Non executable files will be read to get environment variables */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    mode = OPEN;
    f    = fopen(command[0], "r");
  }

  /* In case of success: */
  /* """"""""""""""""""" */
  if (f != NULL)
  {
    size_t n = 0;

    /* Read all the lines produced by the generator and try to */
    /* feed the new environment with them                      */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""" */
    buf = NULL;
    while ((read = my_getline(&buf, &n, f)) > 0)
    {
      char *ptr;

      if ((p = strchr(buf, '=')) != NULL)
      {
        /* The environment variable name must have */
        /* at least 1 character.                   */
        /* """"""""""""""""""""""""""""""""""""""" */
        if (*buf == '=')
          goto nextline;

        *p            = '\0';
        buf[read - 1] = '\0';

        env_var       = xmalloc(sizeof(env_var_t));
        env_var->name = xstrdup(buf);

        /* The environment variable name must start */
        /* with '_' or an alphabetic character.     */
        /* """""""""""""""""""""""""""""""""""""""" */
        if (!isalpha(*env_var->name) && *env_var->name != '_')
          goto nextline;

        /* The rest of the environment variable name */
        /* must be '_' or an alphabetic or numeric   */
        /* character.                                */
        /* """"""""""""""""""""""""""""""""""""""""" */
        ptr = env_var->name + 1;
        while (*ptr != '\0')
        {
          if (!isalnum(*ptr) && *ptr != '_')
            goto nextline;
          ptr++;
        }

        env_var->value = xstrdup(p + 1);

        /* Update the environ variable tree */
        /* """""""""""""""""""""""""""""""" */
        env_tree = bst_insert(env_tree, env_var, item_comp, &updated);
        if (!updated)
          env_count++;
      }

    nextline:

      if (buf)
        free(buf);
      buf = NULL;
      n   = 0;
    }
  }
  else
    trace(LOG_INFO, "environment: cannot open %s.", command[0]);

  if (mode == POPEN)
    pclose_exec(f, pid);

clean:
  for (int n = count; n--;)
    free(command[n]);
  free(command);

  return rc;
}

/* ==================================================================== */
/* This function is called by the exec_command function.                */
/*                                                                      */
/* If the "umask" parameter is set then try to use its value to set the */
/* umask of the command to be executed.                                 */
/* if present with no value, then the parent's umask is used.           */
/* This parameter only takes one value, the others will be ignored.     */
/* ==================================================================== */
void
set_umask(rule_t *rule)
{
  mode_t   mask;
  param_t *param;

  mask  = umask(0);
  param = get_param(rule, "umask");
  if (param == NULL)
    umask(mask);
  else
  {
    ll_t *mask_list;

    mask_list = param->val_list;
    if (mask_list->len == 0)
      umask(mask);
    else
    {
      char  *end;
      mode_t new_mask;

      errno    = 0;
      new_mask = strtol(mask_list->head->data, &end, 8);
      if (*end != '\0' || errno == EINVAL)
        umask(mask);
      else
        umask(new_mask);

      if (mask_list->len > 1)
        trace(LOG_DATA,
              "[%s] "
              "too many umasks, only the first will be considered.",
              rule->tag);
    }
  }
}

/* ========================================================================= */
/* This function is called by main.                                          */
/*                                                                           */
/* Apply various checks to see if the command of the command can be executed */
/* Notable checks:                                                           */
/* - check_paths                                                             */
/* - check_users_groups_netgroups                                            */
/* - check_rule_options                                                      */
/* - check_plugins                                                           */
/*                                                                           */
/* Calls exec_command to try to run an executable.                           */
/* ========================================================================= */
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
             char   *req_group,
             user_t *user_data,
             ht_t   *user_pw_ok_ht,
             char   *plugins_dir,
             char  **extra_path_array,
             char  **error_msg)
{
  ll_t      *val_list;
  ll_node_t *node;
  param_t   *pwd_param;
  param_t   *disabled_param;

  uid_t req_uid;
  char *req_username;
  gid_t req_gid;
  char *req_groupname;

  int identity_failure; /* if the password attribute is present           */
                        /* and the usersg/groups/netgroups checking fails */
                        /* ask a password to allow the command.           */

  char *err;
  char *failed_plugin = NULL;

  char *val;
  char *str;

  *new_uid   = 0;
  *new_gid   = 0;
  *error_msg = NULL;

  /* Abort if the invalid parameter is present in the rule. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (get_param(rule, "invalid"))
  {
    my_asprintf(error_msg,
                "The rule \"%s\" is invalid, ask your sys administrator "
                "to analyze the logs and fix it.",
                rule->tag);

    goto error;
  }

  /* Check is the rule is disabled and tell the user why. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
  disabled_param = get_param(rule, "disabled");
  if (disabled_param)
  {
    ll_t      *val_list;
    ll_node_t *node;
    char      *val;

    printf("The rule \"%s\" has been disabled, by your sys administrator.\n",
           rule->tag);

    val_list = disabled_param->val_list;
    if (val_list && val_list->len > 0)
    {
      printf("Reason:\n");
      node = (ll_node_t *)val_list->head;
      while (node)
      {
        val = (char *)node->data;
        printf("%s\n", val);

        node = node->next;
      }
    }
    exit(EXIT_FAILURE);
  }

  {
    /* Check if the executable is not present in a denied path. */
    /* '''''''''''''''''''''''''''''''''''''''''''''''''''''''' */
    if (check_paths(rule, rule->executable, "!paths", real_pathname))
    {
      char *msg = "[%s] path constraints not respected for \"%s\".";

      my_asprintf(error_msg, msg, rule->tag, rule->executable);

      trace(LOG_WARN, msg, rule->tag, rule->executable);
      goto error;
    }

    /* Check if the executable is present in a mandatory path. */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (!check_paths(rule, rule->executable, "paths", real_pathname))
    {
      char *msg = "[%s] path constraints not respected for \"%s\".";

      my_asprintf(error_msg, msg, rule->tag, rule->executable);

      trace(LOG_WARN, msg, rule->tag, rule->executable);
      goto error;
    }
  }

  /* Store the requested uid and user name. */
  /* """""""""""""""""""""""""""""""""""""" */
  if (req_user && str_to_user(req_user, &req_uid, &req_username) == -1)
  {
    my_asprintf(error_msg,
                "[%s] invalid requested user \"%s\".",
                rule->tag,
                req_user);

    goto error;
  }

  /* Store the requested gid and group name. */
  /* """"""""""""""""""""""""""""""""""""""" */
  if (req_group && str_to_group(req_group, &req_gid, &req_groupname) == -1)
  {
    my_asprintf(error_msg,
                "[%s] invalid requested group \"%s\".",
                rule->tag,
                req_user);

    goto error;
  }

  str = argvtostr(argc - 1, argv + 1);

  if (daemon_flag)
    trace(LOG_INFO, "%s: sys %s [Daemon] %d", user_data->name, str, getpid());
  else
    trace(LOG_INFO,
          "%s: sys %s => %d/%d",
          user_data->name,
          str,
          getpid(),
          getppid());

  free(str);

  *new_username = *new_groupname = NULL;

  /* Get the target uid */
  /* """""""""""""""""" */
  val_list = get_param_val_list(rule, "uid");
  if (val_list)
  {
    if (val_list->len == 0)
    {
      my_asprintf(error_msg, "[%s] uid: the user/uid is not given.", rule->tag);

      goto error;
    }

    if (req_user == NULL)
    {
      /* Without value, the default uid is 0 (root) */
      /* """""""""""""""""""""""""""""""""""""""""" */
      node = (ll_node_t *)val_list->head;
      val  = (char *)node->data;

      if (str_to_user(val, new_uid, new_username) == -1)
      {
        my_asprintf(error_msg,
                    "[%s] uid: unknown user \"%s\".",
                    rule->tag,
                    val);

        goto error;
      }
    }
    else
    {
      /* With value(s), check if the requested user is one in which */
      /* new_username belongs to.                                   */
      /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      int found = 0;

      node = (ll_node_t *)val_list->head;
      while (node)
      {
        int uid; /* Temp variable for is_number. */

        val = (char *)node->data;
        if (str_to_user(val, new_uid, new_username) == -1)
        {
          my_asprintf(error_msg,
                      "[%s] uid: unknown user \"%s\".",
                      rule->tag,
                      val);

          goto error;
        }

        if (!is_number(&uid, val))
        {
          if (strcmp(*new_username, req_username) == 0)
          {
            found = 1;
            break;
          }
        }
        else if (*new_uid == req_uid)
        {
          found = 1;
          break;
        }

        node = node->next;
      }

      if (!found)
      {
        my_asprintf(error_msg,
                    "[%s], \"%s\" is not found in the allowed usernames/uids.",
                    rule->tag,
                    req_user);

        goto error;
      }
    }
  }
  else
  {
    if (req_user != NULL)
    {
      /* The user requested by -u is used if valid. */
      /* """""""""""""""""""""""""""""""""""""""""" */
      if (str_to_user(req_user, new_uid, new_username) == -1)
      {
        my_asprintf(error_msg,
                    "[%s] uid: unknown user \"%s\".",
                    rule->tag,
                    req_user);

        goto error;
      }
    }
    else
      *new_username = "root";
  }

  /* Get the target gids                                  */
  /*                                                      */
  /* if gid is not present gid 0 is assumed.              */
  /* if gid is present with values, they will replace the */
  /*   supplementary gids with the first one promoted as  */
  /*   the new gid.                                       */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""" */
  val_list = get_param_val_list(rule, "gid");
  if (val_list)
  {
    if (val_list->len == 0)
    {
      my_asprintf(error_msg,
                  "[%s] gid: the group/gid is not given.",
                  rule->tag);

      goto error;
    }

    if (req_group == NULL)
    {
      /* Without value, the default gid is 0 (root) */
      /* """""""""""""""""""""""""""""""""""""""""" */
      node = (ll_node_t *)val_list->head;
      val  = (char *)node->data;

      if (str_to_group(val, new_gid, new_groupname) == -1)
      {
        my_asprintf(error_msg,
                    "[%s] gid: unknown group \"%s\".",
                    rule->tag,
                    val);

        goto error;
      }
    }
    else
    {
      /* With value(s), check if the requested group is one in which */
      /* new_username belongs to.                                    */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      int found = 0;

      node = (ll_node_t *)val_list->head;
      while (node)
      {
        int gid; /* Temp variable for is_number. */

        val = (char *)node->data;
        if (str_to_group(val, new_gid, new_groupname) == -1)
        {
          my_asprintf(error_msg,
                      "[%s] gid: unknown group \"%s\".",
                      rule->tag,
                      val);

          goto error;
        }

        if (!is_number(&gid, val))
        {
          if (strcmp(*new_groupname, req_groupname) == 0)
          {
            found = 1;
            break;
          }
        }
        else if (*new_gid == req_gid)
        {
          found = 1;
          break;
        }

        node = node->next;
      }

      if (!found)
      {
        my_asprintf(error_msg,
                    "[%s], \"%s\" is not found in the allowed groupnames/gids.",
                    rule->tag,
                    req_group);

        goto error;
      }
    }
  }
  else
  {
    if (req_group != NULL)
    {
      /* The group requested by -g is used if valid. */
      /* More verifications will be made later.      */
      /* """"""""""""""""""""""""""""""""""""""""""" */
      if (str_to_group(req_group, new_gid, new_groupname) == -1)
      {
        my_asprintf(error_msg,
                    "[%s] gid: unknown group \"%s\".",
                    rule->tag,
                    req_group);

        goto error;
      }
    }
    else
    {
      if (strcmp(*new_username, "root") == 0)
        *new_groupname = "root";
      else
      {
        /* The main group of the new user is used. */
        /* """"""""""""""""""""""""""""""""""""""" */
        struct passwd *pwd;

        pwd      = getpwnam(*new_username);
        *new_gid = pwd->pw_gid;
      }
    }
  }

  /* Check that the command line options match the rules given */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (!check_rule_options(rule, &argc, &argv, &err))
  {
    if (err != NULL)
    {
      my_asprintf(error_msg,
                  "[%s] This operation could not be performed: %s",
                  rule->tag,
                  err);

      goto error;
    }
  }

  /* Check if the parameter "password" is set.                       */
  /* If set, a password will be asked of filtering failure if none   */
  /* of the users/groups/parameters if present and has values.       */
  /* If it is followed by a list of users then each of these user's  */
  /* passwords will be requested in sequence otherwise only the root */
  /* password will be asked for.                                     */
  /* if the "password" parameter if not set, each filtering failure  */
  /* will be fatal.                                                  */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  identity_failure = 0; /* Default is to not ask a password on failure. */

  pwd_param = get_param(rule, "password");

  if (!check_plugins(rule, plugins_dir, &failed_plugin))
  {
    my_asprintf(error_msg,
                "[%s] plugin \"%s\" has failed.",
                rule->tag,
                failed_plugin);

    goto error;
  }

  if (pwd_param) /* The "password" parameter is set. */
  {
    /* Always ask password if the password parameter is set and no */
    /* users/groups/netgroups parameter is present.                */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (!has_users_groups_netgroups_param(rule))
      identity_failure = 1;

    /* Get of initialize the list of values attached to the */
    /* password parameter.                                  */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""" */
    val_list = pwd_param->val_list;
    if (!val_list) /* password does not already has a list of users. */
      val_list = ll_new();

    /* Make sure the root user and the final user are in this list. */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (!ll_find(val_list, *new_username, str_comp))
      ll_append(val_list, *new_username);

    if (!ll_find(val_list, "root", str_comp))
      ll_append(val_list, "root");
  }

  /* Check the users/groups/paths filter. */
  /* """""""""""""""""""""""""""""""""""" */

  /* Check if the users data pass the users/groups/netgroups filters. */
  /* '''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''' */
  if (!check_users_groups_netgroups(rule, user_data))
  {
    char *msg = "[%s] the user is not in one of the allowed "
                "users/groups/netgroups.";

    identity_failure = 1;

    trace(LOG_WARN, msg, rule->tag);
  }

  /* Build a proper environment for the command associated to the command */
  /* according to the "environment" parameter                             */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  *new_environ = process_env(rule, environ, new_environ);

  /* Exec the command in its new environment. */
  /* """""""""""""""""""""""""""""""""""""""" */
  exec_command(rule,
               argc,
               argv,
               *new_environ,
               daemon_flag,
               user_data,
               user_pw_ok_ht,
               *new_uid,
               *new_gid,
               *new_username,
               *new_groupname,
               pwd_param,
               identity_failure,
               extra_path_array,
               error_msg);
error:

  return 0;
}

/* ============================================================== */
/* Fill a user_t structure with the user's data.                  */
/*                                                                */
/* user_data (out) The resulting users's data will be put here    */
/*                 The memory space will be dynamically allocated */
/*                 in this function.                              */
/* ============================================================== */
void
get_user_data(user_t *user_data)
{
  unsigned       user_groups_nb;
  struct passwd *pw;
  gid_t          gid;
  unsigned       gid_in_groups;
  unsigned       g;

  char *hostname;
  long  host_name_max;

  host_name_max = sysconf(_SC_HOST_NAME_MAX);
  if (host_name_max == -1)
    host_name_max = 256;

  hostname = xmalloc(host_name_max + 1);
  gethostname(hostname, host_name_max + 1);
  user_data->hostname = xstrdup(hostname);
  free(hostname);

  user_data->uid = getuid();
  user_data->gid = getgid();

  pw = getpwuid(user_data->uid);
  if (pw == NULL)
  {
    error("users: unknown user (pid: %d", user_data->uid);
    exit(1);
  }

  user_data->name  = xstrdup(pw->pw_name);
  user_data->shell = xstrdup(pw->pw_shell);

  /* Get all the groups of the user */
  /* """""""""""""""""""""""""""""" */
  gid            = user_data->gid;
  user_groups_nb = getgroups(0, NULL);

  user_data->groups      = xmalloc((user_groups_nb + 1) * sizeof(gid_t));
  user_data->group_names = xmalloc((user_groups_nb + 1) * sizeof(char *));

  getgroups(user_groups_nb + 1, user_data->groups);

  gid_in_groups = 0;
  for (g = 0; g < user_groups_nb; g++)
    if (gid == user_data->groups[g])
    {
      gid_in_groups = 1;
      break;
    }

  /* If the real gid is not in the returned array, add it at its end */
  /* ''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''' */
  if (!gid_in_groups)
  {
    user_data->groups[user_groups_nb] = gid;
    user_groups_nb++;
  }
  user_data->groups_nb = user_groups_nb;

  for (g = 0; g < user_groups_nb; g++)
  {
    struct group *grp;

    if ((grp = getgrgid(user_data->groups[g])) != NULL)
      user_data->group_names[g] = xstrdup(grp->gr_name);
    else
      user_data->group_names[g] = xstrdup("");
  }
}

/* ============================================ */
/* Separate an absolute pathname into its parts */
/* command (in)  absolute pathname              */
/* path    (out) directory of NULL (mallocated) */
/* base    (out) executable name   (mallocated) */
/* ============================================ */
void
split_command_path(char *command, char **path, char **base)
{
  char *slash;

  /* Split the command into path and base */
  /* """"""""""""""""""""""""""""""""""" */
  slash = strrchr(command, '/');
  if (slash != NULL)
  {
    *path = xstrndup(command, slash - command);
    *base = xstrdup(slash + 1);
  }
  else
  {
    *path = NULL;
    *base = xstrdup(command);
  }
}

/* =================================================================== */
/* Build a new rule_t element from data read from the cache the        */
/* element if allocated in this function and should be freed by the    */
/* caller.                                                             */
/*                                                                     */
/* rule_tag      (in) The command name to search for or "@" if we      */
/*                    are looking for a generic command                */
/* orig_rule_tag (in) The command name to search. If rule_tag == "@"   */
/*                    Then the op's command is forged from this        */
/*                    variable.                                        */
/* data          (in) A pointer to the raw data that forms the command */
/* data_len      (in) The length of the raw data                       */
/* =================================================================== */
static rule_t *
build_rule_from_cache(char    *rule_tag,
                      char    *orig_rule_tag,
                      char    *data,
                      uint16_t data_len)
{
  char   *p = (char *)data;
  rule_t *rule;

  rule = xmalloc(sizeof(rule_t));

  /* Reconstruction of a rule_t variable from the raw data */
  /* read from the cache.                                 */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""" */
  rule->tag = xstrdup(rule_tag);
  if (rule_tag[0] == '@')
  {
    /* Replace the dummy '@' by the tag. */
    /* """"""""""""""""""""""""""""""""" */
    rule->command = xmalloc(strlen(orig_rule_tag) + strlen(p));
    strcpy(rule->command, orig_rule_tag);
    strcat(rule->command, p + 1);
    rule->executable = xstrdup(orig_rule_tag);
  }
  else
  {
    rule->command                       = xstrdup(p);
    rule->executable                    = xstrdup(p);
    rule->executable[strcspn(p, " \t")] = '\0';
  }

  if (data_len <= 2)
    error("cache: parameters are missing in the definition of %s.", rule_tag);

  p += strlen(p) + 1;

  rule->param_list = ll_new();

  while (p - data + 1 < data_len)
  {
    char    *str;
    size_t   len;
    param_t *param;

    param = xmalloc(sizeof(param_t));

    len = strlen(p);
    str = strchr(p, ':');
    if (str == NULL)
    {
      param->name     = xstrdup(p);
      param->val_list = ll_new();
    }
    else if (strcmp(str, "=") == 0)
    {
      *str            = '\0';
      param->name     = xstrdup(p);
      param->val_list = ll_new();
    }
    else
    {
      char **values;
      int    i, count;

      *str            = '\0';
      param->name     = xstrdup(p);
      param->val_list = ll_new();
      strarray(str + 1, &values, &count, ",", NOOCTAL | KEEPQUOTES);
      for (i = 0; i < count; i++)
        ll_append(param->val_list, values[i]);
      free(values);
    }

    ll_append(rule->param_list, param);

    p += len + 1;
  }
  free(data);

  return rule;
}

/* ======================================================================= */
/* When the rule is parsed from the .dat files, the parameter's values are */
/* put into a linked list.                                                 */
/* The rule is then put in a BST tree. When the cache is not available,    */
/* the desired rule designated by its tag is searched in this tree but     */
/* variables expansions have already occurred and some variable might      */
/* have expanded to multiple values. Its is then necessary to recreate the */
/* value lists of all the parameters of this operation to get the right    */
/* values.                                                                 */
/* ======================================================================= */
void
update_params_val_lists(rule_t *rule)
{
  ll_node_t *param_node;
  ll_t      *new_val_list;
  param_t   *param;
  char     **val_array;
  char      *val;
  int        val_count;
  int        i, rc;

  /* For each parameter of rule */
  /* """"""""""""""""""""""""" */
  param_node = rule->param_list->head;
  while (param_node)
  {
    ll_node_t *val_node;

    param        = (param_t *)param_node->data;
    new_val_list = ll_new();

    /* For each of its values */
    /* """""""""""""""""""""" */
    val_node = param->val_list->head;
    while (val_node)
    {
      val = (char *)val_node->data;
      rc  = strarray(val, &val_array, &val_count, ",", MERGESEPS | KEEPQUOTES);

      if (rc != 0 || val_count == 1)
        /* The value could not have been split, put the unmodified value */
        /* to the newly created list.                                    */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        ll_append(new_val_list, xstrdup(val));
      else
      {
        /* The value contains commas hence split it into its components */
        /* and add them as new values to the newly created list.        */
        /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        for (i = 0; i < val_count; i++)
          ll_append(new_val_list, xstrdup(val_array[i]));
      }

      /* Remove the old value from the old list */
      /* """""""""""""""""""""""""""""""""""""" */
      free(val);
      ll_delete(param->val_list, val_node);

      val_node = param->val_list->head;
    }
    /* Free the old list and plug the new list. */
    /* """""""""""""""""""""""""""""""""""""""" */
    free(param->val_list);
    param->val_list = new_val_list;

    param_node = param_node->next;
  }
}

/* =============== */
/* Usage function. */
/* =============== */
void
usage(char *progname)
{
  printf("Usage: %s [Options] tag [Tag_options]\n", progname);
  printf("       %s -l\n\n", progname);

  puts("Options:");
  puts("-l     : list the allowed tags for the current user.");
  puts("-d     : daemonize.");
  puts("-u uid : launch with a specific user.");
  puts("-g uid : launch with a specific group.");
  puts("-v     : verbose mode (not implemented yet).");
  puts("-h     : synopsis.");

  exit(1);
}

/* =================== */
/* Program entry point */
/* =================== */
int
main(int argc, char **argv)
{
  int    new_argc;
  char **new_argv;

  rule_tree = NULL;
  var_tree  = NULL;
  env_tree  = NULL;
  env_count = 0;

  ht_t user_pw_ok_ht;

  char *log_filename;

  char *search_dirs = NULL;
  char *log_dir     = NULL;
  char *cache_dir   = NULL;
  char *plugins_dir = NULL;
  char *env_initial_source;
  char *extra_default_paths;

  char **extra_path_array;

  long  max_ext_commands;
  char *max_ext_commands_str = NULL;

  user_t user_data;

  ll_node_t *data_dirnames_elem;

  cache_status_t cache_status;

  ll_t *data_dirnames_list;
  ll_t *data_filenames_list;
  ll_t *misc_filenames_list;

  char *rule_tag;

  rule_t *rule;

  struct ini_info *ini;
  unsigned         ini_error;
  unsigned         ini_error_line;

  char **new_environ   = NULL;
  char  *real_pathname = NULL;

  char  *cfg_filename;
  char  *cache_filename;
  char **data_dirnames;
  int    dirnames_count;

  time_t    t;
  struct tm tm;
  char      day_of_year[4];

  int i;
  int ch;

  struct stat st;

  int   shell_flag   = 0;
  int   verbose_flag = 0;
  int   list_flag    = 0;
  int   daemon_flag  = 0;
  char *req_groups   = NULL;
  char *req_user     = NULL;

  char *error_msg = NULL;

  opterrfd = fileno(stdout); /* Errors to stdout.                          */
  opterr   = 0;              /* Set this to 1 to get egetopt's error msgs. */
  optbad   = '?';            /* Return '?' on error.                       */
  optneed  = ':';            /* Mandatory arg identifier (in OPT_STRING).  */
  optmaybe = '%';            /* Optional arg identifier (in OPT_STRING).   */
  optstart = "-";            /* Characters that can start options.         */

  path_max = pathconf(".", _PC_PATH_MAX);
  if (path_max == -1)
    path_max = 4096;

  /* Manage command line options. */
  /* """""""""""""""""""""""""""" */
  while ((ch = egetopt(argc, argv, "ivlVu:g:dh")) != EOF)
  {
    switch (ch)
    {
      case 'i':
        shell_flag = 1;
        break;
      case 'v':
        verbose_flag = 1;
        fprintf(stderr, "%s\n", "-v is not implemented yet!");
        break;
      case 'l':
        list_flag = 1;
        break;
      case 'V':
        puts(VERSION);
        exit(0);
      case 'u':
        req_user = xstrdup(optarg);
        break;
      case 'g':
        req_groups = xstrdup(optarg);
        break;
      case 'd':
        daemon_flag = 1;
        break;
      case 'h':
        usage(argv[0]);
        break;
    }
    if (ch == optbad)
    {
      fprintf(stderr, "invalid option (%c)\n", optopt);
      exit(1);
    }
  }

  /* Basic verification. */
  /* """"""""""""""""""" */
  if (list_flag && argc > optind)
    fatal("giving a tag is not allowed when in list mode.");

  if (!list_flag && argc == optind)
    fatal("you must provide a tag.");

  /* Store the tag. */
  /* """""""""""""" */
  if (!list_flag)
    rule_tag = xstrdup(argv[optind]);

/* Manage the configuration and the cache directories assignments. */
/* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
#ifdef SYS_CFG_DIR
  cfg_filename = xmalloc(strlen(SYS_CFG_DIR) + strlen(SYS_CFG) + 1 + 1);
  strcpy(cfg_filename, SYS_CFG_DIR);
  strcat(cfg_filename, "/" SYS_CFG);
#else /* Default path for the configuration. */
  cfg_filename = xstrdup("/etc/" SYS_CFG);
#endif

  misc_filenames_list = ll_new();
  ll_append(misc_filenames_list, cfg_filename);

  /* Get the directories in which we will search the .dat files.  */
  /* from the configuration file and store them in a linked list. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  ini = ini_load(cfg_filename, &ini_error, &ini_error_line);
  if (ini == NULL)
  {
    if (ini_error > 0)
      fatal("cannot parse the configuration file (line %u).", ini_error_line);
    else
      fatal("cannot open the configuration file");
  }

  /* Get the logging directory and build the logging file name.   */
  /* On success open it as log_fh.                                */
  /* Logging is essential so any error while setting it is fatal. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  log_dir = ini_get(ini, "Directories", "Logs");
  if (log_dir == NULL)
    log_dir = xstrdup("/var/log/sys");
  else
  {
    ltrim(log_dir, " \t");
    rtrim(log_dir, " \t");

    if (*log_dir == '\0')
    {
      free(log_dir);
      log_dir = xstrdup("/var/log/sys");
    }
  }

  /* Look if we wan get some information from the logging directory and */
  /* if it has proper permissions.                                      */
  /* '''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''' */
  if (stat(log_dir, &st) == 0)
  {
    if (S_ISREG(st.st_mode))
      fatal("%s exists but is not a directory.", log_dir);

    if (st.st_uid != 0 || st.st_gid != 0)
      fatal("%s must be owned by root:root.", log_dir);

    /* TODO improve this test. */
    if ((st.st_mode & 0777) != 0700)
      fatal("%s must have read/write/execute rights for root only.", log_dir);
  }
  else /* Try to create it. */
  {
    int err = 0;

    if (mkdir(log_dir, 0700) == -1 && (err = errno) != EEXIST)
      fatal("cannot create the logs directory %s.", log_dir);

    if (err != EEXIST)
      chown(log_dir, 0, 0);
  }

  log_filename  = xmalloc(strlen("sys.") + strlen(log_dir) + 5);
  *log_filename = '\0';

  /* Add the day number as extension. */
  /* '''''''''''''''''''''''''''''''' */
  t = time(NULL);
  (void)localtime_r(&t, &tm);

  sprintf(day_of_year, "%03d", tm.tm_yday);
  strcat(log_filename, log_dir);
  strcat(log_filename, "/sys.");
  strcat(log_filename, day_of_year);

  log_fh = log_init(log_filename);

  /* Get the cache directory which must already exist for sys to work */
  /* the cache is essential so any error while setting it is fatal.   */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  cache_dir = ini_get(ini, "Directories", "Cache");
  if (cache_dir == NULL)
    cache_dir = xstrdup("/etc");
  else
  {
    ltrim(cache_dir, " \t");
    rtrim(cache_dir, " \t");

    if (*cache_dir == '\0')
    {
      free(cache_dir);
      cache_dir = xstrdup("/etc");
    }
  }

  cache_filename = xmalloc(strlen(cache_dir) + strlen(SYS_CACHE) + 1 + 1);
  strcpy(cache_filename, cache_dir);
  strcat(cache_filename, "/" SYS_CACHE);

  /* Make sure cache_dir is a directory. */
  /* ''''''''''''''''''''''''''''''''''' */
  if (stat(cache_dir, &st) == 0)
  {
    if (S_ISREG(st.st_mode))
      fatal("%s exists but is not a directory.", cache_dir);
  }

  search_dirs = ini_get(ini, "Directories", "Data");
  if (search_dirs == NULL)
    error("conf: no directories entry found in the configuration file.");
  else
  {
    ltrim(search_dirs, " \t");
    rtrim(search_dirs, " \t");
    if (*search_dirs == '\0')
      error("conf: no directories entry found in the configuration file.");
  }

  /* Split the values in 'Data' and store the resulting strings in */
  /* search_dirs.                                                  */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (strarray(search_dirs,
               &data_dirnames,
               &dirnames_count,
               ", \t",
               MERGESEPS | KEEPQUOTES)
      != 0)
    error("conf: cannot parse directories from the configuration file.");

  if (dirnames_count == 0)
    error("conf: no directories declared in the configuration file.");

  /* Data directories must already exist, and belong to root and have   */
  /* read, write and execute rights for root only.                      */
  /* A directory candidate will be ignored if it is not a directory or  */
  /* does not fulfill the above conditions.                             */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  data_dirnames_list = ll_new();

  /* Process each directories in data_dirnames. */
  /* and put the in a linked list.              */
  /* '''''''''''''''''''''''''''''''''''''''''' */
  i = 0;
  while (data_dirnames[i] != NULL)
  {
    /* Validate the directory. */
    /* ''''''''''''''''''''''' */
    if (stat(data_dirnames[i], &st) == 0)
    {
      if (S_ISREG(st.st_mode))
      {
        trace(LOG_WARN, "%s ignored, not a directory.", data_dirnames[i]);
        goto next_data_dirname;
      }

      if (st.st_uid != 0 || st.st_gid != 0)
      {
        trace(LOG_WARN, "%s must be owned by root:root.", data_dirnames[i]);
        goto next_data_dirname;
      }

      /* TODO improve this test. */
      if ((st.st_mode & 0777) != 0700)
      {
        trace(LOG_WARN,
              "%s ignored, must have read/write/execute rights for root only.",
              data_dirnames[i]);
        goto next_data_dirname;
      }
    }
    ll_append(data_dirnames_list, data_dirnames[i]);

  next_data_dirname:
    i++;
  }

#ifdef PLUGINS_ENABLED
  /* Get and validate the plugins directory. The plugin directory */
  /* Must already exist and will non be automatically created.    */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  plugins_dir = ini_get(ini, "Directories", "Plugins");
  if (plugins_dir == NULL)
    plugins_dir = xstrdup("/etc/sys_plugins");
  else
  {
    ltrim(plugins_dir, " \t");
    rtrim(plugins_dir, " \t");
    if (*plugins_dir == '\0')
    {
      free(plugins_dir);
      plugins_dir = xstrdup("/etc/sys_plugins");
    }
  }

  /* Create the directory if it doesn't already exists. */
  /* '''''''''''''''''''''''''''''''''''''''''''''''''' */
  if (stat(plugins_dir, &st) == 0)
  {
    if (S_ISREG(st.st_mode))
      error("%s exists but is not a directory.", plugins_dir);

    if (st.st_uid != 0 || st.st_gid != 0)
      error("%s must be owned by root:root.", plugins_dir);

    /* TODO improve this test. */
    if ((st.st_mode & 0777) != 0700)
      error("%s must have read/write/execute rights for root only.",
            plugins_dir);
  }
#endif

  /* Determine the maximum number of external commands allowed. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  max_ext_commands_str = ini_get(ini, "Miscellaneous", "Max External Commands");
  if (max_ext_commands_str != NULL)
  {
    char *endptr;

    ltrim(max_ext_commands_str, " \t");
    rtrim(max_ext_commands_str, " \t");

    if (*max_ext_commands_str)
    {
      errno            = 0;
      max_ext_commands = strtol(max_ext_commands_str, &endptr, 0);
      if (errno != 0 || *endptr != '\0')
        max_ext_commands = 8; /* Default value. */
      else if (max_ext_commands < 1)
        max_ext_commands = 1; /* max_ext_commands cannot be 0 or negative. */
    }
  }
  else
    max_ext_commands = 8; /* Default value. */

  /* Process the initial environment setting if any. */
  /* """"""""""""""""""""""""""""""""""""""""""""""" */
  env_initial_source = ini_get(ini, "Miscellaneous", "Initial environment");
  if (env_initial_source)
  {
    ltrim(env_initial_source, " \t");
    rtrim(env_initial_source, " \t");

    if (*env_initial_source)
      read_env_from_file(NULL, env_initial_source);
  }

  /* Process the default paths setting if any. */
  /* """"""""""""""""""""""""""""""""""""""""" */
  extra_default_paths = ini_get(ini, "Miscellaneous", "Default paths");
  if (extra_default_paths)
  {
    int n;

    ltrim(extra_default_paths, " \t");
    rtrim(extra_default_paths, " \t");
    if (*extra_default_paths)
    {
      if (strarray(extra_default_paths,
                   &extra_path_array,
                   &n,
                   ", \t",
                   MERGESEPS | KEEPQUOTES)
          != 0)
        trace(LOG_WARN, "[%s] invalid default paths.", cfg_filename);
    }
    else
    {
      free(extra_default_paths);
      extra_path_array = NULL;
    }
  }
  else
    extra_path_array = NULL;

  /* Register the log_shutdown to be run when exiting. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""" */
  atexit(log_shutdown);

  /* Build the ordered list of all the .dat files found. */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""" */
  data_filenames_list = ll_new();
  data_dirnames_elem  = data_dirnames_list->head;
  while (data_dirnames_elem)
  {
    create_data_file_list((char *)data_dirnames_elem->data,
                          data_filenames_list);
    data_dirnames_elem = data_dirnames_elem->next;
  }

  /* Duplicate argc and argv. */
  /* """""""""""""""""""""""" */
  new_argv = xmalloc((argc + 1) * sizeof(char *));
  new_argc = argc;

  for (i = 0; i < argc; i++)
    new_argv[i] = xstrdup(argv[i]);
  new_argv[argc] = NULL;

  get_user_data(&user_data);

  /* Initialize the password hash table which will contain the accounts */
  /* for witch the password has already been validated.                 */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  ht_new(&user_pw_ok_ht, 7);

  /* Algorithm used to get the content of the rules:           */
  /* If the cache exists, is coherent and is not outdated then */
  /*   Use it to get the rules.                                */
  /* Else                                                      */
  /*   get the rules from data files.                          */
  /*   Update the cache with these data in the background.     */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */

  /* Check if we can use the cache or if we must rebuild it. */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
  cache_status = cache_get_status(cache_filename);
  if (!list_flag && cache_status == USABLE && cache_crc16_check(cache_filename)
      && !cache_is_outdated(cache_filename, data_filenames_list)
      && !cache_is_outdated(cache_filename, misc_filenames_list))
  {
    /* ------------------------------------------------------------- */
    /* The cache is usable, not outdated and has the right checksum. */
    /* ------------------------------------------------------------- */
    char    *data;
    uint16_t data_len;

    uid_t new_uid;
    gid_t new_gid;
    char *new_username;
    char *new_groupname;

    if (cache_search(cache_filename, rule_tag, (void **)&data, &data_len))
    {
      rule = build_rule_from_cache(rule_tag, rule_tag, data, data_len);
      process_rule(rule,
                   new_argc,
                   new_argv,
                   &new_environ,
                   &new_uid,
                   &new_gid,
                   &new_username,
                   &new_groupname,
                   daemon_flag,
                   &real_pathname,
                   req_user,
                   req_groups,
                   &user_data,
                   &user_pw_ok_ht,
                   plugins_dir,
                   extra_path_array,
                   &error_msg);

      /* Output the last error message if any and abort. */
      /* """"""""""""""""""""""""""""""""""""""""""""""" */
      if (error_msg)
        error(error_msg);
    }
    else
    {
      int   n;
      char *special;

      for (n = 1; n <= max_ext_commands; n++)
      {
        /* In not found in the cache, try to locate the generic command */
        /* entry in the cache.                                          */
        /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        my_asprintf(&special, "@%d", n);
        if (cache_search(cache_filename, special, (void **)&data, &data_len))
        {
          /* Special rule tag found in cache. */
          /* """""""""""""""""""""""""""""""" */
          rule = build_rule_from_cache(special, rule_tag, data, data_len);
          process_rule(rule,
                       new_argc,
                       new_argv,
                       &new_environ,
                       &new_uid,
                       &new_gid,
                       &new_username,
                       &new_groupname,
                       daemon_flag,
                       &real_pathname,
                       req_user,
                       req_groups,
                       &user_data,
                       &user_pw_ok_ht,
                       plugins_dir,
                       extra_path_array,
                       &error_msg);

          /* Do not try the next rules in case of success. */
          /* else output the last error message.           */
          /* """"""""""""""""""""""""""""""""""""""""""""" */
          if (!error_msg)
            break;
          else
            trace(LOG_WARN, error_msg);
        }
        free(special);
      }

      /* All special rule have failed. */
      /* """"""""""""""""""""""""""""" */
      if (error_msg != NULL)
        //error("%s: invalid rule tag in \"%s\"", rule_tag,
        //      argvtostr(new_argc - 1, new_argv + 1));
        error("Special rules: %s", error_msg);
    }
  }
  else
  {
    uid_t new_uid;
    gid_t new_gid;
    char *new_username;
    char *new_groupname;

    /* -------------------------------------------------------- */
    /* The cache is unusable, outdated or has a bad checksum.   */
    /* Try to (re)build a new cache.                            */
    /* We also get here if we are in list mode. Do not rebuild  */
    /* the cache in this case.                                  */
    /* -------------------------------------------------------- */

    ll_node_t *data_filename_elem;

    /* Parse all the .dat files previously identified. */
    /* """"""""""""""""""""""""""""""""""""""""""""""" */
    data_filename_elem = data_filenames_list->head;
    while (data_filename_elem)
    {
      FILE *yyin = fopen((char *)data_filename_elem->data, "r");
      if (yyin == NULL)
        error("cannot open %s", (char *)data_filename_elem->data);

      /* yyin is not NULL here.                         */
      /* Add objects in the rule_tree and the var_tree. */
      /* """""""""""""""""""""""""""""""""""""""""""""" */
      parse(yyin);
      fclose(yyin);

      if (var_tree)
      {
        /* Only preserve variables declared as global before parsing   */
        /* the next file.                                              */
        /* The algorithm is:                                           */
        /* - create a new empty tree.                                  */
        /* - duplicate each global variables from the current var tree */
        /*   into the new tree.                                        */
        /* - delete the old tree preserving the pointer's contents.    */
        /* - make the new var tree the current one.                    */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        new_var_tree = NULL;

        bst_foreach(var_tree, copy_global_var, &new_var_tree);
        bst_delete_tree(var_tree, NULL);
        var_tree = new_var_tree;
      }

      data_filename_elem = data_filename_elem->next;
    }

    /* Put all the tree's elements in a list to create the cache */
    /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    sys_item_list = ll_new();
    bst_foreach(rule_tree, insert_in_cache, NULL);

    if (sys_item_list->len > 0)
    {
      pid_t  pid;
      rule_t dummy_rule;
      int    status;

      if ((pid = fork()) == 0)
      {
        /* We do not recreate the cache on list request.               */
        /* If the cache is invalid or outdated it will be recreate by  */
        /* the next normal invocation.                                 */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        if (list_flag)
          _exit(0);

        /* Try to (re)create the cache from this list.                     */
        /* This code runs in the background in parallel with the execution */
        /* of the designated command.  Even if collisions are unlikely,    */
        /* a collision detection system rebuilds the cache in case of      */
        /* a collision with a different hash seed until no collision is    */
        /* encountered or the maximum number of 255 rebuilds is reached.   */
        /* an invalid or UNUSABLE cache is removed after that.             */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        if (cache_create(cache_filename))
        {
          int ok = 0;

          /* The seed must take 32 bits because of the hash function used */
          /* but 256 possible values is enough for our use.               */
          /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
          for (int32_t seed = 0; seed < 255; seed++)
          {
            if (!cache_build(cache_filename, sys_item_list, seed))
              break;

            /* Here, the cache status may be USABLE or INVALID. */
            /* """""""""""""""""""""""""""""""""""""""""""""""" */
            if (cache_get_status(cache_filename) == INVALID)
              continue; /* The was a collision. */

            ok = 1; /* No collisions were encountered. */
            break;
          }

          /* Remove the invalid cache. */
          /* """"""""""""""""""""""""" */
          if (!ok)
            unlink(cache_filename); /* No verification of the return code. */
        }
        else
          /* Remove a badly created cache. */
          /* """"""""""""""""""""""""""""" */
          unlink(cache_filename); /* No verification of the return code. */

        _exit(0);
      }
      else
      {
        if (!list_flag)
        {
          dummy_rule.tag = rule_tag;
          if ((rule = bst_search(rule_tree, &dummy_rule, rule_entry_comp))
              != NULL)
          {
            /* After the variables expansion and before the command  */
            /* processing, the parameter's value list of the command */
            /* must be rebuilt as a variable might generate more the */
            /* one value.                                            */
            /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
            update_params_val_lists(rule);

            process_rule(rule,
                         new_argc,
                         new_argv,
                         &new_environ,
                         &new_uid,
                         &new_gid,
                         &new_username,
                         &new_groupname,
                         daemon_flag,
                         &real_pathname,
                         req_user,
                         req_groups,
                         &user_data,
                         &user_pw_ok_ht,
                         plugins_dir,
                         extra_path_array,
                         &error_msg);

            /* Do not try the next rules in case of success. */
            /* else output the last error message.           */
            /* """"""""""""""""""""""""""""""""""""""""""""" */
            if (error_msg)
              error(error_msg);
          }
          else
          {
            int   n;
            char *special;

            for (n = 1; n <= max_ext_commands; n++)
            {
              /* In not found in the cache, try to locate the generic command */
              /* entry in the cache.                                          */
              /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
              my_asprintf(&special, "@%d", n);
              dummy_rule.tag = special;
              if ((rule = bst_search(rule_tree, &dummy_rule, rule_entry_comp))
                  != NULL)
              {
                char  *new_command;
                char **word_array;
                int    count;

                /* Replace the dummy command '@' by the tag in rule->command. */
                /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
                new_command = xmalloc(strlen(rule->command) + strlen(rule_tag)
                                      + 1);

                strarray(rule->command,
                         &word_array,
                         &count,
                         " \t",
                         TRIM | MERGESEPS | KEEPQUOTES);

                if (strcmp(word_array[0], "@") != 0)
                  error("[%s], Invalid command in rule.", rule->tag);

                new_command = strcpy(new_command, rule_tag);

                for (i = 1; i < count; i++)
                {
                  new_command = strcat(new_command, " ");
                  new_command = strcat(new_command, word_array[i]);
                }

                free(rule->command);
                free(rule->executable);
                rule->command    = new_command;
                rule->executable = xstrdup(rule_tag);

                /* After the variables expansion and before the command  */
                /* processing, the parameter's value list if the command */
                /* must be rebuilt as a variable might generate more the */
                /* one value.                                            */
                /* """"""""""""""""""""""""""""""""""""""""""""""""""""" */
                update_params_val_lists(rule);

                process_rule(rule,
                             new_argc,
                             new_argv,
                             &new_environ,
                             &new_uid,
                             &new_gid,
                             &new_username,
                             &new_groupname,
                             daemon_flag,
                             &real_pathname,
                             req_user,
                             req_groups,
                             &user_data,
                             &user_pw_ok_ht,
                             plugins_dir,
                             extra_path_array,
                             &error_msg);

                /* Do not try the next rules in case of success. */
                /* else output the last error message.           */
                /* """"""""""""""""""""""""""""""""""""""""""""" */
                if (!error_msg)
                {
                  free(special);
                  break;
                }
                else
                  trace(LOG_WARN, error_msg);
              }
              free(special);
            }
          }
          if (error_msg)
            error("%s: tag not found in data files.", rule_tag);
        }
        else
        {
          /* List all tags the current user has access to. */
          /* """"""""""""""""""""""""""""""""""""""""""""" */
          bst_foreach(rule_tree, print_rule_tag_header, &user_data);
        }

        /* Wait for the child process to exit and get its exit status. */
        /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
        waitpid(pid, &status, 0);
      }
    }
    else
      error("No rule were found in all .dat files.\n");
  }
  exit(0);
}
