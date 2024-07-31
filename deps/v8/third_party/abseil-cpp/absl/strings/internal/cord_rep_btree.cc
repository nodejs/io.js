// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/internal/cord_rep_btree.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/strings/internal/cord_data_edge.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_consume.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr size_t CordRepBtree::kMaxCapacity;
#endif

namespace {

using NodeStack = CordRepBtree * [CordRepBtree::kMaxDepth];
using EdgeType = CordRepBtree::EdgeType;
using OpResult = CordRepBtree::OpResult;
using CopyResult = CordRepBtree::CopyResult;

constexpr auto kFront = CordRepBtree::kFront;
constexpr auto kBack = CordRepBtree::kBack;

ABSL_CONST_INIT std::atomic<bool> cord_btree_exhaustive_validation(false);

// Implementation of the various 'Dump' functions.
// Prints the entire tree structure or 'rep'. External callers should
// not specify 'depth' and leave it to its default (0) value.
// Rep may be a CordRepBtree tree, or a SUBSTRING / EXTERNAL / FLAT node.
void DumpAll(const CordRep* rep,
             bool include_contents,
             std::ostream& stream,
             size_t depth = 0) {
  // Allow for full height trees + substring -> flat / external nodes.
  assert(depth <= CordRepBtree::kMaxDepth + 2);
  std::string sharing = const_cast<CordRep*>(rep)->refcount.IsOne()
                            ? std::string("Private")
                            : absl::StrCat("Shared(", rep->refcount.Get(), ")");
  std::string sptr = absl::StrCat("0x", absl::Hex(rep));

  // Dumps the data contents of `rep` if `include_contents` is true.
  // Always emits a new line character.
  auto maybe_dump_data = [&stream, include_contents](const CordRep* r) {
    if (include_contents) {
      // Allow for up to 60 wide display of content data, which with some
      // indentation and prefix / labels keeps us within roughly 80-100 wide.
      constexpr size_t kMaxDataLength = 60;
      stream << ", data = \""
             << EdgeData(r).substr(0, kMaxDataLength)
             << (r->length > kMaxDataLength ? "\"..." : "\"");
    }
    stream << '\n';
  };

  // For each level, we print the 'shared/private' state and the rep pointer,
  // indented by two spaces per recursive depth.
  stream << std::string(depth * 2, ' ') << sharing << " (" << sptr << ") ";

  if (rep->IsBtree()) {
    const CordRepBtree* node = rep->btree();
    std::string label =
        node->height() ? absl::StrCat("Node(", node->height(), ")") : "Leaf";
    stream << label << ", len = " << node->length
           << ", begin = " << node->begin() << ", end = " << node->end()
           << "\n";
    for (CordRep* edge : node->Edges()) {
      DumpAll(edge, include_contents, stream, depth + 1);
    }
  } else if (rep->tag == SUBSTRING) {
    const CordRepSubstring* substring = rep->substring();
    stream << "Substring, len = " << rep->length
           << ", start = " << substring->start;
    maybe_dump_data(rep);
    DumpAll(substring->child, include_contents, stream, depth + 1);
  } else if (rep->tag >= FLAT) {
    stream << "Flat, len = " << rep->length
           << ", cap = " << rep->flat()->Capacity();
    maybe_dump_data(rep);
  } else if (rep->tag == EXTERNAL) {
    stream << "Extn, len = " << rep->length;
    maybe_dump_data(rep);
  }
}

// TODO(b/192061034): add 'bytes to copy' logic to avoid large slop on substring
// small data out of large reps, and general efficiency of 'always copy small
// data'. Consider making this a cord rep internal library function.
CordRepSubstring* CreateSubstring(CordRep* rep, size_t offset, size_t n) {
  assert(n != 0);
  assert(offset + n <= rep->length);
  assert(offset != 0 || n != rep->length);

  if (rep->tag == SUBSTRING) {
    CordRepSubstring* substring = rep->substring();
    offset += substring->start;
    rep = CordRep::Ref(substring->child);
    CordRep::Unref(substring);
  }
  assert(rep->IsExternal() || rep->IsFlat());
  CordRepSubstring* substring = new CordRepSubstring();
  substring->length = n;
  substring->tag = SUBSTRING;
  substring->start = offset;
  substring->child = rep;
  return substring;
}

// TODO(b/192061034): consider making this a cord rep library function.
inline CordRep* MakeSubstring(CordRep* rep, size_t offset, size_t n) {
  if (n == rep->length) return rep;
  if (n == 0) return CordRep::Unref(rep), nullptr;
  return CreateSubstring(rep, offset, n);
}

// TODO(b/192061034): consider making this a cord rep library function.
inline CordRep* MakeSubstring(CordRep* rep, size_t offset) {
  if (offset == 0) return rep;
  return CreateSubstring(rep, offset, rep->length - offset);
}

// Resizes `edge` to the provided `length`. Adopts a reference on `edge`.
// This method directly returns `edge` if `length` equals `edge->length`.
// If `is_mutable` is set to true, this function may return `edge` with
// `edge->length` set to the new length depending on the type and size of
// `edge`. Otherwise, this function returns a new CordRepSubstring value.
// Requires `length > 0 && length <= edge->length`.
CordRep* ResizeEdge(CordRep* edge, size_t length, bool is_mutable) {
  assert(length > 0);
  assert(length <= edge->length);
  assert(IsDataEdge(edge));
  if (length >= edge->length) return edge;

  if (is_mutable && (edge->tag >= FLAT || edge->tag == SUBSTRING)) {
    edge->length = length;
    return edge;
  }

  return CreateSubstring(edge, 0, length);
}

template <EdgeType edge_type>
inline absl::string_view Consume(absl::string_view s, size_t n) {
  return edge_type == kBack ? s.substr(n) : s.substr(0, s.size() - n);
}

template <EdgeType edge_type>
inline absl::string_view Consume(char* dst, absl::string_view s, size_t n) {
  if (edge_type == kBack) {
    memcpy(dst, s.data(), n);
    return s.substr(n);
  } else {
    const size_t offset = s.size() - n;
    memcpy(dst, s.data() + offset, n);
    return s.substr(0, offset);
  }
}

// Known issue / optimization weirdness: the store associated with the
// decrement introduces traffic between cpus (even if the result of that
// traffic does nothing), making this faster than a single call to
// refcount.Decrement() checking the zero refcount condition.
template <typename R, typename Fn>
inline void FastUnref(R* r, Fn&& fn) {
  if (r->refcount.IsOne()) {
    fn(r);
  } else if (!r->refcount.DecrementExpectHighRefcount()) {
    fn(r);
  }
}


void DeleteSubstring(CordRepSubstring* substring) {
  CordRep* rep = substring->child;
  if (!rep->refcount.Decrement()) {
    if (rep->tag >= FLAT) {
      CordRepFlat::Delete(rep->flat());
    } else {
      assert(rep->tag == EXTERNAL);
      CordRepExternal::Delete(rep->external());
    }
  }
  delete substring;
}

// Deletes a leaf node data edge. Requires `IsDataEdge(rep)`.
void DeleteLeafEdge(CordRep* rep) {
  assert(IsDataEdge(rep));
  if (rep->tag >= FLAT) {
    CordRepFlat::Delete(rep->flat());
  } else if (rep->tag == EXTERNAL) {
    CordRepExternal::Delete(rep->external());
  } else {
    DeleteSubstring(rep->substring());
  }
}

// StackOperations contains the logic to build a left-most or right-most stack
// (leg) down to the leaf level of a btree, and 'unwind' / 'Finalize' methods to
// propagate node changes up the stack.
template <EdgeType edge_type>
struct StackOperations {
  // Returns true if the node at 'depth' is not shared, i.e. has a refcount
  // of one and all of its parent nodes have a refcount of one.
  inline bool owned(int depth) const { return depth < share_depth; }

