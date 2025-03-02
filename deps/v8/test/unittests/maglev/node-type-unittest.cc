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

template <typename Function>
inline std::unordered_set<NodeType> CompleteLattice(Function Combine) {
  std::unordered_set completed_lattice(kAllNodeTypes);
  bool complete = false;
  while (!complete) {
    complete = true;
    std::unordered_set<NodeType> discovered;
    for (NodeType a : completed_lattice) {
      for (NodeType b : completed_lattice) {
        NodeType combined = Combine(a, b);
        if (!completed_lattice.count(combined)) {
          discovered.insert(combined);
        }
      }
    }
    if (!discovered.empty()) {
      completed_lattice.insert(discovered.begin(), discovered.end());
      complete = false;
    }
  }
  return completed_lattice;
}

static std::unordered_set<NodeType> kJoinLattice =
    CompleteLattice([](NodeType a, NodeType b) { return IntersectType(a, b); });
static std::unordered_set<NodeType> kMeetLattice =
    CompleteLattice([](NodeType a, NodeType b) { return CombineType(a, b); });
static std::unordered_set<NodeType> kCompleteLattice = []() {
  std::unordered_set completed_lattice(kJoinLattice.begin(),
                                       kJoinLattice.end());
  completed_lattice.insert(kMeetLattice.begin(), kMeetLattice.end());
  return completed_lattice;
}();

// Allow-list of joins we have consciously omitted from the join lattice since
// they are not needed often. Checking these types can be more expensive in
// some cases.
std::unordered_set<NodeType> kMissingEntries{
    // HeapNumberOrOddball
    CombineType(NodeType::kNumberOrOddball, NodeType::kAnyHeapObject),
    // BooleanOrHeapNumber
    CombineType(NodeType::kNumberOrBoolean, NodeType::kAnyHeapObject),
};

// The missing node types must be inhabited.
TEST_F(MaglevTest, NodeTypeMissingEntriesExist) {
  for (NodeType missing : kMissingEntries) {
    CHECK(!NodeTypeCannotHaveInstances(missing));
  }
}

// Ensures important NodeTypes are closed under intersection.
TEST_F(MaglevTest, NodeTypeIsClosedUnderIntersect) {
  for (NodeType join : kJoinLattice) {
    CHECK(NodeTypeIs(join, NodeType::kUnknown));
    CHECK_EQ(!kMissingEntries.count(join), IsKnownNodeTypeConstant(join));
  }
}

// Every join of node types must be inhabited.
TEST_F(MaglevTest, NodeTypeIntersectsAreInhabited) {
  for (NodeType t : kCompleteLattice) {
    CHECK_IMPLIES(kJoinLattice.count(t) || kMissingEntries.count(t),
                  !NodeTypeCannotHaveInstances(t));
    CHECK_IMPLIES(NodeTypeCannotHaveInstances(t),
                  !kJoinLattice.count(t) && !kMissingEntries.count(t));
  }
}

// Check that the uninhabitated check behaves.
TEST_F(MaglevTest, NodeTypeCheckIsUninhabited) {
  for (NodeType t : kCompleteLattice) {
    CHECK_IMPLIES(NodeTypeCannotHaveInstances(t),
                  !IsKnownNodeTypeConstant(t) && !kMissingEntries.count(t));
    CHECK_IMPLIES(IsKnownNodeTypeConstant(t) || kMissingEntries.count(t),
                  !NodeTypeCannotHaveInstances(t));
  }
}

// Check impossible pairs are actually impossible.
TEST_F(MaglevTest, NodeTypeCheckLeafTypes) {
  for (auto pair : kNodeTypeExclusivePairs) {
    for (NodeType t : kJoinLattice) {
      CHECK(!NodeTypeIs(t, pair.first) || !NodeTypeIs(t, pair.second));
    }
  }
}

