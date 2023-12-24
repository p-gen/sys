%{
  /* ################################################################### */
  /* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
  /*                                                                     */
  /* This Source Code Form is subject to the terms of the Mozilla Public */
  /* License, v. 2.0. If a copy of the MPL was not distributed with this */
  /* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
  /* ################################################################### */
  
  #include <unistd.h>
  #include <stdio.h>
  #include <string.h>
  #include <stdint.h>
  #include "comp.h"
  #include "bst.h"
  #include "list.h"
  #include "types.h"
  #include "xmalloc.h"
  #include "log.h"
  #include "strarray.h"
  #include "conf.h"
  #include "utils.h"
  
  static char *cur_content;
  static char  sep = ':';
  static ll_t *seen_param_list;
  
  extern bst_node_t *rule_tree;
  extern bst_node_t *var_tree;
  
  var_t   *var  = NULL;
  rule_t  *rule = NULL;
  param_t *param;
  
  static void
  manage_eol(void);
  
  void
  var_check_dependencies(void *node, void *arg);
  
  int
  expand_var_ref(char **str);
  
  static void
  process_old_var(void);
  
  static void
  merge_old_params(void);
  
  static void
  process_old_rule(void);
  
  static param_t *
  new_param(char *yytext, char sep);
%}

%s IN_RULE
%s IN_PARAM
%s IN_VAR

NAME    [a-zA-Z_][a-zA-Z0-9_]*
CR      [\n]
SPACE   [ \t]
PREFIX  [ \t]+
PARAM1  "uid"|"gid"|"paths"|"users"|"groups"|"netgroups"
PARAM2  "environment"|"password"|"help"|"umask"|"owners"|"disabled"
PARAM   ({PARAM1}|{PARAM2})
CMD     "cmd"

%%

<INITIAL>^{SPACE}*{CR} ; /* Ignore blank lines when in INITIAL state. */

^{SPACE}*#.*{CR} ; /* Ignores lines with a # as first non-blank character. */

  /* Variable declaration. */
  /* """"""""""""""""""""" */
<INITIAL,IN_VAR,IN_RULE>^("global"{SPACE}*)?"@"{NAME}{SPACE}*:.* {
  char *equal_addr;
  char *start;
  
  manage_eol();
  
  if (param)
    merge_old_params();
  
  process_old_var();
  process_old_rule();
  
  BEGIN(IN_VAR);
  var        = xmalloc(sizeof(var_t));
  var->name  = NULL;
  var->value = NULL;
  
  if (strncmp(yytext, "global ", 7) == 0 || strncmp(yytext, "global\t", 7) == 0)
  {
    start       = yytext + 7;
    var->global = 1;
  }
  else
  {
    start       = yytext;
    var->global = 0;
  }
  
  if ((equal_addr = strchr(start, ':')) == NULL)
    trace(LOG_DATA,
          "[%s] \"%s\": invalid variable definition.",
          rule->tag,
          yytext);
  else
  {
    *equal_addr = '\0';
    var->name   = xstrdup(start + 1);
    var->value  = xstrdup(equal_addr + 1);
  }
}

  /* Continuation line in variables */
  /* """""""""""""""""""""""""""""" */
<IN_VAR>^{PREFIX}>.* {
  char * start;

  manage_eol();

  start = strchr(yytext, '>');
  if (start == NULL)
    trace(LOG_DATA, "[%s] malformed continuation line.", var->name);
  if (var->value != NULL)
  {
    size_t l = strlen(var->value);

    if (l > 0 && var->value[l - 1] == '\\')
    {
      var->value[l - 1] = '\0';
      var->value        = strappend(var->value, start + 1, NULL);
    }
    else
      var->value = strappend(var->value, "\n", start + 1, NULL);
  }
  else
    trace(LOG_DATA, "[%s] unexpected continuation line.", var->name);
}

  /* Continuation line in commands. */
  /* """""""""""""""""""""""""""""" */
