# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Load this file by adding this to your ~/.lldbinit or your launch.json:
# command script import <this_dir>/lldb_visualizers.py

# for py2/py3 compatibility
from __future__ import print_function

import collections
import functools
import re
import traceback

import lldb

######################
# custom visualizers #
######################


def lazy_unsigned_expression(expr):
  sb_value = None
  uint_value = 0

  def getter(fromValue):
    nonlocal sb_value
    nonlocal uint_value
    if sb_value is None:
      sb_value = fromValue.CreateValueFromExpression(expr, expr)
      uint_value = sb_value.GetValueAsUnsigned()
    return uint_value

  return getter


def lazy_unsigned_constant(name):
  return lazy_unsigned_expression('v8::internal::' + name)


# Values to only evaluate once
FrameSummary_Kind_JAVASCRIPT = lazy_unsigned_constant(
    'FrameSummary::Kind::JAVASCRIPT')


def field_from_address(parent_value, offset_getter, name, typename=None):
  addr = parent_value.GetValueAsUnsigned()
  try:
    tag_mask = kHeapObjectTagMask(parent_value)
    if (addr & tag_mask) == kHeapObjectTag(parent_value):
      heap_base = V8HeapCompressionScheme_base_(parent_value)
      if (addr & ~(kPtrComprCageBaseAlignment(parent_value) - 1)) == heap_base:
        field_address = (addr & ~tag_mask) + offset_getter(parent_value)
        if typename is None:
          return parent_value.CreateValueFromExpression(
              name, '(%s | (unsigned long)(*(int*)(%s)))' %
              (heap_base, field_address))
        else:
          return parent_value.CreateValueFromExpression(
              name, '*(%s *)%s' % (typename, field_address))
  except:
    print(traceback.format_exc())

  return None


class DictProvider:
  updating_for_formatter = False

  def __init__(self):
    self.children = None
    self.name_to_index = None
    self.update_only_for_formatter = False

  def populate_children(self, mapping):
    raise "Not implemented"

  def update(self):
    if self.update_only_for_formatter and not DictProvider.updating_for_formatter:
      return
    if DictProvider.updating_for_formatter:
      # in the future, only update when updating for formatter
      self.update_only_for_formatter = True
      # update only once
      DictProvider.updating_for_formatter = False
    self.children = None
    self.name_to_index = None

  def ensure_populated(self):
    if self.children is None:
      try:
        new_dict = collections.OrderedDict()
        self.populate_children(new_dict)
        self.children = list(new_dict.values())
        self.name_to_index = {k: v for (v, k) in enumerate(new_dict.keys())}
      except:
        print(traceback.format_exc())

  def num_children(self):
    self.ensure_populated()
    return len(self.children)

  def has_children(self):
    return True

  def get_child_index(self, name):
    self.ensure_populated()
    return self.name_to_index.get(name, None)

  def get_child_at_index(self, index):
    self.ensure_populated()
    return self.children[index]()


# Parse printed properties. May be a named property (rendered like " - name: val")
# or a list index (rendered like "    0: val"). If line ends with '{', it is a
# multiline property expected to continue until a line ends with '}'.
CHILD_RE = re.compile(
    r'((?<=^ - )[^:]+|(?<=\n    ) *[0-9]+): (.*(?:[^\{]|\{\n(?:.*\n)+.*\}))$',
    re.MULTILINE)
# Parse an address out of a value, so we can create an object that can be further
# expanded. Note that some things like Oddballs aren't worth trying to expand.
ADDRESS_RE = re.compile(
    r'^(?:\[[^\]]+\] )?(0x[0-9a-fA-F]{8,16})(?: (?!\[(?:Oddball|String)\]|\<(?:Symbol:|Code BUILTIN) .+\>).*)?$'
)
NUMERIC_RE = re.compile(r'^-?[0-9\.]+([eE][+\-]?[0-9]+)?$')
PROPERTY_RE = re.compile(r'    (0x(?:[^:]|:(?! 0x))+): (.+)$')
OBJECT_DUMP_NAME = '[Dump]'


def make_string(parent, pystring, name):
  # Planning on python string representations being valid C/C++
  # However, python repr usually gives single quoted reprs and we need double quotes
  converted = '"%s"' % repr(pystring)[1:-1].replace('"', '\\"')
  return parent.CreateValueFromExpression(name, converted)


def get_string_value(valobj):
  # Count on existing std::string visualizer to give us a summary so we don't
  # need to do valobj.__r_.__value_.__l.__data_
  summary = valobj.GetSummary()
  if summary and len(summary) >= 2 and summary[0] == '"' and summary[-1] == '"':
    # Also count on the std::string summary being a valid python string
    # representation.
    return eval(summary)
  return None


def make_synthetic_field(parent, summary, name):
  m = ADDRESS_RE.match(summary)
  if m:
    return parent.CreateValueFromExpression(
        name, '_v8_internal_Get_Object((void *)%s)' % m.group(1))
  elif NUMERIC_RE.match(summary):
    return parent.CreateValueFromExpression(name, summary)
  else:
    # Fallback to creating a C string of the dumped text
    return make_string(parent, summary, name)


class HeapObjectChildrenProvider(DictProvider):

  def __init__(self, valobj, internal_dict):
    self.valueobject = valobj
    super().__init__()

  def populate_children(self, mapping):
    expression = ('_v8_internal_Print_Object_To_String((void *)%s)' %
                  self.valueobject.GetValueAsUnsigned())
    dump = self.valueobject.CreateValueFromExpression(OBJECT_DUMP_NAME,
                                                      expression)
    stringdump = get_string_value(dump)
    if stringdump:
      for (name, val) in CHILD_RE.findall(stringdump):
        if val.startswith('{\n') and val.endswith('\n }') and name.find(
            'properties') >= 0:
          for index, line in enumerate(val.splitlines()[1:-1]):
            m = PROPERTY_RE.match(line)
            if m:
              mapping[m.group(1)] = functools.partial(make_synthetic_field,
                                                      self.valueobject,
                                                      m.group(2), m.group(1))
        else:
          mapping[name] = functools.partial(make_synthetic_field,
                                            self.valueobject, val, name)
      mapping[OBJECT_DUMP_NAME] = lambda: dump


