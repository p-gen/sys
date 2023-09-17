..
  ###################################################################
  Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
  ###################################################################

**************************************************************************
sys, an alternative tool to sudo.
**************************************************************************

TL;DR
=====
**sys** is a **sudo**-like tool optimized for multi-users
environments where users need to gain temporarily another user's
privilege based on a set of conditions.

The **sys** configuration files are command-based, not user-based like
**sudo** but, like **sudo**, **sys** can allow certain categories of users
to run any command if they meet all the required conditions.

**sys** also has some additional features such as the possibility
to force or restrict the environment and the command line
arguments using patterns and regular expressions.

Read on to find out more about its features.

Disclaimer.
===========
Please feel free to comment, especially on security aspects and potential
vulnerabilities.

Remark.
=======
Some specifications are still subject to change, so please let me know if
you have any comments.

Concepts.
=========
**sys** runs commands as root or as another user. in order to do that, it
locates and interprets *rules* stored in data files or in its cache.
It was designed for an environment with many end users but is easy to
use even if there is only one user in addition to root.

Here is a correct example of a possible rule to launch a root bash shell.
All users, except *pierre*, will be asked to enter a password::

  bash
    cmd:bash $*
    environment:
    users:pierre
    password:

According to this rule, you can enter at the shell prompt (here ``$``
is the non-root shell prompt)::

  $ sys bash [bash_options...]

Here is another way to do a similar thing using ksh instead of bash with a
more elaborate rule in a `.dat` file.
The first four lines do not belong to the rule but explain how variables
can be created and used in data files::

  @ADM_GRP:sys,wheel
  @ESC:$'\e'
  @RED:@{ESC}[32m
  @NO_COLOR:@{ESC}[0m

  root_shell
    cmd:ksh $*
    uid:root
    gid:root
    $ENV:false
    $PS1:{@{RED}sys@{NO_COLOR}}@${HOSTNAME}" # "
    environment:
    users:pierre
    groups:@{ADM_GRP}
    paths:/usr/bin,/bin,/usr/local/bin

This rule can be searched and interpreted using the following command line::

  $ sys root_shell [ksh_options...]

which will run the *ksh* executable as root if the current user belongs
to the *sys* or *wheel* group and if the *ksh* executable is in one of
the listed paths.
The current environment will be transmitted unchanged at the exception
of the ``PS1`` variable which will be added or overridden.

Direct command invocation, as in **sudo**, is possible with a wildcard
like rule.

Data files are interpreted once and cached. They are, of course,
interpreted again if their content changes, and the cache is then
updated.

See the manual for details on rule syntax, wildcard rule and so on.

A global configuration file names ``sys.cfg`` must also be provided.

Compilation and installation.
=============================
The script ``build.sh``, based on the traditional configure from GNU,
is provided. You can get some help using the ``--help`` option.

Building **sys** requires some attention as configuration directories
have to be given to the build mechanism:

- The directory where sys will search for the file ``sys.cfg`` (*CFGDIR*).
  This directory also defaults to ``/etc``.
- On most systems it is also highly recommended to enable the
  authentication using PAM using the ``--enable-pam`` option.

  When not using ``--enable-pam`` nor ``--enable-pam=yes`` or when PAM
  is not available or its development components are not installed,
  the standard authentication method using the password/shadow mechanism
  is automatically selected as if ``--enable-pam=no`` has been used``.
- Plugins, as dynamically loadable code, can be used to add custom
  filtering parameters in a rule. They are disabled by default.

Example::

  $ ./build.sh --with-cfgdir=/etc         \
               --enable-pam               \
               --enable-plugins           \
               --other_configure_settings...

A list of directories in which **sys** will search the data files
containing the rules must also be added in the ``sys.cfg`` file,
as in the example below.

::

  ...
  [Directories]

  Data    = /opt/etc/sys /etc/sys
  Logs    = /var/log/sys
  Plugins = /etc/sys/plugins
  ...

Configuration.
==============
Refer to the ``sys(8)`` and ``sys.cfg(5)`` manual pages to create and
populate data directories with rules and fill in the configuration file.

Permissions.
============
The permissions of the directory identified as *CFGDIR* in the previous
section must be **0700**.
Their owner must be **root**:**root**.
This is not automatically done during the installation to avoid
accidentally changing the permission of existing directories.

The directories indicated in the *Directories* section of ``sys.cfg``
must also belong to **root**:**root** and have their permissions set to
**0700**.
All the files in these directories must have their permissions set
to **0600**.

Example using the setting above::

  [root:root drwx------] /opt/etc/sys
  [root:root -rw-------]   base.dat
  [root:root -rw-------]   default.dat

  [root:root drwx------] /etc/sys
  [root:root -rw-------]   main.dat
