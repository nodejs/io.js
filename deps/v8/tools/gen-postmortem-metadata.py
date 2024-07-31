#!/usr/bin/env python3

#
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
#

#
# Emits a C++ file to be compiled and linked into libv8 to support postmortem
# debugging tools.  Most importantly, this tool emits constants describing V8
# internals:
#
#    v8dbg_type_CLASS__TYPE = VALUE             Describes class type values
#    v8dbg_class_CLASS__FIELD__TYPE = OFFSET    Describes class fields
#    v8dbg_parent_CLASS__PARENT                 Describes class hierarchy
#    v8dbg_frametype_NAME = VALUE               Describes stack frame values
#    v8dbg_off_fp_NAME = OFFSET                 Frame pointer offsets
#    v8dbg_prop_NAME = OFFSET                   Object property offsets
#    v8dbg_NAME = VALUE                         Miscellaneous values
#
# These constants are declared as global integers so that they'll be present in
# the generated libv8 binary.
#

# for py2/py3 compatibility
from __future__ import print_function

import io
import re
import sys

#
# Miscellaneous constants such as tags and masks used for object identification,
# enumeration values used as indexes in internal tables, etc..
#
consts_misc = [
    {
        'name': 'FirstNonstringType',
        'value': 'FIRST_NONSTRING_TYPE'
    },
    {
        'name': 'APIObjectType',
        'value': 'JS_API_OBJECT_TYPE'
    },
    {
        'name': 'SpecialAPIObjectType',
        'value': 'JS_SPECIAL_API_OBJECT_TYPE'
    },
    {
        'name': 'FirstContextType',
        'value': 'FIRST_CONTEXT_TYPE'
    },
    {
        'name': 'LastContextType',
        'value': 'LAST_CONTEXT_TYPE'
    },
    {
        'name': 'FirstJSFunctionType',
        'value': 'FIRST_JS_FUNCTION_TYPE'
    },
    {
        'name': 'LastJSFunctionType',
        'value': 'LAST_JS_FUNCTION_TYPE'
    },
    {
        'name': 'IsNotStringMask',
        'value': 'kIsNotStringMask'
    },
    {
        'name': 'StringTag',
        'value': 'kStringTag'
    },
    {
        'name': 'StringEncodingMask',
        'value': 'kStringEncodingMask'
    },
    {
        'name': 'TwoByteStringTag',
        'value': 'kTwoByteStringTag'
    },
    {
        'name': 'OneByteStringTag',
        'value': 'kOneByteStringTag'
    },
    {
        'name': 'StringRepresentationMask',
        'value': 'kStringRepresentationMask'
    },
    {
        'name': 'SeqStringTag',
        'value': 'kSeqStringTag'
    },
    {
        'name': 'ConsStringTag',
        'value': 'kConsStringTag'
    },
    {
        'name': 'ExternalStringTag',
        'value': 'kExternalStringTag'
    },
    {
        'name': 'SlicedStringTag',
        'value': 'kSlicedStringTag'
    },
    {
        'name': 'ThinStringTag',
        'value': 'kThinStringTag'
    },
    {
        'name': 'HeapObjectTag',
        'value': 'kHeapObjectTag'
    },
    {
        'name': 'HeapObjectTagMask',
        'value': 'kHeapObjectTagMask'
    },
    {
        'name': 'SmiTag',
        'value': 'kSmiTag'
    },
    {
        'name': 'SmiTagMask',
        'value': 'kSmiTagMask'
    },
    {
        'name': 'SmiValueShift',
        'value': 'kSmiTagSize'
    },
    {
        'name': 'SmiShiftSize',
        'value': 'kSmiShiftSize'
    },
    {
        'name': 'SystemPointerSize',
        'value': 'kSystemPointerSize'
    },
    {
        'name': 'SystemPointerSizeLog2',
        'value': 'kSystemPointerSizeLog2'
    },
    {
        'name': 'TaggedSize',
        'value': 'kTaggedSize'
    },
    {
        'name': 'TaggedSizeLog2',
        'value': 'kTaggedSizeLog2'
    },
    {
        'name': 'CodeKindFieldMask',
        'value': 'Code::KindField::kMask'
    },
    {
        'name': 'CodeKindFieldShift',
        'value': 'Code::KindField::kShift'
    },
    {
        'name': 'DeoptimizationDataInlinedFunctionCountIndex',
        'value': 'DeoptimizationData::kInlinedFunctionCountIndex'
    },
    {
        'name': 'DeoptimizationDataLiteralArrayIndex',
        'value': 'DeoptimizationData::kLiteralArrayIndex'
    },
    {
        'name': 'DeoptimizationDataOptimizationIdIndex',
        'value': 'DeoptimizationData::kOptimizationIdIndex'
    },
    {
        'name': 'DeoptimizationDataSharedFunctionInfoIndex',
        'value': 'DeoptimizationData::kSharedFunctionInfoIndex'
    },
    {
        'name': 'DeoptimizationDataInliningPositionsIndex',
        'value': 'DeoptimizationData::kInliningPositionsIndex'
    },
    {
        'name': 'CodeKindBytecodeHandler',
        'value': 'static_cast<int>(CodeKind::BYTECODE_HANDLER)'
    },
    {
        'name': 'CodeKindInterpretedFunction',
        'value': 'static_cast<int>(CodeKind::INTERPRETED_FUNCTION)'
    },
    {
        'name': 'CodeKindBaseline',
        'value': 'static_cast<int>(CodeKind::BASELINE)'
    },
    {
        'name': 'OddballFalse',
        'value': 'Oddball::kFalse'
    },
    {
        'name': 'OddballTrue',
        'value': 'Oddball::kTrue'
    },
    {
        'name': 'OddballNull',
        'value': 'Oddball::kNull'
    },
    {
        'name': 'OddballUndefined',
        'value': 'Oddball::kUndefined'
    },
    {
        'name': 'ContextRegister',
        'value': 'kContextRegister.code()'
    },
    {
        'name': 'ReturnRegister0',
        'value': 'kReturnRegister0.code()'
    },
    {
        'name': 'JSFunctionRegister',
        'value': 'kJSFunctionRegister.code()'
    },
    {
        'name': 'InterpreterBytecodeOffsetRegister',
        'value': 'kInterpreterBytecodeOffsetRegister.code()'
    },
    {
        'name': 'InterpreterBytecodeArrayRegister',
        'value': 'kInterpreterBytecodeArrayRegister.code()'
    },
    {
        'name': 'RuntimeCallFunctionRegister',
        'value': 'kRuntimeCallFunctionRegister.code()'
    },
    {
        'name': 'prop_kind_Data',
        'value': 'static_cast<int>(PropertyKind::kData)'
    },
    {
        'name': 'prop_kind_Accessor',
        'value': 'static_cast<int>(PropertyKind::kAccessor)'
    },
    {
        'name': 'prop_kind_mask',
        'value': 'PropertyDetails::KindField::kMask'
    },
    {
        'name': 'prop_location_Descriptor',
        'value': 'static_cast<int>(PropertyLocation::kDescriptor)'
    },
    {
        'name': 'prop_location_Field',
        'value': 'static_cast<int>(PropertyLocation::kField)'
    },
    {
        'name': 'prop_location_mask',
        'value': 'PropertyDetails::LocationField::kMask'
    },
    {
        'name': 'prop_location_shift',
        'value': 'PropertyDetails::LocationField::kShift'
    },
    {
        'name': 'prop_attributes_NONE',
        'value': 'NONE'
    },
    {
        'name': 'prop_attributes_READ_ONLY',
        'value': 'READ_ONLY'
    },
    {
        'name': 'prop_attributes_DONT_ENUM',
        'value': 'DONT_ENUM'
    },
    {
        'name': 'prop_attributes_DONT_DELETE',
        'value': 'DONT_DELETE'
    },
    {
        'name': 'prop_attributes_mask',
        'value': 'PropertyDetails::AttributesField::kMask'
    },
    {
        'name': 'prop_attributes_shift',
        'value': 'PropertyDetails::AttributesField::kShift'
    },
    {
        'name': 'prop_index_mask',
        'value': 'PropertyDetails::FieldIndexField::kMask'
    },
    {
        'name': 'prop_index_shift',
        'value': 'PropertyDetails::FieldIndexField::kShift'
    },
    {
        'name': 'prop_representation_mask',
        'value': 'PropertyDetails::RepresentationField::kMask'
    },
    {
        'name': 'prop_representation_shift',
        'value': 'PropertyDetails::RepresentationField::kShift'
    },
    {
        'name': 'prop_representation_smi',
        'value': 'Representation::Kind::kSmi'
    },
    {
        'name': 'prop_representation_double',
        'value': 'Representation::Kind::kDouble'
    },
    {
        'name': 'prop_representation_heapobject',
        'value': 'Representation::Kind::kHeapObject'
    },
    {
        'name': 'prop_representation_tagged',
        'value': 'Representation::Kind::kTagged'
    },
    {
        'name': 'prop_desc_key',
        'value': 'DescriptorArray::kEntryKeyIndex'
    },
    {
        'name': 'prop_desc_details',
        'value': 'DescriptorArray::kEntryDetailsIndex'
    },
    {
        'name': 'prop_desc_value',
        'value': 'DescriptorArray::kEntryValueIndex'
    },
    {
        'name': 'prop_desc_size',
        'value': 'DescriptorArray::kEntrySize'
    },
    {
        'name': 'elements_fast_holey_elements',
        'value': 'HOLEY_ELEMENTS'
    },
    {
        'name': 'elements_fast_elements',
        'value': 'PACKED_ELEMENTS'
    },
    {
        'name': 'elements_dictionary_elements',
        'value': 'DICTIONARY_ELEMENTS'
    },
    {
        'name': 'bit_field2_elements_kind_mask',
        'value': 'Map::Bits2::ElementsKindBits::kMask'
    },
    {
        'name': 'bit_field2_elements_kind_shift',
        'value': 'Map::Bits2::ElementsKindBits::kShift'
    },
    {
        'name': 'bit_field3_is_dictionary_map_shift',
        'value': 'Map::Bits3::IsDictionaryMapBit::kShift'
    },
    {
        'name': 'bit_field3_number_of_own_descriptors_mask',
        'value': 'Map::Bits3::NumberOfOwnDescriptorsBits::kMask'
    },
    {
        'name': 'bit_field3_number_of_own_descriptors_shift',
        'value': 'Map::Bits3::NumberOfOwnDescriptorsBits::kShift'
    },
    {
        'name': 'class_Map__instance_descriptors_offset',
        'value': 'Map::kInstanceDescriptorsOffset'
    },
    {
        'name': 'off_fp_context_or_frame_type',
        'value': 'CommonFrameConstants::kContextOrFrameTypeOffset'
    },
    {
        'name': 'off_fp_context',
        'value': 'StandardFrameConstants::kContextOffset'
    },
    {
        'name': 'off_fp_constant_pool',
        'value': 'StandardFrameConstants::kConstantPoolOffset'
    },
    {
        'name': 'off_fp_function',
        'value': 'StandardFrameConstants::kFunctionOffset'
    },
    {
        'name': 'off_fp_args',
        'value': 'StandardFrameConstants::kFixedFrameSizeAboveFp'
    },
    {
        'name': 'off_fp_bytecode_array',
        'value': 'InterpreterFrameConstants::kBytecodeArrayFromFp'
    },
    {
        'name': 'off_fp_bytecode_offset',
        'value': 'InterpreterFrameConstants::kBytecodeOffsetFromFp'
    },
    {
        'name': 'scopeinfo_idx_nparams',
        'value': 'ScopeInfo::kParameterCount'
    },
    {
        'name': 'scopeinfo_idx_ncontextlocals',
        'value': 'ScopeInfo::kContextLocalCount'
    },
    {
        'name': 'scopeinfo_idx_first_vars',
        'value': 'ScopeInfo::kVariablePartIndex'
    },
    {
        'name': 'jsarray_buffer_was_detached_mask',
        'value': 'JSArrayBuffer::WasDetachedBit::kMask'
    },
    {
        'name': 'jsarray_buffer_was_detached_shift',
        'value': 'JSArrayBuffer::WasDetachedBit::kShift'
    },
    {
        'name': 'context_idx_scope_info',
        'value': 'Context::SCOPE_INFO_INDEX'
    },
    {
        'name': 'context_idx_prev',
        'value': 'Context::PREVIOUS_INDEX'
    },
    {
        'name': 'context_min_slots',
        'value': 'Context::MIN_CONTEXT_SLOTS'
    },
    {
        'name': 'native_context_embedder_data_offset',
        'value': 'Internals::kNativeContextEmbedderDataOffset'
    },
    {
        'name': 'namedictionaryshape_prefix_size',
        'value': 'NameDictionaryShape::kPrefixSize'
    },
    {
        'name': 'namedictionaryshape_entry_size',
        'value': 'NameDictionaryShape::kEntrySize'
    },
    {
        'name': 'globaldictionaryshape_entry_size',
        'value': 'GlobalDictionaryShape::kEntrySize'
    },
    {
        'name': 'namedictionary_prefix_start_index',
        'value': 'NameDictionary::kPrefixStartIndex'
    },
    {
        'name': 'numberdictionaryshape_prefix_size',
        'value': 'NumberDictionaryShape::kPrefixSize'
    },
    {
        'name': 'numberdictionaryshape_entry_size',
        'value': 'NumberDictionaryShape::kEntrySize'
    },
    {
        'name': 'simplenumberdictionaryshape_prefix_size',
        'value': 'SimpleNumberDictionaryShape::kPrefixSize'
    },
    {
        'name': 'simplenumberdictionaryshape_entry_size',
        'value': 'SimpleNumberDictionaryShape::kEntrySize'
    },
    {
        'name': 'type_JSError__JS_ERROR_TYPE',
        'value': 'JS_ERROR_TYPE'
    },
]

