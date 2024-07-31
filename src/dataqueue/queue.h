#pragma once

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <base_object.h>
#include <memory_tracker.h>
#include <node.h>
#include <node_bob.h>
#include <node_file.h>
#include <stream_base.h>
#include <uv.h>
#include <v8.h>

#include <memory>
#include <optional>
#include <vector>

namespace node {

// Represents a sequenced collection of data sources that can be
// consumed as a single logical stream of data. Sources can be
// memory-resident or streaming.
//
// There are two essential kinds of DataQueue:
//
// * Idempotent - Multiple reads always produce the same result.
//                This is even the case if individual sources
//                are not memory-resident. Reads never change
//                the state of the DataQueue. Every entry in
//                an Idempotent DataQueue must also be idempotent.
//
// * Non-idempotent - Reads are destructive of the internal state.
//                    A non-idempotent DataQueue can be read at
//                    most once and only by a single reader.
//                    Entries in a non-idempotent DataQueue can
//                    be a mix of idempotent and non-idempotent
//                    entries.
//
// The DataQueue is essentially a collection of DataQueue::Entry
// instances. A DataQueue::Entry is a single logical source of
// data. The data may be memory-resident or streaming. The entry
// can be idempotent or non-idempotent. An entry cannot be read
// by itself, it must be part of a DataQueue to be consumed.
//
// Example of creating an idempotent DataQueue:
//
//   std::shared_ptr<v8::BackingStore> store1 = getBackingStoreSomehow();
//   std::shared_ptr<v8::BackingStore> store2 = getBackingStoreSomehow();
//
//   std::vector<std::unique_ptr<DataQueue::Entry>> list;
//   list.push_back(DataQueue::CreateInMemoryEntryFromBackingStore(
//       store1, 0, len1));
//   list.push_back(DataQueue::CreateInMemoryEntryFromBackingStore(
//       store2, 0, len2));
//
//   std::shared_ptr<DataQueue> data_queue =
//       DataQueue::CreateIdempotent(std::move(list));
//
// Importantly, idempotent DataQueue's are immutable and all entries
// must be provided when the DataQueue is constructed. Every entry
// must be idempotent with known sizes. The entries may be memory
// resident or streaming. Streaming entries must be capable of
// being read multiple times.
//
// Because idempotent DataQueue's will always produce the same results
// when read, they can be sliced. Slices yield a new DataQueue instance
// that is a subset view over the original:
//
//   std::shared_ptr<DataQueue> slice = data_queue.slice(
//       5, v8::Just(10UL));
//
// Example of creating a non-idempotent DataQueue:
//
//   std::shared_ptr<v8::BackingStore> store1 = getBackingStoreSomehow();
//   std::shared_ptr<v8::BackingStore> store2 = getBackingStoreSomehow();
//
//   std::shared_ptr<DataQueue> data_queue = DataQueue::Create();
//
//   data_queue->append(DataQueue::CreateInMemoryEntryFromBackingStore(
//       store1, 0, len1));
//
//   data_queue->append(DataQueue::CreateInMemoryEntryFromBackingStore(
//       store2, 0, len2));
//
// These data-queues can have new entries appended to them. Entries can
// be memory-resident or streaming. Streaming entries might not have
// a known size. Entries may not be capable of being read multiple
// times.
//
// A non-idempotent data queue will, by default, allow any amount of
// entries to be appended to it. To limit the size of the DataQueue,
// or the close the DataQueue (preventing new entries from being
// appending), use the cap() method. The DataQueue can be capped
// at a specific size or whatever size it currently it.
//
// It might not be possible for a non-idempotent DataQueue to provide
// a size because it might not know how much data a streaming entry
// will ultimately provide.
//
// Non-idempotent DataQueues cannot be sliced.
//
// To read from a DataQueue, we use the node::bob::Source API
// (see src/node_bob.h).
//
//   std::shared_ptr<DataQueue::Reader> reader = data_queue->get_reader();
//
//   reader->Pull(
//        [](int status, const DataQueue::Vec* vecs,
//           uint64_t count, Done done) {
//     // status is one of node::bob::Status
//     // vecs is zero or more data buffers containing the read data
//     // count is the number of vecs
//     // done is a callback to be invoked when done processing the data
//   }, options, nullptr, 0, 16);
//
// Keep calling Pull() until status is equal to node::bob::Status::STATUS_EOS.
//
// For idempotent DataQueues, any number of readers can be created and
// pull concurrently from the same DataQueue. The DataQueue can be read
// multiple times. Successful reads should always produce the same result.
// If, for whatever reason, the implementation cannot ensure that the
// data read will remain the same, the read must fail with an error status.
//
// For non-idempotent DataQueues, only a single reader is ever allowed for
// the DataQueue, and the data can only ever be read once.

class DataQueue : public MemoryRetainer {
 public:
  struct Vec {
    uint8_t* base;
    uint64_t len;
  };

