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

#ifndef ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_H_
#define ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_H_

#include <cassert>
#include <cstdint>
#include <iosfwd>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/strings/internal/cord_data_edge.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_flat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// `SetCordBtreeExhaustiveValidation()` can be set to force exhaustive
// validation in debug assertions, and code that calls `IsValid()`
// explicitly. By default, assertions should be relatively cheap and
// AssertValid() can easily lead to O(n^2) complexity as recursive / full tree
// validation is O(n).
void SetCordBtreeExhaustiveValidation(bool do_exaustive_validation);
bool IsCordBtreeExhaustiveValidationEnabled();

class CordRepBtreeNavigator;

// CordRepBtree is as the name implies a btree implementation of a Cordrep tree.
// Data is stored at the leaf level only, non leaf nodes contain down pointers
// only. Allowed types of data edges are FLAT, EXTERNAL and SUBSTRINGs of FLAT
// or EXTERNAL nodes. The implementation allows for data to be added to either
// end of the tree only, it does not provide any 'insert' logic. This has the
// benefit that we can expect good fill ratios: all nodes except the outer
// 'legs' will have 100% fill ratios for trees built using Append/Prepend
// methods. Merged trees will typically have a fill ratio well above 50% as in a
// similar fashion, one side of the merged tree will typically have a 100% fill
// ratio, and the 'open' end will average 50%. All operations are O(log(n)) or
// better, and the tree never needs balancing.
//
// All methods accepting a CordRep* or CordRepBtree* adopt a reference on that
// input unless explicitly stated otherwise. All functions returning a CordRep*
// or CordRepBtree* instance transfer a reference back to the caller.
// Simplified, callers both 'donate' and 'consume' a reference count on each
// call, simplifying the API. An example of building a tree:
//
//   CordRepBtree* tree = CordRepBtree::Create(MakeFlat("Hello"));
//   tree = CordRepBtree::Append(tree, MakeFlat("world"));
//
// In the above example, all inputs are consumed, making each call affecting
// `tree` reference count neutral. The returned `tree` value can be different
// from the input if the input is shared with other threads, or if the tree
// grows in height, but callers typically never have to concern themselves with
// that and trust that all methods DTRT at all times.
class CordRepBtree : public CordRep {
 public:
  // EdgeType identifies `front` and `back` enum values.
  // Various implementations in CordRepBtree such as `Add` and `Edge` are
  // generic and templated on operating on either of the boundary edges.
  // For more information on the possible edges contained in a CordRepBtree
  // instance see the documentation for `edges_`.
  enum class EdgeType { kFront, kBack };

  // Convenience constants into `EdgeType`
  static constexpr EdgeType kFront = EdgeType::kFront;
  static constexpr EdgeType kBack = EdgeType::kBack;

  // Maximum number of edges: based on experiments and performance data, we can
  // pick suitable values resulting in optimum cacheline aligned values. The
  // preferred values are based on 64-bit systems where we aim to align this
  // class onto 64 bytes, i.e.:  6 = 64 bytes, 14 = 128 bytes, etc.
  // TODO(b/192061034): experiment with alternative sizes.
  static constexpr size_t kMaxCapacity = 6;

  // Reasonable maximum height of the btree. We can expect a fill ratio of at
  // least 50%: trees are always expanded at the front or back. Concatenating
  // trees will then typically fold at the top most node, where the lower nodes
  // are at least at capacity on one side of joined inputs. At a lower fill
  // rate of 4 edges per node, we have capacity for ~16 million leaf nodes.
  // We will fail / abort if an application ever exceeds this height, which
  // should be extremely rare (near impossible) and be an indication of an
  // application error: we do not assume it reasonable for any application to
  // operate correctly with such monster trees.
  // Another compelling reason for the number `12` is that any contextual stack
  // required for navigation or insertion requires 12 words and 12 bytes, which
  // fits inside 2 cache lines with some room to spare, and is reasonable as a
  // local stack variable compared to Cord's current near 400 bytes stack use.
  // The maximum `height` value of a node is then `kMaxDepth - 1` as node height
  // values start with a value of 0 for leaf nodes.
  static constexpr size_t kMaxDepth = 12;
  // See comments on height() for why this is an int and not a size_t.
  static constexpr int kMaxHeight = static_cast<int>(kMaxDepth - 1);

