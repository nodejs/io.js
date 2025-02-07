#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "v8-profiler.h"

#include <uv.h>

#include <limits>
#include <queue>
#include <stack>
#include <string>
#include <unordered_map>

namespace v8 {
class BackingStore;
}

namespace node {

template <typename T>
struct MallocedBuffer;

// Set the node name of a MemoryRetainer to klass
#define SET_MEMORY_INFO_NAME(Klass)                                            \
  inline const char* MemoryInfoName() const override { return #Klass; }

// Set the self size of a MemoryRetainer to the stack-allocated size of a
// certain class
#define SET_SELF_SIZE(Klass)                                                   \
  inline size_t SelfSize() const override { return sizeof(Klass); }

// Used when there is no additional fields to track
#define SET_NO_MEMORY_INFO()                                                   \
  inline void MemoryInfo(node::MemoryTracker* tracker) const override {}

class MemoryTracker;
class MemoryRetainerNode;
template <typename T, bool kIsWeak>
class BaseObjectPtrImpl;

namespace crypto {
class NodeBIO;
}

class CleanupHookCallback;

/* Example:
 *
 * class ExampleRetainer : public MemoryRetainer {
 *   public:
 *     // Or use SET_NO_MEMORY_INFO() when there is no additional fields
 *     // to track.
 *     void MemoryInfo(MemoryTracker* tracker) const override {
 *       // Node name and size comes from the MemoryInfoName and SelfSize of
 *       // AnotherRetainerClass
 *       tracker->TrackField("another_retainer", another_retainer_);
 *
 *       // Add non_pointer_retainer as a separate node into the graph
 *       // and track its memory information recursively.
 *       // Note that we need to make sure its size is not accounted in
 *       // ExampleRetainer::SelfSize().
 *       tracker->TrackField("non_pointer_retainer", &non_pointer_retainer_);
 *
 *       // Specify node name and size explicitly
 *       tracker->TrackFieldWithSize("internal_member",
 *                                   internal_member_.size(),
 *                                   "InternalClass");
 *       // Node name falls back to the edge name,
 *       // elements in the container appear as grandchildren nodes
 *       tracker->TrackField("vector", vector_);
 *       // Node name and size come from the JS object
 *       tracker->TrackField("target", target_);
 *     }
 *
 *     // Or use SET_MEMORY_INFO_NAME(ExampleRetainer)
 *     const char* MemoryInfoName() const override {
 *       return "ExampleRetainer";
 *     }
 *
 *     // Classes that only want to return its sizeof() value can use the
 *     // SET_SELF_SIZE(Class) macro instead.
 *     size_t SelfSize() const override {
 *       // We need to exclude the size of non_pointer_retainer so that
 *       // we can track it separately in ExampleRetainer::MemoryInfo().
 *       return sizeof(ExampleRetainer) - sizeof(NonPointerRetainerClass);
 *     }
 *
 *     // Note: no need to implement these two methods when implementing
 *     // a BaseObject or an AsyncWrap class
 *     bool IsRootNode() const override { return !wrapped_.IsWeak(); }
 *     v8::Local<v8::Object> WrappedObject() const override {
 *       return node::PersistentToLocal::Default(wrapped_);
 *     }
 *
 *   private:
 *     AnotherRetainerClass* another_retainer_;
 *     NonPointerRetainerClass non_pointer_retainer;
 *     InternalClass internal_member_;
 *     std::vector<uv_async_t> vector_;
 *     v8::Global<Object> target_;
 *
 *     v8::Global<Object> wrapped_;
 * }
 *
 * This creates the following graph:
 *   Node / ExampleRetainer
 *    |> another_retainer :: Node / AnotherRetainerClass
 *    |> internal_member :: Node / InternalClass
 *    |> vector :: Node / vector (elements will be grandchildren)
 *        |> [1] :: Node / uv_async_t (uv_async_t has predefined names)
 *        |> [2] :: Node / uv_async_t
 *        |> ...
 *    |> target :: TargetClass (JS class name of the target object)
 *    |> wrapped :: WrappedClass (JS class name of the wrapped object)
 *        |> wrapper :: Node / ExampleRetainer (back reference)
 */
class MemoryRetainer {
 public:
  virtual ~MemoryRetainer() = default;

  // Subclasses should implement these methods to provide information
  // for the V8 heap snapshot generator.
  // The MemoryInfo() method is assumed to be called within a context
  // where all the edges start from the node of the current retainer,
  // and point to the nodes as specified by tracker->Track* calls.
  virtual void MemoryInfo(MemoryTracker* tracker) const = 0;
  virtual const char* MemoryInfoName() const = 0;
  virtual size_t SelfSize() const = 0;

  virtual v8::Local<v8::Object> WrappedObject() const {
    return v8::Local<v8::Object>();
  }

  virtual bool IsRootNode() const { return false; }
  virtual v8::EmbedderGraph::Node::Detachedness GetDetachedness() const {
    return v8::EmbedderGraph::Node::Detachedness::kUnknown;
  }
};

/**
 * MemoryRetainerTraits allows defining a custom memory info for a
 * class that can not be modified to implement the MemoryRetainer interface.
 *
 * Example:
 *
 * template <>
 * struct MemoryRetainerTraits<ExampleRetainer> {
 *   static void MemoryInfo(MemoryTracker* tracker,
 *                         const ExampleRetainer& value) {
 *     tracker->TrackField("another_retainer", value.another_retainer_);
 *   }
 *   static const char* MemoryInfoName(const ExampleRetainer& value) {
 *     return "ExampleRetainer";
 *   }
 *   static size_t SelfSize(const ExampleRetainer& value) {
 *     return sizeof(value);
 *   }
 * };
 *
 * This creates the following graph:
 *   Node / ExampleRetainer
 *    |> another_retainer :: Node / AnotherRetainerClass
 */
template <typename T, typename = void>
struct MemoryRetainerTraits {};

class MemoryTracker {
 public:
  // Used to specify node name and size explicitly
  inline void TrackFieldWithSize(const char* edge_name,
                                 size_t size,
                                 const char* node_name = nullptr);
  inline void TrackInlineFieldWithSize(const char* edge_name,
                                       size_t size,
                                       const char* node_name = nullptr);

  // Shortcut to extract the underlying object out of the smart pointer
  template <typename T, typename D>
  inline void TrackField(const char* edge_name,
                         const std::unique_ptr<T, D>& value,
                         const char* node_name = nullptr);

  template <typename T>
  inline void TrackField(const char* edge_name,
                         const std::shared_ptr<T>& value,
                         const char* node_name = nullptr);

  template <typename T, bool kIsWeak>
  void TrackField(const char* edge_name,
                  const BaseObjectPtrImpl<T, kIsWeak>& value,
                  const char* node_name = nullptr);

  // For containers, the elements will be graphed as grandchildren nodes
  // if the container is not empty.
  // By default, we assume the parent count the stack size of the container
  // into its SelfSize so that will be subtracted from the parent size when we
  // spin off a new node for the container.
  // TODO(joyeecheung): use RTTI to retrieve the class name at runtime?
  template <typename T, typename Iterator = typename T::const_iterator>
  inline void TrackField(const char* edge_name,
                         const T& value,
                         const char* node_name = nullptr,
                         const char* element_name = nullptr,
                         bool subtract_from_self = true);
  template <typename T>
  inline void TrackField(const char* edge_name,
                         const std::queue<T>& value,
                         const char* node_name = nullptr,
                         const char* element_name = nullptr);
  template <typename T, typename U>
  inline void TrackField(const char* edge_name,
                         const std::pair<T, U>& value,
                         const char* node_name = nullptr);

  // For the following types, node_name will be ignored and predefined names
  // will be used instead. They are only in the signature for template
  // expansion.
  inline void TrackField(const char* edge_name,
                         const MemoryRetainer& value,
                         const char* node_name = nullptr);
  inline void TrackField(const char* edge_name,
                         const MemoryRetainer* value,
                         const char* node_name = nullptr);
  template <typename T>
  inline void TrackField(const char* edge_name,
                         const std::basic_string<T>& value,
                         const char* node_name = nullptr);
  template <typename T,
            typename test_for_number = typename std::
                enable_if<std::numeric_limits<T>::is_specialized, bool>::type,
            typename dummy = bool>
  inline void TrackField(const char* edge_name,
                         const T& value,
                         const char* node_name = nullptr);
  template <typename T>
  void TrackField(const char* edge_name,
                  const v8::Eternal<T>& value,
                  const char* node_name);
  template <typename T>
  inline void TrackField(const char* edge_name,
                         const v8::PersistentBase<T>& value,
                         const char* node_name = nullptr);
  template <typename T>
  inline void TrackField(const char* edge_name,
                         const v8::Local<T>& value,
                         const char* node_name = nullptr);
  template <typename T>
  inline void TrackField(const char* edge_name,
                         const MallocedBuffer<T>& value,
                         const char* node_name = nullptr);
  inline void TrackField(const char* edge_name,
                         const v8::BackingStore* value,
                         const char* node_name = nullptr);
  inline void TrackField(const char* edge_name,
                         const uv_buf_t& value,
                         const char* node_name = nullptr);
  inline void TrackField(const char* edge_name,
                         const uv_timer_t& value,
                         const char* node_name = nullptr);
  inline void TrackField(const char* edge_name,
                         const uv_async_t& value,
                         const char* node_name = nullptr);
  inline void TrackInlineField(const char* edge_name,
                               const uv_async_t& value,
                               const char* node_name = nullptr);

  // Put a memory container into the graph, create an edge from
  // the current node if there is one on the stack.
  inline void Track(const MemoryRetainer* retainer,
                    const char* edge_name = nullptr);

  // Useful for parents that do not wish to perform manual
  // adjustments to its `SelfSize()` when embedding retainer
  // objects inline.
  // Put a memory container into the graph, create an edge from
  // the current node if there is one on the stack - there should
  // be one, of the container object which the current field is part of.
  // Reduce the size of memory from the container so as to avoid
  // duplication in accounting.
  inline void TrackInlineField(const MemoryRetainer* retainer,
                               const char* edge_name = nullptr);

  // MemoryRetainerTraits implementation helpers.
  template <typename T>
  inline void TraitTrack(const T& retainer, const char* edge_name = nullptr);
  template <typename T>
  inline void TraitTrackInline(const T& retainer,
                               const char* edge_name = nullptr);

  inline v8::EmbedderGraph* graph() { return graph_; }
  inline v8::Isolate* isolate() { return isolate_; }

  inline explicit MemoryTracker(v8::Isolate* isolate,
                                v8::EmbedderGraph* graph)
    : isolate_(isolate), graph_(graph) {}

 private:
  typedef std::unordered_map<const MemoryRetainer*, MemoryRetainerNode*>
      NodeMap;

  inline MemoryRetainerNode* CurrentNode() const;
  inline MemoryRetainerNode* AddNode(const MemoryRetainer* retainer,
                                     const char* edge_name = nullptr);
  inline MemoryRetainerNode* PushNode(const MemoryRetainer* retainer,
                                      const char* edge_name = nullptr);
  inline MemoryRetainerNode* AddNode(const char* node_name,
                                     size_t size,
                                     const char* edge_name = nullptr);
  inline MemoryRetainerNode* PushNode(const char* node_name,
                                      size_t size,
                                      const char* edge_name = nullptr);
  inline void PopNode();

  v8::Isolate* isolate_;
  v8::EmbedderGraph* graph_;
  std::stack<MemoryRetainerNode*> node_stack_;
  NodeMap seen_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
