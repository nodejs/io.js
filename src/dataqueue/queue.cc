#include "queue.h"
#include <async_wrap-inl.h>
#include <base_object-inl.h>
#include <env-inl.h>
#include <memory_tracker-inl.h>
#include <node.h>
#include <node_bob-inl.h>
#include <node_errors.h>
#include <node_external_reference.h>
#include <node_file-inl.h>
#include <stream_base-inl.h>
#include <util-inl.h>
#include <uv.h>
#include <v8.h>
#include <algorithm>
#include <deque>
#include <initializer_list>
#include <memory>
#include <vector>

namespace node {

using v8::ArrayBufferView;
using v8::BackingStore;
using v8::Local;
using v8::Object;
using v8::Value;

namespace {
// ============================================================================
class IdempotentDataQueueReader;
class NonIdempotentDataQueueReader;

class EntryImpl : public DataQueue::Entry {
 public:
  virtual std::shared_ptr<DataQueue::Reader> get_reader() = 0;
};

class DataQueueImpl final : public DataQueue,
                            public std::enable_shared_from_this<DataQueueImpl> {
 public:
  // Constructor for an idempotent, fixed sized DataQueue.
  DataQueueImpl(std::vector<std::unique_ptr<Entry>>&& list, uint64_t size)
      : entries_(std::move(list)),
        idempotent_(true),
        size_(size),
        capped_size_(0) {}

  // Constructor for a non-idempotent DataQueue. This kind of queue can have
  // entries added to it over time. The size is set to 0 initially. The queue
  // can be capped immediately on creation. Depending on the entries that are
  // added, the size can be cleared if any of the entries are not capable of
  // providing a size.
  DataQueueImpl(std::optional<uint64_t> cap = std::nullopt)
      : idempotent_(false), size_(0), capped_size_(cap) {}

  // Disallow moving and copying.
  DataQueueImpl(const DataQueueImpl&) = delete;
  DataQueueImpl(DataQueueImpl&&) = delete;
  DataQueueImpl& operator=(const DataQueueImpl&) = delete;
  DataQueueImpl& operator=(DataQueueImpl&&) = delete;

  std::shared_ptr<DataQueue> slice(
      uint64_t start,
      std::optional<uint64_t> maybeEnd = std::nullopt) override {
    // If the data queue is not idempotent, or the size cannot be determined,
    // we cannot reasonably create a slice. Therefore, return nothing.
    if (!idempotent_ || !size_.has_value()) return nullptr;

    uint64_t size = size_.value();

    // start cannot be greater than the size.
    start = std::min(start, size);

    uint64_t end = std::max(start, std::min(maybeEnd.value_or(size), size));

    DCHECK_LE(start, end);

    uint64_t len = end - start;
    uint64_t remaining = end - start;
    std::vector<std::unique_ptr<Entry>> slices;

    if (remaining > 0) {
      for (const auto& entry : entries_) {
        // The size of every entry should be known since this is an
        // idempotent queue.
        uint64_t entrySize = entry->size().value();
        if (start > entrySize) {
          start -= entrySize;
          continue;
        }

        uint64_t chunkStart = start;
        uint64_t len = std::min(remaining, entrySize - chunkStart);
        slices.emplace_back(entry->slice(chunkStart, chunkStart + len));
        remaining -= len;
        start = 0;

        if (remaining == 0) break;
      }
    }

    return std::make_shared<DataQueueImpl>(std::move(slices), len);
  }

  std::optional<uint64_t> size() const override { return size_; }

  bool is_idempotent() const override { return idempotent_; }

  bool is_capped() const override { return capped_size_.has_value(); }

  std::optional<bool> append(std::unique_ptr<Entry> entry) override {
    if (idempotent_) return std::nullopt;
    if (!entry) return false;

    // If this entry successfully provides a size, we can add it to our size_
    // if that has a value, otherwise, we keep uint64_t empty.
    if (entry->size().has_value() && size_.has_value()) {
      uint64_t entrySize = entry->size().value();
      uint64_t size = size_.value();
      // If capped_size_ is set, size + entrySize cannot exceed capped_size_
      // or the entry cannot be added.
      if (capped_size_.has_value() &&
          (capped_size_.value() < entrySize + size)) {
        return false;
      }
      size_ = size + entrySize;
    } else {
      // This entry cannot provide a size. We can still add it but we have to
      // clear the known size.
      size_ = std::nullopt;
    }

    entries_.push_back(std::move(entry));
    return true;
  }

  void cap(uint64_t limit = 0) override {
    if (is_idempotent()) return;
    // If the data queue is already capped, it is possible to call
    // cap again with a smaller size.
    if (capped_size_.has_value()) {
      capped_size_ = std::min(limit, capped_size_.value());
      return;
    }

    // Otherwise just set the limit.
    capped_size_ = limit;
  }

  std::optional<uint64_t> maybeCapRemaining() const override {
    if (capped_size_.has_value() && size_.has_value()) {
      uint64_t capped_size = capped_size_.value();
      uint64_t size = size_.value();
      return capped_size > size ? capped_size - size : 0UL;
    }
    return std::nullopt;
  }

  void MemoryInfo(node::MemoryTracker* tracker) const override {
    tracker->TrackField(
        "entries", entries_, "std::vector<std::unique_ptr<Entry>>");
  }

  void addBackpressureListener(BackpressureListener* listener) override {
    if (idempotent_) return;
    DCHECK_NOT_NULL(listener);
    backpressure_listeners_.insert(listener);
  }

  void removeBackpressureListener(BackpressureListener* listener) override {
    if (idempotent_) return;
    DCHECK_NOT_NULL(listener);
    backpressure_listeners_.erase(listener);
  }

  void NotifyBackpressure(size_t amount) {
    if (idempotent_) return;
    for (auto& listener : backpressure_listeners_) listener->EntryRead(amount);
  }

  bool HasBackpressureListeners() const noexcept {
    return !backpressure_listeners_.empty();
  }

  std::shared_ptr<Reader> get_reader() override;
  SET_MEMORY_INFO_NAME(DataQueue)
  SET_SELF_SIZE(DataQueueImpl)

 private:
  std::vector<std::unique_ptr<Entry>> entries_;
  bool idempotent_;
  std::optional<uint64_t> size_ = std::nullopt;
  std::optional<uint64_t> capped_size_ = std::nullopt;
  bool locked_to_reader_ = false;

  std::unordered_set<BackpressureListener*> backpressure_listeners_;

  friend class DataQueue;
  friend class IdempotentDataQueueReader;
  friend class NonIdempotentDataQueueReader;
};

// An IdempotentDataQueueReader always reads the entire content of the
// DataQueue with which it is associated, and always from the beginning.
// Reads are non-destructive, meaning that the state of the DataQueue
// will not and cannot be changed.
class IdempotentDataQueueReader final
    : public DataQueue::Reader,
      public std::enable_shared_from_this<IdempotentDataQueueReader> {
 public:
  IdempotentDataQueueReader(std::shared_ptr<DataQueueImpl> data_queue)
      : data_queue_(std::move(data_queue)) {
    CHECK(data_queue_->is_idempotent());
  }

  // Disallow moving and copying.
  IdempotentDataQueueReader(const IdempotentDataQueueReader&) = delete;
  IdempotentDataQueueReader(IdempotentDataQueueReader&&) = delete;
  IdempotentDataQueueReader& operator=(const IdempotentDataQueueReader&) =
      delete;
  IdempotentDataQueueReader& operator=(IdempotentDataQueueReader&&) = delete;

  int Pull(Next next,
           int options,
           DataQueue::Vec* data,
           size_t count,
           size_t max_count_hint = bob::kMaxCountHint) override {
    std::shared_ptr<DataQueue::Reader> self = shared_from_this();

    // If ended is true, this reader has already reached the end and cannot
    // provide any more data.
    if (ended_) {
      std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](uint64_t) {});
      return bob::Status::STATUS_EOS;
    }