  // `Action` defines the action for unwinding changes done at the btree's leaf
  // level that need to be propagated up to the parent node(s). Each operation
  // on a node has an effect / action defined as follows:
  // - kSelf
  //   The operation (add / update, etc) was performed directly on the node as
  //   the node is private to the current thread (i.e.: not shared directly or
  //   indirectly through a refcount > 1). Changes can be propagated directly to
  //   all parent nodes as all parent nodes are also then private to the current
  //   thread.
  // - kCopied
  //   The operation (add / update, etc) was performed on a copy of the original
  //   node, as the node is (potentially) directly or indirectly shared with
  //   other threads. Changes need to be propagated into the parent nodes where
  //   the old down pointer must be unreffed and replaced with this new copy.
  //   Such changes to parent nodes may themselves require a copy if the parent
  //   node is also shared. A kCopied action can propagate all the way to the
  //   top node where we then must unref the `tree` input provided by the
  //   caller, and return the new copy.
  // - kPopped
  //   The operation (typically add) could not be satisfied due to insufficient
  //   capacity in the targeted node, and a new 'leg' was created that needs to
  //   be added into the parent node. For example, adding a FLAT inside a leaf
  //   node that is at capacity will create a new leaf node containing that
  //   FLAT, that needs to be 'popped' up the btree. Such 'pop' actions can
  //   cascade up the tree if parent nodes are also at capacity. A 'Popped'
  //   action propagating all the way to the top of the tree will result in
  //   the tree becoming one level higher than the current tree through a final
  //   `CordRepBtree::New(tree, popped)` call, resulting in a new top node
  //   referencing the old tree and the new (fully popped upwards) 'leg'.
  enum Action { kSelf, kCopied, kPopped };

  // Result of an operation on a node. See the `Action` enum for details.
  struct OpResult {
    CordRepBtree* tree;
    Action action;
  };

  // Return value of the CopyPrefix and CopySuffix methods which can
  // return a node or data edge at any height inside the tree.
  // A height of 0 defines the lowest (leaf) node, a height of -1 identifies
  // `edge` as being a plain data node: EXTERNAL / FLAT or SUBSTRING thereof.
  struct CopyResult {
    CordRep* edge;
    int height;
  };

  // Logical position inside a node:
  // - index: index of the edge.
  // - n: size or offset value depending on context.
  struct Position {
    size_t index;
    size_t n;
  };

  // Creates a btree from the given input. Adopts a ref of `rep`.
  // If the input `rep` is itself a btree, i.e., `IsBtree()`, then this
  // function immediately returns `rep->btree()`. If the input is a valid data
  // edge (see IsDataEdge()), then a new leaf node is returned containing `rep`
  // as the sole data edge. Else, the input is assumed to be a (legacy) concat
  // tree, and the input is consumed and transformed into a btree().
  static CordRepBtree* Create(CordRep* rep);

  // Destroys the provided tree. Should only be called by cord internal API's,
  // typically after a ref_count.Decrement() on the last reference count.
  static void Destroy(CordRepBtree* tree);

  // Destruction
  static void Delete(CordRepBtree* tree) { delete tree; }

  // Use CordRep::Unref() as we overload for absl::Span<CordRep* const>.
  using CordRep::Unref;

  // Unrefs all edges in `edges` which are assumed to be 'likely one'.
  static void Unref(absl::Span<CordRep* const> edges);

  // Appends / Prepends an existing CordRep instance to this tree.
  // The below methods accept three types of input:
  // 1) `rep` is a data node (See `IsDataNode` for valid data edges).
  // `rep` is appended or prepended to this tree 'as is'.
  // 2) `rep` is a BTREE.
  // `rep` is merged into `tree` respecting the Append/Prepend order.
  // 3) `rep` is some other (legacy) type.
  // `rep` is converted in place and added to `tree`
  // Requires `tree` and `rep` to be not null.
  static CordRepBtree* Append(CordRepBtree* tree, CordRep* rep);
  static CordRepBtree* Prepend(CordRepBtree* tree, CordRep* rep);

  // Append/Prepend the data in `data` to this tree.
  // The `extra` parameter defines how much extra capacity should be allocated
  // for any additional FLAT being allocated. This is an optimization hint from
  // the caller. For example, a caller may need to add 2 string_views of data
  // "abc" and "defghi" which are not consecutive. The caller can in this case
  // invoke `AddData(tree, "abc", 6)`, and any newly added flat is allocated
  // where possible with at least 6 bytes of extra capacity beyond `length`.
  // This helps avoiding data getting fragmented over multiple flats.
  // There is no limit on the size of `data`. If `data` can not be stored inside
  // a single flat, then the function will iteratively add flats until all data
  // has been consumed and appended or prepended to the tree.
  static CordRepBtree* Append(CordRepBtree* tree, string_view data,
                              size_t extra = 0);
  static CordRepBtree* Prepend(CordRepBtree* tree, string_view data,
                               size_t extra = 0);

  // Returns a new tree, containing `n` bytes of data from this instance
  // starting at offset `offset`. Where possible, the returned tree shares
  // (re-uses) data edges and nodes with this instance to minimize the
  // combined memory footprint of both trees.
  // Requires `offset + n <= length`. Returns `nullptr` if `n` is zero.
  CordRep* SubTree(size_t offset, size_t n);

  // Removes `n` trailing bytes from `tree`, and returns the resulting tree
  // or data edge. Returns `tree` if n is zero, and nullptr if n == length.
  // This function is logically identical to:
  //   result = tree->SubTree(0, tree->length - n);
  //   Unref(tree);
  //   return result;
  // However, the actual implementation will as much as possible perform 'in
  // place' modifications on the tree on all nodes and edges that are mutable.
  // For example, in a fully privately owned tree with the last edge being a
  // flat of length 12, RemoveSuffix(1) will simply set the length of that data
  // edge to 11, and reduce the length of all nodes on the edge path by 1.
  static CordRep* RemoveSuffix(CordRepBtree* tree, size_t n);

