#ifndef XMALLOC_H
#define XMALLOC_H

void *
xmalloc(size_t size);

void *
xcalloc(size_t num, size_t size);

void *
xrealloc(void *ptr, size_t size);

char *
xstrdup(const char *string);

char *
xstrndup(const char *str, unsigned len);

#endif
