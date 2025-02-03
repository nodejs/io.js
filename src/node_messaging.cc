#include "node_messaging.h"

#include "async_wrap-inl.h"
#include "debug_utils-inl.h"
#include "memory_tracker-inl.h"
#include "node_buffer.h"
#include "node_contextify.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_process-inl.h"
#include "util-inl.h"

using node::contextify::ContextifyContext;
using node::errors::TryCatchScope;
using v8::Array;
using v8::ArrayBuffer;
using v8::BackingStore;
using v8::CompiledWasmModule;
using v8::Context;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::HandleScope;
using v8::Isolate;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::ObjectTemplate;
using v8::SharedArrayBuffer;
using v8::SharedValueConveyor;
using v8::String;
using v8::Symbol;
using v8::Value;
using v8::ValueDeserializer;
using v8::ValueSerializer;
using v8::WasmModuleObject;

namespace node {

using BaseObjectPtrList = std::vector<BaseObjectPtr<BaseObject>>;
using TransferMode = BaseObject::TransferMode;

// Hack to have WriteHostObject inform ReadHostObject that the value
// should be treated as a regular JS object. Used to transfer process.env.
static const uint32_t kNormalObject = static_cast<uint32_t>(-1);

namespace worker {

Maybe<bool> TransferData::FinalizeTransferWrite(
    Local<Context> context, ValueSerializer* serializer) {
  return Just(true);
}

Message::Message(MallocedBuffer<char>&& buffer)
    : main_message_buf_(std::move(buffer)) {}

bool Message::IsCloseMessage() const {
  return main_message_buf_.data == nullptr;
}

namespace {

// This is used to tell V8 how to read transferred host objects, like other
// `MessagePort`s and `SharedArrayBuffer`s, and make new JS objects out of them.
class DeserializerDelegate : public ValueDeserializer::Delegate {
 public:
  DeserializerDelegate(
      Message* m,
      Environment* env,
      const std::vector<BaseObjectPtr<BaseObject>>& host_objects,
      const std::vector<Local<SharedArrayBuffer>>& shared_array_buffers,
      const std::vector<CompiledWasmModule>& wasm_modules,
      const std::optional<SharedValueConveyor>& shared_value_conveyor)
      : env_(env),
        host_objects_(host_objects),
        shared_array_buffers_(shared_array_buffers),
        wasm_modules_(wasm_modules),
        shared_value_conveyor_(shared_value_conveyor) {}

  MaybeLocal<Object> ReadHostObject(Isolate* isolate) override {
    // Identifying the index in the message's BaseObject array is sufficient.
    uint32_t id;
    if (!deserializer->ReadUint32(&id))
      return MaybeLocal<Object>();
    if (id != kNormalObject) {
      CHECK_LT(id, host_objects_.size());
      Local<Object> object = host_objects_[id]->object(isolate);
      if (env_->js_transferable_constructor_template()->HasInstance(object)) {
        return BaseObject::Unwrap<JSTransferable>(object)->target();
      } else {
        return object;
      }
    }
    EscapableHandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Value> object;
    if (!deserializer->ReadValue(context).ToLocal(&object))
      return MaybeLocal<Object>();
    CHECK(object->IsObject());
    return scope.Escape(object.As<Object>());
  }

  MaybeLocal<SharedArrayBuffer> GetSharedArrayBufferFromId(
      Isolate* isolate, uint32_t clone_id) override {
    CHECK_LT(clone_id, shared_array_buffers_.size());
    return shared_array_buffers_[clone_id];
  }

  MaybeLocal<WasmModuleObject> GetWasmModuleFromId(
      Isolate* isolate, uint32_t transfer_id) override {
    CHECK_LT(transfer_id, wasm_modules_.size());
    return WasmModuleObject::FromCompiledModule(
        isolate, wasm_modules_[transfer_id]);
  }

  const SharedValueConveyor* GetSharedValueConveyor(Isolate* isolate) override {
    CHECK(shared_value_conveyor_.has_value());
    return &shared_value_conveyor_.value();
  }

  ValueDeserializer* deserializer = nullptr;

 private:
  Environment* env_;
  const std::vector<BaseObjectPtr<BaseObject>>& host_objects_;
  const std::vector<Local<SharedArrayBuffer>>& shared_array_buffers_;
  const std::vector<CompiledWasmModule>& wasm_modules_;
  const std::optional<SharedValueConveyor>& shared_value_conveyor_;
};

}  // anonymous namespace

MaybeLocal<Value> Message::Deserialize(Environment* env,
                                       Local<Context> context,
                                       Local<Value>* port_list) {
  Context::Scope context_scope(context);

  CHECK(!IsCloseMessage());
  if (port_list != nullptr && !transferables_.empty()) {
    // Need to create this outside of the EscapableHandleScope, but inside
    // the Context::Scope.
    *port_list = Array::New(env->isolate());
  }

  EscapableHandleScope handle_scope(env->isolate());

  // Create all necessary objects for transferables, e.g. MessagePort handles.
  std::vector<BaseObjectPtr<BaseObject>> host_objects(transferables_.size());
  auto cleanup = OnScopeLeave([&]() {
    for (BaseObjectPtr<BaseObject> object : host_objects) {
      if (!object) continue;

      // If the function did not finish successfully, host_objects will contain
      // a list of objects that will never be passed to JS. Therefore, we
      // destroy them here.
      object->Detach();
    }
  });

  for (uint32_t i = 0; i < transferables_.size(); ++i) {
    HandleScope handle_scope(env->isolate());
    TransferData* data = transferables_[i].get();
    host_objects[i] = data->Deserialize(
        env, context, std::move(transferables_[i]));
    if (!host_objects[i]) return {};
    if (port_list != nullptr) {
      // If we gather a list of all message ports, and this transferred object
      // is a message port, add it to that list. This is a bit of an odd case
      // of special handling for MessagePorts (as opposed to applying to all
      // transferables), but it's required for spec compliance.
      DCHECK((*port_list)->IsArray());
      Local<Array> port_list_array = port_list->As<Array>();
      Local<Object> obj = host_objects[i]->object();
      if (env->message_port_constructor_template()->HasInstance(obj)) {
        if (port_list_array->Set(context,
                                 port_list_array->Length(),
                                 obj).IsNothing()) {
          return {};
        }
      }
    }
  }
  transferables_.clear();

  std::vector<Local<SharedArrayBuffer>> shared_array_buffers;
  // Attach all transferred SharedArrayBuffers to their new Isolate.
  for (uint32_t i = 0; i < shared_array_buffers_.size(); ++i) {
    Local<SharedArrayBuffer> sab =
        SharedArrayBuffer::New(env->isolate(), shared_array_buffers_[i]);
    shared_array_buffers.push_back(sab);
  }

  DeserializerDelegate delegate(this,
                                env,
                                host_objects,
                                shared_array_buffers,
                                wasm_modules_,
                                shared_value_conveyor_);
  ValueDeserializer deserializer(
      env->isolate(),
      reinterpret_cast<const uint8_t*>(main_message_buf_.data),
      main_message_buf_.size,
      &delegate);
  delegate.deserializer = &deserializer;

  // Attach all transferred ArrayBuffers to their new Isolate.
  for (uint32_t i = 0; i < array_buffers_.size(); ++i) {
    Local<ArrayBuffer> ab =
        ArrayBuffer::New(env->isolate(), std::move(array_buffers_[i]));
    deserializer.TransferArrayBuffer(i, ab);
  }

  if (deserializer.ReadHeader(context).IsNothing())
    return {};
  Local<Value> return_value;
  if (!deserializer.ReadValue(context).ToLocal(&return_value))
    return {};

  for (BaseObjectPtr<BaseObject> base_object : host_objects) {
    if (base_object->FinalizeTransferRead(context, &deserializer).IsNothing())
      return {};
  }

  host_objects.clear();
  return handle_scope.Escape(return_value);
}

void Message::AddSharedArrayBuffer(
    std::shared_ptr<BackingStore> backing_store) {
  shared_array_buffers_.emplace_back(std::move(backing_store));
}

void Message::AddTransferable(std::unique_ptr<TransferData>&& data) {
  transferables_.emplace_back(std::move(data));
}

uint32_t Message::AddWASMModule(CompiledWasmModule&& mod) {
  wasm_modules_.emplace_back(std::move(mod));
  return wasm_modules_.size() - 1;
}

void Message::AdoptSharedValueConveyor(SharedValueConveyor&& conveyor) {
  shared_value_conveyor_.emplace(std::move(conveyor));
}

namespace {

MaybeLocal<Function> GetEmitMessageFunction(Local<Context> context) {
  Isolate* isolate = context->GetIsolate();
  Local<Object> per_context_bindings;
  Local<Value> emit_message_val;
  if (!GetPerContextExports(context).ToLocal(&per_context_bindings) ||
      !per_context_bindings->Get(context,
                                FIXED_ONE_BYTE_STRING(isolate, "emitMessage"))
          .ToLocal(&emit_message_val)) {
    return MaybeLocal<Function>();
  }
  CHECK(emit_message_val->IsFunction());
  return emit_message_val.As<Function>();
}

MaybeLocal<Function> GetDOMException(Local<Context> context) {
  Isolate* isolate = context->GetIsolate();
  Local<Object> per_context_bindings;
  Local<Value> domexception_ctor_val;
  if (!GetPerContextExports(context).ToLocal(&per_context_bindings) ||
      !per_context_bindings->Get(context,
                                FIXED_ONE_BYTE_STRING(isolate, "DOMException"))
          .ToLocal(&domexception_ctor_val)) {
    return MaybeLocal<Function>();
  }
  CHECK(domexception_ctor_val->IsFunction());
  Local<Function> domexception_ctor = domexception_ctor_val.As<Function>();
  return domexception_ctor;
}

void ThrowDataCloneException(Local<Context> context, Local<String> message) {
  Isolate* isolate = context->GetIsolate();
  Local<Value> argv[] = {message,
                         FIXED_ONE_BYTE_STRING(isolate, "DataCloneError")};
  Local<Value> exception;
  Local<Function> domexception_ctor;
  if (!GetDOMException(context).ToLocal(&domexception_ctor) ||
      !domexception_ctor->NewInstance(context, arraysize(argv), argv)
           .ToLocal(&exception)) {
    return;
  }
  isolate->ThrowException(exception);
}

// This tells V8 how to serialize objects that it does not understand
// (e.g. C++ objects) into the output buffer, in a way that our own
// DeserializerDelegate understands how to unpack.
class SerializerDelegate : public ValueSerializer::Delegate {
 public:
  SerializerDelegate(Environment* env, Local<Context> context, Message* m)
      : env_(env), context_(context), msg_(m) {}