    // If this is the first pull from this reader, we are first going to
    // check to see if there is anything at all to actually do.
    if (!current_index_.has_value()) {
      // First, let's check the number of entries. If there are no entries,
      // we've reached the end and have nothing to do.
      // Because this is an idempotent dataqueue, we should always know the
      // size...
      if (data_queue_->entries_.empty()) {
        ended_ = true;
        std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](uint64_t) {});
        return bob::Status::STATUS_EOS;
      }

      current_index_ = 0;
    }

    // We have current_index_, awesome, we are going to keep reading from
    // it until we receive and end.

    auto current_reader = getCurrentReader();
    if (current_reader == nullptr) {
      // Getting the current reader for an entry could fail for several
      // reasons. For an FdEntry, for instance, getting the reader may
      // fail if the file has been modified since the FdEntry was created.
      // We handle the case simply by erroring.
      std::move(next)(UV_EINVAL, nullptr, 0, [](uint64_t) {});
      return UV_EINVAL;
    }

    CHECK(!pull_pending_);
    pull_pending_ = true;
    int status = current_reader->Pull(
        [this, next = std::move(next)](
            int status, const DataQueue::Vec* vecs, uint64_t count, Done done) {
          pull_pending_ = false;
          // In each of these cases, we do not expect that the source will
          // actually have provided any actual data.
          CHECK_IMPLIES(status == bob::Status::STATUS_BLOCK ||
                            status == bob::Status::STATUS_WAIT ||
                            status == bob::Status::STATUS_EOS,
                        vecs == nullptr && count == 0);
          if (status == bob::Status::STATUS_EOS) {
            uint32_t current = current_index_.value() + 1;
            current_reader_ = nullptr;
            // We have reached the end of this entry. If this is the last entry,
            // then we are done. Otherwise, we advance the current_index_, clear
            // the current_reader_ and wait for the next read.

            if (current == data_queue_->entries_.size()) {
              // Yes, this was the final entry. We're all done.
              ended_ = true;
            } else {
              // This was not the final entry, so we update the index and
              // continue on by performing another read.
              current_index_ = current;
              status = bob::STATUS_CONTINUE;
            }
            std::move(next)(status, nullptr, 0, [](uint64_t) {});
            return;
          }

          std::move(next)(status, vecs, count, std::move(done));
        },
        options,
        data,
        count,
        max_count_hint);

    // The pull was handled synchronously. If we're not ended, we want to
    // make sure status returned is CONTINUE.
    if (!pull_pending_) {
      if (!ended_) return bob::Status::STATUS_CONTINUE;
      // For all other status, we just fall through and return it straightaway.
    }

    // The other statuses that can be returned by the pull are:
    //  bob::Status::STATUS_CONTINUE - means that the entry has more data
    //                                 to pull.
    //  bob::Status::STATUS_BLOCK - means that the entry has more data to
    //                              pull but it is not available yet. The
    //                              caller should not keep calling pull for
    //                              now but may check again later.
    //  bob::Status::STATUS_WAIT - means that the entry has more data to
    //                             pull but it won't be provided
    //                             synchronously, instead the next() callback
    //                             will be called when the data is available.
    //
    // For any of these statuses, we want to keep the current index and
    // current_reader_ set for the next pull.

    return status;
  }

  DataQueue::Reader* getCurrentReader() {
    CHECK(!ended_);
    CHECK(current_index_.has_value());
    if (current_reader_ == nullptr) {
      auto& entry = data_queue_->entries_[current_index_.value()];
      // Because this is an idempotent reader, let's just be sure to
      // doublecheck that the entry itself is actually idempotent
      DCHECK(entry->is_idempotent());
      current_reader_ = static_cast<EntryImpl&>(*entry).get_reader();
    }
    return current_reader_.get();
  }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(IdempotentDataQueueReader)
  SET_SELF_SIZE(IdempotentDataQueueReader)

 private:
  std::shared_ptr<DataQueueImpl> data_queue_;
  std::optional<uint32_t> current_index_ = std::nullopt;
  std::shared_ptr<DataQueue::Reader> current_reader_ = nullptr;
  bool ended_ = false;
  bool pull_pending_ = false;
};

