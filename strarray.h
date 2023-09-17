/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#ifndef STRARRAY_H
#define STRARRAY_H

enum strarray_options
{
  TRIM          = 1, /* Trims the in string before processing.              */
  MERGESAMESEPS = 2, /* A sequence of same separators is equal to just one. */
  MERGESEPS     = 4, /* All separators are equal to just one.               */
  KEEPQUOTES    = 8, /* Quotes are keeped.                                  */
  NOOCTAL       = 16 /* Numbrers with leading 0 are not octal.              */
};

int
strarray(char *in, char ***out, int *count, char *seps, int options);

#endif