  void ThrowDataCloneError(Local<String> message) override {
    ThrowDataCloneException(context_, message);
  }

  bool HasCustomHostObject(Isolate* isolate) override { return true; }

  Maybe<bool> IsHostObject(Isolate* isolate, Local<Object> object) override {
    if (BaseObject::IsBaseObject(env_->isolate_data(), object)) {
      return Just(true);
    }

    return Just(JSTransferable::IsJSTransferable(env_, context_, object));
  }

  Maybe<bool> WriteHostObject(Isolate* isolate, Local<Object> object) override {
    if (BaseObject::IsBaseObject(env_->isolate_data(), object)) {
      return WriteHostObject(
          BaseObjectPtr<BaseObject>{BaseObject::Unwrap<BaseObject>(object)});
    }

    if (JSTransferable::IsJSTransferable(env_, context_, object)) {
      BaseObjectPtr<JSTransferable> js_transferable =
          JSTransferable::Wrap(env_, object);
      return WriteHostObject(js_transferable);
    }

    // Convert process.env to a regular object.
    auto env_proxy_ctor_template = env_->env_proxy_ctor_template();
    if (!env_proxy_ctor_template.IsEmpty() &&
        env_proxy_ctor_template->HasInstance(object)) {
      HandleScope scope(isolate);
      // TODO(bnoordhuis) Prototype-less object in case process.env contains
      // a "__proto__" key? process.env has a prototype with concomitant
      // methods like toString(). It's probably confusing if that gets lost
      // in transmission.
      Local<Object> normal_object = Object::New(isolate);
      if (env_->env_vars()
              ->AssignToObject(isolate, env_->context(), normal_object)
              .IsNothing()) {
        return Nothing<bool>();
      }
      serializer->WriteUint32(kNormalObject);  // Instead of a BaseObject.
      return serializer->WriteValue(env_->context(), normal_object);
    }

    ThrowDataCloneError(env_->clone_unsupported_type_str());
    return Nothing<bool>();
  }

  Maybe<uint32_t> GetSharedArrayBufferId(
      Isolate* isolate,
      Local<SharedArrayBuffer> shared_array_buffer) override {
    uint32_t i;
    for (i = 0; i < seen_shared_array_buffers_.size(); ++i) {
      if (PersistentToLocal::Strong(seen_shared_array_buffers_[i]) ==
          shared_array_buffer) {
        return Just(i);
      }
    }

    seen_shared_array_buffers_.emplace_back(
      Global<SharedArrayBuffer> { isolate, shared_array_buffer });
    msg_->AddSharedArrayBuffer(shared_array_buffer->GetBackingStore());
    return Just(i);
  }

  Maybe<uint32_t> GetWasmModuleTransferId(
      Isolate* isolate, Local<WasmModuleObject> module) override {
    return Just(msg_->AddWASMModule(module->GetCompiledModule()));
  }

  bool AdoptSharedValueConveyor(Isolate* isolate,
                                SharedValueConveyor&& conveyor) override {
    msg_->AdoptSharedValueConveyor(std::move(conveyor));
    return true;
  }

  Maybe<bool> Finish(Local<Context> context) {
    for (uint32_t i = 0; i < host_objects_.size(); i++) {
      BaseObjectPtr<BaseObject> host_object = std::move(host_objects_[i]);
      std::unique_ptr<TransferData> data;
      if (i < first_cloned_object_index_) {
        data = host_object->TransferForMessaging();
      } else {
        data = host_object->CloneForMessaging();
      }
      if (!data) return Nothing<bool>();
      if (data->FinalizeTransferWrite(context, serializer).IsNothing())
        return Nothing<bool>();
      msg_->AddTransferable(std::move(data));
    }
    return Just(true);
  }

  inline void AddHostObject(BaseObjectPtr<BaseObject> host_object) {
    // Make sure we have not started serializing the value itself yet.
    CHECK_EQ(first_cloned_object_index_, SIZE_MAX);
    host_objects_.emplace_back(std::move(host_object));
  }

  // Some objects in the transfer list may register sub-objects that can be
  // transferred. This could e.g. be a public JS wrapper object, such as a
  // FileHandle, that is registering its C++ handle for transfer.
  inline Maybe<bool> AddNestedHostObjects() {
    for (size_t i = 0; i < host_objects_.size(); i++) {
      std::vector<BaseObjectPtr<BaseObject>> nested_transferables;
      if (!host_objects_[i]->NestedTransferables().To(&nested_transferables))
        return Nothing<bool>();
      for (auto& nested_transferable : nested_transferables) {
        if (std::find(host_objects_.begin(),
                      host_objects_.end(),
                      nested_transferable) == host_objects_.end()) {
          AddHostObject(nested_transferable);
        }
      }
    }
    return Just(true);
  }