// A NonIdempotentDataQueueReader reads entries from the DataEnqueue
// and removes those entries from the queue as they are fully consumed.
// This means that reads are destructive and the state of the DataQueue
// is mutated as the read proceeds.
class NonIdempotentDataQueueReader final
    : public DataQueue::Reader,
      public std::enable_shared_from_this<NonIdempotentDataQueueReader> {
 public:
  NonIdempotentDataQueueReader(std::shared_ptr<DataQueueImpl> data_queue)
      : data_queue_(std::move(data_queue)) {
    CHECK(!data_queue_->is_idempotent());
  }

  // Disallow moving and copying.
  NonIdempotentDataQueueReader(const NonIdempotentDataQueueReader&) = delete;
  NonIdempotentDataQueueReader(NonIdempotentDataQueueReader&&) = delete;
  NonIdempotentDataQueueReader& operator=(const NonIdempotentDataQueueReader&) =
      delete;
  NonIdempotentDataQueueReader& operator=(NonIdempotentDataQueueReader&&) =
      delete;

  int Pull(Next next,
           int options,
           DataQueue::Vec* data,
           size_t count,
           size_t max_count_hint = bob::kMaxCountHint) override {
    std::shared_ptr<DataQueue::Reader> self = shared_from_this();

    // If ended is true, this reader has already reached the end and cannot
    // provide any more data.
    if (ended_) {
      std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](uint64_t) {});
      return bob::Status::STATUS_EOS;
    }

    // If the collection of entries is empty, there's nothing currently left to
    // read. How we respond depends on whether the data queue has been capped
    // or not.
    if (data_queue_->entries_.empty()) {
      // If the data_queue_ is empty, and not capped, then we can reasonably
      // expect more data to be provided later, but we don't know exactly when
      // that'll happe, so the proper response here is to return a blocked
      // status.
      if (!data_queue_->is_capped()) {
        std::move(next)(bob::Status::STATUS_BLOCK, nullptr, 0, [](uint64_t) {});
        return bob::STATUS_BLOCK;
      }

      // However, if we are capped, the status will depend on whether the size
      // of the data_queue_ is known or not.

      if (data_queue_->size().has_value()) {
        // If the size is known, and it is still less than the cap, then we
        // still might get more data. We just don't know exactly when that'll
        // come, so let's return a blocked status.
        if (data_queue_->size().value() < data_queue_->capped_size_.value()) {
          std::move(next)(
              bob::Status::STATUS_BLOCK, nullptr, 0, [](uint64_t) {});
          return bob::STATUS_BLOCK;
        }

        // Otherwise, if size is equal to or greater than capped, we are done.
        // Fall through to allow the end handling to run.
      }

      // If the size is not known, and the data queue is capped, no additional
      // entries are going to be added to the queue. Since we are all out of
      // entries, we're done. There's nothing left to read.
      current_reader_ = nullptr;
      ended_ = true;
      std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](uint64_t) {});
      return bob::STATUS_EOS;
    }

    auto current_reader = getCurrentReader();
    if (current_reader == nullptr) {
      std::move(next)(UV_EINVAL, nullptr, 0, [](uint64_t) {});
      return UV_EINVAL;
    }

    // If we got here, we have an entry to read from.
    CHECK(!pull_pending_);
    pull_pending_ = true;
    int status = current_reader->Pull(
        [this, next = std::move(next)](
            int status, const DataQueue::Vec* vecs, uint64_t count, Done done) {
          pull_pending_ = false;

          // In each of these cases, we do not expect that the source will
          // actually have provided any actual data.
          CHECK_IMPLIES(status == bob::Status::STATUS_BLOCK ||
                            status == bob::Status::STATUS_WAIT ||
                            status == bob::Status::STATUS_EOS,
                        vecs == nullptr && count == 0);
          if (status == bob::Status::STATUS_EOS) {
            data_queue_->entries_.erase(data_queue_->entries_.begin());
            ended_ = data_queue_->entries_.empty();
            current_reader_ = nullptr;
            if (!ended_) status = bob::Status::STATUS_CONTINUE;
            std::move(next)(status, nullptr, 0, [](uint64_t) {});
            return;
          }

          // If there is a backpressure listener, lets report on how much data
          // was actually read.
          if (data_queue_->HasBackpressureListeners()) {
            // How much did we actually read?
            size_t read = 0;
            for (uint64_t n = 0; n < count; n++) {
              read += vecs[n].len;
            }
            data_queue_->NotifyBackpressure(read);
          }

          // Now that we have updated this readers state, we can forward
          // everything on to the outer next.
          std::move(next)(status, vecs, count, std::move(done));
        },
        options,
        data,
        count,
        max_count_hint);

    if (!pull_pending_) {
      // The callback was resolved synchronously. Let's check our status.
      if (!ended_) return bob::Status::STATUS_CONTINUE;
      // For all other status, we just fall through and return it straightaway.
    }

    // The other statuses that can be returned by the pull are:
    //  bob::Status::STATUS_CONTINUE - means that the entry has more data
    //                                 to pull.
    //  bob::Status::STATUS_BLOCK - means that the entry has more data to
    //                              pull but it is not available yet. The
    //                              caller should not keep calling pull for
    //                              now but may check again later.
    //  bob::Status::STATUS_WAIT - means that the entry has more data to
    //                             pull but it won't be provided
    //                             synchronously, instead the next() callback
    //                             will be called when the data is available.
    //
    // For any of these statuses, we want to keep the current index and
    // current_reader_ set for the next pull.

    return status;
  }

  DataQueue::Reader* getCurrentReader() {
    CHECK(!ended_);
    CHECK(!data_queue_->entries_.empty());
    if (current_reader_ == nullptr) {
      auto& entry = data_queue_->entries_.front();
      current_reader_ = static_cast<EntryImpl&>(*entry).get_reader();
    }
    return current_reader_.get();
  }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(NonIdempotentDataQueueReader)
  SET_SELF_SIZE(NonIdempotentDataQueueReader)

 private:
  std::shared_ptr<DataQueueImpl> data_queue_;
  std::shared_ptr<DataQueue::Reader> current_reader_ = nullptr;
  bool ended_ = false;
  bool pull_pending_ = false;
};

