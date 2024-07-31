/**********************************************************************
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (C) Daniel Stenberg
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include "ares_private.h"


/* only do the following on windows
 */
#if defined(_WIN32) && !defined(MSDOS)

#  ifdef __WATCOMC__
/*
 * Watcom needs a DllMain() in order to initialise the clib startup code.
 */
BOOL WINAPI DllMain(HINSTANCE hnd, DWORD reason, LPVOID reserved)
{
  (void)hnd;
  (void)reason;
  (void)reserved;
  return (TRUE);
}
#  endif

#endif /* WIN32 builds only */
