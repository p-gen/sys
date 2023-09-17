..
  ###################################################################
  Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
  ###################################################################

*******
sys.cfg
*******

--------------------------------------
the global configuration file for sys.
--------------------------------------

:Author: p.gen.progs@gmail.com
:Date:   2023
:Copyright: MPL-2.0
:Manual section: 5
:Manual group: File formats and conventions

DESCRIPTION
===========

This file must have an ``.ini``-like file syntax and can contain the
sections described below, note that their capitalization does not matter.

It must be located in the directory specified using the parameter
``--with-cfg`` during the build phase.
This directory is ``/etc`` by default..

It must belong to **root**:**root** and have **0600** permissions.

``[Directories]`` section
=========================

This  section must contain the following properties:

- *Data*

  must contain a list of directories in which **sys** will search
  for *data* files.

  Each value in this list will be examined in order and must be separated by
  a space or a comma.

  Example::

    Data=/opt/etc/sys /etc/sys

- *Logs*

  must contain the directory where the log files will be put.

  Example::

    Logs=/var/log/sys

- *Cache*

  must contain an existing directory which will host the sys cache.
  This directory does not need to be secured because the cache will
  only be readable and writable by the root user and will be rebuilt if
  deleted or modified.

  When not present this setting defaults to ``/etc``.

  Example::

    Cache=/var/cache

- *Plugins*

  must contain the directory where the plugins will be looked for.
  This directory must belong to **root**:**root** and have a mode set to
  **0700**.
  It will be created if it does not already exists.

  The default value for this setting is ``/etc/sys_plugins``.

  Example::

    Plugins=/etc/sys/plugins

``[Miscellaneous]`` section
===========================

In this section the following properties are understood:

- *Initial environment*

  Its value (if present) must be either a file containing a list of
  environment variables definitions (one per line) or an executable
  which objective is to generate a a set of line containing environment
  variables in the form ``variable=content``.
  These variables will overload the existing environ variable if any.

  The executable form of this file must be owned by root to be run.

  These variables can be altered again by using the *environment*
  parameter in rules.

- *Default paths*

  When no path are given, neither using the *paths* parameter, nor using
  a full path in the *cmd* parameter in the rule, then a default list
  of paths is used.
  Normally this list is read using the ``_SC_PATH`` system configuration
  variable it it exists or defaults to `/bin`` and `/usr/bin``.

  This list will be completed by the space or comma-separated path list
  specified in this parameter.

- *Maximum external commands*

  This number, which defaults to **8**, sets the maximum number of
  special rule to be attempted when the given tag in not found in the
  **data** files.

  It must be at least equal to 1.

Example
=======

::

  # =============================================================
  # General syntax: keyword = list in sections delimited with [].
  #                 as in standard .ini files.
  # - list elements can be separated by commas, spaces or tabs.
  # - = can be preceded or followed by spaces.
  # =============================================================

  [Directories]
  Data    = /opt/etc/sys /etc/sys
  Logs    = /var/log/sys
  Plugins = /etc/sys/plugins
  Cache   = /var/cache

  [Miscellaneous]
  Initial environment =
  Default paths = /usr/local/bin /opt/bin