<IN_RULE>^{PREFIX}>.* {
  char *start;
  
  manage_eol();
  
  start = strchr(yytext, '>');
  if (start == NULL)
    trace(LOG_DATA, "[%s] malformed continuation line.", rule->tag);
  if (cur_content != NULL)
  {
    size_t l = strlen(cur_content);
  
    if (l > 0 && cur_content[l - 1] == '\\')
    {
      cur_content[l - 1] = '\0';
      cur_content        = strappend(cur_content, start + 1, NULL);
    }
    else
      cur_content = strappend(cur_content, "\n", start + 1, NULL);
  }
  else
    trace(LOG_DATA, "[%s] unexpected continuation line.", rule->tag);
}

  /* Command declaration. */
  /* """""""""""""""""""" */
<INITIAL,IN_VAR,IN_RULE>^({NAME}|"@"[1-9][0-9]*){SPACE}* {
  size_t tag_len;
  
  manage_eol();
  
  if (param)
    merge_old_params();
  
  process_old_var();
  process_old_rule();
  
  BEGIN(IN_RULE);
  
  free(cur_content);
  cur_content = NULL;
  
  ll_free(seen_param_list, free);
  
  rule = xmalloc(sizeof(rule_t));
  
  tag_len         = strcspn(yytext, " \t"); /* Remove trailing blanks in tag. */
  yytext[tag_len] = '\0';
  
  rule->tag        = xstrdup(yytext);
  rule->is_valid   = 1;
  rule->command    = NULL;
  rule->executable = NULL;
  rule->param_list = ll_new();
}

  /* Executable declaration in command. */
  /* """""""""""""""""""""""""""""""""" */
<IN_RULE>^{PREFIX}{CMD}{SPACE}*:.* {

  manage_eol();

  if (param)
    merge_old_params();

  sep   = ':';
  param = new_param(yytext, sep);
}

  /* Parameter declaration in command. */
  /* """"""""""""""""""""""""""""""""" */
<IN_RULE>^{PREFIX}!?{PARAM}{SPACE}*:.* {

  manage_eol();

  if (param)
    merge_old_params();

  sep   = ':';
  param = new_param(yytext, sep);
  if (param)
    ll_append(rule->param_list, param);
}

  /* Parameter declaration in command. */
  /* """"""""""""""""""""""""""""""""" */
<IN_RULE>^{PREFIX}%{NAME}{SPACE}*:.* {

  manage_eol();

  if (param)
    merge_old_params();

  sep   = ':';
  param = new_param(yytext, sep);
  if (param)
    ll_append(rule->param_list, param);
}

  /* Positional argument restriction in command. */
  /* """"""""""""""""""""""""""""""""""""""""""" */
<IN_RULE>^{PREFIX}!?"$"([0-9.?]|([*+,;]?))[1-9]?{SPACE}*:.* {

  manage_eol();

  if (param)
    merge_old_params();

  sep   = ':';
  param = new_param(yytext, sep);
  if (param)
    ll_append(rule->param_list, param);
}

  /* Environment variable affectation in command. */
  /* """""""""""""""""""""""""""""""""""""""""""" */
<IN_RULE>^{PREFIX}!?"$"[^\n:]+{SPACE}*:.* {

  manage_eol();

  if (param)
    merge_old_params();

  sep   = ':';
  param = new_param(yytext, sep);
  if (param)
    ll_append(rule->param_list, param);
}

  /* Command ending detection. */
  /* """"""""""""""""""""""""" */
<IN_RULE>^{SPACE}* {
  if (param)
    merge_old_params();

  cur_content = NULL;
  BEGIN(INITIAL);
}

  /* Variable ending detection. */
  /* """""""""""""""""""""""""" */
<IN_VAR>^{SPACE}*{CR} {
  process_old_var();
  cur_content=NULL;
  unput(yytext[yyleng - 1]);
  BEGIN(INITIAL);
}

{CR} ; /* ignore spurious \n */

  /* Default rule -> Error. */
  /* """""""""""""""""""""" */
.* {
  if (param)
    merge_old_params();
  if (rule)
  {
    if (!rule->is_valid)
      trace(LOG_DATA,"[%s] \%s\": invalid rule, see previous message, "
               "please contact your sys administrator.",
               rule->tag, yytext);
    else
    {
      trace(LOG_DATA,"[%s] \"%s\": syntax error, "
                 "please contact your sys administrator.",
                 rule->tag, yytext);
      rule->is_valid = 0;
    }
  }
  else
    trace(LOG_DATA,"[%s] Configuration syntax error, "
               "please contact your sys administrator.",
               yytext);

  cur_content=NULL;
  BEGIN(INITIAL);
}