  // Returns the character at the given offset.
  char GetCharacter(size_t offset) const;

  // Returns true if this node holds a single data edge, and if so, sets
  // `fragment` to reference the contained data. `fragment` is an optional
  // output parameter and allowed to be null.
  bool IsFlat(absl::string_view* fragment) const;

  // Returns true if the data of `n` bytes starting at offset `offset`
  // is contained in a single data edge, and if so, sets fragment to reference
  // the contained data. `fragment` is an optional output parameter and allowed
  // to be null.
  bool IsFlat(size_t offset, size_t n, absl::string_view* fragment) const;

  // Returns a span (mutable range of bytes) of up to `size` bytes into the
  // last FLAT data edge inside this tree under the following conditions:
  // - none of the nodes down into the FLAT node are shared.
  // - the last data edge in this tree is a non-shared FLAT.
  // - the referenced FLAT has additional capacity available.
  // If all these conditions are met, a non-empty span is returned, and the
  // length of the flat node and involved tree nodes have been increased by
  // `span.length()`. The caller is responsible for immediately assigning values
  // to all uninitialized data reference by the returned span.
  // Requires `this->refcount.IsOne()`: this function forces the caller to do
  // this fast path check on the top level node, as this is the most commonly
  // shared node of a cord tree.
  Span<char> GetAppendBuffer(size_t size);

  // Extracts the right-most data edge from this tree iff:
  // - the tree and all internal edges to the right-most node are not shared.
  // - the right-most node is a FLAT node and not shared.
  // - the right-most node has at least the desired extra capacity.
  //
  // Returns {tree, nullptr} if any of the above conditions are not met.
  // This method effectively removes data from the tree. The intent of this
  // method is to allow applications appending small string data to use
  // pre-existing capacity, and add the modified rep back to the tree.
  //
  // Simplified such code would look similar to this:
  //   void MyTreeBuilder::Append(string_view data) {
  //     ExtractResult result = CordRepBtree::ExtractAppendBuffer(tree_, 1);
  //     if (CordRep* rep = result.extracted) {
  //       size_t available = rep->Capacity() - rep->length;
  //       size_t n = std::min(data.size(), n);
  //       memcpy(rep->Data(), data.data(), n);
  //       rep->length += n;
  //       data.remove_prefix(n);
  //       if (!result.tree->IsBtree()) {
  //         tree_ = CordRepBtree::Create(result.tree);
  //       }
  //       tree_ = CordRepBtree::Append(tree_, rep);
  //     }
  //     ...
  //     // Remaining edge in `result.tree`.
  //   }
  static ExtractResult ExtractAppendBuffer(CordRepBtree* tree,
                                           size_t extra_capacity = 1);

  // Returns the `height` of the tree. The height of a tree is limited to
  // kMaxHeight. `height` is implemented as an `int` as in some places we
  // use negative (-1) values for 'data edges'.
  int height() const { return static_cast<int>(storage[0]); }

  // Properties: begin, back, end, front/back boundary indexes.
  size_t begin() const { return static_cast<size_t>(storage[1]); }
  size_t back() const { return static_cast<size_t>(storage[2]) - 1; }
  size_t end() const { return static_cast<size_t>(storage[2]); }
  size_t index(EdgeType edge) const {
    return edge == kFront ? begin() : back();
  }

  // Properties: size and capacity.
  // `capacity` contains the current capacity of this instance, where
  // `kMaxCapacity` contains the maximum capacity of a btree node.
  // For now, `capacity` and `kMaxCapacity` return the same value, but this may
  // change in the future if we see benefit in dynamically sizing 'small' nodes
  // to 'large' nodes for large data trees.
  size_t size() const { return end() - begin(); }
  size_t capacity() const { return kMaxCapacity; }

  // Edge access
  inline CordRep* Edge(size_t index) const;
  inline CordRep* Edge(EdgeType edge_type) const;
  inline absl::Span<CordRep* const> Edges() const;
  inline absl::Span<CordRep* const> Edges(size_t begin, size_t end) const;

  // Returns reference to the data edge at `index`.
  // Requires this instance to be a leaf node, and `index` to be valid index.
  inline absl::string_view Data(size_t index) const;

  // Diagnostics: returns true if `tree` is valid and internally consistent.
  // If `shallow` is false, then the provided top level node and all child nodes
  // below it are recursively checked. If `shallow` is true, only the provided
  // node in `tree` and the cumulative length, type and height of the direct
  // child nodes of `tree` are checked. The value of `shallow` is ignored if the
  // internal `cord_btree_exhaustive_validation` diagnostics variable is true,
  // in which case the performed validations works as if `shallow` were false.
  // This function is intended for debugging and testing purposes only.
  static bool IsValid(const CordRepBtree* tree, bool shallow = false);