#
# The following useful fields are missing accessors, so we define fake ones.
# Please note that extra accessors should _only_ be added to expose offsets that
# can be used to access actual V8 objects' properties. They should not be added
# for exposing other values. For instance, enumeration values or class'
# constants should be exposed by adding an entry in the "consts_misc" table, not
# in this "extras_accessors" table.
#
extras_accessors = [
    'JSFunction, code, Tagged<Code>, kCodeOffset',
    'JSFunction, context, Context, kContextOffset',
    'JSFunction, shared, SharedFunctionInfo, kSharedFunctionInfoOffset',
    'HeapObject, map, Map, kMapOffset',
    'JSObject, elements, Object, kElementsOffset',
    'JSObject, internal_fields, uintptr_t, kHeaderSize',
    'FixedArray, data, uintptr_t, kHeaderSize',
    'BytecodeArray, data, uintptr_t, kHeaderSize',
    'JSArrayBuffer, backing_store, uintptr_t, kBackingStoreOffset',
    'JSArrayBuffer, byte_length, size_t, kRawByteLengthOffset',
    'JSArrayBufferView, byte_length, size_t, kRawByteLengthOffset',
    'JSArrayBufferView, byte_offset, size_t, kRawByteOffsetOffset',
    'JSDate, value, Object, kValueOffset',
    'JSRegExp, source, Object, kSourceOffset',
    'JSTypedArray, external_pointer, uintptr_t, kExternalPointerOffset',
    'JSTypedArray, length, Object, kRawLengthOffset',
    'Map, instance_size_in_words, char, kInstanceSizeInWordsOffset',
    'Map, inobject_properties_start_or_constructor_function_index, char, kInobjectPropertiesStartOrConstructorFunctionIndexOffset',
    'Map, instance_type, uint16_t, kInstanceTypeOffset',
    'Map, bit_field, char, kBitFieldOffset',
    'Map, bit_field2, char, kBitField2Offset',
    'Map, bit_field3, int, kBitField3Offset',
    'Map, prototype, Object, kPrototypeOffset',
    'Oddball, kind, int, offsetof(Oddball, kind_)',
    'HeapNumber, value, double, offsetof(HeapNumber, value_)',
    'ExternalString, resource, Object, offsetof(ExternalString, resource_)',
    'SeqOneByteString, chars, char, OFFSET_OF_DATA_START(SeqOneByteString)',
    'SeqTwoByteString, chars, char, OFFSET_OF_DATA_START(SeqTwoByteString)',
    'UncompiledData, inferred_name, String, kInferredNameOffset',
    'UncompiledData, start_position, int32_t, kStartPositionOffset',
    'UncompiledData, end_position, int32_t, kEndPositionOffset',
    'Script, source, Object, kSourceOffset',
    'Script, name, Object, kNameOffset',
    'Script, line_ends, Object, kLineEndsOffset',
    'SharedFunctionInfo, raw_function_token_offset, int16_t, kFunctionTokenOffsetOffset',
    'SharedFunctionInfo, internal_formal_parameter_count, uint16_t, kFormalParameterCountOffset',
    'SharedFunctionInfo, flags, int, kFlagsOffset',
    'SharedFunctionInfo, length, uint16_t, kLengthOffset',
    'SlicedString, parent, String, offsetof(SlicedString, parent_)',
    'Code, flags, uint32_t, kFlagsOffset',
    'Code, instruction_start, Address, kInstructionStartOffset',
    'Code, instruction_stream, Tagged<InstructionStream>, kInstructionStreamOffset',
    'Code, instruction_size, int, kInstructionSizeOffset',
    'InstructionStream, instruction_start, uintptr_t, kHeaderSize',
    'String, length, int32_t, offsetof(String, length_)',
    'DescriptorArray, header_size, uintptr_t, kHeaderSize',
    'ConsString, first, String, offsetof(ConsString, first_)',
    'ConsString, second, String, offsetof(ConsString, second_)',
    'SlicedString, offset, SMI, offsetof(SlicedString, offset_)',
    'ThinString, actual, String, offsetof(ThinString, actual_)',
    'Symbol, name, Object, offsetof(Symbol, description_)',
    'FixedArrayBase, length, SMI, kLengthOffset',
]

