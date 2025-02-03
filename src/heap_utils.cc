#include "diagnosticfilename-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node_external_reference.h"
#include "path.h"
#include "permission/permission.h"
#include "stream_base-inl.h"
#include "util-inl.h"

// Copied from https://github.com/nodejs/node/blob/b07dc4d19fdbc15b4f76557dc45b3ce3a43ad0c3/src/util.cc#L36-L41.
#ifdef _WIN32
#include <io.h>  // _S_IREAD _S_IWRITE
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif  // S_IRUSR
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif  // S_IWUSR
#endif

using v8::Array;
using v8::Boolean;
using v8::Context;
using v8::Data;
using v8::EmbedderGraph;
using v8::EscapableHandleScope;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::HandleScope;
using v8::HeapProfiler;
using v8::HeapSnapshot;
using v8::Isolate;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Uint8Array;
using v8::Value;

namespace node {
namespace heap {

class JSGraphJSNode : public EmbedderGraph::Node {
 public:
  const char* Name() override { return "<JS Node>"; }
  size_t SizeInBytes() override { return 0; }
  bool IsEmbedderNode() override { return false; }
  Local<Data> V8Value() { return PersistentToLocal::Strong(persistent_); }

  JSGraphJSNode(Isolate* isolate, Local<Data> val) : persistent_(isolate, val) {
    CHECK(!val.IsEmpty());
  }

  struct Equal {
    inline bool operator()(JSGraphJSNode* a, JSGraphJSNode* b) const {
      Local<Data> data_a = a->V8Value();
      Local<Data> data_b = a->V8Value();
      if (data_a->IsValue()) {
        if (!data_b->IsValue()) {
          return false;
        }
        return data_a.As<Value>()->SameValue(data_b.As<Value>());
      }
      return data_a == data_b;
    }
  };

 private:
  Global<Data> persistent_;
};

class JSGraph : public EmbedderGraph {
 public:
  explicit JSGraph(Isolate* isolate) : isolate_(isolate) {}

  Node* V8Node(const Local<v8::Data>& value) override {
    std::unique_ptr<JSGraphJSNode> n { new JSGraphJSNode(isolate_, value) };
    auto it = engine_nodes_.find(n.get());
    if (it != engine_nodes_.end())
      return *it;
    engine_nodes_.insert(n.get());
    return AddNode(std::unique_ptr<Node>(n.release()));
  }

  Node* V8Node(const Local<v8::Value>& value) override {
    return V8Node(value.As<v8::Data>());
  }

  Node* AddNode(std::unique_ptr<Node> node) override {
    Node* n = node.get();
    nodes_.emplace(std::move(node));
    return n;
  }

  void AddEdge(Node* from, Node* to, const char* name = nullptr) override {
    edges_[from].insert(std::make_pair(name, to));
  }