def summarize_heap_object_internal(valueobject, inner_getter):
  # By indicating that this synthentic child provider is called from an object
  # with a formatter, we can ensure it is updated only when that formatter is
  # called, preventing it from being reevaluated multiple times on a single step
  DictProvider.updating_for_formatter = True
  dumpobj = valueobject.GetChildMemberWithName(OBJECT_DUMP_NAME)
  DictProvider.updating_for_formatter = False
  if dumpobj and dumpobj.GetError().Success():
    dump = get_string_value(dumpobj)
    if dump:
      oneline = dump.splitlines()[0]
      if oneline:
        return oneline
  return '%s: Address=0x%x' % (valueobject.GetDisplayTypeName(),
                               inner_getter().GetValueAsUnsigned())


class HandleChildrenProvider(HeapObjectChildrenProvider):

  def __init__(self, valobj, internal_dict):
    super().__init__(
        valobj.GetChildMemberWithName('location_').Dereference(), internal_dict)


def summarize_handle(valueobject, unused_dict):
  return summarize_heap_object_internal(
      valueobject, lambda: valueobject.GetNonSyntheticValue().
      GetChildMemberWithName('location_').Dereference())


class TaggedChildrenProvider(HeapObjectChildrenProvider):

  def __init__(self, valobj, internal_dict):
    super().__init__(valobj.GetChildMemberWithName('ptr_'), internal_dict)


def summarize_tagged(valueobject, unused_dict):
  return summarize_heap_object_internal(
      valueobject,
      lambda: valueobject.GetNonSyntheticValue().GetChildMemberWithName('ptr_'))


def read_pointer(ptrobject):
  if not ptrobject.TypeIsPointerType():
    ptrobject = ptrobject.AddressOf()
  ptr = ptrobject.GetValueAsUnsigned()
  # If it's not 8 byte aligned, assume it's uninitialized and return 0 instead.
  # Could we do more validation?
  if (ptr & 7) == 0:
    return ptr
  return 0


def summarize_stack_frame(valueobject, unused_dict):
  ptr = read_pointer(valueobject)
  if ptr:
    typeval = valueobject.CreateValueFromExpression(
        '[Type]', '((v8::internal::StackFrame *)%s)->type()' % ptr)
    return 'StackFrame: %s' % typeval.GetValue()
    # Is it worth getting more here?


def summarize_stack_trace_debug_details(valueobject, unused_dict):
  summary = get_string_value(valueobject.GetChildMemberWithName('summary'))
  frametype = valueobject.GetChildMemberWithName('type').GetValue()
  firstline, _, _ = summary.partition('\n')
  _, _, result = firstline.partition(':')
  return frametype + result


class IsolateChildrenProvider(DictProvider):

  def __init__(self, valobj, internal_dict):
    self.valueobject = valobj
    super().__init__()

  def populate_children(self, mapping):
    ptr = read_pointer(self.valueobject)
    if ptr:
      mapping[
          '[Stack Trace]'] = lambda: self.valueobject.CreateValueFromExpression(
              '[Stack Trace]',
              '_v8_internal_Expand_StackTrace((v8::internal::Isolate *)%s)' %
              ptr)


class FrameSummaryChildrenProvider(DictProvider):

  def __init__(self, valobj, internal_dict):
    self.valueobject = valobj
    super().__init__()

  def populate_children(self, mapping):
    unionelement = 'base_'
    kind = self.valueobject.GetChildMemberWithName(
        unionelement).GetChildMemberWithName('kind_').GetValueAsUnsigned()
    if kind == FrameSummary_Kind_JAVASCRIPT(self.valueobject):
      unionelement = 'javascript_summary_'
    # TODO: Add other summary kinds
    mapping[unionelement] = lambda: self.valueobject.GetChildMemberWithName(
        unionelement)


def __lldb_init_module(debugger, unused_dict):
  debugger.HandleCommand(
      'type summary add -p --summary-string "Address=0x${var%x}" v8::internal::Address'
  )
  debugger.HandleCommand(
      'type synthetic add -p -l lldb_visualizers.HandleChildrenProvider -x "^v8::internal::Handle<"'
  )
  debugger.HandleCommand(
      'type synthetic add -p -l lldb_visualizers.TaggedChildrenProvider -x "^v8::internal::Tagged<"'
  )
  debugger.HandleCommand(
      'type summary add -p -F lldb_visualizers.summarize_handle -x "^v8::internal::Handle<"'
  )
  debugger.HandleCommand(
      'type summary add -p -F lldb_visualizers.summarize_tagged -x "^v8::internal::Tagged<"'
  )
  debugger.HandleCommand(
      'type summary add -F lldb_visualizers.summarize_stack_frame v8::internal::StackFrame'
  )
  debugger.HandleCommand(
      'type summary add -F lldb_visualizers.summarize_stack_trace_debug_details _v8_internal_debugonly::StackTraceDebugDetails'
  )
  debugger.HandleCommand(
      'type synthetic add -l lldb_visualizers.IsolateChildrenProvider v8::internal::Isolate'
  )
  debugger.HandleCommand(
      'type synthetic add -l lldb_visualizers.FrameSummaryChildrenProvider v8::internal::FrameSummary'
  )