  // Diagnostics: asserts that the provided tree is valid.
  // `AssertValid()` performs a shallow validation by default. `shallow` can be
  // set to false in which case an exhaustive validation is performed. This
  // function is implemented in terms of calling `IsValid()` and asserting the
  // return value to be true. See `IsValid()` for more information.
  // This function is intended for debugging and testing purposes only.
  static CordRepBtree* AssertValid(CordRepBtree* tree, bool shallow = true);
  static const CordRepBtree* AssertValid(const CordRepBtree* tree,
                                         bool shallow = true);

  // Diagnostics: dump the contents of this tree to `stream`.
  // This function is intended for debugging and testing purposes only.
  static void Dump(const CordRep* rep, std::ostream& stream);
  static void Dump(const CordRep* rep, absl::string_view label,
                   std::ostream& stream);
  static void Dump(const CordRep* rep, absl::string_view label,
                   bool include_contents, std::ostream& stream);

  // Adds the edge `edge` to this node if possible. `owned` indicates if the
  // current node is potentially shared or not with other threads. Returns:
  // - {kSelf, <this>}
  //   The edge was directly added to this node.
  // - {kCopied, <node>}
  //   The edge was added to a copy of this node.
  // - {kPopped, New(edge, height())}
  //   A new leg with the edge was created as this node has no extra capacity.
  template <EdgeType edge_type>
  inline OpResult AddEdge(bool owned, CordRep* edge, size_t delta);

  // Replaces the front or back edge with the provided new edge. Returns:
  // - {kSelf, <this>}
  //   The edge was directly set in this node. The old edge is unreffed.
  // - {kCopied, <node>}
  //   A copy of this node was created with the new edge value.
  // In both cases, the function adopts a reference on `edge`.
  template <EdgeType edge_type>
  OpResult SetEdge(bool owned, CordRep* edge, size_t delta);

  // Creates a new empty node at the specified height.
  static CordRepBtree* New(int height = 0);

  // Creates a new node containing `rep`, with the height being computed
  // automatically based on the type of `rep`.
  static CordRepBtree* New(CordRep* rep);

  // Creates a new node containing both `front` and `back` at height
  // `front.height() + 1`. Requires `back.height() == front.height()`.
  static CordRepBtree* New(CordRepBtree* front, CordRepBtree* back);

  // Creates a fully balanced tree from the provided tree by rebuilding a new
  // tree from all data edges in the input. This function is automatically
  // invoked internally when the tree exceeds the maximum height.
  static CordRepBtree* Rebuild(CordRepBtree* tree);

 private:
  CordRepBtree() = default;
  ~CordRepBtree() = default;

  // Initializes the main properties `tag`, `begin`, `end`, `height`.
  inline void InitInstance(int height, size_t begin = 0, size_t end = 0);

  // Direct property access begin / end
  void set_begin(size_t begin) { storage[1] = static_cast<uint8_t>(begin); }
  void set_end(size_t end) { storage[2] = static_cast<uint8_t>(end); }

  // Decreases the value of `begin` by `n`, and returns the new value. Notice
  // how this returns the new value unlike atomic::fetch_add which returns the
  // old value. This is because this is used to prepend edges at 'begin - 1'.
  size_t sub_fetch_begin(size_t n) {
    storage[1] -= static_cast<uint8_t>(n);
    return storage[1];
  }

  // Increases the value of `end` by `n`, and returns the previous value. This
  // function is typically used to append edges at 'end'.
  size_t fetch_add_end(size_t n) {
    const uint8_t current = storage[2];
    storage[2] = static_cast<uint8_t>(current + n);
    return current;
  }

  // Returns the index of the last edge starting on, or before `offset`, with
  // `n` containing the relative offset of `offset` inside that edge.
  // Requires `offset` < length.
  Position IndexOf(size_t offset) const;

  // Returns the index of the last edge starting before `offset`, with `n`
  // containing the relative offset of `offset` inside that edge.
  // This function is useful to find the edges for some span of bytes ending at
  // `offset` (i.e., `n` bytes). For example:
  //
  //   Position pos = IndexBefore(n)
  //   edges = Edges(begin(), pos.index)     // All full edges (may be empty)
  //   last = Sub(Edge(pos.index), 0, pos.n) // Last partial edge (may be empty)
  //
  // Requires 0 < `offset` <= length.
  Position IndexBefore(size_t offset) const;

  // Returns the index of the edge ending at (or on) length `length`, and the
  // number of bytes inside that edge up to `length`. For example, if we have a
  // Node with 2 edges, one of 10 and one of 20 long, then IndexOfLength(27)
  // will return {1, 17}, and IndexOfLength(10) will return {0, 10}.
  Position IndexOfLength(size_t n) const;

  // Identical to the above function except starting from the position `front`.
  // This function is equivalent to `IndexBefore(front.n + offset)`, with
  // the difference that this function is optimized to start at `front.index`.
  Position IndexBefore(Position front, size_t offset) const;