  // Returns the node at 'depth'.
  inline CordRepBtree* node(int depth) const { return stack[depth]; }

  // Builds a `depth` levels deep stack starting at `tree` recording which nodes
  // are private in the form of the 'share depth' where nodes are shared.
  inline CordRepBtree* BuildStack(CordRepBtree* tree, int depth) {
    assert(depth <= tree->height());
    int current_depth = 0;
    while (current_depth < depth && tree->refcount.IsOne()) {
      stack[current_depth++] = tree;
      tree = tree->Edge(edge_type)->btree();
    }
    share_depth = current_depth + (tree->refcount.IsOne() ? 1 : 0);
    while (current_depth < depth) {
      stack[current_depth++] = tree;
      tree = tree->Edge(edge_type)->btree();
    }
    return tree;
  }

  // Builds a stack with the invariant that all nodes are private owned / not
  // shared. This is used in iterative updates where a previous propagation
  // guaranteed all nodes are owned / private.
  inline void BuildOwnedStack(CordRepBtree* tree, int height) {
    assert(height <= CordRepBtree::kMaxHeight);
    int depth = 0;
    while (depth < height) {
      assert(tree->refcount.IsOne());
      stack[depth++] = tree;
      tree = tree->Edge(edge_type)->btree();
    }
    assert(tree->refcount.IsOne());
    share_depth = depth + 1;
  }

  // Processes the final 'top level' result action for the tree.
  // See the 'Action' enum for the various action implications.
  static inline CordRepBtree* Finalize(CordRepBtree* tree, OpResult result) {
    switch (result.action) {
      case CordRepBtree::kPopped:
        tree = edge_type == kBack ? CordRepBtree::New(tree, result.tree)
                                  : CordRepBtree::New(result.tree, tree);
        if (ABSL_PREDICT_FALSE(tree->height() > CordRepBtree::kMaxHeight)) {
          tree = CordRepBtree::Rebuild(tree);
          ABSL_RAW_CHECK(tree->height() <= CordRepBtree::kMaxHeight,
                         "Max height exceeded");
        }
        return tree;
      case CordRepBtree::kCopied:
        CordRep::Unref(tree);
        ABSL_FALLTHROUGH_INTENDED;
      case CordRepBtree::kSelf:
        return result.tree;
    }
    ABSL_UNREACHABLE();
    return result.tree;
  }

  // Propagate the action result in 'result' up into all nodes of the stack
  // starting at depth 'depth'. 'length' contains the extra length of data that
  // was added at the lowest level, and is updated into all nodes of the stack.
  // See the 'Action' enum for the various action implications.
  // If 'propagate' is true, then any copied node values are updated into the
  // stack, which is used for iterative processing on the same stack.
  template <bool propagate = false>
  inline CordRepBtree* Unwind(CordRepBtree* tree, int depth, size_t length,
                              OpResult result) {
    // TODO(mvels): revisit the below code to check if 3 loops with 3
    // (incremental) conditions is faster than 1 loop with a switch.
    // Benchmarking and perf recordings indicate the loop with switch is
    // fastest, likely because of indirect jumps on the tight case values and
    // dense branches. But it's worth considering 3 loops, as the `action`
    // transitions are mono directional. E.g.:
    //   while (action == kPopped) {
    //     ...
    //   }
    //   while (action == kCopied) {
    //     ...
    //   }
    //   ...
    // We also  found that an "if () do {}" loop here seems faster, possibly
    // because it allows the branch predictor more granular heuristics on
    // 'single leaf' (`depth` == 0) and 'single depth' (`depth` == 1) cases
    // which appear to be the most common use cases.
    if (depth != 0) {
      do {
        CordRepBtree* node = stack[--depth];
        const bool owned = depth < share_depth;
        switch (result.action) {
          case CordRepBtree::kPopped:
            assert(!propagate);
            result = node->AddEdge<edge_type>(owned, result.tree, length);
            break;
          case CordRepBtree::kCopied:
            result = node->SetEdge<edge_type>(owned, result.tree, length);
            if (propagate) stack[depth] = result.tree;
            break;
          case CordRepBtree::kSelf:
            node->length += length;
            while (depth > 0) {
              node = stack[--depth];
              node->length += length;
            }
            return node;
        }
      } while (depth > 0);
    }
    return Finalize(tree, result);
  }

