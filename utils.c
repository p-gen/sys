/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <termios.h>

#include "list.h"
#include "log.h"
#include "utils.h"
#include "xmalloc.h"

extern char **environ;

/* ===================================================================== */
/* Put the first word of str, truncated to len characters, in buf.       */
/* Return a pointer in str pointing just after the word.                 */
/* buf must have been pre-allocated to accept at least len+1 characters. */
/* Note that buf can contains a sting full of spaces is str was not      */
/* trimmed before the call.                                              */
/* ===================================================================== */
char *
get_word(char *str, char *buf, size_t len, char sep)
{
  char *s = str;

  /* Set the new string start. */
  /* """"""""""""""""""""""""" */
  str = s;

  /* Get the word. */
  /*"""""""""""""" */
  while (*s && *s != sep && s - str < len)
    s++;

  strncpy(buf, str, s - str);
  buf[s - str] = 0;

  if (*s)
    s++;

  return s;
}

/* =============================================== */
/* Convert an array of strings to an unique string */
/* The space for the resulting string is allocated */
/* in the function.                                */
/* =============================================== */
char *
argvtostr(int argc, char **argv)
{
  char *str = NULL;
  int   len;
  int   i;

  for (i = len = 0; i < argc; i++)
    len += strlen(argv[i]) + 1;

  str    = xmalloc(len + 1);
  str[0] = '\0';

  for (i = 0; i < argc; i++)
  {
    strcat(str, argv[i]);
    strcat(str, " ");
  }

  if (len > 1)
    str[len - 1] = '\0';

  return str;
}

/**************************************************************************/
/* From lookup2.                                                          */
/* mix -- mix 3 32-bit values reversibly.                                 */
/* For every delta with one or two bits set, and the deltas of all three  */
/*   high bits or all three low bits, whether the original value of a,b,c */
/*   is almost all zero or is uniformly distributed,                      */
/* * If mix() is run forward or backward, at least 32 bits in a,b,c       */
/*   have at least 1/4 probability of changing.                           */
/* * If mix() is run forward, every bit of c will change between 1/3 and  */
/*   2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)   */
/* mix() was built out of 36 single-cycle latency instructions in a       */
/*   structure that could supported 2x parallelism, like so:              */
/*       a -= b;                                                          */
/*       a -= c; x = (c>>13);                                             */
/*       b -= c; a ^= x;                                                  */
/*       b -= a; x = (a<<8);                                              */
/*       c -= a; b ^= x;                                                  */
/*       c -= b; x = (b>>13);                                             */
/*       ...                                                              */
/*   Unfortunately, superscalar Pentiums and Sparcs can't take advantage  */
/*   of that parallelism.  They've also turned some of those single-cycle */
/*   latency instructions into multi-cycle latency instructions.  Still,  */
/*   this is the fastest good hash I could find.  There were about 2^^68  */
/*   to choose from.  I only looked at a billion or so.                   */
/**************************************************************************/
#define mix(a, b, c) \
  {                  \
    a -= b;          \
    a -= c;          \
    a ^= (c >> 13);  \
    b -= c;          \
    b -= a;          \
    b ^= (a << 8);   \
    c -= a;          \
    c -= b;          \
    c ^= (b >> 13);  \
    a -= b;          \
    a -= c;          \
    a ^= (c >> 12);  \
    b -= c;          \
    b -= a;          \
    b ^= (a << 16);  \
    c -= a;          \
    c -= b;          \
    c ^= (b >> 5);   \
    a -= b;          \
    a -= c;          \
    a ^= (c >> 3);   \
    b -= c;          \
    b -= a;          \
    b ^= (a << 10);  \
    c -= a;          \
    c -= b;          \
    c ^= (b >> 15);  \
  }