  ValueSerializer* serializer = nullptr;

 private:
  Maybe<bool> WriteHostObject(BaseObjectPtr<BaseObject> host_object) {
    BaseObject::TransferMode mode = host_object->GetTransferMode();
    if (mode == TransferMode::kDisallowCloneAndTransfer) {
      ThrowDataCloneError(env_->clone_unsupported_type_str());
      return Nothing<bool>();
    }

    if (mode & TransferMode::kTransferable) {
      for (uint32_t i = 0; i < host_objects_.size(); i++) {
        if (host_objects_[i] == host_object) {
          serializer->WriteUint32(i);
          return Just(true);
        }
      }
      ThrowDataCloneError(env_->clone_transfer_needed_str());
      return Nothing<bool>();
    }

    uint32_t index = host_objects_.size();
    if (first_cloned_object_index_ == SIZE_MAX)
      first_cloned_object_index_ = index;
    serializer->WriteUint32(index);
    host_objects_.push_back(host_object);
    return Just(true);
  }

  Environment* env_;
  Local<Context> context_;
  Message* msg_;
  std::vector<Global<SharedArrayBuffer>> seen_shared_array_buffers_;
  std::vector<BaseObjectPtr<BaseObject>> host_objects_;
  size_t first_cloned_object_index_ = SIZE_MAX;

