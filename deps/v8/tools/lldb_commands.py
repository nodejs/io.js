# Copyright 2017 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Load this file by adding this to your ~/.lldbinit:
# command script import <this_dir>/lldb_commands.py

# for py2/py3 compatibility
from __future__ import print_function

import os
import re
import shlex

import lldb

#####################
# Helper functions. #
#####################

def current_thread(debugger):
  return debugger.GetSelectedTarget().GetProcess().GetSelectedThread()

def current_frame(debugger):
  return current_thread(debugger).GetSelectedFrame()


def no_arg_cmd(debugger, cmd, print_error=True):
  cast_to_void_expr = '(void) {}'.format(cmd)
  evaluate_result = current_frame(debugger).EvaluateExpression(cast_to_void_expr)
  # When a void function is called the return value type is 0x1001 which
  # is specified in http://tiny.cc/bigskz. This does not indicate
  # an error so we check for that value below.
  kNoResult = 0x1001
  result = evaluate_result.GetError()
  is_success = not result.fail or result.value == kNoResult
  if not is_success:
    if print_error:
      print("Failed to evaluate command {} :".format(cmd))
      print(result.description)
  else:
    print("")
  return (is_success, result, cmd)


def ptr_arg_cmd(debugger, name, param, cmd, fields=[], print_error=True):
  if not param:
    print("'{}' requires an argument".format(name))
    return (False, None, "")
  value = current_frame(debugger).EvaluateExpression(param)
  error = value.GetError()
  if error.fail:
    print("Error evaluating {}\n{}".format(param, error))
    return (False, error, "")
  # Proceed, ignoring visualizers if they are enabled.
  value = value.GetNonSyntheticValue()
  # Process interesting fields, if this is a structure.
  while True:
    for f in fields:
      field = value.GetValueForExpressionPath(f)
      if field.IsValid():
        value = field
        break
    else:
      break
  return no_arg_cmd(debugger, cmd.format(value.GetValue()), print_error)


def print_handle(debugger, command_name, param, print_func):
  value = current_frame(debugger).EvaluateExpression(param)
  error = value.GetError()
  if error.fail:
    print("Error evaluating {}\n{}".format(param, error))
    return (False, error, "")
  # Attempt to print, ignoring visualizers if they are enabled
  result = print_func(value.GetNonSyntheticValue())
  if not result[0]:
    print("{} cannot print a value of type {}".format(command_name,
                                                      value.type.name))
  return result


def print_direct(debugger, command_name, value):
  CMD = "_v8_internal_Print_Object((v8::internal::Address*)({}))"
  return no_arg_cmd(debugger, CMD.format(value))


def print_indirect(debugger, command_name, value):
  CMD = "_v8_internal_Print_Object(*(v8::internal::Address**)({}))"
  return no_arg_cmd(debugger, CMD.format(value))


V8_LLDB_COMMANDS = []


def lldbCommand(fn):
  V8_LLDB_COMMANDS.append(fn.__name__)
  return fn


#####################
# lldb commands.    #
#####################

@lldbCommand
def job(debugger, param, *args):
  """Print a v8 heap object"""
  CMD = "_v8_internal_Print_Object((void*)({}))"
  # Allow for Tagged<T>, containing a ptr_.
  ptr_arg_cmd(debugger, 'job', param, CMD, [".ptr_"])


@lldbCommand
def jh(debugger, param, *args):
  """Print v8::internal::(Maybe)?(Direct|Indirect)?Handle value"""

  def print_func(value):
    # Indirect handles contain a location_.
    field = value.GetValueForExpressionPath(".location_")
    if field.IsValid():
      return print_indirect(debugger, 'jh', field.value)
    # With v8_enable_direct_handle=true, direct handles contain a obj_.
    field = value.GetValueForExpressionPath(".obj_")
    if field.IsValid():
      return print_direct(debugger, 'jh', field.value)
    # Without v8_enable_direct_handle=true, direct handles contain a handle_.
    field = value.GetValueForExpressionPath(".handle_")
    if field.IsValid():
      return print_func(field)
    # We don't know how to print this...
    return (False, None, "")

  return print_handle(debugger, 'jh', param, print_func)


