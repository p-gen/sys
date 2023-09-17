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
#include <ctype.h>
#include <unistd.h>
#include "xmalloc.h"
#include "strarray.h"

/* ========================================================================= */
/* Constructs the array out by splitting the strings 'in' according to the   */
/* separators in 'seps'.                                                     */
/* if TRIM is set in options, leading and trailing spaces as recognized by   */
/* isspace are removed before processing the string 'in'.                    */
/* the splitting process obey the following rules:                           */
/* if MERGESAMESEPS is set in options, a sequence of the same separator is   */
/* considered as just one instance of this separator.                        */
/* if NOOCTAL is set in options, Numbres with a leading 0 will not be        */
/* considered as octel.                                                      */
/* if MERGESEPS is set in options, a sequence of separators as defined       */
/* in 'seps' is considered as just one instance of this separator.           */
/* - in 'in' and "seps' \x obeys the C rules.                                */
/* - Sequences of characters in 'in' surrounded by " or ' are protected and  */
/*   passed as is.                                                           */
/* - a NULL entry is always added as the latest entry of the array of stings */
/*   in 'out'.                                                               */
/* - the number of strings in 'out' is returned in 'count' (excluding the    */
/*   latest NULL entry).                                                     */
/* if KEEPQUOTES is set in options, The quotes are processed but not removed */
/* in the resulting strings.                                                 */
/*                                                                           */
/* the return code is  0 if the function did its job without problem.        */
/*                    -1 if there were a problem allocating memory.          */
/*                    -2 if a " or a ' was not terminated.                   */
/*                    -3 if the first argument is NULL.                      */
/*                                                                           */
/* It's the responsibility of the user to free the array and the strings     */
/* allocated by the function.                                                */
/* ========================================================================= */