  friend class worker::Message;
};

}  // anonymous namespace

Maybe<bool> Message::Serialize(Environment* env,
                               Local<Context> context,
                               Local<Value> input,
                               const TransferList& transfer_list_v,
                               Local<Object> source_port) {
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(context);

  // Verify that we're not silently overwriting an existing message.
  CHECK(main_message_buf_.is_empty());

  SerializerDelegate delegate(env, context, this);
  ValueSerializer serializer(env->isolate(), &delegate);
  delegate.serializer = &serializer;

  std::vector<Local<ArrayBuffer>> array_buffers;
  for (uint32_t i = 0; i < transfer_list_v.length(); ++i) {
    Local<Value> entry_val = transfer_list_v[i];
    if (!entry_val->IsObject()) {
      // Only object can be transferred.
      ThrowDataCloneException(context, env->clone_untransferable_str());
      return Nothing<bool>();
    }
    Local<Object> entry = entry_val.As<Object>();
    // See https://github.com/nodejs/node/pull/30339#issuecomment-552225353
    // for details.
    if (entry->HasPrivate(context, env->untransferable_object_private_symbol())
            .ToChecked()) {
      ThrowDataCloneException(context, env->transfer_unsupported_type_str());
      return Nothing<bool>();
    }

    // Currently, we support ArrayBuffers and BaseObjects for which
    // GetTransferMode() returns kTransferable.
    if (entry->IsArrayBuffer()) {
      Local<ArrayBuffer> ab = entry.As<ArrayBuffer>();
      // If we cannot render the ArrayBuffer unusable in this Isolate,
      // copying the buffer will have to do.
      // Note that we can currently transfer ArrayBuffers even if they were
      // not allocated by Node’s ArrayBufferAllocator in the first place,
      // because we pass the underlying v8::BackingStore around rather than
      // raw data *and* an Isolate with a non-default ArrayBuffer allocator
      // is always going to outlive any Workers it creates, and so will its
      // allocator along with it.
      if (!ab->IsDetachable() || ab->WasDetached()) {
        ThrowDataCloneException(context, env->transfer_unsupported_type_str());
        return Nothing<bool>();
      }
      if (std::find(array_buffers.begin(), array_buffers.end(), ab) !=
          array_buffers.end()) {
        ThrowDataCloneException(
            context,
            FIXED_ONE_BYTE_STRING(
                env->isolate(),
                "Transfer list contains duplicate ArrayBuffer"));
        return Nothing<bool>();
      }
      // We simply use the array index in the `array_buffers` list as the
      // ID that we write into the serialized buffer.
      uint32_t id = array_buffers.size();
      array_buffers.push_back(ab);
      serializer.TransferArrayBuffer(id, ab);
      continue;
    }

    // Check if the source MessagePort is being transferred.
    if (!source_port.IsEmpty() && entry == source_port) {
      ThrowDataCloneException(
          context,
          FIXED_ONE_BYTE_STRING(env->isolate(),
                                "Transfer list contains source port"));
      return Nothing<bool>();
    }
    BaseObjectPtr<BaseObject> host_object;
    if (BaseObject::IsBaseObject(env->isolate_data(), entry)) {
      host_object =
          BaseObjectPtr<BaseObject>{BaseObject::Unwrap<BaseObject>(entry)};
    } else {
      if (!JSTransferable::IsJSTransferable(env, context, entry)) {
        ThrowDataCloneException(context, env->clone_untransferable_str());
        return Nothing<bool>();
      }
      host_object = JSTransferable::Wrap(env, entry);
    }

    if (env->message_port_constructor_template()->HasInstance(entry) &&
        (!host_object ||
         static_cast<MessagePort*>(host_object.get())->IsDetached())) {
      ThrowDataCloneException(
          context,
          FIXED_ONE_BYTE_STRING(
              env->isolate(),
              "MessagePort in transfer list is already detached"));
      return Nothing<bool>();
    }
    if (std::find(delegate.host_objects_.begin(),
                  delegate.host_objects_.end(),
                  host_object) != delegate.host_objects_.end()) {
      ThrowDataCloneException(
          context,
          String::Concat(
              env->isolate(),
              FIXED_ONE_BYTE_STRING(env->isolate(),
                                    "Transfer list contains duplicate "),
              entry->GetConstructorName()));
      return Nothing<bool>();
    }
    if (host_object &&
        host_object->GetTransferMode() == TransferMode::kTransferable) {
      delegate.AddHostObject(host_object);
    } else {
      ThrowDataCloneException(context, env->clone_untransferable_str());
      return Nothing<bool>();
    }
  }
  if (delegate.AddNestedHostObjects().IsNothing())
    return Nothing<bool>();

  serializer.WriteHeader();
  if (serializer.WriteValue(context, input).IsNothing()) {
    return Nothing<bool>();
  }

  for (Local<ArrayBuffer> ab : array_buffers) {
    // If serialization succeeded, we render it inaccessible in this Isolate.
    std::shared_ptr<BackingStore> backing_store = ab->GetBackingStore();
    ab->Detach(Local<Value>()).Check();

    array_buffers_.emplace_back(std::move(backing_store));
  }

  if (delegate.Finish(context).IsNothing())
    return Nothing<bool>();

  // The serializer gave us a buffer allocated using `malloc()`.
  std::pair<uint8_t*, size_t> data = serializer.Release();
  CHECK_NOT_NULL(data.first);
  main_message_buf_ =
      MallocedBuffer<char>(reinterpret_cast<char*>(data.first), data.second);
  return Just(true);
}

void Message::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("array_buffers_", array_buffers_);
  tracker->TrackField("shared_array_buffers", shared_array_buffers_);
  tracker->TrackField("transferables", transferables_);
}

MessagePortData::MessagePortData(MessagePort* owner)
    : owner_(owner) {
}

MessagePortData::~MessagePortData() {
  CHECK_NULL(owner_);
  Disentangle();
}

void MessagePortData::MemoryInfo(MemoryTracker* tracker) const {
  Mutex::ScopedLock lock(mutex_);
  tracker->TrackField("incoming_messages", incoming_messages_);
}

void MessagePortData::AddToIncomingQueue(std::shared_ptr<Message> message) {
  // This function will be called by other threads.
  Mutex::ScopedLock lock(mutex_);
  incoming_messages_.emplace_back(std::move(message));

  if (owner_ != nullptr) {
    Debug(owner_, "Adding message to incoming queue");
    owner_->TriggerAsync();
  }
}

void MessagePortData::Entangle(MessagePortData* a, MessagePortData* b) {
  auto group = std::make_shared<SiblingGroup>();
  group->Entangle({a, b});
}

void MessagePortData::Disentangle() {
  if (group_) {
    group_->Disentangle(this);
  }
}

MessagePort::~MessagePort() {
  if (data_) Detach();
}

MessagePort::MessagePort(Environment* env,
                         Local<Context> context,
                         Local<Object> wrap)
  : HandleWrap(env,
               wrap,
               reinterpret_cast<uv_handle_t*>(&async_),
               AsyncWrap::PROVIDER_MESSAGEPORT),
    data_(new MessagePortData(this)) {
  auto onmessage = [](uv_async_t* handle) {
    // Called when data has been put into the queue.
    MessagePort* channel = ContainerOf(&MessagePort::async_, handle);
    channel->OnMessage(MessageProcessingMode::kNormalOperation);
  };

  CHECK_EQ(uv_async_init(env->event_loop(),
                         &async_,
                         onmessage), 0);
  // Reset later to indicate success of the constructor.
  bool succeeded = false;
  auto cleanup = OnScopeLeave([&]() { if (!succeeded) Close(); });

  Local<Value> fn;
  if (!wrap->Get(context, env->oninit_symbol()).ToLocal(&fn))
    return;

  if (fn->IsFunction()) {
    Local<Function> init = fn.As<Function>();
    if (init->Call(context, wrap, 0, nullptr).IsEmpty())
      return;
  }

  Local<Function> emit_message_fn;
  if (!GetEmitMessageFunction(context).ToLocal(&emit_message_fn))
    return;
  emit_message_fn_.Reset(env->isolate(), emit_message_fn);

  succeeded = true;
  Debug(this, "Created message port");
}

bool MessagePort::IsDetached() const {
  return data_ == nullptr || IsHandleClosing();
}

void MessagePort::TriggerAsync() {
  if (IsHandleClosing()) return;
  CHECK_EQ(uv_async_send(&async_), 0);
}

void MessagePort::Close(v8::Local<v8::Value> close_callback) {
  Debug(this, "Closing message port, data set = %d", static_cast<int>(!!data_));

  if (data_) {
    // Wrap this call with accessing the mutex, so that TriggerAsync()
    // can check IsHandleClosing() without race conditions.
    Mutex::ScopedLock sibling_lock(data_->mutex_);
    HandleWrap::Close(close_callback);
  } else {
    HandleWrap::Close(close_callback);
  }
}

void MessagePort::New(const FunctionCallbackInfo<Value>& args) {
  // This constructor just throws an error. Unfortunately, we can’t use V8’s
  // ConstructorBehavior::kThrow, as that also removes the prototype from the
  // class (i.e. makes it behave like an arrow function).
  Environment* env = Environment::GetCurrent(args);
  THROW_ERR_CONSTRUCT_CALL_INVALID(env);
}

MessagePort* MessagePort::New(
    Environment* env,
    Local<Context> context,
    std::unique_ptr<MessagePortData> data,
    std::shared_ptr<SiblingGroup> sibling_group) {
  Context::Scope context_scope(context);
  Local<FunctionTemplate> ctor_templ =
      GetMessagePortConstructorTemplate(env->isolate_data());

  // Construct a new instance, then assign the listener instance and possibly
  // the MessagePortData to it.
  Local<Object> instance;
  if (!ctor_templ->InstanceTemplate()->NewInstance(context).ToLocal(&instance))
    return nullptr;
  MessagePort* port = new MessagePort(env, context, instance);
  CHECK_NOT_NULL(port);
  if (port->IsHandleClosing()) {
    // Construction failed with an exception.
    return nullptr;
  }

  if (data) {
    CHECK(!sibling_group);
    port->Detach();
    port->data_ = std::move(data);

    // This lock is here to avoid race conditions with the `owner_` read
    // in AddToIncomingQueue(). (This would likely be unproblematic without it,
    // but it's better to be safe than sorry.)
    Mutex::ScopedLock lock(port->data_->mutex_);
    port->data_->owner_ = port;
    // If the existing MessagePortData object had pending messages, this is
    // the easiest way to run that queue.
    port->TriggerAsync();
  } else if (sibling_group) {
    sibling_group->Entangle(port->data_.get());
  }
  return port;
}

MaybeLocal<Value> MessagePort::ReceiveMessage(Local<Context> context,
                                              MessageProcessingMode mode,
                                              Local<Value>* port_list) {
  std::shared_ptr<Message> received;
  {
    // Get the head of the message queue.
    Mutex::ScopedLock lock(data_->mutex_);

    Debug(this, "MessagePort has message");

    bool wants_message =
        receiving_messages_ ||
        mode == MessageProcessingMode::kForceReadMessages;
    // We have nothing to do if:
    // - There are no pending messages
    // - We are not intending to receive messages, and the message we would
    //   receive is not the final "close" message.
    if (data_->incoming_messages_.empty() ||
        (!wants_message &&
         !data_->incoming_messages_.front()->IsCloseMessage())) {
      return env()->no_message_symbol();
    }

    received = data_->incoming_messages_.front();
    data_->incoming_messages_.pop_front();
  }

  if (received->IsCloseMessage()) {
    Close();
    return env()->no_message_symbol();
  }

  if (!env()->can_call_into_js()) return MaybeLocal<Value>();

  return received->Deserialize(env(), context, port_list);
}

void MessagePort::OnMessage(MessageProcessingMode mode) {
  Debug(this, "Running MessagePort::OnMessage()");
  // Maybe the async handle was triggered empty or more than needed.
  // The data_ could be freed or, the handle has been/is being closed.
  // A possible case for this, is transfer the MessagePort to another
  // context, it will call the constructor and trigger the async handle empty.
  // Because all data was sent from the previous context.
  if (IsDetached()) return;

  HandleScope handle_scope(env()->isolate());
  Local<Context> context =
      object(env()->isolate())->GetCreationContextChecked();

  size_t processing_limit;
  if (mode == MessageProcessingMode::kNormalOperation) {
    Mutex::ScopedLock lock(data_->mutex_);
    processing_limit = std::max(data_->incoming_messages_.size(),
                                static_cast<size_t>(1000));
  } else {
    processing_limit = std::numeric_limits<size_t>::max();
  }

  // data_ can only ever be modified by the owner thread, so no need to lock.
  // However, the message port may be transferred while it is processing
  // messages, so we need to check that this handle still owns its `data_` field
  // on every iteration.
  while (data_) {
    if (processing_limit-- == 0) {
      // Prevent event loop starvation by only processing those messages without
      // interruption that were already present when the OnMessage() call was
      // first triggered, but at least 1000 messages because otherwise the
      // overhead of repeatedly triggering the uv_async_t instance becomes
      // noticeable, at least on Windows.
      // (That might require more investigation by somebody more familiar with
      // Windows.)
      TriggerAsync();
      return;
    }

    HandleScope handle_scope(env()->isolate());
    Context::Scope context_scope(context);
    Local<Function> emit_message = PersistentToLocal::Strong(emit_message_fn_);

    Local<Value> payload;
    Local<Value> port_list = Undefined(env()->isolate());
    Local<Value> message_error;
    Local<Value> argv[3];

    {
      // Catch any exceptions from parsing the message itself (not from
      // emitting it) as 'messageeror' events.
      TryCatchScope try_catch(env());
      if (!ReceiveMessage(context, mode, &port_list).ToLocal(&payload)) {
        if (try_catch.HasCaught() && !try_catch.HasTerminated())
          message_error = try_catch.Exception();
        goto reschedule;
      }
    }
    if (payload == env()->no_message_symbol()) break;

    if (!env()->can_call_into_js()) {
      Debug(this, "MessagePort drains queue because !can_call_into_js()");
      // In this case there is nothing to do but to drain the current queue.
      continue;
    }

    argv[0] = payload;
    argv[1] = port_list;
    argv[2] = env()->message_string();

    if (MakeCallback(emit_message, arraysize(argv), argv).IsEmpty()) {
    reschedule:
      if (!message_error.IsEmpty()) {
        argv[0] = message_error;
        argv[1] = Undefined(env()->isolate());
        argv[2] = env()->messageerror_string();
        USE(MakeCallback(emit_message, arraysize(argv), argv));
      }

      // Re-schedule OnMessage() execution in case of failure.
      if (data_)
        TriggerAsync();
      return;
    }
  }
}

void MessagePort::OnClose() {
  Debug(this, "MessagePort::OnClose()");
  if (data_) {
    // Detach() returns move(data_).
    Detach()->Disentangle();
  }
}

std::unique_ptr<MessagePortData> MessagePort::Detach() {
  CHECK(data_);
  Mutex::ScopedLock lock(data_->mutex_);
  data_->owner_ = nullptr;
  return std::move(data_);
}

BaseObject::TransferMode MessagePort::GetTransferMode() const {
  if (IsDetached()) return TransferMode::kDisallowCloneAndTransfer;
  return TransferMode::kTransferable;
}

std::unique_ptr<TransferData> MessagePort::TransferForMessaging() {
  Close();
  return Detach();
}

BaseObjectPtr<BaseObject> MessagePortData::Deserialize(
    Environment* env,
    Local<Context> context,
    std::unique_ptr<TransferData> self) {
  return BaseObjectPtr<MessagePort> { MessagePort::New(
      env, context,
      static_unique_pointer_cast<MessagePortData>(std::move(self))) };
}

Maybe<bool> MessagePort::PostMessage(Environment* env,
                                     Local<Context> context,
                                     Local<Value> message_v,
                                     const TransferList& transfer_v) {
  Isolate* isolate = env->isolate();
  Local<Object> obj = object(isolate);
  TryCatchScope try_catch(env);

  std::shared_ptr<Message> msg = std::make_shared<Message>();

  // Per spec, we need to both check if transfer list has the source port, and
  // serialize the input message, even if the MessagePort is closed or detached.

  Maybe<bool> serialization_maybe =
      msg->Serialize(env, context, message_v, transfer_v, obj);
  if (try_catch.HasCaught() && !try_catch.HasTerminated()) {
    try_catch.ReThrow();
  }
  if (data_ == nullptr) {
    return serialization_maybe;
  }
  if (serialization_maybe.IsNothing()) {
    return Nothing<bool>();
  }

  std::string error;
  Maybe<bool> res = data_->Dispatch(msg, &error);
  if (res.IsNothing())
    return res;

  if (!error.empty())
    ProcessEmitWarning(env, error.c_str());

  return res;
}

Maybe<bool> MessagePortData::Dispatch(
    std::shared_ptr<Message> message,
    std::string* error) {
  if (!group_) {
    if (error != nullptr)
      *error = "MessagePortData is not entangled.";
    return Nothing<bool>();
  }
  return group_->Dispatch(this, message, error);
}

static Maybe<bool> ReadIterable(Environment* env,
                                Local<Context> context,
                                // NOLINTNEXTLINE(runtime/references)
                                TransferList& transfer_list,
                                Local<Value> object) {
  if (!object->IsObject()) return Just(false);

  if (object->IsArray()) {
    Local<Array> arr = object.As<Array>();
    size_t length = arr->Length();
    transfer_list.AllocateSufficientStorage(length);
    for (size_t i = 0; i < length; i++) {
      if (!arr->Get(context, i).ToLocal(&transfer_list[i]))
        return Nothing<bool>();
    }
    return Just(true);
  }

  Isolate* isolate = env->isolate();
  Local<Value> iterator_method;
  if (!object.As<Object>()->Get(context, Symbol::GetIterator(isolate))
      .ToLocal(&iterator_method)) return Nothing<bool>();
  if (!iterator_method->IsFunction()) return Just(false);

  Local<Value> iterator;
  if (!iterator_method.As<Function>()->Call(context, object, 0, nullptr)
      .ToLocal(&iterator)) return Nothing<bool>();
  if (!iterator->IsObject()) return Just(false);

  Local<Value> next;
  if (!iterator.As<Object>()->Get(context, env->next_string()).ToLocal(&next))
    return Nothing<bool>();
  if (!next->IsFunction()) return Just(false);

  std::vector<Local<Value>> entries;
  while (env->can_call_into_js()) {
    Local<Value> result;
    if (!next.As<Function>()->Call(context, iterator, 0, nullptr)
        .ToLocal(&result)) return Nothing<bool>();
    if (!result->IsObject()) return Just(false);

    Local<Value> done;
    if (!result.As<Object>()->Get(context, env->done_string()).ToLocal(&done))
      return Nothing<bool>();
    if (done->BooleanValue(isolate)) break;

    Local<Value> val;
    if (!result.As<Object>()->Get(context, env->value_string()).ToLocal(&val))
      return Nothing<bool>();
    entries.push_back(val);
  }

  if (!entries.empty()) {
    transfer_list.AllocateSufficientStorage(entries.size());
    std::copy(entries.begin(), entries.end(), &transfer_list[0]);
  }

  return Just(true);
}

bool GetTransferList(Environment* env,
                     Local<Context> context,
                     Local<Value> transfer_list_v,
                     TransferList* transfer_list_out) {
  if (transfer_list_v->IsNullOrUndefined()) {
    // Browsers ignore null or undefined, and otherwise accept an array or an
    // options object.
    return true;
  }

  if (!transfer_list_v->IsObject()) {
    THROW_ERR_INVALID_ARG_TYPE(
        env, "Optional transferList argument must be an iterable");
    return false;
  }

  bool was_iterable;
  if (!ReadIterable(env, context, *transfer_list_out, transfer_list_v)
           .To(&was_iterable))
    return false;
  if (!was_iterable) {
    Local<Value> transfer_option;
    if (!transfer_list_v.As<Object>()
             ->Get(context, env->transfer_string())
             .ToLocal(&transfer_option))
      return false;
    if (!transfer_option->IsUndefined()) {
      if (!ReadIterable(env, context, *transfer_list_out, transfer_option)
               .To(&was_iterable))
        return false;
      if (!was_iterable) {
        THROW_ERR_INVALID_ARG_TYPE(
            env, "Optional options.transfer argument must be an iterable");
        return false;
      }
    }
  }

  return true;
}

void MessagePort::PostMessage(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Object> obj = args.This();
  Local<Context> context = obj->GetCreationContextChecked();

  if (args.Length() == 0) {
    return THROW_ERR_MISSING_ARGS(env, "Not enough arguments to "
                                       "MessagePort.postMessage");
  }

  TransferList transfer_list;
  if (!GetTransferList(env, context, args[1], &transfer_list)) {
    return;
  }
  MessagePort* port = Unwrap<MessagePort>(args.This());
  // Even if the backing MessagePort object has already been deleted, we still
  // want to serialize the message to ensure spec-compliant behavior w.r.t.
  // transfers.
  if (port == nullptr || port->IsHandleClosing()) {
    Message msg;
    USE(msg.Serialize(env, context, args[0], transfer_list, obj));
    return;
  }

  Maybe<bool> res = port->PostMessage(env, context, args[0], transfer_list);
  if (res.IsJust())
    args.GetReturnValue().Set(res.FromJust());
}

void MessagePort::Start() {
  Debug(this, "Start receiving messages");
  receiving_messages_ = true;
  Mutex::ScopedLock lock(data_->mutex_);
  if (!data_->incoming_messages_.empty())
    TriggerAsync();
}

void MessagePort::Stop() {
  Debug(this, "Stop receiving messages");
  receiving_messages_ = false;
}

void MessagePort::Start(const FunctionCallbackInfo<Value>& args) {
  MessagePort* port;
  ASSIGN_OR_RETURN_UNWRAP(&port, args.This());
  if (!port->data_) {
    return;
  }
  port->Start();
}

void MessagePort::Stop(const FunctionCallbackInfo<Value>& args) {
  MessagePort* port;
  CHECK(args[0]->IsObject());
  ASSIGN_OR_RETURN_UNWRAP(&port, args[0].As<Object>());
  if (!port->data_) {
    return;
  }
  port->Stop();
}

void MessagePort::CheckType(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  args.GetReturnValue().Set(
      GetMessagePortConstructorTemplate(env->isolate_data())
          ->HasInstance(args[0]));
}

void MessagePort::Drain(const FunctionCallbackInfo<Value>& args) {
  MessagePort* port;
  ASSIGN_OR_RETURN_UNWRAP(&port, args[0].As<Object>());
  port->OnMessage(MessageProcessingMode::kForceReadMessages);
}

void MessagePort::ReceiveMessage(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  if (!args[0]->IsObject() ||
      !env->message_port_constructor_template()->HasInstance(args[0])) {
    return THROW_ERR_INVALID_ARG_TYPE(env,
        "The \"port\" argument must be a MessagePort instance");
  }
  MessagePort* port = Unwrap<MessagePort>(args[0].As<Object>());
  if (port == nullptr) {
    // Return 'no messages' for a closed port.
    args.GetReturnValue().Set(
        Environment::GetCurrent(args)->no_message_symbol());
    return;
  }

  MaybeLocal<Value> payload =
      port->ReceiveMessage(port->object()->GetCreationContextChecked(),
                           MessageProcessingMode::kForceReadMessages);
  if (!payload.IsEmpty())
    args.GetReturnValue().Set(payload.ToLocalChecked());
}

void MessagePort::MoveToContext(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  if (!args[0]->IsObject() ||
      !env->message_port_constructor_template()->HasInstance(args[0])) {
    return THROW_ERR_INVALID_ARG_TYPE(env,
        "The \"port\" argument must be a MessagePort instance");
  }
  MessagePort* port = Unwrap<MessagePort>(args[0].As<Object>());
  if (port == nullptr || port->IsHandleClosing()) {
    Isolate* isolate = env->isolate();
    THROW_ERR_CLOSED_MESSAGE_PORT(isolate);
    return;
  }

  Local<Value> context_arg = args[1];
  ContextifyContext* context_wrapper;
  if (!context_arg->IsObject() ||
      (context_wrapper = ContextifyContext::ContextFromContextifiedSandbox(
          env, context_arg.As<Object>())) == nullptr) {
    return THROW_ERR_INVALID_ARG_TYPE(env, "Invalid context argument");
  }

  std::unique_ptr<MessagePortData> data;
  if (!port->IsDetached())
    data = port->Detach();

  Context::Scope context_scope(context_wrapper->context());
  MessagePort* target =
      MessagePort::New(env, context_wrapper->context(), std::move(data));
  if (target != nullptr)
    args.GetReturnValue().Set(target->object());
}

void MessagePort::Entangle(MessagePort* a, MessagePort* b) {
  MessagePortData::Entangle(a->data_.get(), b->data_.get());
}

void MessagePort::Entangle(MessagePort* a, MessagePortData* b) {
  MessagePortData::Entangle(a->data_.get(), b);
}

void MessagePort::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("data", data_);
  tracker->TrackField("emit_message_fn", emit_message_fn_);
}

