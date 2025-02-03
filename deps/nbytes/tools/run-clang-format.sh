#!/usr/bin/env bash

# Copyright 2023 Yagiz Nizipli and Daniel Lemire

# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
# the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
# FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

set -e
COMMAND=$*
SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"
MAINSOURCE=$SCRIPTPATH/..
ALL_FILES=$(cd $MAINSOURCE && git ls-tree --full-tree --name-only -r HEAD | grep -e ".*\.\(c\|h\|cc\|cpp\|hh\)\$")

if clang-format-17 --version  2>/dev/null | grep -qF 'version 17.'; then
  cd $MAINSOURCE; clang-format-17 --style=file --verbose -i "$@" $ALL_FILES
  exit 0
elif clang-format --version  2>/dev/null | grep -qF 'version 17.'; then
  cd $MAINSOURCE; clang-format --style=file --verbose -i "$@" $ALL_FILES
  exit 0
fi
echo "Trying to use docker"
command -v docker >/dev/null 2>&1 || { echo >&2 "Please install docker. E.g., go to https://www.docker.com/products/docker-desktop Type 'docker' to diagnose the problem."; exit 1; }
docker info >/dev/null 2>&1 || { echo >&2 "Docker server is not running? type 'docker info'."; exit 1; }

if [ -t 0 ]; then DOCKER_ARGS=-it; fi
docker pull kszonek/clang-format-17

docker run --rm $DOCKER_ARGS -v "$MAINSOURCE":"$MAINSOURCE":Z  -w "$MAINSOURCE" -u "$(id -u $USER):$(id -g $USER)" kszonek/clang-format-17 --style=file --verbose -i "$@" $ALL_FILES
