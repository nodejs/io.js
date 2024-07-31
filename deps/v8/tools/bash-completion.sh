#!/bin/bash
# Copyright 2012 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Inspired by and based on:
# https://chromium.googlesource.com/chromium/src/+/master/tools/bash-completion

# Flag completion rule for bash.
# To load in your shell, "source path/to/this/file".

v8_source="$(realpath "$(dirname "$BASH_SOURCE")"/..)"

_get_v8_flags() {
  # The first `sed` command joins lines when a line ends with '('.
  # See http://sed.sourceforge.net/sedfaq3.html#s3.2
  local flags_file="$v8_source/src/flags/flag-definitions.h"
  local defines=$( \
    sed -e :a -e '/($/N; s/(\n\s*/(/; ta' < "$flags_file" \
    | grep "^DEFINE" \
    | grep -v "DEFINE_IMPLICATION" \
    | grep -v "DEFINE_NEG_IMPLICATION" \
    | grep -v "DEFINE_VALUE_IMPLICATION" \
    | sed -e 's/_/-/g'; \
    grep "^  V(harmony_" "$flags_file" \
    | sed -e 's/^  V/DEFINE-BOOL/' \
    | sed -e 's/_/-/g'; \
    grep "^  V(" "$v8_source/src/wasm/wasm-feature-flags.h" \
    | sed -e 's/^  V(/DEFINE-BOOL(experimental-wasm-/' \
    | sed -e 's/_/-/g')
  sed -ne 's/^DEFINE-[^(]*(\([^,]*\).*/--\1/p' <<< "$defines"
  sed -ne 's/^DEFINE-BOOL(\([^,]*\).*/--no\1/p' <<< "$defines"
}

_get_d8_flags() {
  grep "strcmp(argv\[i\]" "$v8_source/src/d8/d8.cc" \
    | sed -ne 's/^[^"]*"--\([^"]*\)".*/--\1/p'
}

_d8_flag() {
  local targets
  targets=$(_get_v8_flags ; _get_d8_flags)
  COMPREPLY=($(compgen -W "$targets" -- "${COMP_WORDS[COMP_CWORD]}"))
  return 0
}

_test_flag() {
  local targets
  targets=$(_get_v8_flags)
  COMPREPLY=($(compgen -W "$targets" -- "${COMP_WORDS[COMP_CWORD]}"))
  return 0
}

complete -F _d8_flag -f d8 v8 v8-debug
complete -F _test_flag -f cctest v8_unittests

# Many distros set up their own GDB completion scripts. The logic below is
# careful to wrap any such functions (with additional logic), rather than
# overwriting them.
# An additional complication is that tested distros dynamically load their
# completion scripts on first use. So in order to be able to detect their
# presence, we have to force-load them first.
_maybe_setup_gdb_completions() {
  # We only want to set up the wrapping once:
  [[ -n "$_ORIGINAL_GDB_COMPLETIONS" ]] && return 0;

  # This path works for Debian, Ubuntu, and Gentoo; other distros unknown.
  # Feel free to submit patches to extend the logic.
  local GDB_COMP
  for GDB_COMP in "/usr/share/bash-completion/completions/gdb"; do
    [[ -f "$GDB_COMP" ]] && source $GDB_COMP
  done
  _ORIGINAL_GDB_COMPLETIONS="$(complete -p gdb 2>/dev/null \
                               | sed -e 's/^.*-F \([^ ]*\).*/\1/')"

  _gdb_v8_flag() {
    local c i next
    for (( i=1; i<$(($COMP_CWORD-1)); i++ )); do
      c=${COMP_WORDS[$i]}
      if [ "$c" = "-args" ] || [ "$c" = "--args" ] || [ "$c" == "--" ]; then
        next=$(basename -- ${COMP_WORDS[$(($i+1))]})
        if [ "$next" = "d8" ] ; then
          _d8_flag
          return 0
        elif [ "$next" = "v8_unittests" ] || [ "$next" = "cctest" ]; then
          _test_flag
          return 0
        fi
      fi
    done
    [[ -n "$_ORIGINAL_GDB_COMPLETIONS" ]] && $_ORIGINAL_GDB_COMPLETIONS
    return 0
  }
  complete -F _gdb_v8_flag -f gdb
}
_maybe_setup_gdb_completions
unset _maybe_setup_gdb_completions

_get_gm_flags() {
  "$v8_source/tools/dev/gm.py" --print-completions

  # cctest ignore directory structure, it's always "cctest/filename".
  find "$v8_source/test/cctest/" -type f -name 'test-*' | \
    xargs basename -a -s ".cc" | \
    while read -r item; do echo "cctest/$item/*"; done

  # Find all out/* directories that have an args.gn file.
  find out/ -maxdepth 1 -type d | \
    while read -r dir; do [[ -f "$dir/args.gn" ]] && echo $dir; done
}

_gm_flag() {
  local targets=$(_get_gm_flags)
  COMPREPLY=($(compgen -W "$targets" -- "${COMP_WORDS[COMP_CWORD]}"))
  return 0
}

# gm might be an alias, based on https://v8.dev/docs/build-gn#gm.
complete -F _gm_flag gm.py gm
