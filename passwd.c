/* ################################################################### */
/* Copyright 2022, Pierre Gentile (p.gen.progs@gmail.com)              */
/*                                                                     */
/* This Source Code Form is subject to the terms of the Mozilla Public */
/* License, v. 2.0. If a copy of the MPL was not distributed with this */
/* file, You can obtain one at https://mozilla.org/MPL/2.0/.           */
/* ################################################################### */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <shadow.h>
#include <crypt.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include "config.h"
#include "log.h"
#include "utils.h"
#include "passwd.h"

static volatile int got_int;

static void catch (int);

static char *
my_getpass(const char *prompt)
{
  struct termios term;
  unsigned short flags;
  char          *p;
  int            c;
  FILE          *file;

  static char pbuf[128] = { '\0' };
  struct sigaction new, osigint, osigtstp;

  if ((file = fopen("/dev/tty", "r+")) == NULL)
    return (NULL);
  setbuf(file, NULL);

  got_int      = 0;
  new.sa_flags = 0;

  new.sa_handler = catch;
  (void)sigemptyset(&new.sa_mask);
  (void)sigaction(SIGINT, &new, &osigint); /* trap INT (^C). */

  new.sa_handler = SIG_IGN;
  (void)sigaction(SIGTSTP, &new, &osigtstp); /* ignore STOP. */
  (void)ioctl(fileno(file), TCGETS, &term);
  flags = term.c_lflag;
  term.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
  (void)ioctl(fileno(file), TCSETSF, &term);

  (void)fputs(prompt, file);
  p = pbuf;

  while (!got_int && (c = fgetc(file)) != '\n' && c != '\r' && c != EOF)
  {
    if (p < &pbuf[128])
      *p++ = (char)c;
  }

  *p = '\0';

  (void)fputc('\n', file);

  term.c_lflag = flags;
  (void)ioctl(fileno(file), TCSETSW, &term);
  (void)sigaction(SIGINT, &osigint, NULL);
  (void)sigaction(SIGTSTP, &osigtstp, NULL);
  (void)fclose(file);

  if (got_int)
  { /* interrupted -> clear the input. */
    pbuf[0] = '\0';
  }
  return (pbuf);
}

static void catch (int x)
{
  got_int = 1;
  write(2, "<Cancelled!>", 13);
}

#if defined(HAVE_LIBPAM) && PAM_ENABLED == 1
/* ============================================================ */
/* Conversation function used by the PAM authentication system. */
/* ============================================================ */
int
pam_conv_func(int                        n,
              const struct pam_message **msg,
              struct pam_response      **resp,
              void                      *data)
{
  struct pam_response *aresp;
  char                 buf[PAM_MAX_RESP_SIZE];
  int                  i;

  if (n <= 0 || n > PAM_MAX_NUM_MSG)
    return PAM_CONV_ERR;

  if ((aresp = calloc(n, sizeof *aresp)) == NULL)
    return (PAM_BUF_ERR);

  for (i = 0; i < n; ++i)
  {
    aresp[i].resp_retcode = 0;
    aresp[i].resp         = NULL;

    switch (msg[i]->msg_style)
    {
      case PAM_PROMPT_ECHO_OFF:
        aresp[i].resp = strdup(my_getpass(msg[i]->msg));

        if (aresp[i].resp == NULL)
          goto fail;

        break;

      case PAM_PROMPT_ECHO_ON:
        fputs(msg[i]->msg, stderr);

        if (fgets(buf, sizeof buf, stdin) == NULL)
          goto fail;

        aresp[i].resp = strdup(buf);
        if (aresp[i].resp == NULL)
          goto fail;

        break;

      case PAM_ERROR_MSG:
        fputs(msg[i]->msg, stderr);

        if (strlen(msg[i]->msg) > 0
            && msg[i]->msg[strlen(msg[i]->msg) - 1] != '\n')
          fputc('\n', stderr);

        break;

      case PAM_TEXT_INFO:
        fputs(msg[i]->msg, stdout);

        if (strlen(msg[i]->msg) > 0
            && msg[i]->msg[strlen(msg[i]->msg) - 1] != '\n')
          fputc('\n', stdout);

        break;

      default:
        goto fail;
    }
  }
  *resp = aresp;
  return (PAM_SUCCESS);

fail:
  for (i = 0; i < n; ++i)
  {
    if (aresp[i].resp != NULL)
    {
      memset(aresp[i].resp, 0, strlen(aresp[i].resp));
      free(aresp[i].resp);
    }
  }

  memset(aresp, 0, n * sizeof *aresp);
  *resp = NULL;

  return (PAM_CONV_ERR);
}

/* ======================================================== */
/* Ask a user its passwd and check its validity through PAM */
/* username (in) users's username                           */
/* return 1 if OK, 0 if not                                 */
/* ======================================================== */
int
check_password_pam(char *username)
{
  int ret = 0;
  int rc  = 0;

  pam_handle_t   *pamh;
  struct pam_conv conv = { pam_conv_func, NULL };

  conv.appdata_ptr = username;

  if (pam_start("passwd", username, &conv, &pamh) == PAM_SUCCESS)
  {
    if (pam_authenticate(pamh, 0) == PAM_SUCCESS)
      rc = 1;

    pam_end(pamh, ret);
  }

  return rc;
}
#endif

int
check_password_files(char *username)
{
  char          *password;
  char          *encrypted;
  struct passwd *pwd;
  struct spwd   *hash;

  pwd = getpwnam(username);
  if (pwd == NULL)
  {
    trace(LOG_ERROR, "couldn't get password record.");
    return 0;
  }

  hash = getspnam(username);
  if (hash == NULL && errno == EACCES)
  {
    trace(LOG_ERROR, "no permission to read shadow password file.");
    return 0;
  }

  if (hash != NULL) /* If there is a shadow password record */
  {
    if (strcmp(hash->sp_pwdp, "!") == 0 || strcmp(hash->sp_pwdp, "*") == 0
        || strcmp(hash->sp_pwdp, "*LK*") == 0)
    {
      trace(LOG_ERROR, "this account is locked.");
      return 0;
    }

    pwd->pw_passwd = hash->sp_pwdp; /* Use the shadow password */
  }

  password = my_getpass("Local password: ");
  if (password == NULL)
    return 0;

  /* Encrypt password and erase clear text version immediately */
  errno     = 0;
  encrypted = crypt(password, pwd->pw_passwd);
  for (char *p = password; *p != '\0';)
    *p++ = '\0';

  if (errno != 0 || encrypted == NULL)
  {
    switch (errno)
    {
      case EINVAL:
        trace(LOG_ERROR,
              "hashing method that is not supported "
              "or invalid hash.");
        break;

      case ERANGE:
        trace(LOG_ERROR, "password is to long or too short.");
        break;

      case ENOMEM:
        trace(LOG_ERROR, "failed to allocate internal scratch memory.");
        break;

      case ENOSYS:
      case EOPNOTSUPP:
        trace(LOG_ERROR, "hashing method not supported or not implemented.");
        break;
    }
    return 0;
  }

  return !!(strcmp(encrypted, pwd->pw_passwd) == 0);
}