@lldbCommand
def jlh(debugger, param, *args):
  """Print v8::(Maybe)?Local value"""

  def print_func(value):
    # After https://crrev.com/c/4335544, v8::MaybeLocal contains a local_.
    field = value.GetValueForExpressionPath(".local_")
    if field.IsValid():
      value = field
    # After https://crrev.com/c/4335544, v8::Local contains a location_.
    field = value.GetValueForExpressionPath(".location_")
    if field.IsValid():
      return print_indirect(debugger, 'jlh', field.value)
    # Before https://crrev.com/c/4335544, v8::Local contained a val_.
    field = value.GetValueForExpressionPath(".val_")
    if field.IsValid():
      return print_indirect(debugger, 'jlh', field.value)
    # With v8_enable_direct_handle=true, v8::Local contains a ptr_.
    field = value.GetValueForExpressionPath(".ptr_")
    if field.IsValid():
      return print_direct(debugger, 'jlh', field.value)
    # We don't know how to print this...
    return (False, None, "")

  return print_handle(debugger, 'jlh', param, print_func)


@lldbCommand
def jl(debugger, param, *args):
  """Print v8::Local handle value"""
  return jlh(debugger, param, *args)


@lldbCommand
def jco(debugger, param, *args):
  """Print the code object at the given pc (default: current pc)"""
  CMD = "_v8_internal_Print_Code((void*)({}))"
  if not param:
    param = str(current_frame(debugger).FindRegister("pc").value)
  no_arg_cmd(debugger, CMD.format(param))


@lldbCommand
def jdh(debugger, param, *args):
  """Print JSDispatchEntry object in the JSDispatchTable with the given dispatch handle"""
  CMD = "_v8_internal_Print_Dispatch_Handle((uint32_t)({}))"
  # Allow for JSDispatchHandle, containing a value_.
  ptr_arg_cmd(debugger, 'jdh', param, CMD, [".value_"])


@lldbCommand
def jca(debugger, param, *args):
  """Print a v8 Code object assembly code from an internal code address"""
  param = shlex.split(param)
  if not param:
    obj = str(current_frame(debugger).FindRegister("pc").value)
  else:
    obj = param[0]
  range_limit = param[1] if len(param) > 1 else 30
  CMD = "_v8_internal_Print_OnlyCode((void*)({}), (size_t)({}))"
  no_arg_cmd(debugger, CMD.format(obj, range_limit))


@lldbCommand
def jtt(debugger, param, *args):
  """Print the complete transition tree starting at the given v8 map"""
  CMD = "_v8_internal_Print_TransitionTree((void*)({}), false)"
  # Allow for Tagged<T>, containing a ptr_.
  ptr_arg_cmd(debugger, 'jtt', param, CMD, [".ptr_"])


@lldbCommand
def jttr(debugger, param, *args):
  """Print the complete transition tree starting at the root map of the given v8 map"""
  CMD = "_v8_internal_Print_TransitionTree((void*)({}), true)"
  # Allow for Tagged<T>, containing a ptr_.
  ptr_arg_cmd(debugger, 'jttr', param, CMD, [".ptr_"])


@lldbCommand
def jst(debugger, *args):
  """Print the current JavaScript stack trace"""
  no_arg_cmd(debugger, "_v8_internal_Print_StackTrace()")


@lldbCommand
def pn(debugger, param, *args):
  """Print a v8 TurboFan graph node"""
  CMD = "_v8_internal_Node_Print((void*)({}))"
  # Allow for Tagged<T>, containing a ptr_.
  ptr_arg_cmd(debugger, 'pn', param, CMD, [".ptr_"])