#
# The following is a whitelist of classes we expect to find when scanning the
# source code. This list is not exhaustive, but it's still useful to identify
# when this script gets out of sync with the source. See load_objects().
#
expected_classes = [
    'ConsString', 'FixedArray', 'HeapNumber', 'JSArray', 'JSFunction',
    'JSObject', 'JSRegExp', 'JSPrimitiveWrapper', 'Map', 'Oddball', 'Script',
    'SeqOneByteString', 'SharedFunctionInfo', 'ScopeInfo', 'JSPromise',
    'DescriptorArray'
];


#
# The following structures store high-level representations of the structures
# for which we're going to emit descriptive constants.
#
types = {}  # set of all type names
typeclasses = {}  # maps type names to corresponding class names
klasses = {}  # known classes, including parents
fields = []  # field declarations
offsetof_fields = []  # field declarations using offsetof

header = '''
/*
 * This file is generated by %s.  Do not edit directly.
 */

#include "src/init/v8.h"
#include "src/codegen/register.h"
#include "src/execution/frames.h"
#include "src/execution/frames-inl.h" /* for architecture-specific frame constants */
#include "src/objects/contexts.h"
#include "src/objects/objects.h"
#include "src/objects/data-handler.h"
#include "src/objects/js-promise.h"
#include "src/objects/js-regexp-string-iterator.h"
#include "src/objects/megadom-handler.h"

namespace v8 {
namespace internal {

extern "C" {

/* stack frame constants */
#define FRAME_CONST(value, klass)       \
    V8_EXPORT int v8dbg_frametype_##klass = StackFrame::value;

STACK_FRAME_TYPE_LIST(FRAME_CONST)

#undef FRAME_CONST

''' % sys.argv[0]