/***************************************************************************/
/* From lookup2.                                                           */
/* hash_data() -- hash a variable-length key into a 32-bit value           */
/*   k       : the key (the unaligned variable-length array of bytes)      */
/*   len     : the length of the key, counting by bytes                    */
/*   initval : can be any 4-byte value                                     */
/* Returns a 32-bit value.  Every bit of the key affects every bit of      */
/* the return value.  Every 1-bit and 2-bit delta achieves avalanche.      */
/* About 6*len+35 instructions.                                            */
/*                                                                         */
/* The best hash table sizes are powers of 2.  There is no need to do      */
/* mod a prime (mod is sooo slow!).  If you need less than 32 bits,        */
/* use a bitmask.  For example, if you need only 10 bits, do               */
/*   h = (h & hashmask(10));                                               */
/* In which case, the hash table should have hashsize(10) elements.        */
/*                                                                         */
/* If you are hashing n strings (char **)k, do it like this:               */
/*   for (i=0, h=0; i<n; ++i) h = hash( k[i], len[i], h);                  */
/*                                                                         */
/* By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this  */
/* code any way you wish, private, educational, or commercial.  It's free. */
/*                                                                         */
/* See http://burtleburtle.net/bob/hash/evahash.html                       */
/* Use for hash table lookup, or anything where one collision in 2^^32 is  */
/* acceptable.  Do NOT use for cryptographic purposes.                     */
/* k;       the key                                                        */
/* length;  the length of the key                                          */
/* initval; the previous hash, or an arbitrary value                       */
/***************************************************************************/
uint32_t
hash_data(char *k, uint32_t length, uint32_t initval)
{
  register uint32_t a, b, c, len;

  /* Set up the internal state */
  len = length;
  a = b = 0x9e3779b9; /* the golden ratio; an arbitrary value */
  c     = initval;    /* the previous hash value */

  /*---------------------------------------- handle most of the key */
  while (len >= 12)
  {
    a += (k[0] + ((uint32_t)k[1] << 8) + ((uint32_t)k[2] << 16)
          + ((uint32_t)k[3] << 24));
    b += (k[4] + ((uint32_t)k[5] << 8) + ((uint32_t)k[6] << 16)
          + ((uint32_t)k[7] << 24));
    c += (k[8] + ((uint32_t)k[9] << 8) + ((uint32_t)k[10] << 16)
          + ((uint32_t)k[11] << 24));
    mix(a, b, c);
    k += 12;
    len -= 12;
  }

  /*------------------------------------- handle the last 11 bytes */
  c += length;
  switch (len) /* all the case statements fall through */
  {
    case 11:
      c += ((uint32_t)k[10] << 24);
    case 10:
      c += ((uint32_t)k[9] << 16);
    case 9:
      c += ((uint32_t)k[8] << 8);
      /* the first byte of c is reserved for the length */
    case 8:
      b += ((uint32_t)k[7] << 24);
    case 7:
      b += ((uint32_t)k[6] << 16);
    case 6:
      b += ((uint32_t)k[5] << 8);
    case 5:
      b += k[4];
    case 4:
      a += ((uint32_t)k[3] << 24);
    case 3:
      a += ((uint32_t)k[2] << 16);
    case 2:
      a += ((uint32_t)k[1] << 8);
    case 1:
      a += k[0];
      /* case 0: nothing left to add */
  }
  mix(a, b, c);
  /*-------------------------------------------- report the result */
  return c;
}

/* ============================================================== */
/* Utility function to find the non-directory part of a pathname. */
/* ============================================================== */
const char *
basename(const char *path)
{
  const char *s;

  s = strrchr(path, '/');
  if (s)
  {
    return s + 1;
  }
  return path;
}

/* ========================================================== */
/* Utility function to check if a name refers to a directory. */
/* ========================================================== */
int
isdir(const char *path)
{
  struct stat buf;
  int         fd;

  /* Assume stat() may not be implemented; use fstat */
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return 0;

  if (fstat(fd, &buf) < 0)
  {
    close(fd);
    return 0;
  }

  close(fd);

  return S_ISDIR(buf.st_mode);
}

/* ======================================== */
/* trim characters from beginning of string */
/* ======================================== */
void
ltrim(char *str, const char *trim)
{
  size_t i;
  size_t len = strlen(str);
  size_t beg = strspn(str, trim);
  if (beg > 0)
    for (i = beg; i <= len; ++i)
      str[i - beg] = str[i];
}

/* ================================== */
/* trim characters from end of string */
/* ================================== */
void
rtrim(char *str, const char *trim)
{
  size_t len = strlen(str);
  while (len > 0 && strchr(trim, str[len - 1]))
    str[--len] = '\0';
}

/* ============================================= */
/* Test if a pathname corresponds to a symb link */
/* ============================================= */
int
is_symb_link(char *path)
{
  int status;

  struct stat statbuf;

  status = stat(path, &statbuf);
  if (status < 0)
    fatal("%s not found", path);

  return statbuf.st_mode & S_IFLNK;
}

