# -*- Autoconf -*-

# SYNOPSIS
#
#   ARES_CHECK_USER_NAMESPACE
#
# DESCRIPTION
#
#   This macro checks whether the local system supports Linux user namespaces.
#   If so, it calls AC_DEFINE(HAVE_USER_NAMESPACE).
#
# Copyright (C) The c-ares team
# SPDX-License-Identifier: MIT

AC_DEFUN([ARES_CHECK_USER_NAMESPACE],[dnl
 AC_CACHE_CHECK([whether user namespaces are supported],
  ares_cv_user_namespace,[
  AC_LANG_PUSH([C])
  AC_RUN_IFELSE([AC_LANG_SOURCE([[
#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

int userfn(void *d) {
  usleep(100000);  /* synchronize by sleep */
  return (getuid() != 0);
}
char userst[1024*1024];
int main() {
  char buffer[1024];
  int rc, status, fd;
  pid_t child = clone(userfn, userst + 1024*1024, CLONE_NEWUSER|SIGCHLD, 0);
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
  ]])],[ares_cv_user_namespace=yes],[ares_cv_user_namespace=no],[ares_cv_user_namespace=no])
 AC_LANG_POP([C])
 ])
 if test "$ares_cv_user_namespace" = yes; then
   AC_DEFINE([HAVE_USER_NAMESPACE],[1],[Whether user namespaces are available])
 fi
]) # ARES_CHECK_USER_NAMESPACE
