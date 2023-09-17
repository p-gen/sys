/* log.h */

/* Part of hhttppss, a simple HTTP server skeleton
 * Author: Berke Durak
 * Released in the public domain
 */

#ifndef LOG_H
#define LOG_H

typedef enum
{
  LOG_DEBUG, /* 0 */
  LOG_INFO,  /* 1 */
  LOG_DATA,  /* 2 */
  LOG_WARN,  /* 3 */
  LOG_ERROR, /* 4 */
  LOG_COUNT
} log_priority;

#ifdef DEBUG
#define DEBUG_TEST 1
#else
#define DEBUG_TEST 0
#endif

/* needs c99. */
/* """""""""" */
#define debug(fmt, ...)                  \
  do                                     \
  {                                      \
    if (DEBUG_TEST)                      \
      fprintf(stderr, fmt, __VA_ARGS__); \
  } while (0)

FILE *
log_init(char *log_file);

void
log_shutdown();

void
fatal(const char *format, ...);

void
error(char *msg, ...);

void
trace(log_priority p, char *msg, ...);

extern FILE *log_fh;

#endif