int
strarray(char *in, char ***out, int *count, char *seps, int options)
{
  int    rc = 0;
  char  *buf;
  char   quote = ' ';
  int    len;
  char  *ptr;
  char **p;
  int    in_special = 0;
  int    number;

  char in_chars[]  = "\\\'\"\?abfnrtv";
  char out_chars[] = "\\\'\"\?\a\b\f\n\r\t\v";

  /* initialize the strings counter */
  /* """""""""""""""""""""""""""""" */
  *count = 0;

  /* determine the input length after possibly having to trim the leading and */
  /* trailing spaces                                                          */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (in == NULL)
    return -3;

  len = strlen(in);
  if (options & TRIM)
  {
    /* skip leading spaces */

    while (*in != '\0' && isspace(*in))
    {
      len--;
      in++;
    }

    /* cut trailing spaces */

    while (len && isspace(*(in + len - 1)))
      len--;
  }

  /* Allocate space for the first string pointer and and a buffer.         */
  /* the buffer length cannot be more athan the input length so we set its */
  /* size to that                                                          */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  (*count)++;
  *out = (char **)malloc(*count * sizeof(char *));
  if (*out == NULL)
    return -1;

  buf = (*out)[*count - 1] = calloc(strlen(in) + 1, 1);
  if (buf == NULL)
    return -1;

  /* manage the case of an empty imput */
  /* """"""""""""""""""""""""""""""""" */
  if (len == 0)
  {
    (*count)--;
    rc = 0;
  }

  /* process input string, mainly character after character */
  /* """""""""""""""""""""""""""""""""""""""""""""""""""""" */
  while (len-- > 0 && *in != '\0')
  {
    char c = *in;

    /* manage the start/end of protected sequence (between " and ') */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    if (*in == '"' || *in == '\'')
    {
      if (quote == ' ')
      {
        quote = c;
        if (options & KEEPQUOTES)
          *buf++ = c;
      }
      else if (c == quote)
      {
        quote = ' ';
        if (options & KEEPQUOTES)
          *buf++ = c;
      }
      else
        *buf++ = c;
    }
    /* manage the start/end of protected characters and special octal and */
    /* hexadecimal sequence                                               */
    /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
    else if (c == '\\' && quote == ' ')
    {
      if (*(in + 1) != '\0')
        in_special = 1;
      else
        *buf++ = c;
    }
    else if (in_special)
    {
      char *pos;
      int   n;

      number = 0; /* will receive the converted value from octal or *
                   | hexadecimal sequence                           */

      /* octal sequences (\n, \b ...) */
      /* '''''''''''''''''''''''''''' */
      if (!(options & NOOCTAL) && c >= '0' && c <= '7')
      {
        /* octal */
        for (n = 2; n >= 0 && len-- > 0; n--)
        {
          if (c < '0' || c > '7')
          {
            in_special = 0;
            break;
          }
          number += (c - '0') << (n * 3);
          c = *++in;
        }
        in--;

        if (in_special)
        {
          *buf++     = (char)number;
          in_special = 0;
        }
      }
      /* hexadecimal sequences (\x...) */
      /* ''''''''''''''''''''''''''''' */
      else if (c == 'x')
      {

        /* hexadecimal */
        /* ''''''''''' */
        for (n = 1; n >= 0 && len-- > 0; n--)
        {
          int base;
          int incr;

          c = *++in;

          if (!isxdigit(c))
          {
            in_special = 0;
            break;
          }

          incr = 0;

          if (c >= 'a') /* assuming ASCII */
          {
            base = 'a';
            incr = 10;
          }
          else if (c >= 'A')
          {
            base = 'A';
            incr = 10;
          }
          else
            base = '0';

          number += (c - base + incr) << (n * 4);
        }

        if (in_special)
        {
          *buf++     = (char)number;
          in_special = 0;
        }
      }

      /* special c sequences (\n, \b ...) */
      /* '''''''''''''''''''''''''''''''' */
      else if ((pos = strchr(in_chars, c)) != NULL)
      {
        in_special = 0;
        *buf++     = *(out_chars + (pos - in_chars));
      }
      else if ((pos = strchr(seps, c)) == NULL)
      {
        in_special = 0;
        *buf++     = c;
      }
      else
      {
        in_special = 0;
        *buf++     = '\\';
        *buf++     = c;
      }
    }
    /* manage the case where we hit a separator */
    /* """""""""""""""""""""""""""""""""""""""" */
    else if (strchr(seps, c) != NULL && quote == ' ')
    {
      *buf = '\0';
      /* free the extra spaces */
      ptr = xstrdup((*out)[*count - 1]);

      if (ptr == NULL)
        return -1;

      free((*out)[*count - 1]);
      (*out)[*count - 1] = ptr;

      (*count)++;

      p = (char **)realloc(*out, *count * sizeof(char *));
      if (p == NULL)
      {
        rc = -1;
        break;
      }
      else
      {
        *out = p;
        buf = (*out)[*count - 1] = (char *)calloc(strlen(in) + 1, 1);
        if (buf == NULL)
        {
          rc = -1;
          break;
        }

        *buf = '\0';
      }

      /* in the case of the multi char separator option equals to 1         */
      /* ignore the other occurrences of this separator in the input string */
      /* """""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
      if (options & MERGESAMESEPS)
        while (*(in + 1) == c && *(in + 1) != '\0' && len-- > 0)
          in++;
      else if (options & MERGESEPS)
        while (strchr(seps, *(in + 1)) != NULL && *(in + 1) != '\0'
               && len-- > 0)
          in++;
    }
    /* a normal character is read */
    /* """""""""""""""""""""""""" */
    else
      *buf++ = c;

    in++;
  }

  /* free the extra spaces in the last string if at least a string */
  /* has been  read                                                */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  if (*count > 0)
  {
    ptr = xstrdup((*out)[*count - 1]);

    if (ptr == NULL)
      return -1;

    free((*out)[*count - 1]);
    (*out)[*count - 1] = ptr;
  }
  else
    free((*out)[*count]);

  /* add a NULL pointer at the end of the array */
  /* """""""""""""""""""""""""""""""""""""""""" */
  p = (char **)realloc(*out, ((*count) + 1) * sizeof(char *));
  if (p == NULL)
    return -1;
  else
    *out = p;
  (*out)[*count] = NULL;

  /* manage the case of unbalanced quotes */
  /* """""""""""""""""""""""""""""""""""" */
  if (quote == ' ')
    return rc;
  else
    return -2;
}

#ifdef TEST
int
main(int argc, char *argv[])
{
  char **tokens;
  int    rc, i, count;

  if (argc != 3)
  {
    printf("usage: %s strings TRIM(1)|MERGESAMESEPS(2)|MERGESEPS(4)\n",
           argv[0]);
    exit(1);
  }

  printf("in='%s'\n", argv[1]);
  rc = strarray(argv[1], &tokens, &count, " \t", atoi(argv[2]));

  printf("rc=%d, count=%d\n", rc, count);

  for (i = 0; i < count; i++)
    printf("token[%d]='%s'\n", i, tokens[i]);

  for (i = 0; i < count; i++)
    free(tokens[i]);
  free(tokens);
}
#endif
