" Copyright (c) 2015 the V8 project authors. All rights reserved.
" Use of this source code is governed by a BSD-style license that can be
" found in the LICENSE file.
"
" Adds a "Compile this file" function, using ninja. On Mac, binds Cmd-k to
" this command. On Windows, Ctrl-F7 (which is the same as the VS default).
" On Linux, <Leader>o, which is \o by default ("o"=creates .o files)
"
" Adds a "Build this target" function, using ninja. This is not bound
" to any key by default, but can be used via the :CrBuild command.
" It builds 'd8' by default, but :CrBuild target1 target2 etc works as well,
" i.e. :CrBuild all or :CrBuild d8 cctest v8_unittests.
"
" Requires that gyp has already generated build.ninja files, and that ninja is
" in your path (which it is automatically if depot_tools is in your path).
"
" Add the following to your .vimrc file:
"     so /path/to/src/tools/vim/ninja-build.vim

pythonx << endpython
import os
import vim


def path_to_current_buffer():
  """Returns the absolute path of the current buffer."""
  return vim.current.buffer.name


def path_to_source_root():
  """Returns the absolute path to the V8 source root."""
  candidate = os.path.dirname(path_to_current_buffer())
  # This is a list of files that need to identify the src directory. The shorter
  # it is, the more likely it's wrong. The longer it is, the more likely it is to
  # break when we rename directories.
  fingerprints = ['.git', 'build', 'include', 'samples', 'src', 'testing',
                  'third_party', 'tools']
  while candidate and not all(
      [os.path.isdir(os.path.join(candidate, fp)) for fp in fingerprints]):
    candidate = os.path.dirname(candidate)
  return candidate


def path_to_build_dir(configuration):
  """Returns <v8_root>/<output_dir>/(Release|Debug)."""

  v8_root = path_to_source_root()
  sys.path.append(os.path.join(v8_root, 'tools', 'vim'))
  from ninja_output import GetNinjaOutputDirectory
  return GetNinjaOutputDirectory(v8_root, configuration)


def compute_ninja_command_for_targets(targets='', configuration=None):
  build_dir = path_to_build_dir(configuration);
  build_cmd = ' '.join(['autoninja', '-C', build_dir, targets])
  vim.command('return "%s"' % build_cmd)


def compute_ninja_command_for_current_buffer(configuration=None):
  """Returns the shell command to compile the file in the current buffer."""
  build_dir = path_to_build_dir(configuration)

  # ninja needs filepaths for the ^ syntax to be relative to the
  # build directory.
  file_to_build = path_to_current_buffer()
  file_to_build = os.path.relpath(file_to_build, build_dir) + '^'
  if sys.platform == 'win32':
    # Escape \ for Vim, and ^ for both Vim and shell.
    file_to_build = file_to_build.replace('\\', '\\\\').replace('^', '^^^^')
  compute_ninja_command_for_targets(file_to_build, configuration)
endpython

fun! s:MakeWithCustomCommand(build_cmd)
  let l:oldmakepgr = &makeprg
  let &makeprg=a:build_cmd
  if exists(':Make') == 2
    Make
  else
    silent make | cwindow
  endif
  if !has('gui_running')
    redraw!
  endif
  let &makeprg = l:oldmakepgr
endfun

fun! s:NinjaCommandForCurrentBuffer()
  pythonx compute_ninja_command_for_current_buffer()
endfun

fun! s:NinjaCommandForTargets(targets)
  pythonx compute_ninja_command_for_targets(vim.eval('a:targets'))
endfun

fun! CrCompileFile()
  call s:MakeWithCustomCommand(s:NinjaCommandForCurrentBuffer())
endfun

fun! CrBuild(...)
  let l:targets = a:0 > 0 ? join(a:000, ' ') : ''
  if (l:targets !~ '\i')
    let l:targets = 'd8'
  endif
  call s:MakeWithCustomCommand(s:NinjaCommandForTargets(l:targets))
endfun

command! CrCompileFile call CrCompileFile()
command! -nargs=* CrBuild call CrBuild(<q-args>)

if has('mac')
  map <D-k> :CrCompileFile<cr>
  imap <D-k> <esc>:CrCompileFile<cr>
elseif has('win32')
  map <C-F7> :CrCompileFile<cr>
  imap <C-F7> <esc>:CrCompileFile<cr>
elseif has('unix')
  map <Leader>o :CrCompileFile<cr>
endif
