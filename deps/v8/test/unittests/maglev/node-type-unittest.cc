// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_MAGLEV

#include "src/maglev/maglev-ir.h"
#include "test/unittests/maglev/maglev-test.h"

namespace v8 {
namespace internal {
namespace maglev {

static std::initializer_list<NodeType> kAllNodeTypes{
#define TYPE(Name, _) NodeType::k##Name,
    NODE_TYPE_LIST(TYPE)
#undef TYPE
};

inline constexpr bool IsKnownNodeTypeConstant(NodeType type) {
  switch (type) {
#define CASE(Name, _) case NodeType::k##Name:
    NODE_TYPE_LIST(CASE)
#undef CASE
    return true;
  }
  return false;
}

// Ensures NodeTypes are closed under intersection modulo heap object bit.
TEST_F(MaglevTest, NodeTypeIsClosedUnderIntersect) {
  for (NodeType a : kAllNodeTypes) {
    CHECK(IsKnownNodeTypeConstant(a));
    for (NodeType b : kAllNodeTypes) {
      NodeType combined = IntersectType(a, b);
      // Somtimes the intersection keeps more information, e.g.
      // Intersect(HeapNumber,Boolean) is a NumberOrOddball|HeapObject
      NodeType combined_not_obj =
          NodeType(static_cast<int>(combined) &
                   ~static_cast<int>(NodeType::kAnyHeapObject));
      CHECK(IsKnownNodeTypeConstant(combined) ||
            IsKnownNodeTypeConstant(combined_not_obj));
    }
  }
}

// Ensure StaticTypeForMap is consistent with actual maps.
TEST_F(MaglevTest, NodeTypeApproximationIsConsistent) {
  for (auto idx = RootIndex::kFirstRoot; idx <= RootIndex::kLastRoot; ++idx) {
    Tagged<Object> obj = isolate()->roots_table().slot(idx).load(isolate());
    if (obj.ptr() == kNullAddress || !IsMap(obj)) continue;
    Tagged<Map> map = Map::cast(obj);
    compiler::MapRef map_ref = MakeRef(broker(), map);

    for (NodeType a : kAllNodeTypes) {
      bool isInstance = IsInstanceOfNodeType(map_ref, a, broker());
      bool isSubtype = NodeTypeIs(StaticTypeForMap(map_ref), a);
      CHECK_IMPLIES(isSubtype, isInstance);
      CHECK_IMPLIES(!isInstance, !isSubtype);
    }
  }
}

// Ensure CombineType is consistent with actual maps.
TEST_F(MaglevTest, NodeTypeCombineIsConsistent) {
  for (auto idx = RootIndex::kFirstRoot; idx <= RootIndex::kLastRoot; ++idx) {
    Tagged<Object> obj = isolate()->roots_table().slot(idx).load(isolate());
    if (obj.ptr() == kNullAddress || !IsMap(obj)) continue;
    Tagged<Map> map = Map::cast(obj);
    compiler::MapRef map_ref = MakeRef(broker(), map);

    for (NodeType a : kAllNodeTypes) {
      for (NodeType b : kAllNodeTypes) {
        NodeType combined_type = CombineType(a, b);

        bool mapIsA = IsInstanceOfNodeType(map_ref, a, broker());
        bool mapIsB = IsInstanceOfNodeType(map_ref, b, broker());
        CHECK_EQ(IsInstanceOfNodeType(map_ref, combined_type, broker()),
                 mapIsA && mapIsB);
      }
    }
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV
