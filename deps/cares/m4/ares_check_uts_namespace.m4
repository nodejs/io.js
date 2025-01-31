# -*- Autoconf -*-

# SYNOPSIS
#
#   ARES_CHECK_UTS_NAMESPACE
#
# DESCRIPTION
#
#   This macro checks whether the local system supports Linux UTS namespaces.
#   Also requires user namespaces to be available, so that non-root users
#   can enter the namespace.
#   If so, it calls AC_DEFINE(HAVE_UTS_NAMESPACE).
#
# Copyright (C) The c-ares team
# SPDX-License-Identifier: MIT

AC_DEFUN([ARES_CHECK_UTS_NAMESPACE],[dnl
 AC_CACHE_CHECK([whether UTS namespaces are supported],
  ares_cv_uts_namespace,[
  AC_LANG_PUSH([C])
  AC_RUN_IFELSE([AC_LANG_SOURCE([[
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int utsfn(void *d) {
  char buffer[1024];
  const char *name = "autoconftest";
  int rc = sethostname(name, strlen(name));
  if (rc != 0) return 1;
  gethostname(buffer, 1024);
  return (strcmp(buffer, name) != 0);
}

char st2[1024*1024];
int fn(void *d) {
  pid_t child;
  int rc, status;
  usleep(100000);  /* synchronize by sleep */
  if (getuid() != 0) return 1;
  child = clone(utsfn, st2 + 1024*1024, CLONE_NEWUTS|SIGCHLD, 0);
  if (child < 0) return 1;
  rc = waitpid(child, &status, 0);
  if (rc <= 0) return 1;
  if (!WIFEXITED(status)) return 1;
  return WEXITSTATUS(status);
}
char st[1024*1024];
int main() {
  char buffer[1024];
  int rc, status, fd;
  pid_t child = clone(fn, st + 1024*1024, CLONE_NEWUSER|SIGCHLD, 0);
  if (child < 0) return 1;

  snprintf(buffer, sizeof(buffer), "/proc/%d/uid_map", child);
  fd = open(buffer, O_CREAT|O_WRONLY|O_TRUNC, 0755);
  snprintf(buffer, sizeof(buffer), "0 %d 1\n", getuid());
  write(fd, buffer, strlen(buffer));
  close(fd);

  rc = waitpid(child, &status, 0);
  if (rc <= 0) return 1;
  if (!WIFEXITED(status)) return 1;
  return WEXITSTATUS(status);
}
]])
  ],[ares_cv_uts_namespace=yes],[ares_cv_uts_namespace=no],[ares_cv_uts_namespace=no])
 AC_LANG_POP([C])
 ])
 if test "$ares_cv_uts_namespace" = yes; then
   AC_DEFINE([HAVE_UTS_NAMESPACE],[1],[Whether UTS namespaces are available])
 fi
]) # ARES_CHECK_UTS_NAMESPACE