Local<FunctionTemplate> GetMessagePortConstructorTemplate(
    IsolateData* isolate_data) {
  // Factor generating the MessagePort JS constructor into its own piece
  // of code, because it is needed early on in the child environment setup.
  Local<FunctionTemplate> templ =
      isolate_data->message_port_constructor_template();
  if (!templ.IsEmpty())
    return templ;

  {
    Isolate* isolate = isolate_data->isolate();
    Local<FunctionTemplate> m = NewFunctionTemplate(isolate, MessagePort::New);
    m->SetClassName(isolate_data->message_port_constructor_string());
    m->InstanceTemplate()->SetInternalFieldCount(
        MessagePort::kInternalFieldCount);
    m->Inherit(HandleWrap::GetConstructorTemplate(isolate_data));

    SetProtoMethod(isolate, m, "postMessage", MessagePort::PostMessage);
    SetProtoMethod(isolate, m, "start", MessagePort::Start);

    isolate_data->set_message_port_constructor_template(m);
  }

  return GetMessagePortConstructorTemplate(isolate_data);
}

// static
BaseObjectPtr<JSTransferable> JSTransferable::Wrap(Environment* env,
                                                   Local<Object> target) {
  Local<Context> context = env->context();
  Local<Value> wrapper_val =
      target->GetPrivate(context, env->js_transferable_wrapper_private_symbol())
          .ToLocalChecked();
  DCHECK(wrapper_val->IsObject() || wrapper_val->IsUndefined());
  BaseObjectPtr<JSTransferable> wrapper;
  if (wrapper_val->IsObject()) {
    wrapper =
        BaseObjectPtr<JSTransferable>{Unwrap<JSTransferable>(wrapper_val)};
  } else {
    Local<Object> wrapper_obj = env->js_transferable_constructor_template()
                                    ->GetFunction(context)
                                    .ToLocalChecked()
                                    ->NewInstance(context)
                                    .ToLocalChecked();
    // Make sure the JSTransferable wrapper object is not garbage collected
    // until the strong BaseObjectPtr's reference count is decreased to 0.
    wrapper = MakeDetachedBaseObject<JSTransferable>(env, wrapper_obj, target);
    target
        ->SetPrivate(
            context, env->js_transferable_wrapper_private_symbol(), wrapper_obj)
        .ToChecked();
  }
  return wrapper;
}