  MaybeLocal<Array> CreateObject() const {
    EscapableHandleScope handle_scope(isolate_);
    Local<Context> context = isolate_->GetCurrentContext();
    Environment* env = Environment::GetCurrent(context);

    std::unordered_map<Node*, Local<Object>> info_objects;
    Local<Array> nodes = Array::New(isolate_, nodes_.size());
    Local<String> edges_string = FIXED_ONE_BYTE_STRING(isolate_, "edges");
    Local<String> is_root_string = FIXED_ONE_BYTE_STRING(isolate_, "isRoot");
    Local<String> name_string = env->name_string();
    Local<String> size_string = env->size_string();
    Local<String> value_string = env->value_string();
    Local<String> wraps_string = FIXED_ONE_BYTE_STRING(isolate_, "wraps");
    Local<String> to_string = FIXED_ONE_BYTE_STRING(isolate_, "to");

    for (const std::unique_ptr<Node>& n : nodes_)
      info_objects[n.get()] = Object::New(isolate_);

    {
      HandleScope handle_scope(isolate_);
      size_t i = 0;
      for (const std::unique_ptr<Node>& n : nodes_) {
        Local<Object> obj = info_objects[n.get()];
        Local<Value> value;
        std::string name_str;
        const char* prefix = n->NamePrefix();
        if (prefix == nullptr) {
          name_str = n->Name();
        } else {
          name_str = n->NamePrefix();
          name_str += " ";
          name_str += n->Name();
        }
        if (!String::NewFromUtf8(isolate_, name_str.c_str()).ToLocal(&value) ||
            obj->Set(context, name_string, value).IsNothing() ||
            obj->Set(context,
                     is_root_string,
                     Boolean::New(isolate_, n->IsRootNode()))
                .IsNothing() ||
            obj->Set(
                   context,
                   size_string,
                   Number::New(isolate_, static_cast<double>(n->SizeInBytes())))
                .IsNothing() ||
            obj->Set(context, edges_string, Array::New(isolate_)).IsNothing()) {
          return MaybeLocal<Array>();
        }
        if (nodes->Set(context, i++, obj).IsNothing())
          return MaybeLocal<Array>();
        if (!n->IsEmbedderNode()) {
          Local<Data> data = static_cast<JSGraphJSNode*>(n.get())->V8Value();
          if (data->IsValue() &&
              obj->Set(context, value_string, data.As<Value>()).IsNothing())
            return MaybeLocal<Array>();
        }
      }
    }

    for (const std::unique_ptr<Node>& n : nodes_) {
      Node* wraps = n->WrapperNode();
      if (wraps == nullptr) continue;
      Local<Object> from = info_objects[n.get()];
      Local<Object> to = info_objects[wraps];
      if (from->Set(context, wraps_string, to).IsNothing())
        return MaybeLocal<Array>();
    }

    for (const auto& edge_info : edges_) {
      Node* source = edge_info.first;
      Local<Value> edges;
      if (!info_objects[source]->Get(context, edges_string).ToLocal(&edges) ||
          !edges->IsArray()) {
        return MaybeLocal<Array>();
      }

      size_t i = 0;
      size_t j = 0;
      for (const auto& edge : edge_info.second) {
        Local<Object> to_object = info_objects[edge.second];
        Local<Object> edge_obj = Object::New(isolate_);
        Local<Value> edge_name_value;
        const char* edge_name = edge.first;
        if (edge_name != nullptr) {
          if (!String::NewFromUtf8(isolate_, edge_name)
              .ToLocal(&edge_name_value)) {
            return MaybeLocal<Array>();
          }
        } else {
          edge_name_value = Number::New(isolate_, static_cast<double>(j++));
        }
        if (edge_obj->Set(context, name_string, edge_name_value).IsNothing() ||
            edge_obj->Set(context, to_string, to_object).IsNothing() ||
            edges.As<Array>()->Set(context, i++, edge_obj).IsNothing()) {
          return MaybeLocal<Array>();
        }
      }
    }

    return handle_scope.Escape(nodes);
  }

 private:
  Isolate* isolate_;
  std::unordered_set<std::unique_ptr<Node>> nodes_;
  std::set<JSGraphJSNode*, JSGraphJSNode::Equal> engine_nodes_;
  std::unordered_map<Node*, std::set<std::pair<const char*, Node*>>> edges_;
};

void BuildEmbedderGraph(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  JSGraph graph(env->isolate());
  Environment::BuildEmbedderGraph(env->isolate(), &graph, env);
  Local<Array> ret;
  if (graph.CreateObject().ToLocal(&ret))
    args.GetReturnValue().Set(ret);
}

namespace {
class FileOutputStream : public v8::OutputStream {
 public:
  FileOutputStream(const int fd, uv_fs_t* req) : fd_(fd), req_(req) {}

  int GetChunkSize() override {
    return 65536;  // big chunks == faster
  }

  void EndOfStream() override {}

  WriteResult WriteAsciiChunk(char* data, const int size) override {
    DCHECK_EQ(status_, 0);
    int offset = 0;
    while (offset < size) {
      const uv_buf_t buf = uv_buf_init(data + offset, size - offset);
      const int num_bytes_written = uv_fs_write(nullptr,
                                                req_,
                                                fd_,
                                                &buf,
                                                1,
                                                -1,
                                                nullptr);
      uv_fs_req_cleanup(req_);
      if (num_bytes_written < 0) {
        status_ = num_bytes_written;
        return kAbort;
      }
      DCHECK_LE(static_cast<size_t>(num_bytes_written), buf.len);
      offset += num_bytes_written;
    }
    DCHECK_EQ(offset, size);
    return kContinue;
  }