  // Returns the index of the edge directly beyond the edge containing offset
  // `offset`, with `n` containing the distance of that edge from `offset`.
  // This function is useful for iteratively finding suffix nodes and remaining
  // partial bytes in left-most suffix nodes as for example in CopySuffix.
  // Requires `offset` < length.
  Position IndexBeyond(size_t offset) const;

  // Creates a new leaf node containing as much data as possible from `data`.
  // The data is added either forwards or reversed depending on `edge_type`.
  // Callers must check the length of the returned node to determine if all data
  // was copied or not.
  // See the `Append/Prepend` function for the meaning and purpose of `extra`.
  template <EdgeType edge_type>
  static CordRepBtree* NewLeaf(absl::string_view data, size_t extra);

  // Creates a raw copy of this Btree node with the specified length, copying
  // all properties, but without adding any references to existing edges.
  CordRepBtree* CopyRaw(size_t new_length) const;

  // Creates a full copy of this Btree node, adding a reference on all edges.
  CordRepBtree* Copy() const;

  // Creates a partial copy of this Btree node, copying all edges up to `end`,
  // adding a reference on each copied edge, and sets the length of the newly
  // created copy to `new_length`.
  CordRepBtree* CopyBeginTo(size_t end, size_t new_length) const;

  // Returns a tree containing the edges [tree->begin(), end) and length
  // of `new_length`. This method consumes a reference on the provided
  // tree, and logically performs the following operation:
  //   result = tree->CopyBeginTo(end, new_length);
  //   CordRep::Unref(tree);
  //   return result;
  static CordRepBtree* ConsumeBeginTo(CordRepBtree* tree, size_t end,
                                      size_t new_length);

  // Creates a partial copy of this Btree node, copying all edges starting at
  // `begin`, adding a reference on each copied edge, and sets the length of
  // the newly created copy to `new_length`.
  CordRepBtree* CopyToEndFrom(size_t begin, size_t new_length) const;

  // Extracts and returns the front edge from the provided tree.
  // This method consumes a reference on the provided tree, and logically
  // performs the following operation:
  //   edge = CordRep::Ref(tree->Edge(kFront));
  //   CordRep::Unref(tree);
  //   return edge;
  static CordRep* ExtractFront(CordRepBtree* tree);

  // Returns a tree containing the result of appending `right` to `left`.
  static CordRepBtree* MergeTrees(CordRepBtree* left, CordRepBtree* right);

  // Fallback functions for `Create()`, `Append()` and `Prepend()` which
  // deal with legacy / non conforming input, i.e.: CONCAT trees.
  static CordRepBtree* CreateSlow(CordRep* rep);
  static CordRepBtree* AppendSlow(CordRepBtree*, CordRep* rep);
  static CordRepBtree* PrependSlow(CordRepBtree*, CordRep* rep);

  // Recursively rebuilds `tree` into `stack`. If 'consume` is set to true, the
  // function will consume a reference on `tree`. `stack` is a null terminated
  // array containing the new tree's state, with the current leaf node at
  // stack[0], and parent nodes above that, or null for 'top of tree'.
  static void Rebuild(CordRepBtree** stack, CordRepBtree* tree, bool consume);

  // Aligns existing edges to start at index 0, to allow for a new edge to be
  // added to the back of the current edges.
  inline void AlignBegin();

  // Aligns existing edges to end at `capacity`, to allow for a new edge to be
  // added in front of the current edges.
  inline void AlignEnd();

  // Adds the provided edge to this node.
  // Requires this node to have capacity for the edge. Realigns / moves
  // existing edges as needed to prepend or append the new edge.
  template <EdgeType edge_type>
  inline void Add(CordRep* rep);

  // Adds the provided edges to this node.
  // Requires this node to have capacity for the edges. Realigns / moves
  // existing edges as needed to prepend or append the new edges.
  template <EdgeType edge_type>
  inline void Add(absl::Span<CordRep* const>);

  // Adds data from `data` to this node until either all data has been consumed,
  // or there is no more capacity for additional flat nodes inside this node.
  // Requires the current node to be a leaf node, data to be non empty, and the
  // current node to have capacity for at least one more data edge.
  // Returns any remaining data from `data` that was not added, which is
  // depending on the edge type (front / back) either the remaining prefix of
  // suffix of the input.
  // See the `Append/Prepend` function for the meaning and purpose of `extra`.
  template <EdgeType edge_type>
  absl::string_view AddData(absl::string_view data, size_t extra);

  // Replace the front or back edge with the provided value.
  // Adopts a reference on `edge` and unrefs the old edge.
  template <EdgeType edge_type>
  inline void SetEdge(CordRep* edge);

  // Returns a partial copy of the current tree containing the first `n` bytes
  // of data. `CopyResult` contains both the resulting edge and its height. The
  // resulting tree may be less high than the current tree, or even be a single
  // matching data edge if `allow_folding` is set to true.
  // For example, if `n == 1`, then the result will be the single data edge, and
  // height will be set to -1 (one below the owning leaf node). If n == 0, this
  // function returns null. Requires `n <= length`
  CopyResult CopyPrefix(size_t n, bool allow_folding = true);