// static
bool JSTransferable::IsJSTransferable(Environment* env,
                                      v8::Local<v8::Context> context,
                                      v8::Local<v8::Object> object) {
  return object->HasPrivate(context, env->transfer_mode_private_symbol())
      .ToChecked();
}

JSTransferable::JSTransferable(Environment* env,
                               Local<Object> obj,
                               Local<Object> target)
    : BaseObject(env, obj) {
  target_.Reset(env->isolate(), target);
}

JSTransferable::~JSTransferable() {
  HandleScope scope(env()->isolate());
  target_.Get(env()->isolate())
      ->DeletePrivate(env()->context(),
                      env()->js_transferable_wrapper_private_symbol());
}

Local<Object> JSTransferable::target() const {
  DCHECK(!target_.IsEmpty());
  return target_.Get(env()->isolate());
}

BaseObject::TransferMode JSTransferable::GetTransferMode() const {
  // Implement `kClone in this ? kCloneable : kTransferable`.
  HandleScope handle_scope(env()->isolate());
  errors::TryCatchScope ignore_exceptions(env());

  Local<Value> transfer_mode_val =
      target()
          ->GetPrivate(env()->context(), env()->transfer_mode_private_symbol())
          .ToLocalChecked();
  if (!transfer_mode_val->IsUint32()) {
    return TransferMode::kDisallowCloneAndTransfer;
  }
  return static_cast<TransferMode>(transfer_mode_val.As<v8::Uint32>()->Value());
}

