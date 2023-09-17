#ifndef UTILS_H
#define UTILS_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

void
data_error(const char *format, ...);

char *
get_rule(char *str, char *buf, size_t len);

char *
argvtostr(int argc, char **argv);

uint32_t
hash_string(char *key);

uint32_t
hash_data(char *k, uint32_t length, uint32_t initval);

const char *
basename(const char *path);

int
isdir(const char *path);

void
ltrim(char *str, const char *trim);

void
rtrim(char *str, const char *trim);

char *
str_replace(char *search, char *replace, char *subject);

int
is_symb_link(char *path);

int
is_file(char *path);

int
is_executable(char *path);

int
is_number(int *number, const char *str);

char *
strrep(const char *s1, const char *s2, const char *s3);

char *
strappend(char *str, ...);

FILE *
popen_exec(char **command, const char *mode, pid_t *child_pid);

int
pclose_exec(FILE *f, pid_t pid);

ssize_t
my_getline(char **lineptr, size_t *n, FILE *stream);

int
my_strcasecmp(const char *str1, const char *str2);

int
my_vasprintf(char **strp, const char *fmt, va_list ap);

int
my_asprintf(char **strp, const char *fmt, ...);

int
is_blank_str(const char *str);

int
octal_to_rwx(char *smodes, char *dmodes);

int
modes_to_octal(char *smodes, char *dmodes);

char *
get_word(char *str, char *buf, size_t len, char sep);

ssize_t
xread(int fd, void *buf, size_t n);

ssize_t
xwrite(int fd, void *buf, size_t n);

char *
mkerr(char *msg, ...);

int
file_exists(const char *filename);

int
is_in_foreground_process_group(void);

void
swap(void **a, void **b);

void
hexdump(const char *buf, FILE *fp, const char *prefix, size_t size);

#endif