  // Returns a partial copy of the current tree containing all data starting
  // after `offset`. `CopyResult` contains both the resulting edge and its
  // height. The resulting tree may be less high than the current tree, or even
  // be a single matching data edge. For example, if `n == length - 1`, then the
  // result will be a single data edge, and height will be set to -1 (one below
  // the owning leaf node).
  // Requires `offset < length`
  CopyResult CopySuffix(size_t offset);

  // Returns a OpResult value of {this, kSelf} or {Copy(), kCopied}
  // depending on the value of `owned`.
  inline OpResult ToOpResult(bool owned);

  // Adds `rep` to the specified tree, returning the modified tree.
  template <EdgeType edge_type>
  static CordRepBtree* AddCordRep(CordRepBtree* tree, CordRep* rep);

  // Adds `data` to the specified tree, returning the modified tree.
  // See the `Append/Prepend` function for the meaning and purpose of `extra`.
  template <EdgeType edge_type>
  static CordRepBtree* AddData(CordRepBtree* tree, absl::string_view data,
                               size_t extra = 0);

  // Merges `src` into `dst` with `src` being added either before (kFront) or
  // after (kBack) `dst`. Requires the height of `dst` to be greater than or
  // equal to the height of `src`.
  template <EdgeType edge_type>
  static CordRepBtree* Merge(CordRepBtree* dst, CordRepBtree* src);

  // Fallback version of GetAppendBuffer for large trees: GetAppendBuffer()
  // implements an inlined version for trees of limited height (3 levels),
  // GetAppendBufferSlow implements the logic for large trees.
  Span<char> GetAppendBufferSlow(size_t size);

  // `edges_` contains all edges starting from this instance.
  // These are explicitly `child` edges only, a cord btree (or any cord tree in
  // that respect) does not store `parent` pointers anywhere: multiple trees /
  // parents can reference the same shared child edge. The type of these edges
  // depends on the height of the node. `Leaf nodes` (height == 0) contain `data
  // edges` (external or flat nodes, or sub-strings thereof). All other nodes
  // (height > 0) contain pointers to BTREE nodes with a height of `height - 1`.
  CordRep* edges_[kMaxCapacity];

  friend class CordRepBtreeTestPeer;
  friend class CordRepBtreeNavigator;
};

inline CordRepBtree* CordRep::btree() {
  assert(IsBtree());
  return static_cast<CordRepBtree*>(this);
}

inline const CordRepBtree* CordRep::btree() const {
  assert(IsBtree());
  return static_cast<const CordRepBtree*>(this);
}

inline void CordRepBtree::InitInstance(int height, size_t begin, size_t end) {
  tag = BTREE;
  storage[0] = static_cast<uint8_t>(height);
  storage[1] = static_cast<uint8_t>(begin);
  storage[2] = static_cast<uint8_t>(end);
}

inline CordRep* CordRepBtree::Edge(size_t index) const {
  assert(index >= begin());
  assert(index < end());
  return edges_[index];
}

inline CordRep* CordRepBtree::Edge(EdgeType edge_type) const {
  return edges_[edge_type == kFront ? begin() : back()];
}

inline absl::Span<CordRep* const> CordRepBtree::Edges() const {
  return {edges_ + begin(), size()};
}

inline absl::Span<CordRep* const> CordRepBtree::Edges(size_t begin,
                                                      size_t end) const {
  assert(begin <= end);
  assert(begin >= this->begin());
  assert(end <= this->end());
  return {edges_ + begin, static_cast<size_t>(end - begin)};
}

inline absl::string_view CordRepBtree::Data(size_t index) const {
  assert(height() == 0);
  return EdgeData(Edge(index));
}

inline CordRepBtree* CordRepBtree::New(int height) {
  CordRepBtree* tree = new CordRepBtree;
  tree->length = 0;
  tree->InitInstance(height);
  return tree;
}

inline CordRepBtree* CordRepBtree::New(CordRep* rep) {
  CordRepBtree* tree = new CordRepBtree;
  int height = rep->IsBtree() ? rep->btree()->height() + 1 : 0;
  tree->length = rep->length;
  tree->InitInstance(height, /*begin=*/0, /*end=*/1);
  tree->edges_[0] = rep;
  return tree;
}

inline CordRepBtree* CordRepBtree::New(CordRepBtree* front,
                                       CordRepBtree* back) {
  assert(front->height() == back->height());
  CordRepBtree* tree = new CordRepBtree;
  tree->length = front->length + back->length;
  tree->InitInstance(front->height() + 1, /*begin=*/0, /*end=*/2);
  tree->edges_[0] = front;
  tree->edges_[1] = back;
  return tree;
}

inline void CordRepBtree::Unref(absl::Span<CordRep* const> edges) {
  for (CordRep* edge : edges) {
    if (ABSL_PREDICT_FALSE(!edge->refcount.Decrement())) {
      CordRep::Destroy(edge);
    }
  }
}