std::unique_ptr<TransferData> JSTransferable::TransferForMessaging() {
  return TransferOrClone<TransferMode::kTransferable>();
}

std::unique_ptr<TransferData> JSTransferable::CloneForMessaging() const {
  return TransferOrClone<TransferMode::kCloneable>();
}

template <TransferMode mode>
std::unique_ptr<TransferData> JSTransferable::TransferOrClone() const {
  // Call `this[symbol]()` where `symbol` is `kClone` or `kTransfer`,
  // which should return an object with `data` and `deserializeInfo` properties;
  // `data` is written to the serializer later, and `deserializeInfo` is stored
  // on the `TransferData` instance as a string.
  HandleScope handle_scope(env()->isolate());
  Local<Context> context = env()->isolate()->GetCurrentContext();
  Local<Symbol> method_name = mode == TransferMode::kCloneable
                                  ? env()->messaging_clone_symbol()
                                  : env()->messaging_transfer_symbol();

  Local<Value> method;
  if (!target()->Get(context, method_name).ToLocal(&method) ||
      !method->IsFunction()) {
    return {};
  }
  Local<Value> result_v;
  if (!method.As<Function>()
           ->Call(context, target(), 0, nullptr)
           .ToLocal(&result_v) ||
      !result_v->IsObject()) {
    return {};
  }

  Local<Object> result = result_v.As<Object>();
  Local<Value> data;
  Local<Value> deserialize_info;
  if (!result->Get(context, env()->data_string()).ToLocal(&data) ||
      !result->Get(context, env()->deserialize_info_string())
           .ToLocal(&deserialize_info)) {
    return {};
  }
  Utf8Value deserialize_info_str(env()->isolate(), deserialize_info);
  if (*deserialize_info_str == nullptr) return {};
  return std::make_unique<Data>(*deserialize_info_str,
                                Global<Value>(env()->isolate(), data));
}

Maybe<BaseObjectPtrList> JSTransferable::NestedTransferables() const {
  // Call `this[kTransferList]()` and return the resulting list of BaseObjects.
  HandleScope handle_scope(env()->isolate());
  Local<Context> context = env()->isolate()->GetCurrentContext();
  Local<Symbol> method_name = env()->messaging_transfer_list_symbol();

  Local<Value> method;
  if (!target()->Get(context, method_name).ToLocal(&method)) {
    return Nothing<BaseObjectPtrList>();
  }
  if (!method->IsFunction()) return Just(BaseObjectPtrList{});

  Local<Value> list_v;
  if (!method.As<Function>()
           ->Call(context, target(), 0, nullptr)
           .ToLocal(&list_v)) {
    return Nothing<BaseObjectPtrList>();
  }
  if (!list_v->IsArray()) return Just(BaseObjectPtrList{});
  Local<Array> list = list_v.As<Array>();

  BaseObjectPtrList ret;
  for (size_t i = 0; i < list->Length(); i++) {
    Local<Value> value;
    if (!list->Get(context, i).ToLocal(&value))
      return Nothing<BaseObjectPtrList>();
    if (!value->IsObject()) {
      continue;
    }
    Local<Object> obj = value.As<Object>();
    if (BaseObject::IsBaseObject(env()->isolate_data(), obj)) {
      ret.emplace_back(Unwrap<BaseObject>(obj));
      continue;
    }
    if (!JSTransferable::IsJSTransferable(env(), context, obj)) {
      continue;
    }
    ret.emplace_back(JSTransferable::Wrap(env(), obj));
  }
  return Just(ret);
}

Maybe<void> JSTransferable::FinalizeTransferRead(
    Local<Context> context, ValueDeserializer* deserializer) {
  // Call `this[kDeserialize](data)` where `data` comes from the return value
  // of `this[kTransfer]()` or `this[kClone]()`.
  HandleScope handle_scope(env()->isolate());
  Local<Value> data;
  if (!deserializer->ReadValue(context).ToLocal(&data)) return Nothing<void>();

  Local<Symbol> method_name = env()->messaging_deserialize_symbol();
  Local<Value> method;
  if (!target()->Get(context, method_name).ToLocal(&method)) {
    return Nothing<void>();
  }
  if (!method->IsFunction()) return JustVoid();

  if (method.As<Function>()->Call(context, target(), 1, &data).IsEmpty()) {
    return Nothing<void>();
  }
  return JustVoid();
}

JSTransferable::Data::Data(std::string&& deserialize_info,
                           v8::Global<v8::Value>&& data)
    : deserialize_info_(std::move(deserialize_info)),
      data_(std::move(data)) {}

BaseObjectPtr<BaseObject> JSTransferable::Data::Deserialize(
    Environment* env,
    Local<Context> context,
    std::unique_ptr<TransferData> self) {
  // Create the JS wrapper object that will later be filled with data passed to
  // the `[kDeserialize]()` method on it. This split is necessary, because here
  // we need to create an object with the right prototype and internal fields,
  // but the actual JS data stored in the serialized data can only be read at
  // the end of the stream, after the main message has been read.

  if (context != env->context()) {
    THROW_ERR_MESSAGE_TARGET_CONTEXT_UNAVAILABLE(env);
    return {};
  }
  HandleScope handle_scope(env->isolate());
  Local<Value> info;
  if (!ToV8Value(context, deserialize_info_).ToLocal(&info)) return {};

  Local<Value> ret;
  CHECK(!env->messaging_deserialize_create_object().IsEmpty());
  if (!env->messaging_deserialize_create_object()
           ->Call(context, Null(env->isolate()), 1, &info)
           .ToLocal(&ret) ||
      !ret->IsObject()) {
    return {};
  }

  if (!JSTransferable::IsJSTransferable(env, context, ret.As<Object>())) {
    return {};
  }
  return JSTransferable::Wrap(env, ret.As<Object>());
}

Maybe<bool> JSTransferable::Data::FinalizeTransferWrite(
    Local<Context> context, ValueSerializer* serializer) {
  HandleScope handle_scope(context->GetIsolate());
  auto ret = serializer->WriteValue(context, PersistentToLocal::Strong(data_));
  data_.Reset();
  return ret;
}

std::shared_ptr<SiblingGroup> SiblingGroup::Get(const std::string& name) {
  Mutex::ScopedLock lock(SiblingGroup::groups_mutex_);
  std::shared_ptr<SiblingGroup> group;
  auto i = groups_.find(name);
  if (i == groups_.end() || i->second.expired()) {
    group = std::make_shared<SiblingGroup>(name);
    groups_[name] = group;
  } else {
    group = i->second.lock();
  }
  return group;
}

void SiblingGroup::CheckSiblingGroup(const std::string& name) {
  Mutex::ScopedLock lock(SiblingGroup::groups_mutex_);
  auto i = groups_.find(name);
  if (i != groups_.end() && i->second.expired())
    groups_.erase(name);
}

SiblingGroup::SiblingGroup(const std::string& name)
    : name_(name) { }

SiblingGroup::~SiblingGroup() {
  // If this is a named group, check to see if we can remove the group
  if (!name_.empty())
    CheckSiblingGroup(name_);
}