footer = '''
}

}
}
'''

#
# Get the base class
#
def get_base_class(klass):
  if (klass == 'Object'):
    return klass;

  if (not (klass in klasses)):
    return None;

  k = klasses[klass];

  return get_base_class(k['parent']);

#
# Loads class hierarchy and type information from "objects.h" etc.
#
def load_objects():
  #
  # Construct a dictionary for the classes we're sure should be present.
  #
  checktypes = {};
  for klass in expected_classes:
    checktypes[klass] = True;


  for filename in sys.argv[2:]:
    if not filename.endswith("-inl.h"):
      load_objects_from_file(filename, checktypes)

  if (len(checktypes) > 0):
    for klass in checktypes:
      print('error: expected class \"%s\" not found' % klass);

    sys.exit(1);


def load_objects_from_file(objfilename, checktypes):
  objfile = io.open(objfilename, 'r', encoding='utf-8');
  in_insttype = False;
  in_torque_insttype = False
  in_torque_fulldef = False

  typestr = '';
  torque_typestr = ''
  torque_fulldefstr = ''
  uncommented_file = ''

  #
  # Iterate the header file line-by-line to collect type and class
  # information. For types, we accumulate a string representing the entire
  # InstanceType enum definition and parse it later because it's easier to
  # do so without the embedded newlines.
  #
  for line in objfile:
    if (line.startswith('enum InstanceType : uint16_t {')):
      in_insttype = True;
      continue;

    if (line.startswith('#define TORQUE_ASSIGNED_INSTANCE_TYPE_LIST')):
      in_torque_insttype = True
      continue

    if (line.startswith('#define TORQUE_INSTANCE_CHECKERS_SINGLE_FULLY_DEFINED')):
      in_torque_fulldef = True
      continue

    if (in_insttype and line.startswith('};')):
      in_insttype = False;
      continue;

    if (in_torque_insttype and (not line or line.isspace())):
      in_torque_insttype = False
      continue

    if (in_torque_fulldef and (not line or line.isspace())):
      in_torque_fulldef = False
      continue

    pre = line.strip()
    line = re.sub('// .*', '', line.strip());

    if (in_insttype):
      typestr += line;
      continue;

    if (in_torque_insttype):
      torque_typestr += line
      continue

    if (in_torque_fulldef):
      torque_fulldefstr += line
      continue

    uncommented_file += '\n' + line

  for match in re.finditer(
      r'\n(?:V8_OBJECT\s+)?class(?:\s+V8_EXPORT(?:_PRIVATE)?)?'
      r'\s+(\w[^:;]*)'
      r'(?:: public (\w[^{]*))?\s*{\s*', uncommented_file):
    klass = match.group(1).strip();
    pklass = match.group(2);
    if (pklass):
      # Check for generated Torque class.
      gen_match = re.match(
          r'TorqueGenerated\w+\s*<\s*\w+,\s*(\w+)\s*>',
          pklass)
      if (gen_match):
        pklass = gen_match.group(1)
      # Strip potential template arguments from parent
      # class.
      match = re.match(r'(\w+)(<.*>)?', pklass.strip());
      pklass = match.group(1).strip();
    klasses[klass] = { 'parent': pklass };

  #
  # Process the instance type declaration.
  #
  entries = typestr.split(',');
  for entry in entries:
    types[re.sub(r'\s*=.*', '', entry).lstrip()] = True
  entries = torque_typestr.split('\\')
  for entry in entries:
    name = re.sub(r' *V\(|\).*', '', entry)
    types[name] = True
  entries = torque_fulldefstr.split('\\')
  for entry in entries:
    entry = entry.strip()
    if not entry:
      continue
    start = entry.find('(');
    end = entry.find(')', start);
    rest = entry[start + 1: end];
    args = re.split(r'\s*,\s*', rest)
    typename = args[0]
    typeconst = args[1]
    types[typeconst] = True
    typeclasses[typeconst] = typename
  #
  # Infer class names for each type based on a systematic transformation.
  # For example, "JS_FUNCTION_TYPE" becomes "JSFunction".  We find the
  # class for each type rather than the other way around because there are
  # fewer cases where one type maps to more than one class than the other
  # way around.
  #
  for type in types:
    usetype = type

    #
    # Remove the "_TYPE" suffix and then convert to camel case,
    # except that a "JS" prefix remains uppercase (as in
    # "JS_FUNCTION_TYPE" => "JSFunction").
    #
    if (not usetype.endswith('_TYPE')):
      continue;

    usetype = usetype[0:len(usetype) - len('_TYPE')];
    parts = usetype.split('_');
    cctype = '';

    if (parts[0] == 'JS'):
      cctype = 'JS';
      start = 1;
    else:
      cctype = '';
      start = 0;

    for ii in range(start, len(parts)):
      part = parts[ii];
      cctype += part[0].upper() + part[1:].lower();

    #
    # Mapping string types is more complicated.  Both types and
    # class names for Strings specify a representation (e.g., Seq,
    # Cons, External, or Sliced) and an encoding (TwoByte/OneByte),
    # In the simplest case, both of these are explicit in both
    # names, as in:
    #
    #       EXTERNAL_ONE_BYTE_STRING_TYPE => ExternalOneByteString
    #
    # However, sometimes the type name has more information
    # than the class, as in:
    #
    #       CONS_ONE_BYTE_STRING_TYPE => ConsString
    #
    # To figure this out dynamically, we first check for a
    # representation and encoding.
    # If that doesn't yield a valid class name, we strip out the
    # representation.
    #
    if (cctype.endswith('String')):
      if (not (cctype in klasses)):
        cctype = re.sub('OneByte', '', cctype);
        cctype = re.sub('TwoByte', '', cctype);

    #
    # Despite all that, some types have no corresponding class.
    #
    if (cctype in klasses):
      typeclasses[type] = cctype;
      if (cctype in checktypes):
        del checktypes[cctype];

