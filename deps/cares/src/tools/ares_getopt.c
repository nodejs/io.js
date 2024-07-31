/*
 * Original file name getopt.c  Initial import into the c-ares source tree
 * on 2007-04-11.  Lifted from version 5.2 of the 'Open Mash' project with
 * the modified BSD license, BSD license without the advertising clause.
 *
 */

/*
 * getopt.c --
 *
 *      Standard UNIX getopt function.  Code is from BSD.
 *
 * Copyright (c) 1987-2001 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * A. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * B. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * C. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ares_getopt.h"

#define BADCH  (int)'?'
#define BADARG (int)':'
#define EMSG   ""

void ares_getopt_init(ares_getopt_state_t *state, int nargc, const char * const * nargv)
{
  memset(state, 0, sizeof(*state));
  state->opterr = 1;
  state->optind = 1;
  state->place  = EMSG;
  state->argc   = nargc;
  state->argv   = nargv;
}

/*
 * ares_getopt --
 *    Parse argc/argv argument vector.
 */
int ares_getopt(ares_getopt_state_t *state, const char *ostr)
{
  const char *oli; /* option letter list index */

  /* update scanning pointer */
  if (!*state->place) {
    if (state->optind >= state->argc) {
      return -1;
    }
    state->place = state->argv[state->optind];
    if (*(state->place) != '-') {
      return -1;
    }
    state->place++;

    /* found "--" */
    if (*(state->place) == '-') {
      state->optind++;
      return -1;
    }

    /* Found just - */
    if (!*(state->place)) {
      state->optopt = 0;
      return BADCH;
    }
  }

  /* option letter okay? */
  state->optopt = *(state->place);
  state->place++;
  oli = strchr(ostr, state->optopt);

  if (oli == NULL) {
    if (!(*state->place)) {
      ++state->optind;
    }
    if (state->opterr) {
      (void)fprintf(stderr, "%s: illegal option -- %c\n", __FILE__,
                    state->optopt);
    }
    return BADCH;
  }

  /* don't need argument */
  if (*++oli != ':') {
    state->optarg = NULL;
    if (!*state->place) {
      ++state->optind;
    }
  } else {
    /* need an argument */
    if (*state->place) {                         /* no white space */
      state->optarg = state->place;
    } else if (state->argc <= ++state->optind) { /* no arg */
      state->place = EMSG;
      if (*ostr == ':') {
        return BADARG;
      }
      if (state->opterr) {
        (void)fprintf(stderr, "%s: option requires an argument -- %c\n",
                      __FILE__, state->optopt);
      }
      return BADARG;
    } else { /* white space */
      state->optarg = state->argv[state->optind];
    }
    state->place = EMSG;
    ++state->optind;
  }
  return state->optopt; /* dump back option letter */
}