std::shared_ptr<DataQueue::Reader> DataQueueImpl::get_reader() {
  if (is_idempotent()) {
    return std::make_shared<IdempotentDataQueueReader>(shared_from_this());
  }

  if (locked_to_reader_) return nullptr;
  locked_to_reader_ = true;

  return std::make_shared<NonIdempotentDataQueueReader>(shared_from_this());
}

// ============================================================================

// An empty, always idempotent entry.
class EmptyEntry final : public EntryImpl {
 public:
  class EmptyReader final : public DataQueue::Reader,
                            public std::enable_shared_from_this<EmptyReader> {
   public:
    int Pull(Next next,
             int options,
             DataQueue::Vec* data,
             size_t count,
             size_t max_count_hint = bob::kMaxCountHint) override {
      auto self = shared_from_this();
      if (ended_) {
        std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](uint64_t) {});
        return bob::Status::STATUS_EOS;
      }

      ended_ = true;
      std::move(next)(
          bob::Status::STATUS_CONTINUE, nullptr, 0, [](uint64_t) {});
      return bob::Status::STATUS_CONTINUE;
    }

    SET_NO_MEMORY_INFO()
    SET_MEMORY_INFO_NAME(EmptyReader)
    SET_SELF_SIZE(EmptyReader)

   private:
    bool ended_ = false;
  };

  EmptyEntry() = default;

  // Disallow moving and copying.
  EmptyEntry(const EmptyEntry&) = delete;
  EmptyEntry(EmptyEntry&&) = delete;
  EmptyEntry& operator=(const EmptyEntry&) = delete;
  EmptyEntry& operator=(EmptyEntry&&) = delete;

  std::shared_ptr<DataQueue::Reader> get_reader() override {
    return std::make_shared<EmptyReader>();
  }

  std::unique_ptr<Entry> slice(
      uint64_t start,
      std::optional<uint64_t> maybeEnd = std::nullopt) override {
    if (start != 0) return nullptr;
    return std::make_unique<EmptyEntry>();
  }

  std::optional<uint64_t> size() const override { return 0; }

  bool is_idempotent() const override { return true; }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(EmptyEntry)
  SET_SELF_SIZE(EmptyEntry)
};

