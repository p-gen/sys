#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

m4_define(my_version, [m4_esyscmd([tr -d '\n' < version])])

AC_PREREQ([2.69])
AC_INIT([sys],[my_version],[p.gen.progs@gmail.com])
AC_CONFIG_SRCDIR([sys.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([no-dist-gzip dist-bzip2 foreign -Wall])
AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CC
AC_PROG_LEX([noyywrap])
if test "x$LEX" = "x:"; then
  AC_MSG_ERROR([flex or lex is required to build this programl.])
fi

# Checks for libraries.
AC_SEARCH_LIBS([crypt],[crypt], CRYPT_LIB="-l$ac_lib", CRYPT_LIB="")

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h netdb.h shadow.h stddef.h stdint.h stdlib.h \
                  string.h sys/time.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_GETGROUPS
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([dup2 gethostname getspnam localtime_r pathconf regcomp \
                strcasecmp strchr strcspn strdup strerror strrchr strspn \
                strstr strtol innetgr vasprintf])

# OS-specific tests
case "${host_os}" in
*freebsd*|*dragonfly*)
  CPPFLAGS="-D__BSD_VISIBLE $CPPFLAGS"
  ;;

*darwin*)
  CPPFLAGS="-D_DARWIN_C_SOURCE $CPPFLAGS"
  ;;

*netbsd*)
  CPPFLAGS="-D_NETBSD_SOURCE $CPPFLAGS"
  ;;

*openbsd*|*bitrig*)
  CPPFLAGS="-D_BSD_SOURCE $CPPFLAGS"
  ;;
esac

AC_ARG_WITH([cfgdir],
    AS_HELP_STRING([--with-cfgdir],
    [directory containing the configuration file [/etc]]),
    [test -d "$withval" && cfg_dir="$withval" || cfg_dir="/etc"
    echo setting cfg file directory... $cfg_dir],
    [cfg_dir="/etc"])

AC_SUBST(cfg_dir)

AC_ARG_ENABLE(pam,
[  --enable-pam            enable PAM authentication, disabled by default], [
  if test "$enableval" = "yes"
  then
    AC_CHECK_LIB(pam, pam_end)
    AC_CHECK_LIB(dl,dlopen)
    AC_CHECK_HEADERS([utmpx.h],
    [#include <utmpx.h>],,
    [#include <utmp.h>])
    AC_CHECK_HEADERS([security/pam_appl.h],
    PAMHEADERS=yes,
    PAMHEADERS=)
    if test $PAMHEADERS; then
      AC_DEFINE([PAM_ENABLED],[1],[PAM enabled])
      PAMTARGET="pam"
    else
      [echo "WARNING: PAM headers not found, disabling PAM"]
      AC_DEFINE([PAM_ENABLED],[0],[PAM disabled])
      PAMTARGET="no-pam"
    fi
  else
    AC_DEFINE([PAM_ENABLED],[0],[PAM disabled])
    PAMTARGET="no-pam"
  fi
])

case "$PAMTARGET" in
  no-pam) echo authentication with PAM disabled
          ;;

  pam)    echo authentication with PAM enabled
          ;;
esac

PLUGINS=no
AC_ARG_ENABLE(plugins,
[  --enable-plugins        enable filtering with plugings, disabled by default], [
  if test "$enableval" = "yes"
  then
    AC_SEARCH_LIBS(dlopen,dl, PLUGINS=yes,,)
    if test "$PLUGINS" = "yes"; then
      XTRALIBS="$XTRALIBS -ldl"
      AC_DEFINE([PLUGINS_ENABLED],[1],[Plugings enabled])
      PLUGINGSTARGET="plugins"
    else
      AC_DEFINE([PLUGINS_ENABLED],[0],[dlopen not found, plugins disabled])
      PLUGINGSTARGET="no-plugins"
    fi
  else
    AC_DEFINE([PLUGINS_ENABLED],[0],[Plugins disabled])
    PLUGINGSTARGET="no-plugins"
  fi
])

case "$PLUGINSTARGET" in
  no-plugins) echo filtering with plugins are disabled
          ;;

  plugins)    echo filtering with plugins are enabled
          ;;
esac

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
