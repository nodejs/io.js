#ifndef SRC_MEMORY_TRACKER_INL_H_
#define SRC_MEMORY_TRACKER_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "memory_tracker.h"
#include "util-inl.h"

namespace node {

// Fallback edge_name if node_name is not available, or "" if edge_name
// is not available either.
inline const char* GetNodeName(const char* node_name, const char* edge_name) {
  if (node_name != nullptr) {
    return node_name;
  }
  if (edge_name != nullptr) {
    return edge_name;
  }
  return "";
}

class MemoryRetainerNode : public v8::EmbedderGraph::Node {
 public:
  inline MemoryRetainerNode(MemoryTracker* tracker,
                            const MemoryRetainer* retainer)
      : retainer_(retainer) {
    CHECK_NOT_NULL(retainer_);
    v8::HandleScope handle_scope(tracker->isolate());
    v8::Local<v8::Object> obj = retainer_->WrappedObject();
    if (!obj.IsEmpty())
      wrapper_node_ = tracker->graph()->V8Node(obj.As<v8::Value>());

    name_ = retainer_->MemoryInfoName();
    size_ = retainer_->SelfSize();
    detachedness_ = retainer_->GetDetachedness();
  }

  inline MemoryRetainerNode(MemoryTracker* tracker,
                            const char* name,
                            size_t size,
                            bool is_root_node = false)
      : retainer_(nullptr) {
    name_ = name;
    size_ = size;
    is_root_node_ = is_root_node;
  }

  const char* Name() override { return name_; }
  const char* NamePrefix() override { return "Node /"; }
  size_t SizeInBytes() override { return size_; }
  // TODO(addaleax): Merging this with the "official" WrapperNode() method
  // seems to lose accuracy, e.g. SizeInBytes() is disregarded.
  // Figure out whether to do anything about that.
  Node* JSWrapperNode() { return wrapper_node_; }

  bool IsRootNode() override {
    if (retainer_ != nullptr) {
      return retainer_->IsRootNode();
    }
    return is_root_node_;
  }
  v8::EmbedderGraph::Node::Detachedness GetDetachedness() override {
    return detachedness_;
  }

 private:
  friend class MemoryTracker;

  // If retainer_ is not nullptr, then it must have a wrapper_node_,
  // and we have
  // name_ == retainer_->MemoryInfoName()
  // size_ == retainer_->SelfSize()
  // is_root_node_ == retainer_->IsRootNode()
  const MemoryRetainer* retainer_;
  Node* wrapper_node_ = nullptr;