%%
/* ============================================================== */
/* Function automatically launched at the end of each parsed file.*/
/* ============================================================== */
int
yywrap(void)
{
  if (param)
    merge_old_params();

  free(cur_content);
  cur_content = NULL;

  return 1;
}

void
manage_eol(void)
{
  int c;

  c = input();
  if (c > 0 && c != '\n')
    unput(c);
}

/* ===================== */
/* parser main function. */
/* ===================== */
void
parse(FILE *file)
{
  seen_param_list = ll_new();
  yyin            = file;

  yylex();

  process_old_var();
  process_old_rule();

  ll_destroy(seen_param_list, free);
}

/* ================================================================== */
/* Finalize a new variable by expanding embedded variables references */
/* and add it in the variables tree.                                  */
/* ================================================================== */
static void
process_old_var(void)
{
  if (var != NULL)
  {
    size_t l = strlen(var->value);

    if (l > 0 && var->value[l - 1] == '\\')
      var->value[l - 1] = '\0';

    expand_var_ref(&var->value);
    var_tree = bst_insert(var_tree, var, item_comp, NULL);

    var = NULL;
  }
}

/* ================================================================= */
/* Build a parameter by merging its initial declaration will all its */
/* continuation lines.                                               */
/* ================================================================= */
static void
merge_old_params(void)
{
  char **array;
  int    n, nb;

  if (cur_content)
  {
    size_t l;

    l = strlen(cur_content);

    if (l > 0 && cur_content[l - 1] == '\\')
      cur_content[l - 1] = '\0';

    if (strcmp(param->name, "cmd") == 0)
    {
      rule->command    = xstrdup(cur_content);
      l                = strcspn(cur_content, " \t");
      rule->executable = xstrndup(cur_content, l);
    }
    else
    {
      strarray(cur_content, &array, &nb, ",;", MERGESAMESEPS | KEEPQUOTES);
      for (n = 0; n < nb; n++)
        ll_append(param->val_list, array[n]);
    }
  }
}

/* ============================================================== */
/* Finalize a new rule by expanding embedded variables references */
/* and add it in the rule tree.                                   */
/* ============================================================== */
static void
process_old_rule(void)
{
  param_t *param;
  char    *val;

  if (rule)
  {
    ll_node_t *param_node = rule->param_list->head;

    if (!rule->is_valid || rule->command == NULL || rule->executable == NULL)
    {
      /* And add the special invalid parameter to invalid rules. */
      /* """"""""""""""""""""""""""""""""""""""""""""""""""""""" */
      param           = xmalloc(sizeof(param_t));
      param->name     = xstrdup("invalid");
      param->val_list = ll_new();

      ll_append(rule->param_list, param);
    }
    else
      /* Normal treatment for valid rules. */
      /* """"""""""""""""""""""""""""""""" */
      while (param_node)
      {
        ll_node_t *val_node;

        param    = (param_t *)(param_node->data);
        val_node = param->val_list->head;

        while (val_node)
        {
          val = (char *)(val_node->data);
          expand_var_ref(&val);
          val_node->data = val;

          val_node = val_node->next;
        }

        param_node = param_node->next;
      }

    rule_tree = bst_insert(rule_tree, rule, item_comp, NULL);

    rule = NULL;
  }
}

/* ========================================== */
/* Initiates the creation of a new parameter. */
/* ========================================== */
static param_t *
new_param(char *yytext, char sep)
{
  char *end;
  char *name;

  ltrim(yytext, " \t");
  end = strchr(yytext, sep);

  if (!rule->is_valid)
    return NULL;

  name = xstrndup(yytext, end - yytext);

  /* Check if the parameter with the same name has already been seen */
  /* for this rule.                                                  */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (!ll_find(seen_param_list, name, str_comp))
  {
    ll_prepend(seen_param_list, xstrdup(name));

    param           = xmalloc(sizeof(param_t));
    param->name     = name;
    param->val_list = ll_new();

    free(cur_content);
    cur_content = NULL;
    cur_content = xstrdup(end + 1);

    return param;
  }
  else
  {
    trace(LOG_DATA, "[%s] \"%s\": duplicate entry.", rule->tag, name);
    free(name);
    rule->is_valid = 0;

    return NULL;
  }
}
