#!/usr/bin/env bash

# This is used by the Node.js installer, which expects the cygwin/mingw
# shell script to already be present in the npm dependency folder.

(set -o igncr) 2>/dev/null && set -o igncr; # cygwin encoding fix

basedir=`dirname "$0"`

case `uname` in
  *CYGWIN*) basedir=`cygpath -w "$basedir"`;;
esac

if [ `uname` = 'Linux' ] && type wslpath &>/dev/null ; then
  IS_WSL="true"
fi

function no_node_dir {
  # if this didn't work, then everything else below will fail
  echo "Could not determine Node.js install directory" >&2
  exit 1
}

NODE_EXE="$basedir/node.exe"
if ! [ -x "$NODE_EXE" ]; then
  NODE_EXE="$basedir/node"
fi
if ! [ -x "$NODE_EXE" ]; then
  NODE_EXE=node
fi

# this path is passed to node.exe, so it needs to match whatever
# kind of paths Node.js thinks it's using, typically win32 paths.
CLI_BASEDIR="$("$NODE_EXE" -p 'require("path").dirname(process.execPath)' 2> /dev/null)"
if [ $? -ne 0 ]; then
  # this fails under WSL 1 so add an additional message. we also suppress stderr above
  # because the actual error raised is not helpful. in WSL 1 node.exe cannot handle
  # output redirection properly. See https://github.com/microsoft/WSL/issues/2370
  if [ "$IS_WSL" == "true" ]; then
    echo "WSL 1 is not supported. Please upgrade to WSL 2 or above." >&2
  fi
  no_node_dir
fi
NPM_PREFIX_JS="$CLI_BASEDIR/node_modules/npm/bin/npm-prefix.js"
NPX_CLI_JS="$CLI_BASEDIR/node_modules/npm/bin/npx-cli.js"
NPM_PREFIX=`"$NODE_EXE" "$NPM_PREFIX_JS"`
if [ $? -ne 0 ]; then
  no_node_dir
fi
NPM_PREFIX_NPX_CLI_JS="$NPM_PREFIX/node_modules/npm/bin/npx-cli.js"

# a path that will fail -f test on any posix bash
NPX_WSL_PATH="/.."

# WSL can run Windows binaries, so we have to give it the win32 path
# however, WSL bash tests against posix paths, so we need to construct that
# to know if npm is installed globally.
if [ "$IS_WSL" == "true" ]; then
  NPX_WSL_PATH=`wslpath "$NPM_PREFIX_NPX_CLI_JS"`
fi
if [ -f "$NPM_PREFIX_NPX_CLI_JS" ] || [ -f "$NPX_WSL_PATH" ]; then
  NPX_CLI_JS="$NPM_PREFIX_NPX_CLI_JS"
fi

"$NODE_EXE" "$NPX_CLI_JS" "$@"