#
# For a given macro call, pick apart the arguments and return an object
# describing the corresponding output constant.  See load_fields().
#
def parse_field(call):
  # Replace newlines with spaces.
  for ii in range(0, len(call)):
    if (call[ii] == '\n'):
      call[ii] == ' ';

  idx = call.find('(');
  kind = call[0:idx];
  rest = call[idx + 1: len(call) - 1];
  args = re.findall(r'[^\s,][^(),]*(?:\([^()]*\))?(?=\s*(?:,|$))', rest)

  klass = args[0];
  field = args[1];
  dtype = None
  offset = None
  if kind.startswith('WEAK_ACCESSORS'):
    dtype = 'weak'
    offset = args[2];
  elif not (kind.startswith('SMI_ACCESSORS') or kind.startswith('ACCESSORS_TO_SMI')):
    dtype = args[2].replace('<', '_').replace('>', '_')
    offset = args[3];
  else:
    offset = args[2];
    dtype = 'SMI'

  if offset.startswith("offsetof(") or offset.startswith(
      "OFFSET_OF_DATA_START("):
    offsetof_fields.append((klass, field, offset))
    value = 'OffsetsForDebug::%s_%s' % (klass, field)
  else:
    value = '%s::%s' % (klass, offset)

  assert(offset is not None and dtype is not None);
  return ({'name': 'class_%s__%s__%s' % (klass, field, dtype), 'value': value})

