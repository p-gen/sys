#ifndef COMP_H
#define COMP_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

int
str_comp(const void *a, const void *b);

int
rule_entry_comp(const void *a, const void *b);

int
var_entry_comp(const void *a, const void *b);

int
item_comp(const void *a, const void *b);

#endif