  int status() const { return status_; }

 private:
  const int fd_;
  uv_fs_t* req_;
  int status_ = 0;
};

class HeapSnapshotStream : public AsyncWrap,
                           public StreamBase,
                           public v8::OutputStream {
 public:
  HeapSnapshotStream(
      Environment* env,
      HeapSnapshotPointer&& snapshot,
      Local<Object> obj) :
      AsyncWrap(env, obj, AsyncWrap::PROVIDER_HEAPSNAPSHOT),
      StreamBase(env),
      snapshot_(std::move(snapshot)) {
    MakeWeak();
    StreamBase::AttachToObject(GetObject());
  }

  ~HeapSnapshotStream() override {}

  int GetChunkSize() override {
    return 65536;  // big chunks == faster
  }

  void EndOfStream() override {
    EmitRead(UV_EOF);
    snapshot_.reset();
  }

  WriteResult WriteAsciiChunk(char* data, int size) override {
    int len = size;
    while (len != 0) {
      uv_buf_t buf = EmitAlloc(size);
      ssize_t avail = len;
      if (static_cast<ssize_t>(buf.len) < avail)
        avail = buf.len;
      memcpy(buf.base, data, avail);
      data += avail;
      len -= static_cast<int>(avail);
      EmitRead(size, buf);
    }
    return kContinue;
  }

  int ReadStart() override {
    CHECK_NE(snapshot_, nullptr);
    snapshot_->Serialize(this, HeapSnapshot::kJSON);
    return 0;
  }

  int ReadStop() override {
    return 0;
  }

  int DoShutdown(ShutdownWrap* req_wrap) override {
    UNREACHABLE();
  }

  int DoWrite(WriteWrap* w,
              uv_buf_t* bufs,
              size_t count,
              uv_stream_t* send_handle) override {
    UNREACHABLE();
  }

  bool IsAlive() override { return snapshot_ != nullptr; }
  bool IsClosing() override { return snapshot_ == nullptr; }
  AsyncWrap* GetAsyncWrap() override { return this; }

  void MemoryInfo(MemoryTracker* tracker) const override {
    if (snapshot_ != nullptr) {
      tracker->TrackFieldWithSize(
          "snapshot", sizeof(*snapshot_), "HeapSnapshot");
    }
  }

  SET_MEMORY_INFO_NAME(HeapSnapshotStream)
  SET_SELF_SIZE(HeapSnapshotStream)

 private:
  HeapSnapshotPointer snapshot_;
};

inline void TakeSnapshot(Environment* env,
                         v8::OutputStream* out,
                         HeapProfiler::HeapSnapshotOptions options) {
  HeapSnapshotPointer snapshot{
      env->isolate()->GetHeapProfiler()->TakeHeapSnapshot(options)};
  snapshot->Serialize(out, HeapSnapshot::kJSON);
}

}  // namespace

Maybe<void> WriteSnapshot(Environment* env,
                          const char* filename,
                          HeapProfiler::HeapSnapshotOptions options) {
  uv_fs_t req;
  int err;

  const int fd = uv_fs_open(nullptr,
                            &req,
                            filename,
                            O_WRONLY | O_CREAT | O_TRUNC,
                            S_IWUSR | S_IRUSR,
                            nullptr);
  uv_fs_req_cleanup(&req);
  if ((err = fd) < 0) {
    env->ThrowUVException(err, "open", nullptr, filename);
    return Nothing<void>();
  }

  FileOutputStream stream(fd, &req);
  TakeSnapshot(env, &stream, options);
  if ((err = stream.status()) < 0) {
    env->ThrowUVException(err, "write", nullptr, filename);
    return Nothing<void>();
  }

  err = uv_fs_close(nullptr, &req, fd, nullptr);
  uv_fs_req_cleanup(&req);
  if (err < 0) {
    env->ThrowUVException(err, "close", nullptr, filename);
    return Nothing<void>();
  }

  return JustVoid();
}

void DeleteHeapSnapshot(const HeapSnapshot* snapshot) {
  const_cast<HeapSnapshot*>(snapshot)->Delete();
}

BaseObjectPtr<AsyncWrap> CreateHeapSnapshotStream(
    Environment* env, HeapSnapshotPointer&& snapshot) {
  HandleScope scope(env->isolate());

  if (env->streambaseoutputstream_constructor_template().IsEmpty()) {
    // Create FunctionTemplate for HeapSnapshotStream
    Local<FunctionTemplate> os = FunctionTemplate::New(env->isolate());
    os->Inherit(AsyncWrap::GetConstructorTemplate(env));
    Local<ObjectTemplate> ost = os->InstanceTemplate();
    ost->SetInternalFieldCount(StreamBase::kInternalFieldCount);
    os->SetClassName(
        FIXED_ONE_BYTE_STRING(env->isolate(), "HeapSnapshotStream"));
    StreamBase::AddMethods(env, os);
    env->set_streambaseoutputstream_constructor_template(ost);
  }

  Local<Object> obj;
  if (!env->streambaseoutputstream_constructor_template()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return {};
  }
  return MakeBaseObject<HeapSnapshotStream>(env, std::move(snapshot), obj);
}

HeapProfiler::HeapSnapshotOptions GetHeapSnapshotOptions(
    Local<Value> options_value) {
  CHECK(options_value->IsUint8Array());
  Local<Uint8Array> arr = options_value.As<Uint8Array>();
  uint8_t* options =
      static_cast<uint8_t*>(arr->Buffer()->Data()) + arr->ByteOffset();
  HeapProfiler::HeapSnapshotOptions result;
  result.snapshot_mode = options[0]
                             ? HeapProfiler::HeapSnapshotMode::kExposeInternals
                             : HeapProfiler::HeapSnapshotMode::kRegular;
  result.numerics_mode = options[1]
                             ? HeapProfiler::NumericsMode::kExposeNumericValues
                             : HeapProfiler::NumericsMode::kHideNumericValues;
  return result;
}

void CreateHeapSnapshotStream(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_EQ(args.Length(), 1);
  auto options = GetHeapSnapshotOptions(args[0]);
  HeapSnapshotPointer snapshot{
      env->isolate()->GetHeapProfiler()->TakeHeapSnapshot(options)};
  CHECK(snapshot);
  BaseObjectPtr<AsyncWrap> stream =
      CreateHeapSnapshotStream(env, std::move(snapshot));
  if (stream)
    args.GetReturnValue().Set(stream->object());
}

void TriggerHeapSnapshot(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = args.GetIsolate();
  CHECK_EQ(args.Length(), 2);
  Local<Value> filename_v = args[0];
  auto options = GetHeapSnapshotOptions(args[1]);

  if (filename_v->IsUndefined()) {
    DiagnosticFilename name(env, "Heap", "heapsnapshot");
    THROW_IF_INSUFFICIENT_PERMISSIONS(
        env,
        permission::PermissionScope::kFileSystemWrite,
        Environment::GetCwd(env->exec_path()));
    if (WriteSnapshot(env, *name, options).IsNothing()) return;
    if (String::NewFromUtf8(isolate, *name).ToLocal(&filename_v)) {
      args.GetReturnValue().Set(filename_v);
    }
    return;
  }

  BufferValue path(isolate, filename_v);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemWrite, path.ToStringView());
  if (WriteSnapshot(env, *path, options).IsNothing()) return;
  return args.GetReturnValue().Set(filename_v);
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  SetMethod(context, target, "buildEmbedderGraph", BuildEmbedderGraph);
  SetMethod(context, target, "triggerHeapSnapshot", TriggerHeapSnapshot);
  SetMethod(
      context, target, "createHeapSnapshotStream", CreateHeapSnapshotStream);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(BuildEmbedderGraph);
  registry->Register(TriggerHeapSnapshot);
  registry->Register(CreateHeapSnapshotStream);
}

}  // namespace heap
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(heap_utils, node::heap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(heap_utils,
                                node::heap::RegisterExternalReferences)