// Ensure StaticTypeForConstant is consistent with actual objects.
TEST_F(MaglevTest, ConstantNodeTypeApproximationIsConsistent) {
  for (auto idx = RootIndex::kFirstRoot; idx <= RootIndex::kLastRoot; ++idx) {
    Tagged<Object> obj = isolate()->roots_table().slot(idx).load(isolate());
    if (obj.ptr() == kNullAddress || !obj.IsHeapObject()) continue;
    compiler::HeapObjectRef ref = MakeRef(broker(), Cast<HeapObject>(obj));
    NodeType t = StaticTypeForConstant(broker(), ref);
    CHECK(!NodeTypeCannotHaveInstances(t));
    for (NodeType a : kCompleteLattice) {
      bool is_instance = IsInstanceOfNodeType(ref.map(broker()), a, broker());
      bool is_subtype = NodeTypeIs(t, a);
      CHECK_IMPLIES(is_subtype, is_instance);
      CHECK_IMPLIES(!is_instance, !is_subtype);
    }
  }
}

// Ensure StaticTypeForMap is consistent with actual maps.
TEST_F(MaglevTest, NodeTypeApproximationIsConsistent) {
  for (auto idx = RootIndex::kFirstRoot; idx <= RootIndex::kLastRoot; ++idx) {
    Tagged<Object> obj = isolate()->roots_table().slot(idx).load(isolate());
    if (obj.ptr() == kNullAddress || !IsMap(obj)) continue;
    Tagged<Map> map = Cast<Map>(obj);
    compiler::MapRef map_ref = MakeRef(broker(), map);

    for (NodeType a : kCompleteLattice) {
      bool is_instance = IsInstanceOfNodeType(map_ref, a, broker());
      bool is_subtype = NodeTypeIs(StaticTypeForMap(map_ref, broker()), a);
      CHECK_IMPLIES(is_subtype, is_instance);
      CHECK_IMPLIES(!is_instance, !is_subtype);
    }
  }
}

// Ensure CombineType is consistent with actual maps.
TEST_F(MaglevTest, NodeTypeCombineIsConsistent) {
  for (auto idx = RootIndex::kFirstRoot; idx <= RootIndex::kLastRoot; ++idx) {
    Tagged<Object> obj = isolate()->roots_table().slot(idx).load(isolate());
    if (obj.ptr() == kNullAddress || !IsMap(obj)) continue;
    Tagged<Map> map = Cast<Map>(obj);
    compiler::MapRef map_ref = MakeRef(broker(), map);

    for (NodeType a : kCompleteLattice) {
      for (NodeType b : kJoinLattice) {
        NodeType combined_type = CombineType(a, b);

        bool map_is_a = IsInstanceOfNodeType(map_ref, a, broker());
        bool map_is_b = IsInstanceOfNodeType(map_ref, b, broker());
        bool is_instance =
            IsInstanceOfNodeType(map_ref, combined_type, broker());
        CHECK_EQ(is_instance, map_is_a && map_is_b);
        DCHECK_IMPLIES(NodeTypeCannotHaveInstances(combined_type),
                       !is_instance);
        DCHECK_IMPLIES(is_instance,
                       !NodeTypeCannotHaveInstances(combined_type));
      }
    }
  }
}

// Ensure IntersectType is consistent with actual maps.
TEST_F(MaglevTest, NodeTypeIntersectIsConsistent) {
  for (auto idx = RootIndex::kFirstRoot; idx <= RootIndex::kLastRoot; ++idx) {
    Tagged<Object> obj = isolate()->roots_table().slot(idx).load(isolate());
    if (obj.ptr() == kNullAddress || !IsMap(obj)) continue;
    Tagged<Map> map = Cast<Map>(obj);
    compiler::MapRef map_ref = MakeRef(broker(), map);

    for (NodeType a : kCompleteLattice) {
      for (NodeType b : kMeetLattice) {
        NodeType join_type = IntersectType(a, b);
        bool map_is_a = IsInstanceOfNodeType(map_ref, a, broker());
        bool map_is_b = IsInstanceOfNodeType(map_ref, b, broker());
        CHECK_IMPLIES(map_is_a || map_is_b,
                      IsInstanceOfNodeType(map_ref, join_type, broker()));
        CHECK_IMPLIES(!IsInstanceOfNodeType(map_ref, join_type, broker()),
                      !(map_is_a && map_is_b));
      }
    }
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV
