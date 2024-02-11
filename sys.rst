..
  ###################################################################
  Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
  ###################################################################

===
sys
===

----------------------------------------------------------------------
a rule-based command-line tool for executing a command as another user
----------------------------------------------------------------------

:Author: p.gen.progs@gmail.com
:date: 2023
:Copyright: MPL-2.0
:Manual section: 8
:Manual group: Utilities

SYNOPSIS
========

**sys** [*options*...] [path]*tag* [*arguments*...]

*options* can be one of:

-d      daemonize

-u uid  launch with a specific user

-g uid  launch with a specific group

-v      verbose mode

-h      synopsis

|
| *options* must appear **before** the tag if any.
| *arguments* are passed to the executable to be run.

DESCRIPTION
===========

In short, **sys** allows the execution of scripts and binaries as another
user, root by default, in a restricted context.
To do that it needs to have access to a set of **rules** grouped in at least
one data files.

The **rules** must be separated by at least one empty line.

Each **rule** may contain:

- a *tag* (**mandatory**) starting at the first column,
- various parameters using the syntax: ``parameter1:[value1[,[value2],...]``
  preceded by at least one space or tabulation mark.
  A '``;``' can also be used to delimit the values instead of the '``,``':

  - a *cmd* parameter (**mandatory**) to give the name of the *executable*
    and instructions on how to handle its arguments,
  - other optional parameters to set or extend the execution environment
    of the *executable* or to condition its execution to other criteria.

In addition to these **rules**, data files may also contain **variables**
whose purpose is to facilitate the writing of the **rule** definitions
and make them easier to read and maintain.

The *tag* is used by **sys** to locate the correct **rule** in the
data files.

If the *tag* is prefixed with a path (starts with a set of strings
delimited by the ``/`` character) then only the part after thge last
``/`` will be looked for in **rules** for a match, refer to the *paths*
parameter definition in the section named "Parameters related to the
execution context." below.

If **sys** cannot find a matching **rule** for this *tag*, it will
try to use special generic **rules** identified with *tags* named as
``@``\ *n* where *n* is a number greater than or equal to 1.

Note that he existence of these special rules is not mandatory. but in
this case a rule for this *tag* must exist.

These special **rules** will be considered in order until one of them
succeeds or the maximum number of attempts is exceeded.
Note that holes are allowed in their numbering sequence.

The number following ``@`` ith their name cannot be greater than **8**
by default but this value can be increased or decreased using a setting
in the **sys** configuration file.
See the **sys.cfg(5)** man page for more details.

If such special **rules** exist and no other suitable **rule**
could be found using the given *tag*, then **sys** will replace the
pseudo-executable ``@`` in them with the *tag* and try to interpret it
according to the parameters present in that special **rule** if any.

Note that ``@`` must be the first word in the value of the mandatory *cmd*
parameter, see below for an example.

In a way, these special **rules** can allow **sys** to be used a bit like
**sudo**.

Here is a example of a simple **rule** definition.
The ASCII arrows on the right and the rest of the lines are there for
explanation in this manual, but are not part of the rule definition::

    list            <-- the tag of the rule passed to sys
      cmd:ls $*     <-- the (decorated) executable to run
      users:pierre  <-- a restriction (list can only be used by pierre)

With such a definition, when the user enters the command line:
``sys list arguments`` then ``ls arguments`` is run as root if, and only
if, the real user is 'pierre'.

Note that the tag whose name is ``list`` must start a line without a
leading blank and that the following lines forming the rest of the rule
must be indented.

Here is an example of how to define a special rule::

    @1
      cmd:@ $*     <-- the @ is a substitute for the requested command.
      environment: <-- do not clear the existing environment variables
      groups:root  <-- members of the root group wont need a password.
      password:    <-- ask for a password in the other cases.

Warning:
  If the *tag* given on the command line is a path (contains a '``/``')
  then the *executable* value is replaced by the value of the *tag* and
  the modified rule will not be cached.

  This kind of *tags* are only taken into account when special *tags*
  are defined.

In addition to data files, **sys** also needs a configuration file whose
content is documented in the **sys.cfg(5)** man page.

So, in summary, you'll need:

- a configuration which must to be named ``sys.cfg`` and will be used to
  configure **sys** itself.
  It must belong to root and only be readable and writable by root.
  It has a simple ``.ini``-like syntax.
  Its location in the file system is determined at compile time.
- data files to store the definitions of the **rules**.
  They must have a ``.dat`` suffix and do not have to be located in the
  same directory.

  ``.dat`` files must belong to root and only be readable and writable
  by root.

  - ``.dat`` files are located in directories listed in the ``sys.cfg``
    file,
  - you can define as many ``.dat`` files as you like,
  - they are parsed in alphabetical order in each directory where they
    appear,
  - a list of these directories can be given in the in the ``.cfg`` file
    and each of these directories is opened in the order of appearance
    in this list,
  - if a *tag* appears more than once, the last occurrence on the rule
    it tags prevails,
  - the ``.dat`` files can also contain **sys** **variables** whose
    scopes are either local (the default) or usable in the ``.dat`` file
    in which they are defined and in all the ``.dat`` files read *after*.

Variables syntax.
-----------------

**sys** **variable** are declared in ``.dat`` files using the
following syntax

::

  @var:[value]

or for a global variables:

::

  global @var:[value]


they can be expanded using the syntax::

  @{var}

A **sys** **variable** cannot be destroyed but can be given an empty
value.

A **variable** definition must start at a beginning of a line in a
``.dat`` file, just like a *tag*.

The scope of a **variable** is local to the ``.dat`` file in which it
is defined except when it is a global **variable**.
The content of a global variable is not reset when parsing the
next ``.dat`` files.

**Variables** (local or global) must be defined before they can be used,
so only objects that appear after their definitions can use them.

Variables definitions can take more than one line using so called
*continuation lines*.
*continuation lines* starts with at least one leading space or tabulation
mark followed by the character '**>**' and the remaining content value.

Here is an example of a **variable** defined using 3 lines::

  @a:first_part\
    >-second_part\
    >-last_pert

This is equivalent to::

  @a:first_part-second_part-last_pert

Blanks after the '>' are significant.

When the last character of a line in a **variable** definition is not
followed by a ``\``, a newline character if automatically inserted when
continuation lines are present.

Rules syntax.
-------------

**rules** in ``.dat`` files must be defined using the following syntax:

- the *tag* must start at the beginning of a line,
- all the following lines describing the parameters on the **rule** and
  must be indented by at least one space or tabulation mark, the number
  of these blanks is free and can vary,
- these lines must respect the following syntax:

  ::

    [!]name:[value1[,value2,...]]
    %plugin:plugin_file,plugin_arg1,plugin_arg2,...
    $variable:[value]
    $pattern:value

  Note that the semicolon (``;``) can also be used instead of the comma
  (``,``) to separate parameter values.

  Most parameters have their function reversed when prefixed with the
  character '**!**'.

  Some parameters take only zero or one values.

  The variable prefixed by a '``$``' in the syntax above is an
  Unix environment variable, not a **sys** variable.
- A **rule** must be followed by at least one empty line (except for
  the last one in a given ``.dat`` file), but cannot contain empty lines.

Here's an example of a rule::

     ksh
       cmd:ksh $*
       uid:root
       gid:sys
       $PS1:'sys@${HOSTNAME} # '
       environment:
       groups:root,@{admin},wheel
       paths:/bin,/usr/bin

As with **variables**, each item in an **rule** can be defined on more than
one line using continuation lines introduced by the continuation character
'**>**'.

Example::

  groups:root\
    >,@{admin}\
        >,wheel

The detailed syntax after the *tag* is described below.

Recognized parameters in rules:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The parameters can be grouped in four categories:

-  those related to the execution context,
-  those related to the users,
-  those related to restrictions, regular or custom (plugins).
-  the *cmd* parameter describing the command line to be run.

Important:
  - Each parameter can be followed by a comma-separated list of values.
    These values may often be extended regular expressions implicitly
    bounded be a starting ``^`` and an ending ``$`` to prevent stupid
    mistakes, we'll call them "constrained extended regular expressions"
    in the following.

  - Remember that the semicolon can also be used to delimit parameter
    values instead of the comma in the following.

Parameters related to the execution context.
""""""""""""""""""""""""""""""""""""""""""""

*environment*:
    The syntax is: ``environment:[-,][command_line_1,command_line_2,...]``

    The negative form (with a leading ``!``) if present will be ignored.

    ``command_line_1``, ``command_line_2``, ... will be run in sequence
    and must provide on their standard outputs a list on lines containing
    shell environment variables affectations in the form ``name=value``.
    The first command on these command lines must include a full path.

    If ``-`` is present then the initial environment will be cleared
    before the execution of the command lines.

    if no values are given, then the current environment is inherited
    by the command to be executed, possibly completed or surcharged by
    some variables, see *Variable* below.

    Examples:

    -  ``environment:-,/opt/script`` considers the output of
       ``/opt/script`` to create a list of environment variable settings
       after having cleaned the old environment
    -  ``environment:`` transmits the current environment to the
       command to be executed possibly completed or altered.

*Environment variable*:
    The syntax is: ``$VARIABLE_NAME:value``

    ``VARIABLE_NAME`` must comply with the command interpreter's variable
    naming rules.

    ``value`` can be empty in which case the variable will be expanded to
    the empty string.

    These variables will be added to the environment of the command which
    will be executed and may override variables with the same name if
    the existing environment is not empty.

    Example: ``$PAGER:less``

*umask*:
    Syntax:``umask:value``.

    The negative form (with a leading ``!``) if present will be ignored.

    Sets the calling process's file mode creation mask (umask) in the
    target execution environment.
    The value of this parameter will be interpreted as an octal number.

    Example: ``umask:22``

Parameters related to the user who will be used to run the executable.
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

*uid*:
    Syntax:``uid:value[,...]``.

    The negative form (with a leading ``!``) if present will be ignored.

    This parameter sets the UID during the time frame in which the
    command will be executed.

    When this parameter is not present, a default value of 0 will be
    used and the command will be executed as if you were logged as root.

    When the *-u* option is **not** used, the first value after the
    *uid* parameter will be used.

    When the *-u* option is used, then the requested user must be equal
    to one of the values of this parameter.

    ``values`` can be user names or user ids.

*gid*:
    Syntax:``gid:value[,...]``.

    The negative form (with a leading ``!``) if present will be ignored.

    This parameter is similar to *uid* but for the group.

    When this parameter is not present, if *-u* is **not** used, the
    group id 0 will be used and the command will be executed as if you
    were in the root group, otherwise the primary group of the new user
    will be used.

    When the *-g* option is **not** used, the first value after the *gid*
    parameter is used to set the current group.

    When the *-g* option is used then the requested group must be equal
    to one of the values of this parameter.

    If the new user is not root, the new group must be one to which the
    new user belongs to.

    Also when the new user is not root, the new group must be one of the
    new users's supplementary groups.

    ``value`` can be a user name or group ids.

Parameters related to restrictions.
"""""""""""""""""""""""""""""""""""

*disabled*:
    Syntax is: ``disabled:reason1,reason2,...``

    The negative form (with a leading ``!``) if present will be ignored.

    This parameter prohibits the use of the rule. Non-mandatory values
    can be set to provide the user with reasons for disabling this rule.

    Each of these reasons will be printed on a new line in the order
    of appearance.

*users*:
    Syntax is: ``users:user[@host][/YYYYMMDD],...]``

    This parameter takes as values a comma separated list of items
    containing the users **allowed** to execute the command followed by
    optional restrictions.
    All the other users will not be permitted to execute it.

    -  The ``user`` part of each item can be set by their name or their
       UID.
    -  The optional ``host`` part is a constrained regular expression
       describing the hosts from which the user is allowed to execute
       the command.
    -  The optional date part is a string giving the expiration date
       using the YYYYMMDDhhmm format.
       After this date, the command will not be able to be executed.

    If this parameter is prefixed with the character '``!``' (as in
    *!users*) , then its signification is reversed and the list
    designates the users **not allowed** to execute the command.
    Note that when '``!``' is used, date limitations are ignored.

    WARNING:
      The list of users can be empty, if the parameter is *!users*,
      then the whole rule be immediately denied as all users will be
      matched by this parameter.

      if the parameter is *users*, the rule will continue to be analyzed
      as the users may belong to one of the group or netgroup matched
      by the constrained regular expression placed after the parameters
      *groups* or *netgroups* of the rule, see below.

    Examples:

    -  ``users:alice/20251010,bob@srv.*/20163112/,carol,100``
    -  ``!users:carol``
    -  ``users:``

*groups*:
    same as above but for groups. Primary and secondary groups are
    accepted.

*netgroups*:
    same as above but for NIS or LDAP netgroups. Note although than
    netgroups in the list of value are not constrained extended regular
    expression as in *users* and *groups* above.

The parameters *users*, *groups* end *netgroups* are linked in a way
that it is sufficient for one on them to be accepted for the command
to be run.
This, of course, provided that no other mandatory parameter is rejected.

When no *users*, *groups* or *netgroups* parameter is present in a *rule*
then any user, group or netgroup will be be accepted.

The negative forms (with a leading ``!``) of *users*, *groups* and
*netgroups* are first checked for a match and if, and only if, no match
has occurred then the positive forms are checked.
This ensures that the filter rules are analyzed regardless of the order
in which they are specified.

In the same way it if sufficient for him to belong to one of the '``!``'
prefixed *users*, *groups* end *netgroups* parameter to be rejected.

*paths*:
    Syntax is: ``paths:[path][,...]``

    This parameter, which can be negated with '``!``' list the allowed
    (or denied) paths for the target command to belong to.

    The path must be absolute (begin with a '``/``').

    If the *tag* given in the command line has a path (contains a '/')
    then a rule for the last part of it (the basename) will be looked for.
    If such a rule is found then the path in its command part (if any)
    must match the *tag* path and the *tag*'s path must also be present
    in the "paths" parameter's list and not denied in the "!paths"
    parameter list also (if any).

    if the *tag* given in the command line does not have a path then only
    the "paths" and "!paths" parameters (if present)  are considered to
    enable the *executable* to be  run.

    If no path list is given and this parameter is negated with '``!``'
    then the *executable* will **not** be ran, otherwise an empty list
    of paths does not have any filtering effect.

*password*:
    Syntax is: ``password:[user][,...]``

    The negative form (with a leading ``!``) if present will be ignored.

    This parameter, if present, allows the user to bypass "users", "groups"
    and "netgroups" filtering failure.

    When this parameter if absent, no password will be asked for and all
    filtering failure is fatal.

    If this parameter has a list of values, they will be interpreted as
    a list of users.
    The password given must be the password of one of them in addition
    to the target user and '*root* to allow the command to be executed.
    The order in which the user's password is asked for requested will
    be the same as the order of the values in this parameter.

    If this parameter if present and none of the parameters *users*,
    *groups*, *netgroups* or their negations is present or have an empty
    set of values, then a password will be asked for.
    If at least one of these parameters is present in the rule and has
    values, then a password will *only* be requested if the current
    *user*/*group*/*netgroup* is not in the values given.

    No value for this parameter is equivalent to a list of values
    containing *root* and the target user.

    On systems when the PAM mechanism is activated, **sys** can use it for
    the authentication, otherwise the encrypted password will be compared
    with the one in the shadow database.

*owners*:
    Syntax is: ``owners:[user:group][,...]``

    This parameter, if present, allows to set a list of couples of words
    describing the allowed ownership of the executable to be run.
    Is the owner of the executable is not found in this list, the rule
    will be rejected.
    Entries in this list must obey the syntax **user**:**group** where
    **users** and **group** are extended constrained regular expressions.

    Example:
      owners:.*:dba,wwwrun:www

    The negative form (with a leading ``!``) denies executions instead
    of allowing them.

..
  COMMENT BLOCK

  *modes*:
      If set this parameter impose restrictions on the mode of the
      *executable* to be run. The values are constrained regular
      expressions and will be tried in sequence.

      The values can be given in the traditional **octal** form with an
      optional leading ``0`` or in the **rwxrwxrwx** form as given by the
      output of ``ls -l``.

      Example: in ``modes:0754,rwxr--r--`` The second permitted mode is
      equivalent to ``744`` in octal.

      Modes descriptions can also have a negated meaning when given after the
      parameter *!modes*.

Parameter to set the executable name.
"""""""""""""""""""""""""""""""""""""

*cmd*:
    Syntax is: ``cmd:executable``

    The negative form (with a leading ``!``) if present will be ignored.

    This is where you have to define the name of the *executable* to
    be run.

    WARNING:
      **sys** variables will never been expanded here and will be seen
      as ordinary text.

    If the *executable* has an absolute path name and the *paths*
    parameter is also present, then its path must belong to one on the
    paths given after the *paths* parameter.

    This *executable* can be followed by *patterns* to form a pseudo
    command line.

    Example::

      cmd:bash $*

    *patterns* are somewhat similar to the shell's meta-characters
    and can be seen as substitutes for one or more arguments.
    They can be used to control, impose or constrain the arguments of
    the *executable*.

    Here is the list of all the available *patterns*, their meanings
    will be detailed below:

    ``$*``, ``$+``, ``$,``, ``$;``, ``$.``, ``$?``, ``$``\ *n* and
    ``^``\ *word*

    All *patterns* starting with a ``$`` can be prefixed by the character
    ``!`` to invert their functions.

    ``$*``, ``$,``, ``$+`` and ``?``. can also be suffixed with a number
    to individualize them, so that ``$*`` and ``$*1`` behave the same but
    may have different associated constraints for example.

    Here are some examples of legal *pattern* names:
    ``$*``, ``$*1``, ``$,``, ``!$-2``, ``$5``, ``!$1``, ``$+2``, ``$?3``,
    ``^-f``

    Important:
      During the operation of matching of each *pattern* to the arguments
      provided on the command line, it is important to understand that
      a *pattern* will be used as long as it can be match the arguments
      **and** the next pattern does not also match the current argument,
      in which case the next pattern will become the default pattern.

      A command without a *pattern* does not accept any arguments on
      the command line.

    *pattern* features:

    - The ``$``-patterns can also be filtered/constrained by associating
      a filtering **parameters** to it.  see the examples below.

      Here is their detailed meanings:

      - ``$*`` expects a (potentially empty) sequence of arguments,
        if a filtering parameter is active for ``$*`` then all the
        given constrained regular expressions must match these arguments
        until the next pattern (if any) matches one of them.

        if no filtering parameter is associated to ``$*``, then command line
        arguments will be accepted by default until one of them is matched
        by the next pattern (if any).

        In other words, ``$*`` will eat all matching command line
        arguments until it can no longer do so or until the next pattern
        matches an argument.
      - ``$+`` same as for ``$+`` but at least one argument must be present.
      - ``$,`` expects a sequence of arguments, if a filtering parameter is
        active for ``$,`` then **exactly one** of its given constrained
        regular expressions must match theses arguments.
        The other arguments are always accepted until one of them matches
        a textual or positional pattern or there is no more argument
        to consider.
      - ``$;`` same a ``$,`` except that more then one argument can match
        the filter.
      - ``$.`` expect exactly one argument. If constrained regular
        expressions are given then the argument must match one of them.
      - ``$?`` expect an optional argument. If constrained regular
        expressions are given then the argument, if present, must match
        one of them.
      - ``$``\ *n* where *n* is a number says that the *n* th argument
        must be present. If it has an associated optional filter then this
        filter must also match the *n* th argument.

        ``$``\ *n* parameters must appear in increasing order.

        Note that if ``$``\ *n* must be preceded by at least one other
        pattern if *n* is greater the 1 to consume the first command line
        arguments.

        e.g.

          ``cmd:echo $2`` will always be rejected, ``cmd:echo $. $2`` may
          succeed

      If the first five type of ``$``-patterns are followed by a number,
      each one is treated independently of the others.

      e.g. when ``$*1`` and ``$*2`` are present, then each of them can have
      a different set of filtering parameters.

    - The parameters starting with ``^`` mandate that the word that
      follows the ``^`` must be entered as it is in the command line.

      e.g. ``^-a`` will match the command line argument ``-a``.

    - Normal words appearing along the *patterns* (those not prefixed
      with a ``$`` or a ``^``) will be automatically inserted in the command
      line and **must not** be entered in the command line.

    These patterns can be given more than once.

    Examples of pattern usage:
      ``cmd:executable $*``
          allows any number of argument (even 0) if no filtering parameter
          is set for ``$*`` (see below for details about filtering
          parameters).
      ``cmd:executable $1``
          wants exactly one argument whatever it is if no filtering
          parameter is set for ``$1``.
      ``cmd:executable ^-a $2``
          wants exactly one argument whatever it is (if no filtering
          parameter is set for ``$2``) after the required argument
          '``-a``'.
      ``cmd:executable $,1 $,2``
          when the parameters ``$,1:-a`` and ``$,2:-b`` are present, this
          command, wants to see exactly **one** occurrence of ``-a``
          followed by exactly **one** occurrence of ``-b``. Each
          occurrence can be preceded or followed by any number of other
          arguments as in ``-x -a dummy -y -b -z`` by example.
      ``cmd:executable $. $*``
          wants any number of arguments with a first argument whose
          content can be imposed by a filtering parameter.
      ``cmd:executable $* -l``
          allows any number of argument (even 0) if no filtering parameter
          is set for ``$*``. The ``-l`` argument will be automatically
          inserted.

Custom parameters (or plugins) related to restrictions.
"""""""""""""""""""""""""""""""""""""""""""""""""""""""

When **sys** is compiled with plugins enabled (``--enable-plugins``),
custom parameters in the form *%name* are allowed (the leading **%**
in required).

The correct syntax for these custom parameters is:

| ``%plugin_name,plugin_file,arg1,arg2,...``

Where *plugin_file* is the base name of the plugin compiled object
and the *argN* values are strings which will be passed to the plugin
function at run time.

Plugins must be compiled and stored in the plugin_directory defined in
``sys.cfg`` (see sys.cfg.5).  With *gcc* for example, the following
instruction can be used::

  gcc -shared -fPIC -o plugin_name.so plugin_name.c

Plugins must have a mandatory public extern function named *sys_plugin*
respecting the following prototype::

  /* argc   (in)  Number of values for this plugin parameter in the rule.  */
  /* argv   (in)  Array containing the values for this plugin parameter in */
  /*              the rule.                                                */
  /* output (out) Optional string returned by this plugins, plugins are    */
  /*              responsible to allocate the memory for this string. It   */
  /*              will be freed by sys after its invocation.               */
  /*              output must be NULL if no output is produced.            */
  /*              This string will appear in the sys log file if not NULL. */
  /* ===================================================================== */
  int sys_plugin_main(int argc, char ** argv, char ** output);

The *plugin_file* object file may contain a optional public extern
function returning a version string::

  /* PLugin version function, must return a static string. */
  /* ===================================================== */
  char * sys_plugin_version(void)

They *sys_plugin_main* function must return **1** on success and **0**
on failure.

For security reasons, the directory containing the plugins and the
compiled plugin files must belong to **root**:**root** and have
permissions respectively equals to **0700** and **0600**.

Filtering parameter to control the arguments of the target command line.
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

Each one of the patters described above may be controlled (filtered) by a
filtering parameter.

When no filtering parameter is defined for a ``$``-named ``cmd``
parameter, then they will match any words appearing in the command line.

Examples of rule extracts with a filtering parameter:

  ::

    rmusers
      cmd:rm $*
      !$*:.*(/\.\./.*|/\.\.$)  <--- The filtering parameter
      $*:/users/.*             <--- restrictions for $*

  In this example, ``$*`` must match any sequences of words starting
  with ``/users/`` except those containing ``/../`` or those ending with
  ``/..`` for the command line to be accepted.

  * Examples of ``$*`` usages:

    | ``cmd:^-a $* ^-b``
    | *without* a ``$*`` filtering parameter:

    -  Accepted command lines:

         | ``-a x y z -b``
         | ``-a -b``

    -  Denied command lines:

         | ``-x`` (no ``-a`` nor ``-b``)
         | ``-a`` (no ``-b``)
         | ``-b`` (no ``-a``)

    | ``cmd:^-a $* ^-b``
    | *with* a filtering parameter defined as ``$*:A*``:

    - Accepted command lines:

        | ``-a A AA AAA -b``
        | ``-a -b``

    - Denied command lines:

        | ``-a A x AAA -b`` (``$*`` does not match ``x``)

    | ``cmd:^-a $* ^-b $*``
    | *with* a filtering parameter defined as ``$*:a*``:

    - Accepted command lines:

        | ``-a a aa -b aaa``
        | ``-a -b``

    - Denied command lines:

        | ``-a a -b aa x`` (``$*`` does not match ``x``)


  * Examples of ``$``\ *n* usages:

    | ``cmd:^-a $1* ^-b $2*``
    | *with* two filtering parameters defined as ``$1*:a*``
      and ``$*2:b*``:

    - Accepted command lines:

        | ``-a a aa -b bbb``
        | ``-a -b``

    - Denied command lines:

        | ``-a a -b aa`` (``$2`` does not match ``aa``)
        | ``-a x a -v bb`` (``$1`` does not match ``x``)

  * Examples of ``$,`` usages:

    | ``cmd:^-a $, ^-b``
    | *without* a ``$,`` filtering parameter:

    - Accepted command lines:

        | ``-a x y z -b``

    - Denied command lines:

        | ``-a -b`` (``$,`` hasn't matched any argument)

    | ``cmd:^-a $, ^-b``
    | *with* a filtering parameter defined as ``$,:A*``:

    - Accepted command lines:

        | ``-a A -b``
        | ``-a x A y``
        | ``-a A x y``

    - Denied command lines:

        | ``-a A AA -b`` (``$,`` has matched more than one ``A*`` argument)

  * Example of ``$+`` usages:

    | ``cmd:^-a $+ ^-b``
    | *without* a ``$+`` filtering parameter:

    - Accepted command lines:

        | ``-a x y z -b``

    - Denied command lines:

        | ``-a -b`` (``$+`` must match at least one argument)

    | ``cmd:^-a $+ ^-b``
    | *with* a filtering parameter defined as ``$.:A*``:

    - Accepted command lines:

        | ``-a A -b``
        | ``-a A AA y``

    - Denied command lines:

        | ``-a -b`` (``$+`` must match at least one ``A*`` like argument)
        | ``-a A B -b`` (``$+`` does not match ``B``)

  * Example of ``$?`` and ``$.`` usages:

    | ``cmd:$.1 $?1 $?2 $.2``
    | *with* filtering parameters defined as
    |  ``$.1:a``
    |  ``$.2:b``
    |  ``$?1:x``
    |  ``$?2:y``

    - Accepted command lines:

        | ``a b``
        | ``a x b``
        | ``a y b``
        | ``a x y b``

    - Denied command lines:

        | ``a`` (``$.2`` does not match anything)
        | ``b`` (``$.1`` does not match ``b``)
        | ``a z b`` (``$?1`` does not match ``x``)
        | ``a x z b`` (``$?2`` does not match ``y``)

FILES
=====

``sys.cfg`` file:
  Configuration file for the **sys** program itself.

``.dat`` files:
  Files containing the definitions of the rules.

SEE ALSO
========

sys.cfg(5)
