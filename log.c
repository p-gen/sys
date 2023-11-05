/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

#include "xmalloc.h"
#include "utils.h"
#include "log.h"

static char *log_priority_strings[] = { "D", "I", "C", "W", "E" };

log_priority log_level;

/* ======================================================================== */
/* Sets the log filename. This function must be called before all the other */
/* logging functions.                                                       */
/*                                                                          */
/* name (in) log filename                                                   */
/* Returns an open file descriptor if the filename could has been created.  */
/* ======================================================================== */
FILE *
log_init(char *name)
{
  FILE *l;

  /* set the global log filename */
  /* """"""""""""""""""""""""""" */
  l = fopen(name, "a");
  chmod(name, 0600);
  chown(name, 0, 0);
  if (l == NULL)
    fatal("Cannot append to log file \"%s\".", name);

  return l;
}

/* =========================================== */
/* Must be called at before the program ending */
/* =========================================== */
void
log_shutdown()
{
  if (log_fh == NULL)
    return;

  (void)fflush(log_fh);
  (void)fclose(log_fh);
}

/* ==================================== */
/* Internal function to do the real job */
/* ==================================== */
static void
log_msg(log_priority p, char *msg, va_list args)
{
  char     *ps;
  time_t    t;
  struct tm tm;
  va_list   args_copy;

  if (log_fh == NULL)
    return;

  if (p < LOG_COUNT)
    ps = log_priority_strings[p];
  else
    ps = "UNKNOWN";

  t = time(0);
  (void)localtime_r(&t, &tm);
  (void)fprintf(log_fh,
                "%02d/%02d/%04d %02d:%02d:%02d %s: ",
                tm.tm_mday,
                tm.tm_mon + 1,
                tm.tm_year + 1900,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec,
                ps);

  va_copy(args_copy, args);

  if (p == LOG_DATA)
    (void)fprintf(log_fh, "Error in data file: ");

  (void)vfprintf(log_fh, msg, args);

  if (p >= LOG_ERROR)
  {
    (void)fprintf(stderr, "Error: ");

    /* Use the copy of the va_list      */
    /* as the original has been altered */
    /* by the previous vfprintf call.   */
    /* """""""""""""""""""""""""""""""" */
    (void)vfprintf(stderr, msg, args_copy);
    (void)fprintf(stderr, "\n");
  }

  va_end(args_copy);

  (void)fprintf(log_fh, "\n");
  (void)fflush(log_fh);
}

/* ==================================== */
/* Fatal error message without logging. */
/* ==================================== */
void
fatal(const char *format, ...)
{
  va_list args;

  fprintf(stderr, "Error: ");
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");

  exit(EXIT_FAILURE);
}

/* ================================== */
/* Fatal error with logging.          */
/* msg (in) printf like format string */
/* ... (in) printtf like arguments.   */
/* ================================== */
void
error(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  log_msg(LOG_ERROR, msg, args);
  va_end(args);

  exit(EXIT_FAILURE);
}

/* =========================================== */
/* Non fatal error with logging                */
/* log_priority (in) logging priority          */
/* msg          (in) printf like format string */
/* ...          (in) printf like arguments.    */
/* =========================================== */
void
trace(log_priority p, char *msg, ...)
{
  va_list args;
  va_start(args, msg);
  log_msg(p, msg, args);
  va_end(args);
}