  // Otherwise (retainer == nullptr), we set these fields in an ad-hoc way
  bool is_root_node_ = false;
  const char* name_;
  size_t size_ = 0;
  v8::EmbedderGraph::Node::Detachedness detachedness_ =
      v8::EmbedderGraph::Node::Detachedness::kUnknown;
};

void MemoryTracker::TrackFieldWithSize(const char* edge_name,
                                       size_t size,
                                       const char* node_name) {
  if (size > 0) AddNode(GetNodeName(node_name, edge_name), size, edge_name);
}

void MemoryTracker::TrackInlineFieldWithSize(const char* edge_name,
                                             size_t size,
                                             const char* node_name) {
  if (size > 0) AddNode(GetNodeName(node_name, edge_name), size, edge_name);
  CHECK(CurrentNode());
  CurrentNode()->size_ -= size;
}

void MemoryTracker::TrackField(const char* edge_name,
                               const MemoryRetainer& value,
                               const char* node_name) {
  TrackField(edge_name, &value);
}

void MemoryTracker::TrackField(const char* edge_name,
                               const MemoryRetainer* value,
                               const char* node_name) {
  if (value == nullptr) return;
  auto it = seen_.find(value);
  if (it != seen_.end()) {
    graph_->AddEdge(CurrentNode(), it->second, edge_name);
  } else {
    Track(value, edge_name);
  }
}

template <typename T, typename D>
void MemoryTracker::TrackField(const char* edge_name,
                               const std::unique_ptr<T, D>& value,
                               const char* node_name) {
  if (value.get() == nullptr) {
    return;
  }
  TrackField(edge_name, value.get(), node_name);
}

template <typename T>
void MemoryTracker::TrackField(const char* edge_name,
                               const std::shared_ptr<T>& value,
                               const char* node_name) {
  if (value.get() == nullptr) {
    return;
  }
  TrackField(edge_name, value.get(), node_name);
}

template <typename T, bool kIsWeak>
void MemoryTracker::TrackField(const char* edge_name,
                               const BaseObjectPtrImpl<T, kIsWeak>& value,
                               const char* node_name) {
  if (value.get() == nullptr || kIsWeak) return;
  TrackField(edge_name, value.get(), node_name);
}

template <typename T, typename Iterator>
void MemoryTracker::TrackField(const char* edge_name,
                               const T& value,
                               const char* node_name,
                               const char* element_name,
                               bool subtract_from_self) {
  // If the container is empty, the size has been accounted into the parent's
  // self size
  if (value.begin() == value.end()) return;
  // Fall back to edge name if node names are not provided
  if (CurrentNode() != nullptr && subtract_from_self) {
    // Shift the self size of this container out to a separate node
    CurrentNode()->size_ -= sizeof(T);
  }
  PushNode(GetNodeName(node_name, edge_name), sizeof(T), edge_name);
  for (Iterator it = value.begin(); it != value.end(); ++it) {
    // Use nullptr as edge names so the elements appear as indexed properties
    TrackField(nullptr, *it, element_name);
  }
  PopNode();
}

template <typename T>
void MemoryTracker::TrackField(const char* edge_name,
                               const std::queue<T>& value,
                               const char* node_name,
                               const char* element_name) {
  struct ContainerGetter : public std::queue<T> {
    static const typename std::queue<T>::container_type& Get(
        const std::queue<T>& value) {
      return value.*&ContainerGetter::c;
    }
  };

  const auto& container = ContainerGetter::Get(value);
  TrackField(edge_name, container, node_name, element_name);
}

template <typename T, typename test_for_number, typename dummy>
void MemoryTracker::TrackField(const char* edge_name,
                               const T& value,
                               const char* node_name) {
  // For numbers, creating new nodes is not worth the overhead.
  CurrentNode()->size_ += sizeof(T);
}

template <typename T, typename U>
void MemoryTracker::TrackField(const char* edge_name,
                               const std::pair<T, U>& value,
                               const char* node_name) {
  PushNode(node_name == nullptr ? "pair" : node_name,
           sizeof(const std::pair<T, U>),
           edge_name);
  // TODO(joyeecheung): special case if one of these is a number type
  // that meets the test_for_number trait so that their sizes don't get
  // merged into the pair node
  TrackField("first", value.first);
  TrackField("second", value.second);
  PopNode();
}

template <typename T>
void MemoryTracker::TrackField(const char* edge_name,
                               const std::basic_string<T>& value,
                               const char* node_name) {
  TrackFieldWithSize(edge_name, value.size() * sizeof(T), "std::basic_string");
}

template <typename T>
void MemoryTracker::TrackField(const char* edge_name,
                               const v8::Eternal<T>& value,
                               const char* node_name) {
  TrackField(edge_name, value.Get(isolate_));
}

template <typename T>
void MemoryTracker::TrackField(const char* edge_name,
                               const v8::PersistentBase<T>& value,
                               const char* node_name) {
  if (value.IsWeak()) return;
  TrackField(edge_name, value.Get(isolate_));
}

template <typename T>
void MemoryTracker::TrackField(const char* edge_name,
                               const v8::Local<T>& value,
                               const char* node_name) {
  if (!value.IsEmpty())
    graph_->AddEdge(CurrentNode(),
                    graph_->V8Node(value.template As<v8::Value>()),
                    edge_name);
}

template <typename T>
void MemoryTracker::TrackField(const char* edge_name,
                               const MallocedBuffer<T>& value,
                               const char* node_name) {
  TrackFieldWithSize(edge_name, value.size, "MallocedBuffer");
}

void MemoryTracker::TrackField(const char* edge_name,
                               const v8::BackingStore* value,
                               const char* node_name) {
  TrackFieldWithSize(edge_name, value->ByteLength(), "BackingStore");
}

void MemoryTracker::TrackField(const char* name,
                               const uv_buf_t& value,
                               const char* node_name) {
  TrackFieldWithSize(name, value.len, "uv_buf_t");
}

void MemoryTracker::TrackField(const char* name,
                               const uv_timer_t& value,
                               const char* node_name) {
  TrackFieldWithSize(name, sizeof(value), "uv_timer_t");
}

void MemoryTracker::TrackField(const char* name,
                               const uv_async_t& value,
                               const char* node_name) {
  TrackFieldWithSize(name, sizeof(value), "uv_async_t");
}

void MemoryTracker::TrackInlineField(const char* name,
                                     const uv_async_t& value,
                                     const char* node_name) {
  TrackInlineFieldWithSize(name, sizeof(value), "uv_async_t");
}

void MemoryTracker::Track(const MemoryRetainer* retainer,
                          const char* edge_name) {
  v8::HandleScope handle_scope(isolate_);
  auto it = seen_.find(retainer);
  if (it != seen_.end()) {
    if (CurrentNode() != nullptr) {
      graph_->AddEdge(CurrentNode(), it->second, edge_name);
    }
    return;  // It has already been tracked, no need to call MemoryInfo again
  }
  MemoryRetainerNode* n = PushNode(retainer, edge_name);
  retainer->MemoryInfo(this);
  CHECK_EQ(CurrentNode(), n);
  CHECK_NE(n->size_, 0);
  PopNode();
}

void MemoryTracker::TrackInlineField(const MemoryRetainer* retainer,
                                     const char* edge_name) {
  Track(retainer, edge_name);
  CHECK(CurrentNode());
  CurrentNode()->size_ -= retainer->SelfSize();
}

MemoryRetainerNode* MemoryTracker::CurrentNode() const {
  if (node_stack_.empty()) return nullptr;
  return node_stack_.top();
}

MemoryRetainerNode* MemoryTracker::AddNode(const MemoryRetainer* retainer,
                                           const char* edge_name) {
  auto it = seen_.find(retainer);
  if (it != seen_.end()) {
    return it->second;
  }

  MemoryRetainerNode* n = new MemoryRetainerNode(this, retainer);
  graph_->AddNode(std::unique_ptr<v8::EmbedderGraph::Node>(n));
  seen_[retainer] = n;
  if (CurrentNode() != nullptr) graph_->AddEdge(CurrentNode(), n, edge_name);

  if (n->JSWrapperNode() != nullptr) {
    graph_->AddEdge(n, n->JSWrapperNode(), "native_to_javascript");
    graph_->AddEdge(n->JSWrapperNode(), n, "javascript_to_native");
  }

  return n;
}

MemoryRetainerNode* MemoryTracker::AddNode(const char* node_name,
                                           size_t size,
                                           const char* edge_name) {
  MemoryRetainerNode* n = new MemoryRetainerNode(this, node_name, size);
  graph_->AddNode(std::unique_ptr<v8::EmbedderGraph::Node>(n));

  if (CurrentNode() != nullptr) graph_->AddEdge(CurrentNode(), n, edge_name);

  return n;
}

MemoryRetainerNode* MemoryTracker::PushNode(const MemoryRetainer* retainer,
                                            const char* edge_name) {
  MemoryRetainerNode* n = AddNode(retainer, edge_name);
  node_stack_.push(n);
  return n;
}

MemoryRetainerNode* MemoryTracker::PushNode(const char* node_name,
                                            size_t size,
                                            const char* edge_name) {
  MemoryRetainerNode* n = AddNode(node_name, size, edge_name);
  node_stack_.push(n);
  return n;
}

void MemoryTracker::PopNode() {
  node_stack_.pop();
}

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_MEMORY_TRACKER_INL_H_
