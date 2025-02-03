#ifndef SRC_BLOB_SERIALIZER_DESERIALIZER_INL_H_
#define SRC_BLOB_SERIALIZER_DESERIALIZER_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "blob_serializer_deserializer.h"

#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "debug_utils-inl.h"

// This is related to the blob that is used in snapshots and single executable
// applications and has nothing to do with `node_blob.h`.

namespace node {

struct EnvSerializeInfo;
struct PropInfo;
struct RealmSerializeInfo;

namespace builtins {
struct CodeCacheInfo;
}  // namespace builtins

// These operator<< overload declarations are needed because
// BlobSerializerDeserializer::ToStr() uses these.

std::ostream& operator<<(std::ostream& output,
                         const builtins::CodeCacheInfo& info);

std::ostream& operator<<(std::ostream& output,
                         const std::vector<builtins::CodeCacheInfo>& vec);

std::ostream& operator<<(std::ostream& output, const std::vector<uint8_t>& vec);

std::ostream& operator<<(std::ostream& output,
                         const std::vector<PropInfo>& vec);

std::ostream& operator<<(std::ostream& output, const PropInfo& info);

std::ostream& operator<<(std::ostream& output,
                         const std::vector<std::string>& vec);

std::ostream& operator<<(std::ostream& output, const RealmSerializeInfo& i);

std::ostream& operator<<(std::ostream& output, const EnvSerializeInfo& i);

template <typename... Args>
void BlobSerializerDeserializer::Debug(const char* format,
                                       Args&&... args) const {
  if (is_debug) {
    FPrintF(stderr, format, std::forward<Args>(args)...);
  }
}

template <typename T>
std::string BlobSerializerDeserializer::ToStr(const T& arg) const {
  std::stringstream ss;
  ss << arg;
  return ss.str();
}

template <typename T>
std::string BlobSerializerDeserializer::GetName() const {
#define TYPE_LIST(V)                                                           \
  V(builtins::CodeCacheInfo)                                                   \
  V(PropInfo)                                                                  \
  V(std::string)

#define V(TypeName)                                                            \
  if constexpr (std::is_same_v<T, TypeName>) {                                 \
    return #TypeName;                                                          \
  } else  // NOLINT(readability/braces)
  TYPE_LIST(V)
#undef V

  if constexpr (std::is_arithmetic_v<T>) {
    return (std::is_unsigned_v<T>   ? "uint"
            : std::is_integral_v<T> ? "int"
                                    : "float") +
           std::to_string(sizeof(T) * 8) + "_t";
  }
  return "";
}

// Helper for reading numeric types.
template <typename Impl>
template <typename T>
T BlobDeserializer<Impl>::ReadArithmetic() {
  static_assert(std::is_arithmetic_v<T>, "Not an arithmetic type");
  T result;
  ReadArithmetic(&result, 1);
  return result;
}

// Layout of vectors:
// [ 4/8 bytes ] count
// [   ...     ] contents (count * size of individual elements)
template <typename Impl>
template <typename T>
std::vector<T> BlobDeserializer<Impl>::ReadVector() {
  if (is_debug) {
    std::string name = GetName<T>();
    Debug("\nReadVector<%s>()(%d-byte)\n", name.c_str(), sizeof(T));
  }
  size_t count = static_cast<size_t>(ReadArithmetic<size_t>());
  if (count == 0) {
    return std::vector<T>();
  }
  if (is_debug) {
    Debug("Reading %d vector elements...\n", count);
  }
  std::vector<T> result;
  if constexpr (std::is_arithmetic_v<T>) {
    result = ReadArithmeticVector<T>(count);
  } else {
    result = ReadNonArithmeticVector<T>(count);
  }
  if (is_debug) {
    std::string str = std::is_arithmetic_v<T> ? "" : ToStr(result);
    std::string name = GetName<T>();
    Debug("ReadVector<%s>() read %s\n", name.c_str(), str.c_str());
  }
  return result;
}

template <typename Impl>
std::string BlobDeserializer<Impl>::ReadString() {
  std::string_view view = ReadStringView(StringLogMode::kAddressAndContent);
  return std::string(view);
}

template <typename Impl>
std::string_view BlobDeserializer<Impl>::ReadStringView(StringLogMode mode) {
  size_t length = ReadArithmetic<size_t>();
  Debug("ReadStringView(), length=%zu: ", length);

  if (length == 0) {
    Debug("ReadStringView() read an empty view\n");
    return std::string_view();
  }

  std::string_view result(sink.data() + read_total, length);
  Debug("%p, read %zu bytes", result.data(), result.size());
  if (mode == StringLogMode::kAddressAndContent) {
    Debug(", content:%s%s", length > 32 ? "\n" : " ", result);
  }
  Debug("\n");

  read_total += length;
  return result;
}

// Helper for reading an array of numeric types.
template <typename Impl>
template <typename T>
void BlobDeserializer<Impl>::ReadArithmetic(T* out, size_t count) {
  static_assert(std::is_arithmetic_v<T>, "Not an arithmetic type");
  DCHECK_GT(count, 0);  // Should not read contents for vectors of size 0.
  if (is_debug) {
    std::string name = GetName<T>();
    Debug("Read<%s>()(%d-byte), count=%d: ", name.c_str(), sizeof(T), count);
  }

  size_t size = sizeof(T) * count;
  memcpy(out, sink.data() + read_total, size);

  if (is_debug) {
    std::string str =
        "{ " + std::to_string(out[0]) + (count > 1 ? ", ... }" : " }");
    Debug("%s, read %zu bytes\n", str.c_str(), size);
  }
  read_total += size;
}

// Helper for reading numeric vectors.
template <typename Impl>
template <typename Number>
std::vector<Number> BlobDeserializer<Impl>::ReadArithmeticVector(size_t count) {
  static_assert(std::is_arithmetic_v<Number>, "Not an arithmetic type");
  DCHECK_GT(count, 0);  // Should not read contents for vectors of size 0.
  std::vector<Number> result(count);
  ReadArithmetic(result.data(), count);
  return result;
}

// Helper for reading non-numeric vectors.
template <typename Impl>
template <typename T>
std::vector<T> BlobDeserializer<Impl>::ReadNonArithmeticVector(size_t count) {
  static_assert(!std::is_arithmetic_v<T>, "Arithmetic type");
  DCHECK_GT(count, 0);  // Should not read contents for vectors of size 0.
  std::vector<T> result;
  result.reserve(count);
  bool original_is_debug = is_debug;
  is_debug = original_is_debug && !std::is_same_v<T, std::string>;
  for (size_t i = 0; i < count; ++i) {
    if (is_debug) {
      Debug("\n[%d] ", i);
    }
    result.push_back(ReadElement<T>());
  }
  is_debug = original_is_debug;

  return result;
}

template <typename Impl>
template <typename T>
T BlobDeserializer<Impl>::ReadElement() {
  if constexpr (std::is_arithmetic_v<T>) {
    return ReadArithmetic<T>();
  } else if constexpr (std::is_same_v<T, std::string>) {
    return ReadString();
  } else {
    return impl()->template Read<T>();
  }
}

// Helper for writing numeric types.
template <typename Impl>
template <typename T>
size_t BlobSerializer<Impl>::WriteArithmetic(const T& data) {
  static_assert(std::is_arithmetic_v<T>, "Not an arithmetic type");
  return WriteArithmetic(&data, 1);
}

// Layout of vectors:
// [ 4/8 bytes ] count
// [   ...     ] contents (count * size of individual elements)
template <typename Impl>
template <typename T>
size_t BlobSerializer<Impl>::WriteVector(const std::vector<T>& data) {
  if (is_debug) {
    std::string str = std::is_arithmetic_v<T> ? "" : ToStr(data);
    std::string name = GetName<T>();
    Debug("\nAt 0x%x: WriteVector<%s>() (%d-byte), count=%d: %s\n",
          sink.size(),
          name.c_str(),
          sizeof(T),
          data.size(),
          str.c_str());
  }

  size_t written_total = WriteArithmetic<size_t>(data.size());
  if (data.empty()) {
    return written_total;
  }

  if constexpr (std::is_arithmetic_v<T>) {
    written_total += WriteArithmeticVector<T>(data);
  } else {
    written_total += WriteNonArithmeticVector<T>(data);
  }

  if (is_debug) {
    std::string name = GetName<T>();
    Debug("WriteVector<%s>() wrote %d bytes\n", name.c_str(), written_total);
  }

  return written_total;
}

// The layout of a written string:
// [  4/8 bytes     ] length
// [ |length| bytes ] contents
template <typename Impl>
size_t BlobSerializer<Impl>::WriteStringView(std::string_view data,
                                             StringLogMode mode) {
  Debug("At 0x%x: WriteStringView(), length=%zu: %p\n",
        sink.size(),
        data.size(),
        data.data());
  size_t written_total = WriteArithmetic<size_t>(data.size());

  size_t length = data.size();
  if (length == 0) {
    Debug("WriteStringView() wrote an empty view\n");
    return written_total;
  }
  sink.insert(sink.end(), data.data(), data.data() + length);
  written_total += length;

  Debug("WriteStringView() wrote %zu bytes\n", written_total);
  if (mode == StringLogMode::kAddressAndContent) {
    Debug("%s", data);
  }

  return written_total;
}

template <typename Impl>
size_t BlobSerializer<Impl>::WriteString(const std::string& data) {
  return WriteStringView(data, StringLogMode::kAddressAndContent);
}

static size_t kPreviewCount = 16;

// Helper for writing an array of numeric types.
template <typename Impl>
template <typename T>
size_t BlobSerializer<Impl>::WriteArithmetic(const T* data, size_t count) {
  static_assert(std::is_arithmetic_v<T>, "Arithmetic type");
  DCHECK_GT(count, 0);  // Should not write contents for vectors of size 0.
  if (is_debug) {
    size_t preview_count = count < kPreviewCount ? count : kPreviewCount;
    std::string str = "{ ";
    for (size_t i = 0; i < preview_count; ++i) {
      str += (std::to_string(data[i]) + ",");
    }
    if (count > preview_count) {
      str += "...";
    }
    str += "}";
    std::string name = GetName<T>();
    Debug("At 0x%x: Write<%s>() (%zu-byte), count=%zu: %s",
          sink.size(),
          name.c_str(),
          sizeof(T),
          count,
          str.c_str());
  }

  size_t size = sizeof(T) * count;
  const char* pos = reinterpret_cast<const char*>(data);
  sink.insert(sink.end(), pos, pos + size);

  if (is_debug) {
    Debug(", wrote %zu bytes\n", size);
  }
  return size;
}

// Helper for writing numeric vectors.
template <typename Impl>
template <typename Number>
size_t BlobSerializer<Impl>::WriteArithmeticVector(
    const std::vector<Number>& data) {
  static_assert(std::is_arithmetic_v<Number>, "Arithmetic type");
  return WriteArithmetic(data.data(), data.size());
}

// Helper for writing non-numeric vectors.
template <typename Impl>
template <typename T>
size_t BlobSerializer<Impl>::WriteNonArithmeticVector(
    const std::vector<T>& data) {
  static_assert(!std::is_arithmetic_v<T>, "Arithmetic type");
  DCHECK_GT(data.size(),
            0);  // Should not write contents for vectors of size 0.
  size_t written_total = 0;
  bool original_is_debug = is_debug;
  is_debug = original_is_debug && !std::is_same_v<T, std::string>;
  for (size_t i = 0; i < data.size(); ++i) {
    if (is_debug) {
      Debug("\n[%d] ", i);
    }
    written_total += WriteElement<T>(data[i]);
  }
  is_debug = original_is_debug;

  return written_total;
}

template <typename Impl>
template <typename T>
size_t BlobSerializer<Impl>::WriteElement(const T& data) {
  if constexpr (std::is_arithmetic_v<T>) {
    return WriteArithmetic<T>(data);
  } else if constexpr (std::is_same_v<T, std::string>) {
    return WriteString(data);
  } else {
    return impl()->template Write<T>(data);
  }
}

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_BLOB_SERIALIZER_DESERIALIZER_INL_H_