  // A DataQueue::Reader consumes the DataQueue. If the data queue is
  // idempotent, multiple Readers can be attached to the DataQueue at
  // any given time, all guaranteed to yield the same result when the
  // data is read. Otherwise, only a single Reader can be attached.
  class Reader : public MemoryRetainer, public bob::Source<Vec> {
   public:
    using Next = bob::Next<Vec>;
    using Done = bob::Done;
  };

  // A BackpressureListener can be used to receive notifications
  // when a non-idempotent DataQueue releases entries as they
  // are consumed.
  class BackpressureListener {
   public:
    virtual void EntryRead(size_t amount) = 0;
  };

  // A DataQueue::Entry represents a logical chunk of data in the queue.
  // The entry may or may not represent memory-resident data. It may
  // or may not be consumable more than once.
  class Entry : public MemoryRetainer {
   public:
    // Returns a new Entry that is a view over this entries data
    // from the start offset to the ending offset. If the end
    // offset is omitted, the slice extends to the end of the
    // data.
    //
    // Creating a slice is only possible if is_idempotent() returns
    // true. This is because consuming either the original entry or
    // the new entry would change the state of the other in non-
    // deterministic ways. When is_idempotent() returns false, slice()
    // must return a nulled unique_ptr.
    //
    // Creating a slice is also only possible if the size of the
    // entry is known. If size() returns std::nullopt, slice()
    // must return a nulled unique_ptr.
    virtual std::unique_ptr<Entry> slice(
        uint64_t start, std::optional<uint64_t> end = std::nullopt) = 0;

    // Returns the number of bytes represented by this Entry if it is
    // known. Certain types of entries, such as those backed by streams
    // might not know the size in advance and therefore cannot provide
    // a value. In such cases, size() must return v8::Nothing<uint64_t>.
    //
    // If the entry is idempotent, a size should always be available.
    virtual std::optional<uint64_t> size() const = 0;

    // When true, multiple reads on the object must produce the exact
    // same data or the reads will fail. Some sources of entry data,
    // such as streams, may not be capable of preserving idempotency
    // and therefore must not claim to be. If an entry claims to be
    // idempotent and cannot preserve that quality, subsequent reads
    // must fail with an error when a variance is detected.
    virtual bool is_idempotent() const = 0;
  };

  // Creates an idempotent DataQueue with a pre-established collection
  // of entries. All of the entries must also be idempotent otherwise
  // an empty std::unique_ptr will be returned.
  static std::shared_ptr<DataQueue> CreateIdempotent(
      std::vector<std::unique_ptr<Entry>> list);

  // Creates a non-idempotent DataQueue. This kind of queue can be
  // mutated and updated such that multiple reads are not guaranteed
  // to produce the same result. The entries added can be of any type.
  static std::shared_ptr<DataQueue> Create(
      std::optional<uint64_t> capped = std::nullopt);

  // Creates an idempotent Entry from a v8::ArrayBufferView. To help
  // ensure idempotency, the underlying ArrayBuffer is detached from
  // the BackingStore. It is the callers responsibility to ensure that
  // the BackingStore is not otherwise modified through any other
  // means. If the ArrayBuffer is not detachable, nullptr will be
  // returned.
  static std::unique_ptr<Entry> CreateInMemoryEntryFromView(
      v8::Local<v8::ArrayBufferView> view);