#
# Load field offset information from objects-inl.h etc.
#
def load_fields():
  for filename in sys.argv[2:]:
    if filename.endswith("-inl.h"):
      load_fields_from_file(filename)

  for body in extras_accessors:
    fields.append(parse_field('ACCESSORS(%s)' % body));


def load_fields_from_file(filename):
  inlfile = io.open(filename, 'r', encoding='utf-8');

  #
  # Each class's fields and the corresponding offsets are described in the
  # source by calls to macros like "ACCESSORS" (and friends).  All we do
  # here is extract these macro invocations, taking into account that they
  # may span multiple lines and may contain nested parentheses.  We also
  # call parse_field() to pick apart the invocation.
  #
  prefixes = [ 'ACCESSORS', 'ACCESSORS2', 'ACCESSORS_GCSAFE',
         'SMI_ACCESSORS', 'ACCESSORS_TO_SMI',
         'RELEASE_ACQUIRE_ACCESSORS', 'WEAK_ACCESSORS' ];
  prefixes += ([ prefix + "_CHECKED" for prefix in prefixes ] +
         [ prefix + "_CHECKED2" for prefix in prefixes ])
  current = '';
  opens = 0;

  for line in inlfile:
    if (opens > 0):
      # Continuation line
      for ii in range(0, len(line)):
        if (line[ii] == '('):
          opens += 1;
        elif (line[ii] == ')'):
          opens -= 1;

        if (opens == 0):
          break;

      current += line[0:ii + 1];
      continue;

    for prefix in prefixes:
      if (not line.startswith(prefix + '(')):
        continue;

      if (len(current) > 0):
        fields.append(parse_field(current));
        current = '';

      for ii in range(len(prefix), len(line)):
        if (line[ii] == '('):
          opens += 1;
        elif (line[ii] == ')'):
          opens -= 1;

        if (opens == 0):
          break;

      current += line[0:ii + 1];

  if (len(current) > 0):
    fields.append(parse_field(current));
    current = '';

