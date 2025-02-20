// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_CRYPTO_CRYPTO_BIO_H_
#define SRC_CRYPTO_CRYPTO_BIO_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_crypto.h"
#include "openssl/bio.h"
#include "util.h"
#include "v8.h"

namespace node {

class Environment;

namespace crypto {
// This class represents buffers for OpenSSL I/O, implemented as a singly-linked
// list of chunks. It can be used either for writing data from Node to OpenSSL,
// or for reading data back, but not both.
// The structure is only accessed, and owned by, the OpenSSL BIOPointer
// (a.k.a. std::unique_ptr<BIO>).
class NodeBIO : public MemoryRetainer {
 public:
  ~NodeBIO() override;

  static ncrypto::BIOPointer New(Environment* env = nullptr);

  // NewFixed takes a copy of `len` bytes from `data` and returns a BIO that,
  // when read from, returns those bytes followed by EOF.
  static ncrypto::BIOPointer NewFixed(const char* data,
                                      size_t len,
                                      Environment* env = nullptr);

  // Move read head to next buffer if needed
  void TryMoveReadHead();

  // Allocate new buffer for write if needed
  void TryAllocateForWrite(size_t hint);

  // Read `len` bytes maximum into `out`, return actual number of read bytes
  size_t Read(char* out, size_t size);

  // Memory optimization:
  // Deallocate children of write head's child if they're empty
  void FreeEmpty();

  // Return pointer to internal data and amount of
  // contiguous data available to read
  char* Peek(size_t* size);

  // Return pointers and sizes of multiple internal data chunks available for
  // reading
  size_t PeekMultiple(char** out, size_t* size, size_t* count);

  // Find first appearance of `delim` in buffer or `limit` if `delim`
  // wasn't found.
  size_t IndexOf(char delim, size_t limit);

  // Discard all available data
  void Reset();

  // Put `len` bytes from `data` into buffer
  void Write(const char* data, size_t size);

  // Return pointer to contiguous block of reserved data and the size available
  // for future writes. Call Commit() once the write is complete.
  char* PeekWritable(size_t* size);

  // Specify how much data was written into the block returned by
  // PeekWritable().
  void Commit(size_t size);


  // Return size of buffer in bytes
  inline size_t Length() const {
    return length_;
  }

  // Provide a hint about the size of the next pending set of writes. TLS
  // writes records of a maximum length of 16k of data plus a 5-byte header,
  // a MAC (up to 20 bytes for SSLv3, TLS 1.0, TLS 1.1, and up to 32 bytes
  // for TLS 1.2), and padding if a block cipher is used.  If there is a
  // large write this will result in potentially many buffers being
  // allocated and gc'ed which can cause long pauses. By providing a
  // guess about the amount of buffer space that will be needed in the
  // next allocation this overhead is removed.
  inline void set_allocate_tls_hint(size_t size) {
    constexpr size_t kThreshold = 16 * 1024;
    if (size >= kThreshold) {
      allocate_hint_ = (size / kThreshold + 1) * (kThreshold + 5 + 32);
    }
  }

  inline void set_eof_return(int num) {
    eof_return_ = num;
  }

  inline int eof_return() {
    return eof_return_;
  }

  inline void set_initial(size_t initial) {
    initial_ = initial;
  }

  static NodeBIO* FromBIO(BIO* bio);

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackFieldWithSize("buffer", length_, "NodeBIO::Buffer");
  }

  SET_MEMORY_INFO_NAME(NodeBIO)
  SET_SELF_SIZE(NodeBIO)

 private:
  static int New(BIO* bio);
  static int Free(BIO* bio);
  static int Read(BIO* bio, char* out, int len);
  static int Write(BIO* bio, const char* data, int len);
  static int Puts(BIO* bio, const char* str);
  static int Gets(BIO* bio, char* out, int size);
  static long Ctrl(BIO* bio, int cmd, long num,  // NOLINT(runtime/int)
                   void* ptr);

  static const BIO_METHOD* GetMethod();

  // Enough to handle the most of the client hellos
  static const size_t kInitialBufferLength = 1024;
  static const size_t kThroughputBufferLength = 16384;

  class Buffer {
   public:
    Buffer(Environment* env, size_t len) : env_(env),
                                           read_pos_(0),
                                           write_pos_(0),
                                           len_(len),
                                           next_(nullptr) {
      data_ = new char[len];
      if (env_ != nullptr) {
        env_->external_memory_accounter()->Increase(env_->isolate(), len);
      }
    }

    ~Buffer() {
      delete[] data_;
      if (env_ != nullptr) {
        env_->external_memory_accounter()->Decrease(env_->isolate(), len_);
      }
    }

    Environment* env_;
    size_t read_pos_;
    size_t write_pos_;
    size_t len_;
    Buffer* next_;
    char* data_;
  };

  Environment* env_ = nullptr;
  size_t initial_ = kInitialBufferLength;
  size_t length_ = 0;
  size_t allocate_hint_ = 0;
  int eof_return_ = -1;
  Buffer* read_head_ = nullptr;
  Buffer* write_head_ = nullptr;
};

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_CRYPTO_CRYPTO_BIO_H_