/* ============================================= */
/* Test if a pathname corresponds to a real file */
/* ============================================= */
int
is_file(char *path)
{
  int status;

  struct stat statbuf;

  status = stat(path, &statbuf);
  if (status < 0)
    fatal("%s not found", path);

  return S_ISREG(statbuf.st_mode);
}

/* ==================================================== */
/* Test if a pathname corresponds to an executable file */
/* ==================================================== */
int
is_executable(char *path)
{
  int status;

  struct stat statbuf;

  status = stat(path, &statbuf);
  if (status < 0)
    fatal("%s not found", path);

  return S_ISREG(statbuf.st_mode)
         && (statbuf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}

/* =============================================================== */
/* Return 1 if str is a string of digits forming a positive number */
/* if the number argument is not NULL, put the number there.       */
/* =============================================================== */
int
is_number(int *number, const char *str)
{
  char *endptr = NULL;
  int   val    = (int)strtol(str, &endptr, 10);

  if (!endptr || *endptr != '\0')
    return 0;
  else if (val < 0)
    return 0;

  if (number)
    *number = val;

  return 1;
}

/* =================================================================== */
/* This function returns string s1 if string s2 is an empty string, or */
/* if s2 is not found in s1. If s2 is found in s1, the function        */
/* returns a new null-terminated string whose contents are identical   */
/* to s1, except that all occurrences of s2 in the original string s1  */
/* are, in the new string, replaced by the string s3. The caller owns  */
/* the new string and is responsible for freeing it.                   */
/*                                                                     */
/* Strings s1, s2, and s3 must all be null-terminated strings.         */
/*                                                                     */
/* If any of s1, s2, or s3 are NULL, the function returns NULL. If an  */
/* error occurs, the function returns NULL, though unfortunately there */
/* is no way to determine the nature of the error from the call site.  */
/* =================================================================== */
char *
strrep(const char *s1, const char *s2, const char *s3)
{
  if (!s1 || !s2 || !s3)
    return 0;
  size_t s1_len = strlen(s1);
  if (!s1_len)
    return (char *)s1;
  size_t s2_len = strlen(s2);
  if (!s2_len)
    return (char *)s1;

  /* Two-pass approach: figure out how much space to allocate for  */
  /* the new string, pre-allocate it, then perform replacement(s). */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  size_t      count = 0;
  const char *p     = s1;
  do
  {
    p = strstr(p, s2);
    if (p)
    {
      p += s2_len;
      ++count;
    }
  } while (p);

  if (!count)
    return (char *)s1;

  /* The following size arithmetic is extremely cautious, to guard */
  /* against size_t overflows.                                     */
  /* """"""""""""""""""""""""""""""""""""""""""""""""""""""""""""" */
  size_t s1_without_s2_len = s1_len - count * s2_len;
  size_t s3_len            = strlen(s3);
  size_t newstr_len        = s1_without_s2_len + count * s3_len;
  if (s3_len && ((newstr_len <= s1_without_s2_len) || (newstr_len + 1 == 0)))
    /* Overflow. */
    return 0;

  char *newstr = (char *)xmalloc(newstr_len + 1); /* w/ terminator */

  char       *dst          = newstr;
  const char *start_substr = s1;
  size_t      i;
  for (i = 0; i != count; ++i)
  {
    const char *end_substr = strstr(start_substr, s2);
    size_t      substr_len = end_substr - start_substr;
    memcpy(dst, start_substr, substr_len);
    dst += substr_len;
    memcpy(dst, s3, s3_len);
    dst += s3_len;
    start_substr = end_substr + s2_len;
  }

  /* copy remainder of s1, including trailing '\0' */
  /* """"""""""""""""""""""""""""""""""""""""""""" */
  size_t remains = s1_len - (start_substr - s1) + 1;
  memcpy(dst, start_substr, remains);
  return newstr;
}

/* ======================================================================== */
/* Strings concatenation with dynamic memory allocation.                    */
/* IN : a variable number of char * arguments with NULL terminating         */
/*      the sequence.                                                       */
/*      The first one must have been dynamically allocated and is mandatory */
/*                                                                          */
/* Returns a new allocated string containing the concatenation of all       */
/* the arguments. It is the caller's responsibility to free the resulting   */
/* string.                                                                  */
/* ======================================================================== */
char *
strappend(char *str, ...)
{
  size_t  l;
  va_list args;
  char   *s;

  l = 1 + strlen(str);
  va_start(args, str);

  s = va_arg(args, char *);

  while (s)
  {
    l += strlen(s);
    s = va_arg(args, char *);
  }

  va_end(args);

  str = xrealloc(str, l);

  va_start(args, str);
  s = va_arg(args, char *);

  while (s)
  {
    strcat(str, s);
    s = va_arg(args, char *);
  }
  va_end(args);

  return str;
}

FILE *
popen_exec(char **command, const char *mode, pid_t *child_pid)
{
  int    pfp[2], pid;           /* the pipe and the process */
  int    parent_end, child_end; /* of pipe      */
  char  *execpath;
  char **env = { NULL };

  enum
  {
    READ,
    WRITE
  };

  if (*mode == 'r')
  { /* figure out direction   */
    parent_end = READ;
    child_end  = WRITE;
  }
  else if (*mode == 'w')
  {
    parent_end = WRITE;
    child_end  = READ;
  }
  else
    return NULL;

  if (pipe(pfp) == -1) /* get a pipe   */
    return NULL;

  if ((pid = fork()) == -1)
  {                   /* and a process  */
    close(pfp[READ]); /* or dispose of pipe */
    close(pfp[WRITE]);
    return NULL;
  }

  /* --------------- parent code here ------------- */
  /*   need to close one end and fdopen other end   */

  if (pid > 0)
  {
    if (close(pfp[child_end]) == -1)
      return NULL;
    *child_pid = pid;

    return fdopen(pfp[parent_end], mode); /* same mode */
  }

  /* --------------- child code here --------------------- */
  /*   need to redirect stdin or stdout then exec the  rule  */

  if (close(pfp[parent_end]) == -1) /* close the other end  */
    exit(1);                        /* do NOT return  */

  if (dup2(pfp[child_end], child_end) == -1)
    exit(1);

  if (close(pfp[child_end]) == -1) /* done with this one */
    exit(1);

  /* all set to run command */

  execpath   = strdup(command[0]);
  command[0] = strrchr(execpath, '/');

  if (command[0] == NULL)
    command[0] = execpath;
  else
    command[0] += 1;

  execve(execpath, command, env);

  exit(1);
}

int
pclose_exec(FILE *f, pid_t pid)
{
  int status;

  fclose(f);

  do
  {
    if (waitpid(pid, &status, 0) >= 0)
      return status;
    if (errno != EINTR)
      break;
  } while (1);

  return -1;
}

ssize_t
my_getline(char **lineptr, size_t *n, FILE *stream)
{
  char  *bufptr = NULL;
  char  *p;
  size_t size;
  int    c;

  if (lineptr == NULL)
  {
    return -1;
  }
  if (stream == NULL)
  {
    return -1;
  }
  if (n == NULL)
  {
    return -1;
  }
  bufptr = *lineptr;
  size   = *n;

  c = fgetc(stream);
  if (c == EOF)
    return -1;

  if (bufptr == NULL)
  {
    bufptr = xmalloc(128);
    size   = 128;
  }
  p = bufptr;

  while (c != EOF)
  {
    size_t curpos = p - bufptr;

    if (curpos > (size - 1))
    {
      size   = size + 128;
      bufptr = xrealloc(bufptr, size);
      p      = bufptr + curpos;
    }
    *p++ = c;
    if (c == '\n')
    {
      break;
    }
    c = fgetc(stream);
  }

  *p++     = '\0';
  *lineptr = bufptr;
  *n       = size;

  return p - bufptr - 1;
}

/* ========================================= */
/* Case insensitive strcmp.                  */
/* from http://c.snippets.org/code/stricmp.c */
/* ========================================= */
int
my_strcasecmp(const char *str1, const char *str2)
{
#ifdef HAVE_STRCASECMP
  return strcasecmp(str1, str2);
#else
  int retval = 0;

  while (1)
  {
    retval = tolower(*str1++) - tolower(*str2++);

    if (retval)
      break;

    if (*str1 && *str2)
      continue;
    else
      break;
  }
  return retval;
#endif
}

int
my_vasprintf(char **strp, const char *fmt, va_list ap)
{
  va_list ap1;
  int     len;
  char   *buffer;
  int     res;

  va_copy(ap1, ap);
  len = vsnprintf(NULL, 0, fmt, ap1);

  if (len < 0)
    return len;

  va_end(ap1);
  buffer = malloc(len + 1);

  if (!buffer)
    return -1;

  res = vsnprintf(buffer, len + 1, fmt, ap);

  if (res < 0)
    free(buffer);
  else
    *strp = buffer;

  return res;
}

int
my_asprintf(char **strp, const char *fmt, ...)
{
  int     error;
  va_list ap;

  va_start(ap, fmt);
#ifdef HAVE_VASPRINTF
  error = vasprintf(strp, fmt, ap);
#else
  error = my_vasprintf(strp, fmt, ap);
#endif
  va_end(ap);

  return error;
}

int
is_blank_str(const char *str)
{
  while (*str != '\0')
  {
    if (*str != ' ' && *str != '\t')
      return 0;
    str++;
  }
  return 1;
}

/* ======================================================================= */
/* Convert an mode in octal form to its representation as in the output of */
/* "ls -l".                                                                */
/*                                                                         */
/* smodes (in)  source mode in textual octal representation.               */
/* dmodes (out) destination mode in textual octal representation.          */
/* RC           0 OK, 1 KO (bas smode length)                              */
/* ======================================================================= */
int
octal_to_rwx(char *smodes, char *dmodes)
{
  size_t len;
  size_t i;
  int    rc = 1;
  char  *modes;

  len = strlen(smodes);
  if (len != 3 && len != 4)
    rc = 0;
  else
  {
    if (len == 4)
      modes = smodes + 1;
    else
      modes = smodes;

    strcpy(dmodes, "rwxrwxrwx");

    for (i = 0; i < 3; i++)
    {
      if (!((*modes - '0') & 04))
        dmodes[3 * i] = '-';
      if (!((*modes - '0') & 02))
        dmodes[3 * i + 1] = '-';
      if (!((*modes - '0') & 01))
        dmodes[3 * i + 2] = '-';

      modes++;
    }

    if (len == 4)
    {
      if ((smodes[0] - '0') & 04)
      {
        if (dmodes[2] == 'x')
          dmodes[2] = 's';
        else
          dmodes[2] = 'S';
      }
      if ((smodes[0] - '0') & 02)
      {
        if (dmodes[5] == 'x')
          dmodes[5] = 's';
        else
          dmodes[5] = 'S';
      }
      if ((smodes[0] - '0') & 01)
      {
        if (dmodes[5] == 'x')
          dmodes[5] = 't';
        else
          dmodes[5] = 'T';
      }
    }
  }

  return rc;
}

/* ==================================================================== */
/* Convert an mode from its symbolic representation as in the output of */
/* "ls -l" in octal form                                                */
/*                                                                      */
/* smodes (out) source mode in textual symbolic representation.         */
/* dmodes (in)  destination mode in textual octal representation.       */
/* RC           0 OK, 1 KO (bas smode length)                           */
/* ==================================================================== */
int
modes_to_octal(char *smodes, char *dmodes)
{
  size_t len;
  int    rc     = 1;
  size_t offset = 1;
  int    i, group;

  len = strlen(smodes);
  if (len != 9 && len != 10)
    rc = 0;
  else
  {
    if (len == 10)
      smodes++;

    dmodes[0] = dmodes[1] = dmodes[2] = dmodes[3] = '0';
    dmodes[4]                                     = '\0';

    for (group = 0; group < 3; group++)
    {
      for (i = 0; i < 3; i++)
      {
        switch (*smodes)
        {
          case '-':
            break;
          case 'r':
            if (i == 0)
              dmodes[offset] += (1 << 2);
            else
            {
              rc = 1;
              goto out;
            }
            break;
          case 'w':
            if (i == 1)
              dmodes[offset] += (1 << 1);
            else
            {
              rc = 1;
              goto out;
            }
            break;
          case 'x':
            if (i == 2)
              dmodes[offset] += (1 << 0);
            else
            {
              rc = 1;
              goto out;
            }
            break;
          case 's':
            if (i == 2 && group == 1)
            {
              dmodes[offset] += (1 << 0);
              dmodes[0] += (1 << 1);
            }
            else if (i == 2 && group == 2)
            {
              dmodes[offset] += (1 << 0);
              dmodes[0] += (1 << 2);
            }
            else
            {
              rc = 1;
              goto out;
            }
            break;
          case 'S':
            if (i == 2 && group == 1)
              dmodes[0] += (1 << 1);
            else if (i == 2 && group == 2)
              dmodes[0] += (1 << 2);
            else
            {
              rc = 1;
              goto out;
            }
            break;
          case 't':
            if (i == 2 && group == 2)
            {
              dmodes[0] += (1 << 2);
              dmodes[offset] += (1 << 0);
            }
            else
            {
              rc = 1;
              goto out;
            }
            break;
          case 'T':
            if (i == 2 && group == 2)
              dmodes[0] += (1 << 2);
            else
            {
              rc = 1;
              goto out;
            }
            break;
        }
        smodes++;
      }
      offset++;
    }
  }
out:

  return rc;
}

/* ========= */
/* Safe read */
/* ========= */
ssize_t
xread(int fd, void *buf, size_t n)
{
  size_t  rem = n;
  ssize_t r;
  char   *ptr = buf;

  while (rem > 0)
  {
    if ((r = read(fd, ptr, rem)) < 0)
    {
      if (errno == EINTR)
        r = 0;
      else
        return -1;
    }
    else if (r == 0)
      break; /* EOF */

    rem -= r;
    ptr += r;
  }
  return (n - rem);
}

/* ========== */
/* Safe write */
/* ========== */
ssize_t
xwrite(int fd, void *buf, size_t n)
{
  size_t  rem = n;
  ssize_t w;
  char   *ptr = buf;

  while (rem > 0)
  {
    if ((w = write(fd, ptr, rem)) <= 0)
    {
      if (errno == EINTR)
        w = 0;
      else
        return -1; /* errno set by write() */
    }

    rem -= w;
    ptr += w;
  }
  return n;
}

/* ================================================================= */
/* Allocate and initialize a new error message string from the given */
/* arguments.                                                        */
/* Return the newly mallocated string;                               */
/* ================================================================= */
char *
mkerr(char *msg, ...)
{
  char   *buf;
  size_t  size;
  va_list args, args_copy;

  va_start(args, msg);
  va_copy(args_copy, args);

  size = vsnprintf(NULL, 0, msg, args);
  buf  = xmalloc(size + 1);
  vsprintf(buf, msg, args_copy); /* Use args_copy as args has *
                                  | already been processed by *
                                  | vsnprintf.                */
  va_end(args);
  va_end(args_copy);

  return buf;
}

/* ================================== */
/* Check for the existence of a file. */
/* return 1 if yes, else return 0.    */
/* ================================== */
int
file_exists(const char *filename)
{
  FILE *fp = fopen(filename, "r");

  int exist = 0;

  if (fp != NULL)
  {
    exist = 1;
    fclose(fp);
  }

  return exist;
}

/* =============================================================== */
/* Detect if the current terminal belongs to the foreground group. */
/* returns 1 if yes else returns 0.                                */
/* =============================================================== */
int
is_in_foreground_process_group(void)
{
  int fd, fg;

  fd = open("/dev/tty", O_RDONLY);
  if (fd < 0)
    return 0;

  fg = (tcgetpgrp(fd) == getpgid(0));

  close(fd);

  return fg;
}

/* ====================== */
/* pointer swap function. */
/* ====================== */
void
swap(void **a, void **b)
{
  void *tmp;

  tmp = *a;
  *a  = *b;
  *b  = tmp;
}

void
hexdump(const char *buf, FILE *fp, const char *prefix, size_t size)
{
  unsigned int  b;
  unsigned char d[17];
  unsigned int  o, mo;
  size_t        l;

  o = mo = 0;
  l      = strlen(prefix);

  memset(d, '\0', 17);
  for (b = 0; b < size; b++)
  {

    d[b % 16] = isprint(buf[b]) ? (unsigned char)buf[b] : '.';

    if ((b % 16) == 0)
    {
      o = l + 7;
      if (o > mo)
        mo = o;
      fprintf(fp, "%s: %04x:", prefix, b);
    }

    o += 3;
    if (o > mo)
      mo = o;
    fprintf(fp, " %02x", (unsigned char)buf[b]);

    if ((b % 16) == 15)
    {
      mo = o;
      o  = 0;
      fprintf(fp, " |%s|", d);
      memset(d, '\0', 17);
      fprintf(fp, "\n");
    }
  }
  if ((b % 16) != 0)
  {
    for (unsigned int i = 0; i < mo - o; i++)
      fprintf(fp, "%c", ' ');

    fprintf(fp, " |%s", d);
    if (mo > o)
      for (unsigned int i = 0; i < 16 - strlen(d); i++)
        fprintf(fp, "%c", ' ');
    fprintf(fp, "%c", '|');
    memset(d, '\0', 17);
    fprintf(fp, "\n");
  }
}
