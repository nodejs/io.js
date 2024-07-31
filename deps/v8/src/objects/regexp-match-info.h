// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_REGEXP_MATCH_INFO_H_
#define V8_OBJECTS_REGEXP_MATCH_INFO_H_

#include "src/base/compiler-specific.h"
#include "src/objects/fixed-array.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Object;
class String;

// TODO(jgruber): These should no longer be included here; instead, all
// TorqueGeneratedFooAsserts should be emitted into a global .cc file.
#include "torque-generated/src/objects/regexp-match-info-tq.inc"

class RegExpMatchInfoShape final : public AllStatic {
 public:
  static constexpr int kElementSize = kTaggedSize;
  using ElementT = Smi;
  using CompressionScheme = SmiCompressionScheme;
  static constexpr RootIndex kMapRootIndex = RootIndex::kRegExpMatchInfoMap;
  static constexpr bool kLengthEqualsCapacity = true;

#define FIELD_LIST(V)                                                   \
  V(kCapacityOffset, kTaggedSize)                                       \
  V(kNumberOfCaptureRegistersOffset, kTaggedSize)                       \
  V(kLastSubjectOffset, kTaggedSize)                                    \
  V(kLastInputOffset, kTaggedSize)                                      \
  V(kUnalignedHeaderSize, OBJECT_POINTER_PADDING(kUnalignedHeaderSize)) \
  V(kHeaderSize, 0)                                                     \
  V(kCapturesOffset, 0)  // captures[capacity]

  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST
};

// The property RegExpMatchInfo includes the matchIndices array of the last
// successful regexp match (an array of start/end index pairs for the match and
// all the captured substrings), the invariant is that there are at least two
// capture indices.  The array also contains the subject string for the last
// successful match.
class RegExpMatchInfo
    : public TaggedArrayBase<RegExpMatchInfo, RegExpMatchInfoShape> {
  using Super = TaggedArrayBase<RegExpMatchInfo, RegExpMatchInfoShape>;
  OBJECT_CONSTRUCTORS(RegExpMatchInfo, Super);

 public:
  using Shape = RegExpMatchInfoShape;

  V8_EXPORT_PRIVATE static Handle<RegExpMatchInfo> New(
      Isolate* isolate, int capture_count,
      AllocationType allocation = AllocationType::kYoung);

  static Handle<RegExpMatchInfo> ReserveCaptures(
      Isolate* isolate, Handle<RegExpMatchInfo> match_info, int capture_count);

  // Returns the number of captures, which is defined as the length of the
  // matchIndices objects of the last match. matchIndices contains two indices
  // for each capture (including the match itself), i.e. 2 * #captures + 2.
  inline int number_of_capture_registers() const;
  inline void set_number_of_capture_registers(int value);

  // Returns the subject string of the last match.
  inline Tagged<String> last_subject() const;
  inline void set_last_subject(Tagged<String> value,
                               WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Like |last_subject|, but modifiable by the user.
  inline Tagged<Object> last_input() const;
  inline void set_last_input(Tagged<Object> value,
                             WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline int capture(int index) const;
  inline void set_capture(int index, int value);

  static constexpr int capture_start_index(int capture_index) {
    return capture_index * 2;
  }
  static constexpr int capture_end_index(int capture_index) {
    return capture_index * 2 + 1;
  }

  static constexpr int kMinCapacity = 2;

  // Redeclare these here since they are used from generated code.
  static constexpr int kLengthOffset = Shape::kCapacityOffset;
  static constexpr int kLastInputOffset = Shape::kLastInputOffset;
  static constexpr int kLastSubjectOffset = Shape::kLastSubjectOffset;
  static constexpr int kNumberOfCaptureRegistersOffset =
      Shape::kNumberOfCaptureRegistersOffset;

  DECL_CAST(RegExpMatchInfo)
  DECL_PRINTER(RegExpMatchInfo)
  DECL_VERIFIER(RegExpMatchInfo)

  class BodyDescriptor;
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_REGEXP_MATCH_INFO_H_