  // Invokes `Unwind` with `propagate=true` to update the stack node values.
  inline CordRepBtree* Propagate(CordRepBtree* tree, int depth, size_t length,
                                 OpResult result) {
    return Unwind</*propagate=*/true>(tree, depth, length, result);
  }

  // `share_depth` contains the depth at which the nodes in the stack become
  // shared. I.e., if the top most level is shared (i.e.: `!refcount.IsOne()`),
  // then `share_depth` is 0. If the 2nd node is shared (and implicitly all
  // nodes below that) then `share_depth` is 1, etc. A `share_depth` greater
  // than the depth of the stack indicates that none of the nodes in the stack
  // are shared.
  int share_depth;

  NodeStack stack;
};

}  // namespace

void SetCordBtreeExhaustiveValidation(bool do_exaustive_validation) {
  cord_btree_exhaustive_validation.store(do_exaustive_validation,
                                         std::memory_order_relaxed);
}

bool IsCordBtreeExhaustiveValidationEnabled() {
  return cord_btree_exhaustive_validation.load(std::memory_order_relaxed);
}

void CordRepBtree::Dump(const CordRep* rep, absl::string_view label,
                        bool include_contents, std::ostream& stream) {
  stream << "===================================\n";
  if (!label.empty()) {
    stream << label << '\n';
    stream << "-----------------------------------\n";
  }
  if (rep) {
    DumpAll(rep, include_contents, stream);
  } else {
    stream << "NULL\n";
  }
}

void CordRepBtree::Dump(const CordRep* rep, absl::string_view label,
                        std::ostream& stream) {
  Dump(rep, label, false, stream);
}

void CordRepBtree::Dump(const CordRep* rep, std::ostream& stream) {
  Dump(rep, absl::string_view(), false, stream);
}

template <size_t size>
static void DestroyTree(CordRepBtree* tree) {
  for (CordRep* node : tree->Edges()) {
    if (node->refcount.Decrement()) continue;
    for (CordRep* edge : node->btree()->Edges()) {
      if (edge->refcount.Decrement()) continue;
      if (size == 1) {
        DeleteLeafEdge(edge);
      } else {
        CordRepBtree::Destroy(edge->btree());
      }
    }
    CordRepBtree::Delete(node->btree());
  }
  CordRepBtree::Delete(tree);
}

void CordRepBtree::Destroy(CordRepBtree* tree) {
  switch (tree->height()) {
    case 0:
      for (CordRep* edge : tree->Edges()) {
        if (!edge->refcount.Decrement()) {
          DeleteLeafEdge(edge);
        }
      }
      return CordRepBtree::Delete(tree);
    case 1:
      return DestroyTree<1>(tree);
    default:
      return DestroyTree<2>(tree);
  }
}