// ============================================================================

// An entry that consists of a single memory resident v8::BackingStore.
// These are always idempotent and always a fixed, known size.
class InMemoryEntry final : public EntryImpl {
 public:
  struct InMemoryFunctor final {
    std::shared_ptr<BackingStore> backing_store;
    void operator()(uint64_t) { backing_store = nullptr; }
  };

  class InMemoryReader final
      : public DataQueue::Reader,
        public std::enable_shared_from_this<InMemoryReader> {
   public:
    InMemoryReader(InMemoryEntry& entry) : entry_(entry) {}

    int Pull(Next next,
             int options,
             DataQueue::Vec* data,
             size_t count,
             size_t max_count_hint = bob::kMaxCountHint) override {
      auto self = shared_from_this();
      if (ended_) {
        std::move(next)(bob::Status::STATUS_EOS, nullptr, 0, [](uint64_t) {});
        return bob::Status::STATUS_EOS;
      }

      ended_ = true;
      DataQueue::Vec vec{
          reinterpret_cast<uint8_t*>(entry_.backing_store_->Data()) +
              entry_.offset_,
          entry_.byte_length_,
      };

      std::move(next)(bob::Status::STATUS_CONTINUE,
                      &vec,
                      1,
                      InMemoryFunctor({entry_.backing_store_}));
      return bob::Status::STATUS_CONTINUE;
    }

    SET_NO_MEMORY_INFO()
    SET_MEMORY_INFO_NAME(InMemoryReader)
    SET_SELF_SIZE(InMemoryReader)

   private:
    InMemoryEntry& entry_;
    bool ended_ = false;
  };

  InMemoryEntry(std::shared_ptr<BackingStore> backing_store,
                uint64_t offset,
                uint64_t byte_length)
      : backing_store_(std::move(backing_store)),
        offset_(offset),
        byte_length_(byte_length) {
    // The offset_ + byte_length_ cannot extend beyond the size of the
    // backing store, because that would just be silly.
    CHECK_LE(offset_ + byte_length_, backing_store_->ByteLength());
  }

  // Disallow moving and copying.
  InMemoryEntry(const InMemoryEntry&) = delete;
  InMemoryEntry(InMemoryEntry&&) = delete;
  InMemoryEntry& operator=(const InMemoryEntry&) = delete;
  InMemoryEntry& operator=(InMemoryEntry&&) = delete;

  std::shared_ptr<DataQueue::Reader> get_reader() override {
    return std::make_shared<InMemoryReader>(*this);
  }

  std::unique_ptr<Entry> slice(
      uint64_t start,
      std::optional<uint64_t> maybeEnd = std::nullopt) override {
    const auto makeEntry = [&](uint64_t start,
                               uint64_t len) -> std::unique_ptr<Entry> {
      if (len == 0) {
        return std::make_unique<EmptyEntry>();
      }

      return std::make_unique<InMemoryEntry>(backing_store_, start, len);
    };

    start += offset_;

    // The start cannot extend beyond the maximum end point of this entry.
    start = std::min(start, offset_ + byte_length_);

    if (maybeEnd.has_value()) {
      uint64_t end = maybeEnd.value();
      // The end cannot extend beyond the maximum end point of this entry,
      // and the end must be equal to or greater than the start.
      end = std::max(start, std::min(offset_ + end, offset_ + byte_length_));

      return makeEntry(start, end - start);
    }

    // If no end is given, then the new length is the current length
    // minus the adjusted start.
    return makeEntry(start, byte_length_ - start);
  }

  std::optional<uint64_t> size() const override { return byte_length_; }

  bool is_idempotent() const override { return true; }

  void MemoryInfo(node::MemoryTracker* tracker) const override {
    tracker->TrackField(
        "store", backing_store_, "std::shared_ptr<v8::BackingStore>");
  }
  SET_MEMORY_INFO_NAME(InMemoryEntry)
  SET_SELF_SIZE(InMemoryEntry)

 private:
  std::shared_ptr<BackingStore> backing_store_;
  uint64_t offset_;
  uint64_t byte_length_;

  friend class InMemoryReader;
};

// ============================================================================

// An entry that wraps a DataQueue. The entry takes on the characteristics
// of the wrapped dataqueue.
class DataQueueEntry : public EntryImpl {
 public:
  explicit DataQueueEntry(std::shared_ptr<DataQueue> data_queue)
      : data_queue_(std::move(data_queue)) {
    CHECK(data_queue_);
  }