inline CordRepBtree* CordRepBtree::CopyRaw(size_t new_length) const {
  CordRepBtree* tree = new CordRepBtree;

  // `length` and `refcount` are the first members of `CordRepBtree`.
  // We initialize `length` using the given length, have `refcount` be set to
  // ref = 1 through its default constructor, and copy all data beyond
  // 'refcount' which starts with `tag` using a single memcpy: all contents
  // except `refcount` is trivially copyable, and the compiler does not
  // efficiently coalesce member-wise copy of these members.
  // See https://gcc.godbolt.org/z/qY8zsca6z
  // LINT.IfChange(copy_raw)
  tree->length = new_length;
  uint8_t* dst = &tree->tag;
  const uint8_t* src = &tag;
  const ptrdiff_t offset = src - reinterpret_cast<const uint8_t*>(this);
  memcpy(dst, src, sizeof(CordRepBtree) - static_cast<size_t>(offset));
  return tree;
  // LINT.ThenChange()
}

inline CordRepBtree* CordRepBtree::Copy() const {
  CordRepBtree* tree = CopyRaw(length);
  for (CordRep* rep : Edges()) CordRep::Ref(rep);
  return tree;
}

inline CordRepBtree* CordRepBtree::CopyToEndFrom(size_t begin,
                                                 size_t new_length) const {
  assert(begin >= this->begin());
  assert(begin <= this->end());
  CordRepBtree* tree = CopyRaw(new_length);
  tree->set_begin(begin);
  for (CordRep* edge : tree->Edges()) CordRep::Ref(edge);
  return tree;
}

inline CordRepBtree* CordRepBtree::CopyBeginTo(size_t end,
                                               size_t new_length) const {
  assert(end <= capacity());
  assert(end >= this->begin());
  CordRepBtree* tree = CopyRaw(new_length);
  tree->set_end(end);
  for (CordRep* edge : tree->Edges()) CordRep::Ref(edge);
  return tree;
}

inline void CordRepBtree::AlignBegin() {
  // The below code itself does not need to be fast as typically we have
  // mono-directional append/prepend calls, and `begin` / `end` are typically
  // adjusted no more than once. But we want to avoid potential register clobber
  // effects, making the compiler emit register save/store/spills, and minimize
  // the size of code.
  const size_t delta = begin();
  if (ABSL_PREDICT_FALSE(delta != 0)) {
    const size_t new_end = end() - delta;
    set_begin(0);
    set_end(new_end);
    // TODO(mvels): we can write this using 2 loads / 2 stores depending on
    // total size for the kMaxCapacity = 6 case. I.e., we can branch (switch) on
    // size, and then do overlapping load/store of up to 4 pointers (inlined as
    // XMM, YMM or ZMM load/store) and up to 2 pointers (XMM / YMM), which is a)
    // compact and b) not clobbering any registers.
    ABSL_ASSUME(new_end <= kMaxCapacity);
#ifdef __clang__
#pragma unroll 1
#endif
    for (size_t i = 0; i < new_end; ++i) {
      edges_[i] = edges_[i + delta];
    }
  }
}

inline void CordRepBtree::AlignEnd() {
  // See comments in `AlignBegin` for motivation on the hand-rolled for loops.
  const size_t delta = capacity() - end();
  if (delta != 0) {
    const size_t new_begin = begin() + delta;
    const size_t new_end = end() + delta;
    set_begin(new_begin);
    set_end(new_end);
    ABSL_ASSUME(new_end <= kMaxCapacity);
#ifdef __clang__
#pragma unroll 1
#endif
    for (size_t i = new_end - 1; i >= new_begin; --i) {
      edges_[i] = edges_[i - delta];
    }
  }
}

template <>
inline void CordRepBtree::Add<CordRepBtree::kBack>(CordRep* rep) {
  AlignBegin();
  edges_[fetch_add_end(1)] = rep;
}

template <>
inline void CordRepBtree::Add<CordRepBtree::kBack>(
    absl::Span<CordRep* const> edges) {
  AlignBegin();
  size_t new_end = end();
  for (CordRep* edge : edges) edges_[new_end++] = edge;
  set_end(new_end);
}

template <>
inline void CordRepBtree::Add<CordRepBtree::kFront>(CordRep* rep) {
  AlignEnd();
  edges_[sub_fetch_begin(1)] = rep;
}

template <>
inline void CordRepBtree::Add<CordRepBtree::kFront>(
    absl::Span<CordRep* const> edges) {
  AlignEnd();
  size_t new_begin = begin() - edges.size();
  set_begin(new_begin);
  for (CordRep* edge : edges) edges_[new_begin++] = edge;
}

template <CordRepBtree::EdgeType edge_type>
inline void CordRepBtree::SetEdge(CordRep* edge) {
  const int idx = edge_type == kFront ? begin() : back();
  CordRep::Unref(edges_[idx]);
  edges_[idx] = edge;
}

