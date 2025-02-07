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

#include "crypto/crypto_bio.h"
#include "base_object-inl.h"
#include "memory_tracker-inl.h"
#include "util-inl.h"

#include <openssl/bio.h>

#include <climits>
#include <cstring>

namespace node {

using ncrypto::BIOPointer;

namespace crypto {

BIOPointer NodeBIO::New(Environment* env) {
  auto bio = BIOPointer::New(GetMethod());
  if (bio && env != nullptr)
    NodeBIO::FromBIO(bio.get())->env_ = env;
  return bio;
}


BIOPointer NodeBIO::NewFixed(const char* data, size_t len, Environment* env) {
  BIOPointer bio = New(env);

  if (!bio || len > INT_MAX ||
      BIOPointer::Write(&bio, std::string_view(data, len)) !=
          static_cast<int>(len) ||
      BIO_set_mem_eof_return(bio.get(), 0) != 1) {
    return BIOPointer();
  }

  return bio;
}


int NodeBIO::New(BIO* bio) {
  BIO_set_data(bio, new NodeBIO());
  BIO_set_init(bio, 1);

  return 1;
}


int NodeBIO::Free(BIO* bio) {
  if (bio == nullptr)
    return 0;

  if (BIO_get_shutdown(bio)) {
    if (BIO_get_init(bio) && BIO_get_data(bio) != nullptr) {
      delete FromBIO(bio);
      BIO_set_data(bio, nullptr);
    }
  }

  return 1;
}


int NodeBIO::Read(BIO* bio, char* out, int len) {
  BIO_clear_retry_flags(bio);

  NodeBIO* nbio = FromBIO(bio);
  int bytes = nbio->Read(out, len);

  if (bytes == 0) {
    bytes = nbio->eof_return();
    if (bytes != 0) {
      BIO_set_retry_read(bio);
    }
  }

  return bytes;
}


char* NodeBIO::Peek(size_t* size) {
  *size = read_head_->write_pos_ - read_head_->read_pos_;
  return read_head_->data_ + read_head_->read_pos_;
}


size_t NodeBIO::PeekMultiple(char** out, size_t* size, size_t* count) {
  Buffer* pos = read_head_;
  size_t max = *count;
  size_t total = 0;

  size_t i;
  for (i = 0; i < max; i++) {
    size[i] = pos->write_pos_ - pos->read_pos_;
    total += size[i];
    out[i] = pos->data_ + pos->read_pos_;

    /* Don't get past write head */
    if (pos == write_head_)
      break;
    else
      pos = pos->next_;
  }

  if (i == max)
    *count = i;
  else
    *count = i + 1;

  return total;
}


int NodeBIO::Write(BIO* bio, const char* data, int len) {
  BIO_clear_retry_flags(bio);

  FromBIO(bio)->Write(data, len);

  return len;
}


int NodeBIO::Puts(BIO* bio, const char* str) {
  return Write(bio, str, strlen(str));
}


int NodeBIO::Gets(BIO* bio, char* out, int size) {
  NodeBIO* nbio = FromBIO(bio);

  if (nbio->Length() == 0)
    return 0;

  int i = nbio->IndexOf('\n', size);

  // Include '\n', if it's there.  If not, don't read off the end.
  if (i < size && i >= 0 && static_cast<size_t>(i) < nbio->Length())
    i++;

  // Shift `i` a bit to nullptr-terminate string later
  if (size == i)
    i--;

  // Flush read data
  nbio->Read(out, i);

  out[i] = 0;

  return i;
}


long NodeBIO::Ctrl(BIO* bio, int cmd, long num,  // NOLINT(runtime/int)
                   void* ptr) {
  NodeBIO* nbio;
  long ret;  // NOLINT(runtime/int)

  nbio = FromBIO(bio);
  ret = 1;

  switch (cmd) {
    case BIO_CTRL_RESET:
      nbio->Reset();
      break;
    case BIO_CTRL_EOF:
      ret = nbio->Length() == 0;
      break;
    case BIO_C_SET_BUF_MEM_EOF_RETURN:
      nbio->set_eof_return(num);
      break;
    case BIO_CTRL_INFO:
      ret = nbio->Length();
      if (ptr != nullptr)
        *reinterpret_cast<void**>(ptr) = nullptr;
      break;
    case BIO_C_SET_BUF_MEM:
      UNREACHABLE("Can't use SET_BUF_MEM_PTR with NodeBIO");
    case BIO_C_GET_BUF_MEM_PTR:
      UNREACHABLE("Can't use GET_BUF_MEM_PTR with NodeBIO");
    case BIO_CTRL_GET_CLOSE:
      ret = BIO_get_shutdown(bio);
      break;
    case BIO_CTRL_SET_CLOSE:
      BIO_set_shutdown(bio, num);
      break;
    case BIO_CTRL_WPENDING:
      ret = 0;
      break;
    case BIO_CTRL_PENDING:
      ret = nbio->Length();
      break;
    case BIO_CTRL_DUP:
    case BIO_CTRL_FLUSH:
      ret = 1;
      break;
    case BIO_CTRL_PUSH:
    case BIO_CTRL_POP:
    default:
      ret = 0;
      break;
  }
  return ret;
}


const BIO_METHOD* NodeBIO::GetMethod() {
  // Static initialization ensures that this is safe to use concurrently.
  static const BIO_METHOD* method = [&]() {
    BIO_METHOD* method = BIO_meth_new(BIO_TYPE_MEM, "node.js SSL buffer");
    BIO_meth_set_write(method, Write);
    BIO_meth_set_read(method, Read);
    BIO_meth_set_puts(method, Puts);
    BIO_meth_set_gets(method, Gets);
    BIO_meth_set_ctrl(method, Ctrl);
    BIO_meth_set_create(method, New);
    BIO_meth_set_destroy(method, Free);
    return method;
  }();

  return method;
}


void NodeBIO::TryMoveReadHead() {
  // `read_pos_` and `write_pos_` means the position of the reader and writer
  // inside the buffer, respectively. When they're equal - its safe to reset
  // them, because both reader and writer will continue doing their stuff
  // from new (zero) positions.
  while (read_head_->read_pos_ != 0 &&
         read_head_->read_pos_ == read_head_->write_pos_) {
    // Reset positions
    read_head_->read_pos_ = 0;
    read_head_->write_pos_ = 0;

    // Move read_head_ forward, just in case if there're still some data to
    // read in the next buffer.
    if (read_head_ != write_head_)
      read_head_ = read_head_->next_;
  }
}


size_t NodeBIO::Read(char* out, size_t size) {
  size_t bytes_read = 0;
  size_t expected = Length() > size ? size : Length();
  size_t offset = 0;
  size_t left = size;

  while (bytes_read < expected) {
    CHECK_LE(read_head_->read_pos_, read_head_->write_pos_);
    size_t avail = read_head_->write_pos_ - read_head_->read_pos_;
    if (avail > left)
      avail = left;

    // Copy data
    if (out != nullptr)
      memcpy(out + offset, read_head_->data_ + read_head_->read_pos_, avail);
    read_head_->read_pos_ += avail;

    // Move pointers
    bytes_read += avail;
    offset += avail;
    left -= avail;

    TryMoveReadHead();
  }
  CHECK_EQ(expected, bytes_read);
  length_ -= bytes_read;

  // Free all empty buffers, but write_head's child
  FreeEmpty();

  return bytes_read;
}


void NodeBIO::FreeEmpty() {
  if (write_head_ == nullptr)
    return;
  Buffer* child = write_head_->next_;
  if (child == write_head_ || child == read_head_)
    return;
  Buffer* cur = child->next_;
  if (cur == write_head_ || cur == read_head_)
    return;

  Buffer* prev = child;
  while (cur != read_head_) {
    CHECK_NE(cur, write_head_);
    CHECK_EQ(cur->write_pos_, cur->read_pos_);

    Buffer* next = cur->next_;
    delete cur;
    cur = next;
  }
  prev->next_ = cur;
}


size_t NodeBIO::IndexOf(char delim, size_t limit) {
  size_t bytes_read = 0;
  size_t max = Length() > limit ? limit : Length();
  size_t left = limit;
  Buffer* current = read_head_;

  while (bytes_read < max) {
    CHECK_LE(current->read_pos_, current->write_pos_);
    size_t avail = current->write_pos_ - current->read_pos_;
    if (avail > left)
      avail = left;

    // Walk through data
    char* tmp = current->data_ + current->read_pos_;
    size_t off = 0;
    while (off < avail && *tmp != delim) {
      off++;
      tmp++;
    }

    // Move pointers
    bytes_read += off;
    left -= off;

    // Found `delim`
    if (off != avail) {
      return bytes_read;
    }

    // Move to next buffer
    if (current->read_pos_ + avail == current->len_) {
      current = current->next_;
    }
  }
  CHECK_EQ(max, bytes_read);

  return max;
}


void NodeBIO::Write(const char* data, size_t size) {
  size_t offset = 0;
  size_t left = size;

  // Allocate initial buffer if the ring is empty
  TryAllocateForWrite(left);

  while (left > 0) {
    size_t to_write = left;
    CHECK_LE(write_head_->write_pos_, write_head_->len_);
    size_t avail = write_head_->len_ - write_head_->write_pos_;

    if (to_write > avail)
      to_write = avail;

    // Copy data
    memcpy(write_head_->data_ + write_head_->write_pos_,
           data + offset,
           to_write);

    // Move pointers
    left -= to_write;
    offset += to_write;
    length_ += to_write;
    write_head_->write_pos_ += to_write;
    CHECK_LE(write_head_->write_pos_, write_head_->len_);

    // Go to next buffer if there still are some bytes to write
    if (left != 0) {
      CHECK_EQ(write_head_->write_pos_, write_head_->len_);
      TryAllocateForWrite(left);
      write_head_ = write_head_->next_;

      // Additionally, since we're moved to the next buffer, read head
      // may be moved as well.
      TryMoveReadHead();
    }
  }
  CHECK_EQ(left, 0);
}


char* NodeBIO::PeekWritable(size_t* size) {
  TryAllocateForWrite(*size);

  size_t available = write_head_->len_ - write_head_->write_pos_;
  if (*size == 0 || available <= *size)
    *size = available;

  return write_head_->data_ + write_head_->write_pos_;
}


void NodeBIO::Commit(size_t size) {
  write_head_->write_pos_ += size;
  length_ += size;
  CHECK_LE(write_head_->write_pos_, write_head_->len_);

  // Allocate new buffer if write head is full,
  // and there're no other place to go
  TryAllocateForWrite(0);
  if (write_head_->write_pos_ == write_head_->len_) {
    write_head_ = write_head_->next_;

    // Additionally, since we're moved to the next buffer, read head
    // may be moved as well.
    TryMoveReadHead();
  }
}


void NodeBIO::TryAllocateForWrite(size_t hint) {
  Buffer* w = write_head_;
  Buffer* r = read_head_;
  // If write head is full, next buffer is either read head or not empty.
  if (w == nullptr ||
      (w->write_pos_ == w->len_ &&
       (w->next_ == r || w->next_->write_pos_ != 0))) {
    size_t len = w == nullptr ? initial_ :
                             kThroughputBufferLength;
    if (len < hint)
      len = hint;

    // If there is a one time allocation size hint, use it.
    if (allocate_hint_ > len) {
      len = allocate_hint_;
      allocate_hint_ = 0;
    }

    Buffer* next = new Buffer(env_, len);

    if (w == nullptr) {
      next->next_ = next;
      write_head_ = next;
      read_head_ = next;
    } else {
      next->next_ = w->next_;
      w->next_ = next;
    }
  }
}


void NodeBIO::Reset() {
  if (read_head_ == nullptr)
    return;

  while (read_head_->read_pos_ != read_head_->write_pos_) {
    CHECK(read_head_->write_pos_ > read_head_->read_pos_);

    length_ -= read_head_->write_pos_ - read_head_->read_pos_;
    read_head_->write_pos_ = 0;
    read_head_->read_pos_ = 0;

    read_head_ = read_head_->next_;
  }
  write_head_ = read_head_;
  CHECK_EQ(length_, 0);
}


NodeBIO::~NodeBIO() {
  if (read_head_ == nullptr)
    return;

  Buffer* current = read_head_;
  do {
    Buffer* next = current->next_;
    delete current;
    current = next;
  } while (current != read_head_);

  read_head_ = nullptr;
  write_head_ = nullptr;
}


NodeBIO* NodeBIO::FromBIO(BIO* bio) {
  CHECK_NOT_NULL(BIO_get_data(bio));
  return static_cast<NodeBIO*>(BIO_get_data(bio));
}


}  // namespace crypto
}  // namespace node