  // Disallow moving and copying.
  DataQueueEntry(const DataQueueEntry&) = delete;
  DataQueueEntry(DataQueueEntry&&) = delete;
  DataQueueEntry& operator=(const DataQueueEntry&) = delete;
  DataQueueEntry& operator=(DataQueueEntry&&) = delete;

  std::shared_ptr<DataQueue::Reader> get_reader() override {
    return std::make_shared<ReaderImpl>(data_queue_->get_reader());
  }

  std::unique_ptr<Entry> slice(
      uint64_t start, std::optional<uint64_t> end = std::nullopt) override {
    std::shared_ptr<DataQueue> sliced = data_queue_->slice(start, end);
    if (!sliced) return nullptr;

    return std::make_unique<DataQueueEntry>(std::move(sliced));
  }

  // Returns the number of bytes represented by this Entry if it is
  // known. Certain types of entries, such as those backed by streams
  // might not know the size in advance and therefore cannot provide
  // a value. In such cases, size() must return std::nullopt.
  //
  // If the entry is idempotent, a size should always be available.
  std::optional<uint64_t> size() const override { return data_queue_->size(); }

  // When true, multiple reads on the object must produce the exact
  // same data or the reads will fail. Some sources of entry data,
  // such as streams, may not be capable of preserving idempotency
  // and therefore must not claim to be. If an entry claims to be
  // idempotent and cannot preserve that quality, subsequent reads
  // must fail with an error when a variance is detected.
  bool is_idempotent() const override { return data_queue_->is_idempotent(); }

  void MemoryInfo(node::MemoryTracker* tracker) const override {
    tracker->TrackField(
        "data_queue", data_queue_, "std::shared_ptr<DataQueue>");
  }

  DataQueue& getDataQueue() { return *data_queue_; }

  SET_MEMORY_INFO_NAME(DataQueueEntry)
  SET_SELF_SIZE(DataQueueEntry)

 private:
  std::shared_ptr<DataQueue> data_queue_;

  class ReaderImpl : public DataQueue::Reader,
                     public std::enable_shared_from_this<ReaderImpl> {
   public:
    explicit ReaderImpl(std::shared_ptr<DataQueue::Reader> inner)
        : inner_(std::move(inner)) {}

    int Pull(DataQueue::Reader::Next next,
             int options,
             DataQueue::Vec* data,
             size_t count,
             size_t max_count_hint) override {
      auto self = shared_from_this();
      return inner_->Pull(
          std::move(next), options, data, count, max_count_hint);
    }

    SET_NO_MEMORY_INFO()
    SET_MEMORY_INFO_NAME(ReaderImpl)
    SET_SELF_SIZE(ReaderImpl)

   private:
    std::shared_ptr<DataQueue::Reader> inner_;
  };
};

// ============================================================================

// An FdEntry reads from a file descriptor. A check is made before each read
// to determine if the fd has changed on disc. This is a best-effort check
// that only looks at file size, creation, and modification times. The stat
// check is also async, so there's a natural race condition there where the
// file could be modified between the stat and actual read calls. That's
// a tolerable risk here. While FdEntry is considered idempotent, this race
// means that it is indeed possible for multiple reads to return different
// results if the file just happens to get modified.
class FdEntry final : public EntryImpl {
  // TODO(@jasnell, @flakey5):
  // * This should only allow reading from regular files. No directories, no
  // pipes, etc.
  // * The reader should support accepting the buffer(s) from the pull, if any.
  // It should
  //   only allocate a managed buffer if the pull doesn't provide any.
  // * We might want to consider making the stat on each read sync to eliminate
  // the race
  //   condition described in the comment above.
 public:
  static std::unique_ptr<FdEntry> Create(Environment* env, Local<Value> path) {
    // We're only going to create the FdEntry if the file exists.
    uv_fs_t req = uv_fs_t();
    auto cleanup = OnScopeLeave([&] { uv_fs_req_cleanup(&req); });

    auto buf = std::make_shared<BufferValue>(env->isolate(), path);
    if (uv_fs_stat(nullptr, &req, buf->out(), nullptr) < 0) return nullptr;

    return std::make_unique<FdEntry>(
        env, std::move(buf), req.statbuf, 0, req.statbuf.st_size);
  }

  FdEntry(Environment* env,
          std::shared_ptr<BufferValue> path_,
          uv_stat_t stat,
          uint64_t start,
          uint64_t end)
      : env_(env),
        path_(std::move(path_)),
        stat_(stat),
        start_(start),
        end_(end) {
    CHECK_LE(start, end);
  }

  std::shared_ptr<DataQueue::Reader> get_reader() override {
    return ReaderImpl::Create(this);
  }

  std::unique_ptr<Entry> slice(
      uint64_t start, std::optional<uint64_t> end = std::nullopt) override {
    uint64_t new_start = start_ + start;
    uint64_t new_end = end_;
    if (end.has_value()) {
      new_end = std::min(end.value() + start_, end_);
    }

    CHECK(new_start >= start_);
    CHECK(new_end <= end_);

    return std::make_unique<FdEntry>(env_, path_, stat_, new_start, new_end);
  }