  // Creates an idempotent Entry from a v8::BackingStore. It is the
  // callers responsibility to ensure that the BackingStore is not
  // otherwise modified through any other means. If the ArrayBuffer
  // is not detachable, nullptr will be returned.
  static std::unique_ptr<Entry> CreateInMemoryEntryFromBackingStore(
      std::shared_ptr<v8::BackingStore> store,
      uint64_t offset,
      uint64_t length);

  static std::unique_ptr<Entry> CreateDataQueueEntry(
      std::shared_ptr<DataQueue> data_queue);

  static std::unique_ptr<Entry> CreateFdEntry(Environment* env,
                                              v8::Local<v8::Value> path);

  // Creates a Reader for the given queue. If the queue is idempotent,
  // any number of readers can be created, all of which are guaranteed
  // to provide the same data. Otherwise, only a single reader is
  // permitted.
  virtual std::shared_ptr<Reader> get_reader() = 0;

  // Append a single new entry to the queue. Appending is only allowed
  // when is_idempotent() is false. std::nullopt will be returned
  // if is_idempotent() is true. std::optional(false) will be returned if the
  // data queue is not idempotent but the entry otherwise cannot be added.
  virtual std::optional<bool> append(std::unique_ptr<Entry> entry) = 0;

  // Caps the size of this DataQueue preventing additional entries to
  // be added if those cause the size to extend beyond the specified
  // limit.
  //
  // If limit is zero, or is less than the known current size of the
  // data queue, the limit is set to the current known size, meaning
  // that no additional entries can be added at all.
  //
  // If the size of the data queue is not known, the limit will be
  // ignored and no additional entries will be allowed at all.
  //
  // If is_idempotent is true capping is unnecessary because the data
  // queue cannot be appended to. In that case, cap() is a non-op.
  //
  // If the data queue has already been capped, cap can be called
  // again with a smaller size.
  virtual void cap(uint64_t limit = 0) = 0;

  // Returns a new DataQueue that is a view over this queues data
  // from the start offset to the ending offset. If the end offset
  // is omitted, the slice extends to the end of the data.
  //
  // The slice will coverage a range from start up to, but excluding, end.
  //
  // Creating a slice is only possible if is_idempotent() returns
  // true. This is because consuming either the original DataQueue or
  // the new queue would change the state of the other in non-
  // deterministic ways. When is_idempotent() returns false, slice()
  // must return a nulled unique_ptr.
  //
  // Creating a slice is also only possible if the size of the
  // DataQueue is known. If size() returns std::nullopt, slice()
  // must return a null unique_ptr.
  virtual std::shared_ptr<DataQueue> slice(
      uint64_t start, std::optional<uint64_t> end = std::nullopt) = 0;

  // The size of DataQueue is the total size of all of its member entries.
  // If any of the entries is not able to specify a size, the DataQueue
  // will also be incapable of doing so, in which case size() must return
  // std::nullopt.
  virtual std::optional<uint64_t> size() const = 0;

  // A DataQueue is idempotent only if all of its member entries are
  // idempotent.
  virtual bool is_idempotent() const = 0;

  // True only if cap is called or the data queue is a limited to a
  // fixed size.
  virtual bool is_capped() const = 0;

  // If the data queue has been capped, and the size of the data queue
  // is known, maybeCapRemaining will return the number of additional
  // bytes the data queue can receive before reaching the cap limit.
  // If the size of the queue cannot be known, or the cap has not
  // been set, maybeCapRemaining() will return std::nullopt.
  virtual std::optional<uint64_t> maybeCapRemaining() const = 0;

  // BackpressureListeners only work on non-idempotent DataQueues.
  virtual void addBackpressureListener(BackpressureListener* listener) = 0;
  virtual void removeBackpressureListener(BackpressureListener* listener) = 0;

  static void Initialize(Environment* env, v8::Local<v8::Object> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS
