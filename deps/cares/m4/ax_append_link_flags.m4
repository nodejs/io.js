# ===========================================================================
#   https://www.gnu.org/software/autoconf-archive/ax_append_link_flags.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_APPEND_LINK_FLAGS([FLAG1 FLAG2 ...], [FLAGS-VARIABLE], [EXTRA-FLAGS], [INPUT])
#
# DESCRIPTION
#
#   For every FLAG1, FLAG2 it is checked whether the linker works with the
#   flag.  If it does, the flag is added FLAGS-VARIABLE
#
#   If FLAGS-VARIABLE is not specified, the linker's flags (LDFLAGS) is
#   used. During the check the flag is always added to the linker's flags.
#
#   If EXTRA-FLAGS is defined, it is added to the linker's default flags
#   when the check is done.  The check is thus made with the flags: "LDFLAGS
#   EXTRA-FLAGS FLAG".  This can for example be used to force the linker to
#   issue an error when a bad flag is given.
#
#   INPUT gives an alternative input source to AC_COMPILE_IFELSE.
#
#   NOTE: This macro depends on the AX_APPEND_FLAG and AX_CHECK_LINK_FLAG.
#   Please keep this macro in sync with AX_APPEND_COMPILE_FLAGS.
#
# LICENSE
#
#   Copyright (c) 2011 Maarten Bosmans <mkbosmans@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.  This file is offered as-is, without any
#   warranty.

#serial 7

AC_DEFUN([AX_APPEND_LINK_FLAGS],
[AX_REQUIRE_DEFINED([AX_CHECK_LINK_FLAG])
AX_REQUIRE_DEFINED([AX_APPEND_FLAG])
for flag in $1; do
  AX_CHECK_LINK_FLAG([$flag], [AX_APPEND_FLAG([$flag], [m4_default([$2], [LDFLAGS])])], [], [$3], [$4])
done
])dnl AX_APPEND_LINK_FLAGS