  std::optional<uint64_t> size() const override { return end_ - start_; }

  bool is_idempotent() const override { return true; }

  Environment* env() const { return env_; }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(FdEntry)
  SET_SELF_SIZE(FdEntry)

 private:
  Environment* env_;
  std::shared_ptr<BufferValue> path_;
  uv_stat_t stat_;
  uint64_t start_ = 0;
  uint64_t end_ = 0;

  bool is_modified(const uv_stat_t& other) {
    return other.st_size != stat_.st_size ||
           other.st_mtim.tv_nsec != stat_.st_mtim.tv_nsec;
  }

  static bool CheckModified(FdEntry* entry, int fd) {
    uv_fs_t req = uv_fs_t();
    auto cleanup = OnScopeLeave([&] { uv_fs_req_cleanup(&req); });
    // TODO(jasnell): Note the use of a sync fs call here is a bit unfortunate.
    // Doing this asynchronously creates a bit of a race condition tho, a file
    // could be unmodified when we call the operation but then by the time the
    // async callback is triggered to give us that answer the file is modified.
    // While such silliness is still possible here, the sync call at least makes
    // it less likely to hit the race.
    if (uv_fs_fstat(nullptr, &req, fd, nullptr) < 0) return true;
    return entry->is_modified(req.statbuf);
  }

  class ReaderImpl final : public DataQueue::Reader,
                           public StreamListener,
                           public std::enable_shared_from_this<ReaderImpl> {
   public:
    static std::shared_ptr<ReaderImpl> Create(FdEntry* entry) {
      uv_fs_t req;
      auto cleanup = OnScopeLeave([&] { uv_fs_req_cleanup(&req); });
      int file =
          uv_fs_open(nullptr, &req, entry->path_->out(), O_RDONLY, 0, nullptr);
      if (file < 0 || FdEntry::CheckModified(entry, file)) {
        uv_fs_close(nullptr, &req, file, nullptr);
        return nullptr;
      }
      Realm* realm = entry->env()->principal_realm();
      return std::make_shared<ReaderImpl>(
          BaseObjectPtr<fs::FileHandle>(
              fs::FileHandle::New(realm->GetBindingData<fs::BindingData>(),
                                  file,
                                  Local<Object>(),
                                  entry->start_,
                                  entry->end_ - entry->start_)),
          entry);
    }

    explicit ReaderImpl(BaseObjectPtr<fs::FileHandle> handle, FdEntry* entry)
        : env_(handle->env()), handle_(std::move(handle)), entry_(entry) {
      handle_->PushStreamListener(this);
      handle_->env()->AddCleanupHook(cleanup, this);
    }

    ~ReaderImpl() override {
      handle_->env()->RemoveCleanupHook(cleanup, this);
      DrainAndClose();
      handle_->RemoveStreamListener(this);
    }

    uv_buf_t OnStreamAlloc(size_t suggested_size) override {
      return env_->allocate_managed_buffer(suggested_size);
    }

    void OnStreamRead(ssize_t nread, const uv_buf_t& buf) override {
      std::shared_ptr<v8::BackingStore> store =
          env_->release_managed_buffer(buf);

      if (ended_) {
        // If we got here and ended_ is true, it means we ended and drained
        // while the read was pending. We're just going to do nothing.
        CHECK(pending_pulls_.empty());
        return;
      }

      CHECK(reading_);
      auto pending = DequeuePendingPull();

      if (CheckModified(entry_, handle_->GetFD())) {
        DrainAndClose();
        // The file was modified while the read was pending. We need to error.
        std::move(pending.next)(UV_EINVAL, nullptr, 0, [](uint64_t) {});
        return;
      }

      if (nread < 0) {
        if (nread == UV_EOF) {
          std::move(pending.next)(bob::STATUS_EOS, nullptr, 0, [](uint64_t) {});
        } else {
          std::move(pending.next)(nread, nullptr, 0, [](uint64_t) {});
        }

        return DrainAndClose();
      }

      DataQueue::Vec vec;
      vec.base = static_cast<uint8_t*>(store->Data());
      vec.len = static_cast<uint64_t>(nread);
      std::move(pending.next)(
          bob::STATUS_CONTINUE, &vec, 1, [store](uint64_t) {});

      if (pending_pulls_.empty()) {
        reading_ = false;
        if (handle_->IsAlive()) handle_->ReadStop();
      }
    }

    int Pull(Next next,
             int options,
             DataQueue::Vec* data,
             size_t count,
             size_t max_count_hint = bob::kMaxCountHint) override {
      if (ended_ || !handle_->IsAlive()) {
        std::move(next)(bob::STATUS_EOS, nullptr, 0, [](uint64_t) {});
        return bob::STATUS_EOS;
      }

      if (FdEntry::CheckModified(entry_, handle_->GetFD())) {
        DrainAndClose();
        std::move(next)(UV_EINVAL, nullptr, 0, [](uint64_t) {});
        return UV_EINVAL;
      }

      pending_pulls_.emplace_back(std::move(next), shared_from_this());
      if (!reading_) {
        reading_ = true;
        handle_->ReadStart();
      }
      return bob::STATUS_WAIT;
    }

    SET_NO_MEMORY_INFO()
    SET_MEMORY_INFO_NAME(FdEntry::Reader)
    SET_SELF_SIZE(ReaderImpl)

   private:
    struct PendingPull {
      Next next;
      std::shared_ptr<ReaderImpl> self;
      PendingPull(Next next, std::shared_ptr<ReaderImpl> self)
          : next(std::move(next)), self(std::move(self)) {}
    };

    Environment* env_;
    BaseObjectPtr<fs::FileHandle> handle_;
    FdEntry* entry_;
    std::deque<PendingPull> pending_pulls_;
    bool reading_ = false;
    bool ended_ = false;

    static void cleanup(void* self) {
      auto ptr = static_cast<ReaderImpl*>(self);
      ptr->DrainAndClose();
    }

    void DrainAndClose() {
      if (ended_) return;
      ended_ = true;
      while (!pending_pulls_.empty()) {
        auto pending = DequeuePendingPull();
        std::move(pending.next)(bob::STATUS_EOS, nullptr, 0, [](uint64_t) {});
      }
      handle_->ReadStop();

      // We fallback to a sync close on the raw fd here because it is the
      // easiest, simplest thing to do. All of FileHandle's close mechanisms
      // assume async close and cleanup, while DrainAndClose might be running
      // in the destructor during GC, for instance. As a todo, FileHandle could
      // provide a sync mechanism for closing the FD but, for now, this
      // approach works.
      int fd = handle_->Release();
      uv_fs_t req;
      uv_fs_close(nullptr, &req, fd, nullptr);
      uv_fs_req_cleanup(&req);
    }

    PendingPull DequeuePendingPull() {
      CHECK(!pending_pulls_.empty());
      auto pop = OnScopeLeave([this] { pending_pulls_.pop_front(); });
      return std::move(pending_pulls_.front());
    }

    friend class FdEntry;
  };