#
# Emit a block of constants.
#
def emit_constants(out, consts):
  lines = []

  # Fix up overzealous parses.  This could be done inside the
  # parsers but as there are several, it's easiest to do it here.
  ws = re.compile(r'\s+')
  for const in consts:
    name = ws.sub('', const['name'])
    value = ws.sub('', str(const['value']))  # Can be a number.
    lines.append('V8_EXPORT int v8dbg_%s = %s;' % (name, value))

  # Generate without duplicates and with preserved order.
  out.write('\n'.join(dict.fromkeys(lines)))
  out.write('\n');

#
# Emit the whole output file.
#
def emit_config():
  out = open(sys.argv[1], 'w');

  out.write(header);

  out.write("struct OffsetsForDebug {\n")
  for (klass, field, offset) in offsetof_fields:
    out.write("  static const int %s_%s = %s;\n" % (klass, field, offset))
  out.write("};\n")

  out.write('/* miscellaneous constants */\n');
  emit_constants(out, consts_misc);

  out.write('/* class type information */\n');
  consts = [];
  for typename in sorted(typeclasses):
    klass = typeclasses[typename];
    consts.append({
        'name': 'type_%s__%s' % (klass, typename),
        'value': typename
    });

  emit_constants(out, consts);

  out.write('/* class hierarchy information */\n');
  consts = [];
  for klassname in sorted(klasses):
    pklass = klasses[klassname]['parent'];
    if (klassname == pklass):
      continue
    bklass = get_base_class(klassname);
    if (bklass != 'Object'):
      continue;
    if (pklass is None):
      continue;

    consts.append({
        'name': 'parent_%s__%s' % (klassname, pklass),
        'value': 0
    });

  emit_constants(out, consts);

  out.write('/* field information */\n');
  emit_constants(out, fields);

  out.write(footer);

if (len(sys.argv) < 4):
  print('usage: %s output.cc objects.h objects-inl.h' % sys.argv[0]);
  sys.exit(2);

load_objects();
load_fields();
emit_config();