inline CordRepBtree::OpResult CordRepBtree::ToOpResult(bool owned) {
  return owned ? OpResult{this, kSelf} : OpResult{Copy(), kCopied};
}

inline CordRepBtree::Position CordRepBtree::IndexOf(size_t offset) const {
  assert(offset < length);
  size_t index = begin();
  while (offset >= edges_[index]->length) offset -= edges_[index++]->length;
  return {index, offset};
}

inline CordRepBtree::Position CordRepBtree::IndexBefore(size_t offset) const {
  assert(offset > 0);
  assert(offset <= length);
  size_t index = begin();
  while (offset > edges_[index]->length) offset -= edges_[index++]->length;
  return {index, offset};
}

inline CordRepBtree::Position CordRepBtree::IndexBefore(Position front,
                                                        size_t offset) const {
  size_t index = front.index;
  offset = offset + front.n;
  while (offset > edges_[index]->length) offset -= edges_[index++]->length;
  return {index, offset};
}

inline CordRepBtree::Position CordRepBtree::IndexOfLength(size_t n) const {
  assert(n <= length);
  size_t index = back();
  size_t strip = length - n;
  while (strip >= edges_[index]->length) strip -= edges_[index--]->length;
  return {index, edges_[index]->length - strip};
}

inline CordRepBtree::Position CordRepBtree::IndexBeyond(
    const size_t offset) const {
  // We need to find the edge which `starting offset` is beyond (>=)`offset`.
  // For this we can't use the `offset -= length` logic of IndexOf. Instead, we
  // track the offset of the `current edge` in `off`, which we increase as we
  // iterate over the edges until we find the matching edge.
  size_t off = 0;
  size_t index = begin();
  while (offset > off) off += edges_[index++]->length;
  return {index, off - offset};
}

inline CordRepBtree* CordRepBtree::Create(CordRep* rep) {
  if (IsDataEdge(rep)) return New(rep);
  return CreateSlow(rep);
}

inline Span<char> CordRepBtree::GetAppendBuffer(size_t size) {
  assert(refcount.IsOne());
  CordRepBtree* tree = this;
  const int height = this->height();
  CordRepBtree* n1 = tree;
  CordRepBtree* n2 = tree;
  CordRepBtree* n3 = tree;
  switch (height) {
    case 3:
      tree = tree->Edge(kBack)->btree();
      if (!tree->refcount.IsOne()) return {};
      n2 = tree;
      ABSL_FALLTHROUGH_INTENDED;
    case 2:
      tree = tree->Edge(kBack)->btree();
      if (!tree->refcount.IsOne()) return {};
      n1 = tree;
      ABSL_FALLTHROUGH_INTENDED;
    case 1:
      tree = tree->Edge(kBack)->btree();
      if (!tree->refcount.IsOne()) return {};
      ABSL_FALLTHROUGH_INTENDED;
    case 0:
      CordRep* edge = tree->Edge(kBack);
      if (!edge->refcount.IsOne()) return {};
      if (edge->tag < FLAT) return {};
      size_t avail = edge->flat()->Capacity() - edge->length;
      if (avail == 0) return {};
      size_t delta = (std::min)(size, avail);
      Span<char> span = {edge->flat()->Data() + edge->length, delta};
      edge->length += delta;
      switch (height) {
        case 3:
          n3->length += delta;
          ABSL_FALLTHROUGH_INTENDED;
        case 2:
          n2->length += delta;
          ABSL_FALLTHROUGH_INTENDED;
        case 1:
          n1->length += delta;
          ABSL_FALLTHROUGH_INTENDED;
        case 0:
          tree->length += delta;
          return span;
      }
      break;
  }
  return GetAppendBufferSlow(size);
}

extern template CordRepBtree* CordRepBtree::AddCordRep<CordRepBtree::kBack>(
    CordRepBtree* tree, CordRep* rep);

extern template CordRepBtree* CordRepBtree::AddCordRep<CordRepBtree::kFront>(
    CordRepBtree* tree, CordRep* rep);

inline CordRepBtree* CordRepBtree::Append(CordRepBtree* tree, CordRep* rep) {
  if (ABSL_PREDICT_TRUE(IsDataEdge(rep))) {
    return CordRepBtree::AddCordRep<kBack>(tree, rep);
  }
  return AppendSlow(tree, rep);
}

inline CordRepBtree* CordRepBtree::Prepend(CordRepBtree* tree, CordRep* rep) {
  if (ABSL_PREDICT_TRUE(IsDataEdge(rep))) {
    return CordRepBtree::AddCordRep<kFront>(tree, rep);
  }
  return PrependSlow(tree, rep);
}

#ifdef NDEBUG

inline CordRepBtree* CordRepBtree::AssertValid(CordRepBtree* tree,
                                               bool /* shallow */) {
  return tree;
}

inline const CordRepBtree* CordRepBtree::AssertValid(const CordRepBtree* tree,
                                                     bool /* shallow */) {
  return tree;
}

#endif

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_REP_BTREE_H_