  friend class ReaderImpl;
};

// ============================================================================

}  // namespace

std::shared_ptr<DataQueue> DataQueue::CreateIdempotent(
    std::vector<std::unique_ptr<Entry>> list) {
  // Any entry is invalid for an idempotent DataQueue if any of the entries
  // are nullptr or is not idempotent.
  uint64_t size = 0;
  const auto isInvalid = [&size](auto& item) {
    if (item == nullptr || !item->is_idempotent()) {
      return true;  // true means the entry is not valid here.
    }

    // To keep from having to iterate over the entries
    // again, we'll try calculating the size. If any
    // of the entries are unable to provide a size, then
    // we assume we cannot safely treat this entry as
    // idempotent even if it claims to be.
    if (item->size().has_value()) {
      size += item->size().value();
    } else {
      return true;  // true means the entry is not valid here.
    }

    return false;
  };

  if (std::any_of(list.begin(), list.end(), isInvalid)) {
    return nullptr;
  }

  return std::make_shared<DataQueueImpl>(std::move(list), size);
}

std::shared_ptr<DataQueue> DataQueue::Create(std::optional<uint64_t> capped) {
  return std::make_shared<DataQueueImpl>(capped);
}

std::unique_ptr<DataQueue::Entry> DataQueue::CreateInMemoryEntryFromView(
    Local<ArrayBufferView> view) {
  // If the view is not detachable, we do not want to create an InMemoryEntry
  // from it. Why? Because if we're not able to detach the backing store from
  // the underlying buffer, something else could modify the buffer while we're
  // holding the reference, which means we cannot guarantee that reads will be
  // idempotent.
  if (!view->Buffer()->IsDetachable()) {
    return nullptr;
  }
  auto store = view->Buffer()->GetBackingStore();
  auto offset = view->ByteOffset();
  auto length = view->ByteLength();
  USE(view->Buffer()->Detach(Local<Value>()));
  return CreateInMemoryEntryFromBackingStore(std::move(store), offset, length);
}

std::unique_ptr<DataQueue::Entry>
DataQueue::CreateInMemoryEntryFromBackingStore(
    std::shared_ptr<BackingStore> store, uint64_t offset, uint64_t length) {
  CHECK(store);
  if (offset + length > store->ByteLength()) {
    return nullptr;
  }
  return std::make_unique<InMemoryEntry>(std::move(store), offset, length);
}

std::unique_ptr<DataQueue::Entry> DataQueue::CreateDataQueueEntry(
    std::shared_ptr<DataQueue> data_queue) {
  return std::make_unique<DataQueueEntry>(std::move(data_queue));
}

std::unique_ptr<DataQueue::Entry> DataQueue::CreateFdEntry(Environment* env,
                                                           Local<Value> path) {
  return FdEntry::Create(env, path);
}

void DataQueue::Initialize(Environment* env, v8::Local<v8::Object> target) {
  // Nothing to do here currently.
}

void DataQueue::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  // Nothing to do here currently.
}

}  // namespace node