@lldbCommand
def jss(debugger, *args):
  """Skip the jitted stack on x64 to where we entered JS last"""
  frame = current_frame(debugger)
  js_entry_sp = frame.EvaluateExpression(
      "v8::internal::Isolate::Current()->thread_local_top()->js_entry_sp_;") \
       .GetValue()
  sizeof_void = frame.EvaluateExpression("sizeof(void*)").GetValue()
  rbp = frame.FindRegister("rbp")
  rsp = frame.FindRegister("rsp")
  pc = frame.FindRegister("pc")
  rbp = js_entry_sp
  rsp = js_entry_sp + 2 *sizeof_void
  pc.value = js_entry_sp + sizeof_void


@lldbCommand
def jfci(debugger, param, *args):
  """Print v8::FunctionCallbackInfo<T>& info"""
  CMD = "_v8_internal_Print_FunctionCallbackInfo((void*)(&{}))"
  if not param:
    print("'jfci' requires an argument")
  else:
    no_arg_cmd(debugger, CMD.format(param))


@lldbCommand
def jpci(debugger, param, *args):
  """Print v8::PropertyCallbackInfo<T>& info"""
  CMD = "_v8_internal_Print_PropertyCallbackInfo((void*)(&{}))"
  if not param:
    print("'jpci' requires an argument")
  else:
    no_arg_cmd(debugger, CMD.format(param))


# Print whether the object is marked, the mark-bit cell and index. The address
# of the cell is handy for reverse debugging to check when the object was
# marked/unmarked.
@lldbCommand
def jomb(debugger, param, *args):
  """Print whether the object is marked, the markbit cell and index"""
  CMD = "_v8_internal_Print_Object_MarkBit((void*)({}))"
  # Allow for Tagged<T>, containing a ptr_.
  ptr_arg_cmd(debugger, 'jomb', param, CMD, [".ptr_"])


@lldbCommand
def bta(debugger, *args):
  """Print stack trace with assertion scopes"""
  func_name_re = re.compile("([^(<]+)(?:\(.+\))?")
  assert_re = re.compile(
      "^v8::internal::Per\w+AssertType::(\w+)_ASSERT, (false|true)>")
  thread = current_thread(debugger)
  for frame in thread:
    functionSignature = frame.GetDisplayFunctionName()
    if functionSignature is None:
      continue
    functionName = func_name_re.match(functionSignature)
    line = frame.GetLineEntry().GetLine()
    sourceFile = frame.GetLineEntry().GetFileSpec().GetFilename()
    if sourceFile and line:
      sourceFile = sourceFile + ":" + str(line)

    if sourceFile is None:
      sourceFile = ""
    print("[%-2s] %-60s %-40s" % (frame.GetFrameID(),
                                  functionName.group(1),
                                  sourceFile))
    match = assert_re.match(str(functionSignature))
    if match:
      if match.group(3) == "false":
        prefix = "Disallow"
        color = "\033[91m"
      else:
        prefix = "Allow"
        color = "\033[92m"
      print("%s -> %s %s (%s)\033[0m" % (
          color, prefix, match.group(2), match.group(1)))


def setup_source_map_for_relative_paths(debugger):
  # Copied from Chromium's tools/lldb/lldbinit.py.
  # When relative paths are used for debug symbols, lldb cannot find source
  # files. Set up a source map to point to V8's root.
  this_dir = os.path.dirname(os.path.abspath(__file__))
  source_dir = os.path.join(this_dir, os.pardir)

  debugger.HandleCommand(
    'settings set target.source-map ../.. ' + source_dir)


def __lldb_init_module(debugger, dict):
  setup_source_map_for_relative_paths(debugger)
  debugger.HandleCommand('settings set target.x86-disassembly-flavor intel')
  for cmd in V8_LLDB_COMMANDS:
    debugger.HandleCommand(
      'command script add -f lldb_commands.{} {}'.format(cmd, cmd))
