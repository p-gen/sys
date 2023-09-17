#ifndef CACHESTAT_H
#define CACHESTAT_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

typedef enum cache_status_e
{
  EMPTY    = 0xaaaa,
  SEARCHED = 0x5555,
  UNUSABLE = 0xad0b,
  USABLE   = 0x0d60,
  INVALID  = 0xffff
} cache_status_t;
#endif