Maybe<bool> SiblingGroup::Dispatch(
    MessagePortData* source,
    std::shared_ptr<Message> message,
    std::string* error) {

  RwLock::ScopedReadLock lock(group_mutex_);

  // The source MessagePortData is not part of this group.
  if (ports_.find(source) == ports_.end()) {
    if (error != nullptr)
      *error = "Source MessagePort is not entangled with this group.";
    return Nothing<bool>();
  }

  // There are no destination ports.
  if (size() <= 1)
    return Just(false);

  // Transferables cannot be used when there is more
  // than a single destination.
  if (size() > 2 && message->has_transferables()) {
    if (error != nullptr)
      *error = "Transferables cannot be used with multiple destinations.";
    return Nothing<bool>();
  }

  for (MessagePortData* port : ports_) {
    if (port == source)
      continue;
    // This loop should only be entered if there's only a single destination
    for (const auto& transferable : message->transferables()) {
      if (port == transferable.get()) {
        if (error != nullptr) {
          *error = "The target port was posted to itself, and the "
                   "communication channel was lost";
        }
        return Just(true);
      }
    }
    port->AddToIncomingQueue(message);
  }

  return Just(true);
}

void SiblingGroup::Entangle(MessagePortData* port) {
  Entangle({ port });
}

void SiblingGroup::Entangle(std::initializer_list<MessagePortData*> ports) {
  RwLock::ScopedWriteLock lock(group_mutex_);
  for (MessagePortData* data : ports) {
    ports_.insert(data);
    CHECK(!data->group_);
    data->group_ = shared_from_this();
  }
}

void SiblingGroup::Disentangle(MessagePortData* data) {
  auto self = shared_from_this();  // Keep alive until end of function.
  RwLock::ScopedWriteLock lock(group_mutex_);
  ports_.erase(data);
  data->group_.reset();

  data->AddToIncomingQueue(std::make_shared<Message>());
  // If this is an anonymous group and there's another port, close it.
  if (size() == 1 && name_.empty())
    (*(ports_.begin()))->AddToIncomingQueue(std::make_shared<Message>());
}

SiblingGroup::Map SiblingGroup::groups_;
Mutex SiblingGroup::groups_mutex_;

namespace {

static void SetDeserializerCreateObjectFunction(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK(args[0]->IsFunction());
  env->set_messaging_deserialize_create_object(args[0].As<Function>());
}

static void StructuredClone(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  Realm* realm = Realm::GetCurrent(context);
  Environment* env = realm->env();

  Local<Value> value = args[0];

  TransferList transfer_list;
  Local<Object> options = args[1].As<Object>();
  Local<Value> transfer_list_v;
  if (!options->Get(context, env->transfer_string())
           .ToLocal(&transfer_list_v)) {
    return;
  }

  Local<Array> arr = transfer_list_v.As<Array>();
  size_t length = arr->Length();
  transfer_list.AllocateSufficientStorage(length);
  for (size_t i = 0; i < length; i++) {
    if (!arr->Get(context, i).ToLocal(&transfer_list[i])) {
      return;
    }
  }

  std::shared_ptr<Message> msg = std::make_shared<Message>();
  Local<Value> result;
  if (msg->Serialize(env, context, value, transfer_list, Local<Object>())
          .IsNothing() ||
      !msg->Deserialize(env, context, nullptr).ToLocal(&result)) {
    return;
  }
  args.GetReturnValue().Set(result);
}

static void MessageChannel(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  if (!args.IsConstructCall()) {
    THROW_ERR_CONSTRUCT_CALL_REQUIRED(env);
    return;
  }

  Local<Context> context = args.This()->GetCreationContextChecked();
  Context::Scope context_scope(context);

  MessagePort* port1 = MessagePort::New(env, context);
  if (port1 == nullptr) return;
  MessagePort* port2 = MessagePort::New(env, context);
  if (port2 == nullptr) {
    port1->Close();
    return;
  }

  MessagePort::Entangle(port1, port2);

  args.This()->Set(context, env->port1_string(), port1->object())
      .Check();
  args.This()->Set(context, env->port2_string(), port2->object())
      .Check();
}

static void BroadcastChannel(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsString());
  Environment* env = Environment::GetCurrent(args);
  Context::Scope context_scope(env->context());
  Utf8Value name(env->isolate(), args[0]);
  MessagePort* port =
      MessagePort::New(env, env->context(), {}, SiblingGroup::Get(*name));
  if (port != nullptr) {
    args.GetReturnValue().Set(port->object());
  }
}

static void CreatePerIsolateProperties(IsolateData* isolate_data,
                                       Local<ObjectTemplate> target) {
  Isolate* isolate = isolate_data->isolate();

  {
    SetConstructorFunction(isolate,
                           target,
                           "MessageChannel",
                           NewFunctionTemplate(isolate, MessageChannel));
  }

  {
    Local<FunctionTemplate> t = FunctionTemplate::New(isolate);
    t->InstanceTemplate()->SetInternalFieldCount(
        JSTransferable::kInternalFieldCount);
    t->SetClassName(FIXED_ONE_BYTE_STRING(isolate, "JSTransferable"));
    isolate_data->set_js_transferable_constructor_template(t);
  }

  SetConstructorFunction(isolate,
                         target,
                         isolate_data->message_port_constructor_string(),
                         GetMessagePortConstructorTemplate(isolate_data));

  // These are not methods on the MessagePort prototype, because
  // the browser equivalents do not provide them.
  SetMethod(isolate, target, "stopMessagePort", MessagePort::Stop);
  SetMethod(isolate, target, "checkMessagePort", MessagePort::CheckType);
  SetMethod(isolate, target, "drainMessagePort", MessagePort::Drain);
  SetMethod(
      isolate, target, "receiveMessageOnPort", MessagePort::ReceiveMessage);
  SetMethod(
      isolate, target, "moveMessagePortToContext", MessagePort::MoveToContext);
  SetMethod(isolate,
            target,
            "setDeserializerCreateObjectFunction",
            SetDeserializerCreateObjectFunction);
  SetMethod(isolate, target, "broadcastChannel", BroadcastChannel);
  SetMethod(isolate, target, "structuredClone", StructuredClone);
}

static void CreatePerContextProperties(Local<Object> target,
                                       Local<Value> unused,
                                       Local<Context> context,
                                       void* priv) {
  Environment* env = Environment::GetCurrent(context);
  {
    Local<Function> domexception = GetDOMException(context).ToLocalChecked();
    target
        ->Set(context,
              FIXED_ONE_BYTE_STRING(env->isolate(), "DOMException"),
              domexception)
        .Check();
  }
}

static void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(MessageChannel);
  registry->Register(BroadcastChannel);
  registry->Register(MessagePort::New);
  registry->Register(MessagePort::PostMessage);
  registry->Register(MessagePort::Start);
  registry->Register(MessagePort::Stop);
  registry->Register(MessagePort::CheckType);
  registry->Register(MessagePort::Drain);
  registry->Register(MessagePort::ReceiveMessage);
  registry->Register(MessagePort::MoveToContext);
  registry->Register(SetDeserializerCreateObjectFunction);
  registry->Register(StructuredClone);
}

}  // anonymous namespace

}  // namespace worker
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(messaging,
                                    node::worker::CreatePerContextProperties)
NODE_BINDING_PER_ISOLATE_INIT(messaging,
                              node::worker::CreatePerIsolateProperties)
NODE_BINDING_EXTERNAL_REFERENCE(messaging,
                                node::worker::RegisterExternalReferences)