bool CordRepBtree::IsValid(const CordRepBtree* tree, bool shallow) {
#define NODE_CHECK_VALID(x)                                           \
  if (!(x)) {                                                         \
    ABSL_RAW_LOG(ERROR, "CordRepBtree::CheckValid() FAILED: %s", #x); \
    return false;                                                     \
  }
#define NODE_CHECK_EQ(x, y)                                                    \
  if ((x) != (y)) {                                                            \
    ABSL_RAW_LOG(ERROR,                                                        \
                 "CordRepBtree::CheckValid() FAILED: %s != %s (%s vs %s)", #x, \
                 #y, absl::StrCat(x).c_str(), absl::StrCat(y).c_str());        \
    return false;                                                              \
  }

  NODE_CHECK_VALID(tree != nullptr);
  NODE_CHECK_VALID(tree->IsBtree());
  NODE_CHECK_VALID(tree->height() <= kMaxHeight);
  NODE_CHECK_VALID(tree->begin() < tree->capacity());
  NODE_CHECK_VALID(tree->end() <= tree->capacity());
  NODE_CHECK_VALID(tree->begin() <= tree->end());
  size_t child_length = 0;
  for (CordRep* edge : tree->Edges()) {
    NODE_CHECK_VALID(edge != nullptr);
    if (tree->height() > 0) {
      NODE_CHECK_VALID(edge->IsBtree());
      NODE_CHECK_VALID(edge->btree()->height() == tree->height() - 1);
    } else {
      NODE_CHECK_VALID(IsDataEdge(edge));
    }
    child_length += edge->length;
  }
  NODE_CHECK_EQ(child_length, tree->length);
  if ((!shallow || IsCordBtreeExhaustiveValidationEnabled()) &&
      tree->height() > 0) {
    for (CordRep* edge : tree->Edges()) {
      if (!IsValid(edge->btree(), shallow)) return false;
    }
  }
  return true;

#undef NODE_CHECK_VALID
#undef NODE_CHECK_EQ
}

#ifndef NDEBUG

CordRepBtree* CordRepBtree::AssertValid(CordRepBtree* tree, bool shallow) {
  if (!IsValid(tree, shallow)) {
    Dump(tree, "CordRepBtree validation failed:", false, std::cout);
    ABSL_RAW_LOG(FATAL, "CordRepBtree::CheckValid() FAILED");
  }
  return tree;
}

const CordRepBtree* CordRepBtree::AssertValid(const CordRepBtree* tree,
                                              bool shallow) {
  if (!IsValid(tree, shallow)) {
    Dump(tree, "CordRepBtree validation failed:", false, std::cout);
    ABSL_RAW_LOG(FATAL, "CordRepBtree::CheckValid() FAILED");
  }
  return tree;
}

#endif  // NDEBUG

template <EdgeType edge_type>
inline OpResult CordRepBtree::AddEdge(bool owned, CordRep* edge, size_t delta) {
  if (size() >= kMaxCapacity) return {New(edge), kPopped};
  OpResult result = ToOpResult(owned);
  result.tree->Add<edge_type>(edge);
  result.tree->length += delta;
  return result;
}

template <EdgeType edge_type>
OpResult CordRepBtree::SetEdge(bool owned, CordRep* edge, size_t delta) {
  OpResult result;
  const size_t idx = index(edge_type);
  if (owned) {
    result = {this, kSelf};
    CordRep::Unref(edges_[idx]);
  } else {
    // Create a copy containing all unchanged edges. Unchanged edges are the
    // open interval [begin, back) or [begin + 1, end) depending on `edge_type`.
    // We conveniently cover both case using a constexpr `shift` being 0 or 1
    // as `end :== back + 1`.
    result = {CopyRaw(length), kCopied};
    constexpr int shift = edge_type == kFront ? 1 : 0;
    for (CordRep* r : Edges(begin() + shift, back() + shift)) {
      CordRep::Ref(r);
    }
  }
  result.tree->edges_[idx] = edge;
  result.tree->length += delta;
  return result;
}

template <EdgeType edge_type>
CordRepBtree* CordRepBtree::AddCordRep(CordRepBtree* tree, CordRep* rep) {
  const int depth = tree->height();
  const size_t length = rep->length;
  StackOperations<edge_type> ops;
  CordRepBtree* leaf = ops.BuildStack(tree, depth);
  const OpResult result =
      leaf->AddEdge<edge_type>(ops.owned(depth), rep, length);
  return ops.Unwind(tree, depth, length, result);
}

template <>
CordRepBtree* CordRepBtree::NewLeaf<kBack>(absl::string_view data,
                                           size_t extra) {
  CordRepBtree* leaf = CordRepBtree::New(0);
  size_t length = 0;
  size_t end = 0;
  const size_t cap = leaf->capacity();
  while (!data.empty() && end != cap) {
    auto* flat = CordRepFlat::New(data.length() + extra);
    flat->length = (std::min)(data.length(), flat->Capacity());
    length += flat->length;
    leaf->edges_[end++] = flat;
    data = Consume<kBack>(flat->Data(), data, flat->length);
  }
  leaf->length = length;
  leaf->set_end(end);
  return leaf;
}

template <>
CordRepBtree* CordRepBtree::NewLeaf<kFront>(absl::string_view data,
                                            size_t extra) {
  CordRepBtree* leaf = CordRepBtree::New(0);
  size_t length = 0;
  size_t begin = leaf->capacity();
  leaf->set_end(leaf->capacity());
  while (!data.empty() && begin != 0) {
    auto* flat = CordRepFlat::New(data.length() + extra);
    flat->length = (std::min)(data.length(), flat->Capacity());
    length += flat->length;
    leaf->edges_[--begin] = flat;
    data = Consume<kFront>(flat->Data(), data, flat->length);
  }
  leaf->length = length;
  leaf->set_begin(begin);
  return leaf;
}

template <>
absl::string_view CordRepBtree::AddData<kBack>(absl::string_view data,
                                               size_t extra) {
  assert(!data.empty());
  assert(size() < capacity());
  AlignBegin();
  const size_t cap = capacity();
  do {
    CordRepFlat* flat = CordRepFlat::New(data.length() + extra);
    const size_t n = (std::min)(data.length(), flat->Capacity());
    flat->length = n;
    edges_[fetch_add_end(1)] = flat;
    data = Consume<kBack>(flat->Data(), data, n);
  } while (!data.empty() && end() != cap);
  return data;
}

template <>
absl::string_view CordRepBtree::AddData<kFront>(absl::string_view data,
                                                size_t extra) {
  assert(!data.empty());
  assert(size() < capacity());
  AlignEnd();
  do {
    CordRepFlat* flat = CordRepFlat::New(data.length() + extra);
    const size_t n = (std::min)(data.length(), flat->Capacity());
    flat->length = n;
    edges_[sub_fetch_begin(1)] = flat;
    data = Consume<kFront>(flat->Data(), data, n);
  } while (!data.empty() && begin() != 0);
  return data;
}

template <EdgeType edge_type>
CordRepBtree* CordRepBtree::AddData(CordRepBtree* tree, absl::string_view data,
                                    size_t extra) {
  if (ABSL_PREDICT_FALSE(data.empty())) return tree;

  const size_t original_data_size = data.size();
  int depth = tree->height();
  StackOperations<edge_type> ops;
  CordRepBtree* leaf = ops.BuildStack(tree, depth);

  // If there is capacity in the last edge, append as much data
  // as possible into this last edge.
  if (leaf->size() < leaf->capacity()) {
    OpResult result = leaf->ToOpResult(ops.owned(depth));
    data = result.tree->AddData<edge_type>(data, extra);
    if (data.empty()) {
      result.tree->length += original_data_size;
      return ops.Unwind(tree, depth, original_data_size, result);
    }

    // We added some data into this leaf, but not all. Propagate the added
    // length to the top most node, and rebuild the stack with any newly copied
    // or updated nodes. From this point on, the path (leg) from the top most
    // node to the right-most node towards the leaf node is privately owned.
    size_t delta = original_data_size - data.size();
    assert(delta > 0);
    result.tree->length += delta;
    tree = ops.Propagate(tree, depth, delta, result);
    ops.share_depth = depth + 1;
  }

  // We were unable to append all data into the existing right-most leaf node.
  // This means all remaining data must be put into (a) new leaf node(s) which
  // we append to the tree. To make this efficient, we iteratively build full
  // leaf nodes from `data` until the created leaf contains all remaining data.
  // We utilize the `Unwind` method to merge the created leaf into the first
  // level towards root that has capacity. On each iteration with remaining
  // data, we rebuild the stack in the knowledge that right-most nodes are
  // privately owned after the first `Unwind` completes.
  for (;;) {
    OpResult result = {CordRepBtree::NewLeaf<edge_type>(data, extra), kPopped};
    if (result.tree->length == data.size()) {
      return ops.Unwind(tree, depth, result.tree->length, result);
    }
    data = Consume<edge_type>(data, result.tree->length);
    tree = ops.Unwind(tree, depth, result.tree->length, result);
    depth = tree->height();
    ops.BuildOwnedStack(tree, depth);
  }
}

template <EdgeType edge_type>
CordRepBtree* CordRepBtree::Merge(CordRepBtree* dst, CordRepBtree* src) {
  assert(dst->height() >= src->height());

  // Capture source length as we may consume / destroy `src`.
  const size_t length = src->length;

  // We attempt to merge `src` at its corresponding height in `dst`.
  const int depth = dst->height() - src->height();
  StackOperations<edge_type> ops;
  CordRepBtree* merge_node = ops.BuildStack(dst, depth);

  // If there is enough space in `merge_node` for all edges from `src`, add all
  // edges to this node, making a fresh copy as needed if not privately owned.
  // If `merge_node` does not have capacity for `src`, we rely on `Unwind` and
  // `Finalize` to merge `src` into the first level towards `root` where there
  // is capacity for another edge, or create a new top level node.
  OpResult result;
  if (merge_node->size() + src->size() <= kMaxCapacity) {
    result = merge_node->ToOpResult(ops.owned(depth));
    result.tree->Add<edge_type>(src->Edges());
    result.tree->length += src->length;
    if (src->refcount.IsOne()) {
      Delete(src);
    } else {
      for (CordRep* edge : src->Edges()) CordRep::Ref(edge);
      CordRepBtree::Unref(src);
    }
  } else {
    result = {src, kPopped};
  }

  // Unless we merged at the top level (i.e.: src and dst are equal height),
  // unwind the result towards the top level, and finalize the result.
  if (depth) {
    return ops.Unwind(dst, depth, length, result);
  }
  return ops.Finalize(dst, result);
}

CopyResult CordRepBtree::CopySuffix(size_t offset) {
  assert(offset < this->length);

  // As long as `offset` starts inside the last edge, we can 'drop' the current
  // depth. For the most extreme example: if offset references the last data
  // edge in the tree, there is only a single edge / path from the top of the
  // tree to that last edge, so we can drop all the nodes except that edge.
  // The fast path check for this is `back->length >= length - offset`.
  int height = this->height();
  CordRepBtree* node = this;
  size_t len = node->length - offset;
  CordRep* back = node->Edge(kBack);
  while (back->length >= len) {
    offset = back->length - len;
    if (--height < 0) {
      return {MakeSubstring(CordRep::Ref(back), offset), height};
    }
    node = back->btree();
    back = node->Edge(kBack);
  }
  if (offset == 0) return {CordRep::Ref(node), height};

  // Offset does not point into the last edge, so we span at least two edges.
  // Find the index of offset with `IndexBeyond` which provides us the edge
  // 'beyond' the offset if offset is not a clean starting point of an edge.
  Position pos = node->IndexBeyond(offset);
  CordRepBtree* sub = node->CopyToEndFrom(pos.index, len);
  const CopyResult result = {sub, height};

  // `pos.n` contains a non zero value if the offset is not an exact starting
  // point of an edge. In this case, `pos.n` contains the 'trailing' amount of
  // bytes of the edge preceding that in `pos.index`. We need to iteratively
  // adjust the preceding edge with the 'broken' offset until we have a perfect
  // start of the edge.
  while (pos.n != 0) {
    assert(pos.index >= 1);
    const size_t begin = pos.index - 1;
    sub->set_begin(begin);
    CordRep* const edge = node->Edge(begin);

    len = pos.n;
    offset = edge->length - len;

    if (--height < 0) {
      sub->edges_[begin] = MakeSubstring(CordRep::Ref(edge), offset, len);
      return result;
    }

    node = edge->btree();
    pos = node->IndexBeyond(offset);

    CordRepBtree* nsub = node->CopyToEndFrom(pos.index, len);
    sub->edges_[begin] = nsub;
    sub = nsub;
  }
  sub->set_begin(pos.index);
  return result;
}

CopyResult CordRepBtree::CopyPrefix(size_t n, bool allow_folding) {
  assert(n > 0);
  assert(n <= this->length);

  // As long as `n` does not exceed the length of the first edge, we can 'drop'
  // the current depth. For the most extreme example: if we'd copy a 1 byte
  // prefix from a tree, there is only a single edge / path from the top of the
  // tree to the single data edge containing this byte, so we can drop all the
  // nodes except the data node.
  int height = this->height();
  CordRepBtree* node = this;
  CordRep* front = node->Edge(kFront);
  if (allow_folding) {
    while (front->length >= n) {
      if (--height < 0) return {MakeSubstring(CordRep::Ref(front), 0, n), -1};
      node = front->btree();
      front = node->Edge(kFront);
    }
  }
  if (node->length == n) return {CordRep::Ref(node), height};

  // `n` spans at least two nodes, find the end point of the span.
  Position pos = node->IndexOf(n);

  // Create a partial copy of the node up to `pos.index`, with a defined length
  // of `n`. Any 'partial last edge' is added further below as needed.
  CordRepBtree* sub = node->CopyBeginTo(pos.index, n);
  const CopyResult result = {sub, height};

  // `pos.n` contains the 'offset inside the edge for IndexOf(n)'. As long as
  // this is not zero, we don't have a 'clean cut', so we need to make a
  // (partial) copy of that last edge, and repeat this until pos.n is zero.
  while (pos.n != 0) {
    size_t end = pos.index;
    n = pos.n;

    CordRep* edge = node->Edge(pos.index);
    if (--height < 0) {
      sub->edges_[end++] = MakeSubstring(CordRep::Ref(edge), 0, n);
      sub->set_end(end);
      AssertValid(result.edge->btree());
      return result;
    }

    node = edge->btree();
    pos = node->IndexOf(n);
    CordRepBtree* nsub = node->CopyBeginTo(pos.index, n);
    sub->edges_[end++] = nsub;
    sub->set_end(end);
    sub = nsub;
  }
  sub->set_end(pos.index);
  AssertValid(result.edge->btree());
  return result;
}

CordRep* CordRepBtree::ExtractFront(CordRepBtree* tree) {
  CordRep* front = tree->Edge(tree->begin());
  if (tree->refcount.IsOne()) {
    Unref(tree->Edges(tree->begin() + 1, tree->end()));
    CordRepBtree::Delete(tree);
  } else {
    CordRep::Ref(front);
    CordRep::Unref(tree);
  }
  return front;
}

CordRepBtree* CordRepBtree::ConsumeBeginTo(CordRepBtree* tree, size_t end,
                                           size_t new_length) {
  assert(end <= tree->end());
  if (tree->refcount.IsOne()) {
    Unref(tree->Edges(end, tree->end()));
    tree->set_end(end);
    tree->length = new_length;
  } else {
    CordRepBtree* old = tree;
    tree = tree->CopyBeginTo(end, new_length);
    CordRep::Unref(old);
  }
  return tree;
}

CordRep* CordRepBtree::RemoveSuffix(CordRepBtree* tree, size_t n) {
  // Check input and deal with trivial cases 'Remove all/none'
  assert(tree != nullptr);
  assert(n <= tree->length);
  const size_t len = tree->length;
  if (ABSL_PREDICT_FALSE(n == 0)) {
    return tree;
  }
  if (ABSL_PREDICT_FALSE(n >= len)) {
    CordRepBtree::Unref(tree);
    return nullptr;
  }

  size_t length = len - n;
  int height = tree->height();
  bool is_mutable = tree->refcount.IsOne();

  // Extract all top nodes which are reduced to size = 1
  Position pos = tree->IndexOfLength(length);
  while (pos.index == tree->begin()) {
    CordRep* edge = ExtractFront(tree);
    is_mutable &= edge->refcount.IsOne();
    if (height-- == 0) return ResizeEdge(edge, length, is_mutable);
    tree = edge->btree();
    pos = tree->IndexOfLength(length);
  }

  // Repeat the following sequence traversing down the tree:
  // - Crop the top node to the 'last remaining edge' adjusting length.
  // - Set the length for down edges to the partial length in that last edge.
  // - Repeat this until the last edge is 'included in full'
  // - If we hit the data edge level, resize and return the last data edge
  CordRepBtree* top = tree = ConsumeBeginTo(tree, pos.index + 1, length);
  CordRep* edge = tree->Edge(pos.index);
  length = pos.n;
  while (length != edge->length) {
    // ConsumeBeginTo guarantees `tree` is a clean, privately owned copy.
    assert(tree->refcount.IsOne());
    const bool edge_is_mutable = edge->refcount.IsOne();

    if (height-- == 0) {
      tree->edges_[pos.index] = ResizeEdge(edge, length, edge_is_mutable);
      return AssertValid(top);
    }

    if (!edge_is_mutable) {
      // We can't 'in place' remove any suffixes down this edge.
      // Replace this edge with a prefix copy instead.
      tree->edges_[pos.index] = edge->btree()->CopyPrefix(length, false).edge;
      CordRep::Unref(edge);
      return AssertValid(top);
    }

    // Move down one level, rinse repeat.
    tree = edge->btree();
    pos = tree->IndexOfLength(length);
    tree = ConsumeBeginTo(edge->btree(), pos.index + 1, length);
    edge = tree->Edge(pos.index);
    length = pos.n;
  }

  return AssertValid(top);
}

CordRep* CordRepBtree::SubTree(size_t offset, size_t n) {
  assert(n <= this->length);
  assert(offset <= this->length - n);
  if (ABSL_PREDICT_FALSE(n == 0)) return nullptr;

  CordRepBtree* node = this;
  int height = node->height();
  Position front = node->IndexOf(offset);
  CordRep* left = node->edges_[front.index];
  while (front.n + n <= left->length) {
    if (--height < 0) return MakeSubstring(CordRep::Ref(left), front.n, n);
    node = left->btree();
    front = node->IndexOf(front.n);
    left = node->edges_[front.index];
  }

  const Position back = node->IndexBefore(front, n);
  CordRep* const right = node->edges_[back.index];
  assert(back.index > front.index);

  // Get partial suffix and prefix entries.
  CopyResult prefix;
  CopyResult suffix;
  if (height > 0) {
    // Copy prefix and suffix of the boundary nodes.
    prefix = left->btree()->CopySuffix(front.n);
    suffix = right->btree()->CopyPrefix(back.n);

    // If there is an edge between the prefix and suffix edges, then the tree
    // must remain at its previous (full) height. If we have no edges between
    // prefix and suffix edges, then the tree must be as high as either the
    // suffix or prefix edges (which are collapsed to their minimum heights).
    if (front.index + 1 == back.index) {
      height = (std::max)(prefix.height, suffix.height) + 1;
    }

    // Raise prefix and suffixes to the new tree height.
    for (int h = prefix.height + 1; h < height; ++h) {
      prefix.edge = CordRepBtree::New(prefix.edge);
    }
    for (int h = suffix.height + 1; h < height; ++h) {
      suffix.edge = CordRepBtree::New(suffix.edge);
    }
  } else {
    // Leaf node, simply take substrings for prefix and suffix.
    prefix = CopyResult{MakeSubstring(CordRep::Ref(left), front.n), -1};
    suffix = CopyResult{MakeSubstring(CordRep::Ref(right), 0, back.n), -1};
  }

  // Compose resulting tree.
  CordRepBtree* sub = CordRepBtree::New(height);
  size_t end = 0;
  sub->edges_[end++] = prefix.edge;
  for (CordRep* r : node->Edges(front.index + 1, back.index)) {
    sub->edges_[end++] = CordRep::Ref(r);
  }
  sub->edges_[end++] = suffix.edge;
  sub->set_end(end);
  sub->length = n;
  return AssertValid(sub);
}

CordRepBtree* CordRepBtree::MergeTrees(CordRepBtree* left,
                                       CordRepBtree* right) {
  return left->height() >= right->height() ? Merge<kBack>(left, right)
                                           : Merge<kFront>(right, left);
}

bool CordRepBtree::IsFlat(absl::string_view* fragment) const {
  if (height() == 0 && size() == 1) {
    if (fragment) *fragment = Data(begin());
    return true;
  }
  return false;
}

bool CordRepBtree::IsFlat(size_t offset, const size_t n,
                          absl::string_view* fragment) const {
  assert(n <= this->length);
  assert(offset <= this->length - n);
  if (ABSL_PREDICT_FALSE(n == 0)) return false;
  int height = this->height();
  const CordRepBtree* node = this;
  for (;;) {
    const Position front = node->IndexOf(offset);
    const CordRep* edge = node->Edge(front.index);
    if (edge->length < front.n + n) return false;
    if (--height < 0) {
      if (fragment) *fragment = EdgeData(edge).substr(front.n, n);
      return true;
    }
    offset = front.n;
    node = node->Edge(front.index)->btree();
  }
}

char CordRepBtree::GetCharacter(size_t offset) const {
  assert(offset < length);
  const CordRepBtree* node = this;
  int height = node->height();
  for (;;) {
    Position front = node->IndexOf(offset);
    if (--height < 0) return node->Data(front.index)[front.n];
    offset = front.n;
    node = node->Edge(front.index)->btree();
  }
}

Span<char> CordRepBtree::GetAppendBufferSlow(size_t size) {
  // The inlined version in `GetAppendBuffer()` deals with all heights <= 3.
  assert(height() >= 4);
  assert(refcount.IsOne());

  // Build a stack of nodes we may potentially need to update if we find a
  // non-shared FLAT with capacity at the leaf level.
  const int depth = height();
  CordRepBtree* node = this;
  CordRepBtree* stack[kMaxDepth];
  for (int i = 0; i < depth; ++i) {
    node = node->Edge(kBack)->btree();
    if (!node->refcount.IsOne()) return {};
    stack[i] = node;
  }

  // Must be a privately owned, mutable flat.
  CordRep* const edge = node->Edge(kBack);
  if (!edge->refcount.IsOne() || edge->tag < FLAT) return {};

  // Must have capacity.
  const size_t avail = edge->flat()->Capacity() - edge->length;
  if (avail == 0) return {};

  // Build span on remaining capacity.
  size_t delta = (std::min)(size, avail);
  Span<char> span = {edge->flat()->Data() + edge->length, delta};
  edge->length += delta;
  this->length += delta;
  for (int i = 0; i < depth; ++i) {
    stack[i]->length += delta;
  }
  return span;
}

CordRepBtree* CordRepBtree::CreateSlow(CordRep* rep) {
  if (rep->IsBtree()) return rep->btree();

  CordRepBtree* node = nullptr;
  auto consume = [&node](CordRep* r, size_t offset, size_t length) {
    r = MakeSubstring(r, offset, length);
    if (node == nullptr) {
      node = New(r);
    } else {
      node = CordRepBtree::AddCordRep<kBack>(node, r);
    }
  };
  Consume(rep, consume);
  return node;
}

CordRepBtree* CordRepBtree::AppendSlow(CordRepBtree* tree, CordRep* rep) {
  if (ABSL_PREDICT_TRUE(rep->IsBtree())) {
    return MergeTrees(tree, rep->btree());
  }
  auto consume = [&tree](CordRep* r, size_t offset, size_t length) {
    r = MakeSubstring(r, offset, length);
    tree = CordRepBtree::AddCordRep<kBack>(tree, r);
  };
  Consume(rep, consume);
  return tree;
}

CordRepBtree* CordRepBtree::PrependSlow(CordRepBtree* tree, CordRep* rep) {
  if (ABSL_PREDICT_TRUE(rep->IsBtree())) {
    return MergeTrees(rep->btree(), tree);
  }
  auto consume = [&tree](CordRep* r, size_t offset, size_t length) {
    r = MakeSubstring(r, offset, length);
    tree = CordRepBtree::AddCordRep<kFront>(tree, r);
  };
  ReverseConsume(rep, consume);
  return tree;
}

CordRepBtree* CordRepBtree::Append(CordRepBtree* tree, absl::string_view data,
                                   size_t extra) {
  return CordRepBtree::AddData<kBack>(tree, data, extra);
}

CordRepBtree* CordRepBtree::Prepend(CordRepBtree* tree, absl::string_view data,
                                    size_t extra) {
  return CordRepBtree::AddData<kFront>(tree, data, extra);
}

template CordRepBtree* CordRepBtree::AddCordRep<kFront>(CordRepBtree* tree,
                                                        CordRep* rep);
template CordRepBtree* CordRepBtree::AddCordRep<kBack>(CordRepBtree* tree,
                                                       CordRep* rep);
template CordRepBtree* CordRepBtree::AddData<kFront>(CordRepBtree* tree,
                                                     absl::string_view data,
                                                     size_t extra);
template CordRepBtree* CordRepBtree::AddData<kBack>(CordRepBtree* tree,
                                                    absl::string_view data,
                                                    size_t extra);

void CordRepBtree::Rebuild(CordRepBtree** stack, CordRepBtree* tree,
                           bool consume) {
  bool owned = consume && tree->refcount.IsOne();
  if (tree->height() == 0) {
    for (CordRep* edge : tree->Edges()) {
      if (!owned) edge = CordRep::Ref(edge);
      size_t height = 0;
      size_t length = edge->length;
      CordRepBtree* node = stack[0];
      OpResult result = node->AddEdge<kBack>(true, edge, length);
      while (result.action == CordRepBtree::kPopped) {
        stack[height] = result.tree;
        if (stack[++height] == nullptr) {
          result.action = CordRepBtree::kSelf;
          stack[height] = CordRepBtree::New(node, result.tree);
        } else {
          node = stack[height];
          result = node->AddEdge<kBack>(true, result.tree, length);
        }
      }
      while (stack[++height] != nullptr) {
        stack[height]->length += length;
      }
    }
  } else {
    for (CordRep* rep : tree->Edges()) {
      Rebuild(stack, rep->btree(), owned);
    }
  }
  if (consume) {
    if (owned) {
      CordRepBtree::Delete(tree);
    } else {
      CordRepBtree::Unref(tree);
    }
  }
}

CordRepBtree* CordRepBtree::Rebuild(CordRepBtree* tree) {
  // Set up initial stack with empty leaf node.
  CordRepBtree* node = CordRepBtree::New();
  CordRepBtree* stack[CordRepBtree::kMaxDepth + 1] = {node};

  // Recursively build the tree, consuming the input tree.
  Rebuild(stack, tree, /* consume reference */ true);

  // Return top most node
  for (CordRepBtree* parent : stack) {
    if (parent == nullptr) return node;
    node = parent;
  }

  // Unreachable
  assert(false);
  return nullptr;
}

CordRepBtree::ExtractResult CordRepBtree::ExtractAppendBuffer(
    CordRepBtree* tree, size_t extra_capacity) {
  int depth = 0;
  NodeStack stack;

  // Set up default 'no success' result which is {tree, nullptr}.
  ExtractResult result;
  result.tree = tree;
  result.extracted = nullptr;

  // Dive down the right side of the tree, making sure no edges are shared.
  while (tree->height() > 0) {
    if (!tree->refcount.IsOne()) return result;
    stack[depth++] = tree;
    tree = tree->Edge(kBack)->btree();
  }
  if (!tree->refcount.IsOne()) return result;

  // Validate we ended on a non shared flat.
  CordRep* rep = tree->Edge(kBack);
  if (!(rep->IsFlat() && rep->refcount.IsOne())) return result;

  // Verify it has at least the requested extra capacity.
  CordRepFlat* flat = rep->flat();
  const size_t length = flat->length;
  const size_t avail = flat->Capacity() - flat->length;
  if (extra_capacity > avail) return result;

  // Set the extracted flat in the result.
  result.extracted = flat;

  // Cascading delete all nodes that become empty.
  while (tree->size() == 1) {
    CordRepBtree::Delete(tree);
    if (--depth < 0) {
      // We consumed the entire tree: return nullptr for new tree.
      result.tree = nullptr;
      return result;
    }
    rep = tree;
    tree = stack[depth];
  }

  // Remove the edge or cascaded up parent node.
  tree->set_end(tree->end() - 1);
  tree->length -= length;

  // Adjust lengths up the tree.
  while (depth > 0) {
    tree = stack[--depth];
    tree->length -= length;
  }

  // Remove unnecessary top nodes with size = 1. This may iterate all the way
  // down to the leaf node in which case we simply return the remaining last
  // edge in that node and the extracted flat.
  while (tree->size() == 1) {
    int height = tree->height();
    rep = tree->Edge(kBack);
    Delete(tree);
    if (height == 0) {
      // We consumed the leaf: return the sole data edge as the new tree.
      result.tree = rep;
      return result;
    }
    tree = rep->btree();
  }

  // Done: return the (new) top level node and extracted flat.
  result.tree = tree;
  return result;
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
