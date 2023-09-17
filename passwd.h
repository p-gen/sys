#ifndef PASSWD_H
#define PASSWD_H

/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#if defined(HAVE_LIBPAM) && PAM_ENABLED == 1
#include <security/pam_appl.h>
#endif

char *
secure_memset(void *s, int c, size_t n);

char *
getpass_r(const char *prompt);

#if defined(HAVE_LIBPAM) && PAM_ENABLED == 1
int
pam_conv_func(int                        num_msg,
              const struct pam_message **msg,
              struct pam_response      **resp,
              void                      *appdata_ptr);

int
check_password_pam(char *username);
#endif

int
check_password_files(char *username);

#endif
