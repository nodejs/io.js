// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/zlib/google/zip_reader.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/i18n/icu_string_conversions.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "third_party/zlib/google/redact.h"
#include "third_party/zlib/google/zip_internal.h"

#if defined(USE_SYSTEM_MINIZIP)
#include <minizip/unzip.h>
#else
#include "third_party/zlib/contrib/minizip/unzip.h"
#if defined(OS_WIN)
#include "third_party/zlib/contrib/minizip/iowin32.h"
#endif  // defined(OS_WIN)
#endif  // defined(USE_SYSTEM_MINIZIP)

#if defined(OS_POSIX)
#include <sys/stat.h>
#endif

namespace zip {
namespace {

enum UnzipError : int;

std::ostream& operator<<(std::ostream& out, UnzipError error) {
#define SWITCH_ERR(X) \
  case X:             \
    return out << #X;
  switch (error) {
    SWITCH_ERR(UNZ_OK);
    SWITCH_ERR(UNZ_END_OF_LIST_OF_FILE);
    SWITCH_ERR(UNZ_ERRNO);
    SWITCH_ERR(UNZ_PARAMERROR);
    SWITCH_ERR(UNZ_BADZIPFILE);
    SWITCH_ERR(UNZ_INTERNALERROR);
    SWITCH_ERR(UNZ_CRCERROR);
    default:
      return out << "UNZ" << static_cast<int>(error);
  }
#undef SWITCH_ERR
}

bool IsValidFileNameCharacterOnWindows(char16_t c) {
  if (c < 32)
    return false;

  switch (c) {
    case '<':   // Less than
    case '>':   // Greater than
    case ':':   // Colon
    case '"':   // Double quote
    case '|':   // Vertical bar or pipe
    case '?':   // Question mark
    case '*':   // Asterisk
    case '/':   // Forward slash
    case '\\':  // Backslash
      return false;
  }

  return true;
}

// A writer delegate that writes to a given string.
class StringWriterDelegate : public WriterDelegate {
 public:
  explicit StringWriterDelegate(std::string* output) : output_(output) {}

  // WriterDelegate methods:
  bool WriteBytes(const char* data, int num_bytes) override {
    output_->append(data, num_bytes);
    return true;
  }

 private:
  std::string* const output_;
};

#if defined(OS_POSIX)
void SetPosixFilePermissions(int fd, int mode) {
  base::stat_wrapper_t sb;
  if (base::File::Fstat(fd, &sb)) {
    return;
  }
  mode_t new_mode = sb.st_mode;
  // Transfer the executable bit only if the file is readable.
  if ((sb.st_mode & S_IRUSR) == S_IRUSR && (mode & S_IXUSR) == S_IXUSR) {
    new_mode |= S_IXUSR;
  }
  if ((sb.st_mode & S_IRGRP) == S_IRGRP && (mode & S_IXGRP) == S_IXGRP) {
    new_mode |= S_IXGRP;
  }
  if ((sb.st_mode & S_IROTH) == S_IROTH && (mode & S_IXOTH) == S_IXOTH) {
    new_mode |= S_IXOTH;
  }
  if (new_mode != sb.st_mode) {
    fchmod(fd, new_mode);
  }
}
#endif

}  // namespace

ZipReader::ZipReader() {
  Reset();
}

ZipReader::~ZipReader() {
  Close();
}

bool ZipReader::Open(const base::FilePath& zip_path) {
  DCHECK(!zip_file_);

  // Use of "Unsafe" function does not look good, but there is no way to do
  // this safely on Linux. See file_util.h for details.
  zip_file_ = internal::OpenForUnzipping(zip_path.AsUTF8Unsafe());
  if (!zip_file_) {
    LOG(ERROR) << "Cannot open ZIP archive " << Redact(zip_path);
    return false;
  }

  return OpenInternal();
}

bool ZipReader::OpenFromPlatformFile(base::PlatformFile zip_fd) {
  DCHECK(!zip_file_);

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  zip_file_ = internal::OpenFdForUnzipping(zip_fd);
#elif defined(OS_WIN)
  zip_file_ = internal::OpenHandleForUnzipping(zip_fd);
#endif
  if (!zip_file_) {
    LOG(ERROR) << "Cannot open ZIP from file handle " << zip_fd;
    return false;
  }

  return OpenInternal();
}

bool ZipReader::OpenFromString(const std::string& data) {
  zip_file_ = internal::PrepareMemoryForUnzipping(data);
  if (!zip_file_)
    return false;
  return OpenInternal();
}

void ZipReader::Close() {
  if (zip_file_) {
    if (const UnzipError err{unzClose(zip_file_)}; err != UNZ_OK) {
      LOG(ERROR) << "Error while closing ZIP archive: " << err;
    }
  }
  Reset();
}

const ZipReader::Entry* ZipReader::Next() {
  DCHECK(zip_file_);

  if (reached_end_)
    return nullptr;

  DCHECK(ok_);

  // Move to the next entry if we're not trying to open the first entry.
  if (next_index_ > 0) {
    if (const UnzipError err{unzGoToNextFile(zip_file_)}; err != UNZ_OK) {
      reached_end_ = true;
      if (err != UNZ_END_OF_LIST_OF_FILE) {
        LOG(ERROR) << "Cannot go to next entry in ZIP: " << err;
        ok_ = false;
      }
      return nullptr;
    }
  }

  next_index_++;

  if (!OpenEntry()) {
    reached_end_ = true;
    ok_ = false;
    return nullptr;
  }

  return &entry_;
}

bool ZipReader::OpenEntry() {
  DCHECK(zip_file_);

  // Get entry info.
  unz_file_info64 info = {};
  char path_in_zip[internal::kZipMaxPath] = {};
  if (const UnzipError err{unzGetCurrentFileInfo64(
          zip_file_, &info, path_in_zip, sizeof(path_in_zip) - 1, nullptr, 0,
          nullptr, 0)};
      err != UNZ_OK) {
    LOG(ERROR) << "Cannot get entry from ZIP: " << err;
    return false;
  }

  entry_.path_in_original_encoding = path_in_zip;

  // Convert path from original encoding to Unicode.
  std::u16string path_in_utf16;
  const char* const encoding = encoding_.empty() ? "UTF-8" : encoding_.c_str();
  if (!base::CodepageToUTF16(entry_.path_in_original_encoding, encoding,
                             base::OnStringConversionError::SUBSTITUTE,
                             &path_in_utf16)) {
    LOG(ERROR) << "Cannot convert path from encoding " << encoding;
    return false;
  }

  // Normalize path.
  Normalize(path_in_utf16);

  entry_.original_size = info.uncompressed_size;

  // The file content of this entry is encrypted if flag bit 0 is set.
  entry_.is_encrypted = info.flag & 1;
  if (entry_.is_encrypted) {
    // Is the entry AES encrypted.
    entry_.uses_aes_encryption = info.compression_method == 99;
  } else {
    entry_.uses_aes_encryption = false;
  }

  // Construct the last modified time. The timezone info is not present in ZIP
  // archives, so we construct the time as UTC.
  const base::Time::Exploded exploded_time = {
      .year = static_cast<int>(info.tmu_date.tm_year),
      .month =
          static_cast<int>(info.tmu_date.tm_mon + 1),  // 0-based vs 1-based
      .day_of_month = static_cast<int>(info.tmu_date.tm_mday),
      .hour = static_cast<int>(info.tmu_date.tm_hour),
      .minute = static_cast<int>(info.tmu_date.tm_min),
      .second = static_cast<int>(info.tmu_date.tm_sec)};

  if (!base::Time::FromUTCExploded(exploded_time, &entry_.last_modified))
    entry_.last_modified = base::Time::UnixEpoch();

#if defined(OS_POSIX)
  entry_.posix_mode = (info.external_fa >> 16L) & (S_IRWXU | S_IRWXG | S_IRWXO);
#else
  entry_.posix_mode = 0;
#endif

  return true;
}

void ZipReader::Normalize(std::u16string_view in) {
  entry_.is_unsafe = true;

  // Directory entries in ZIP have a path ending with "/".
  entry_.is_directory = base::EndsWith(in, u"/");

  std::u16string normalized_path;
  if (base::StartsWith(in, u"/")) {
    normalized_path = u"ROOT";
    entry_.is_unsafe = false;
  }

  for (;;) {
    // Consume initial path separators.
    const std::u16string_view::size_type i = in.find_first_not_of(u'/');
    if (i == std::u16string_view::npos) {
      break;
    }

    in.remove_prefix(i);
    DCHECK(!in.empty());

    // Isolate next path component.
    const std::u16string_view part = in.substr(0, in.find_first_of(u'/'));
    DCHECK(!part.empty());

    in.remove_prefix(part.size());

    if (!normalized_path.empty())
      normalized_path += u'/';

    if (part == u".") {
      normalized_path += u"DOT";
      entry_.is_unsafe = true;
      continue;
    }

    if (part == u"..") {
      normalized_path += u"UP";
      entry_.is_unsafe = true;
      continue;
    }

    // Windows has more restrictions than other systems when it comes to valid
    // file paths. Replace Windows-invalid characters on all systems for
    // consistency. In particular, this prevents a path component containing
    // colon and backslash from being misinterpreted as an absolute path on
    // Windows.
    for (const char16_t c : part) {
      normalized_path += IsValidFileNameCharacterOnWindows(c) ? c : 0xFFFD;
    }

    entry_.is_unsafe = false;
  }

  // If the entry is a directory, add the final path separator to the entry
  // path.
  if (entry_.is_directory && !normalized_path.empty()) {
    normalized_path += u'/';
    entry_.is_unsafe = false;
  }

  entry_.path = base::FilePath::FromUTF16Unsafe(normalized_path);

  // By construction, we should always get a relative path.
  DCHECK(!entry_.path.IsAbsolute()) << entry_.path;
}

void ZipReader::ReportProgress(ListenerCallback listener_callback,
                               uint64_t bytes) const {
  delta_bytes_read_ += bytes;

  const base::TimeTicks now = base::TimeTicks::Now();
  if (next_progress_report_time_ > now)
    return;

  next_progress_report_time_ = now + progress_period_;
  listener_callback.Run(delta_bytes_read_);
  delta_bytes_read_ = 0;
}

bool ZipReader::ExtractCurrentEntry(WriterDelegate* delegate,
                                    ListenerCallback listener_callback,
                                    uint64_t num_bytes_to_extract) const {
  DCHECK(zip_file_);
  DCHECK_LT(0, next_index_);
  DCHECK(ok_);
  DCHECK(!reached_end_);

  // Use password only for encrypted files. For non-encrypted files, no password
  // is needed, and must be nullptr.
  const char* const password =
      entry_.is_encrypted ? password_.c_str() : nullptr;
  if (const UnzipError err{unzOpenCurrentFilePassword(zip_file_, password)};
      err != UNZ_OK) {
    LOG(ERROR) << "Cannot open file " << Redact(entry_.path)
               << " from ZIP: " << err;
    return false;
  }

  DCHECK(delegate);
  if (!delegate->PrepareOutput())
    return false;

  uint64_t remaining_capacity = num_bytes_to_extract;
  bool entire_file_extracted = false;

  while (remaining_capacity > 0) {
    char buf[internal::kZipBufSize];
    const int num_bytes_read =
        unzReadCurrentFile(zip_file_, buf, internal::kZipBufSize);

    if (num_bytes_read == 0) {
      entire_file_extracted = true;
      break;
    }

    if (num_bytes_read < 0) {
      LOG(ERROR) << "Cannot read file " << Redact(entry_.path)
                 << " from ZIP: " << UnzipError(num_bytes_read);
      break;
    }

    if (listener_callback) {
      ReportProgress(listener_callback, num_bytes_read);
    }

    DCHECK_LT(0, num_bytes_read);
    CHECK_LE(num_bytes_read, internal::kZipBufSize);

    uint64_t num_bytes_to_write = std::min<uint64_t>(
        remaining_capacity, base::checked_cast<uint64_t>(num_bytes_read));
    if (!delegate->WriteBytes(buf, num_bytes_to_write))
      break;

    if (remaining_capacity == base::checked_cast<uint64_t>(num_bytes_read)) {
      // Ensures function returns true if the entire file has been read.
      const int n = unzReadCurrentFile(zip_file_, buf, 1);
      entire_file_extracted = (n == 0);
      LOG_IF(ERROR, n < 0) << "Cannot read file " << Redact(entry_.path)
                           << " from ZIP: " << UnzipError(n);
    }

    CHECK_GE(remaining_capacity, num_bytes_to_write);
    remaining_capacity -= num_bytes_to_write;
  }

  if (const UnzipError err{unzCloseCurrentFile(zip_file_)}; err != UNZ_OK) {
    LOG(ERROR) << "Cannot extract file " << Redact(entry_.path)
               << " from ZIP: " << err;
    entire_file_extracted = false;
  }

  if (entire_file_extracted) {
    delegate->SetPosixFilePermissions(entry_.posix_mode);
    if (entry_.last_modified != base::Time::UnixEpoch()) {
      delegate->SetTimeModified(entry_.last_modified);
    }
  } else {
    delegate->OnError();
  }

  if (listener_callback) {
    listener_callback.Run(delta_bytes_read_);
    delta_bytes_read_ = 0;
  }

  return entire_file_extracted;
}

bool ZipReader::ExtractCurrentEntry(WriterDelegate* delegate,
                                    uint64_t num_bytes_to_extract) const {
  return ExtractCurrentEntry(delegate, ListenerCallback(),
                             num_bytes_to_extract);
}

bool ZipReader::ExtractCurrentEntryWithListener(
    WriterDelegate* delegate,
    ListenerCallback listener_callback) const {
  return ExtractCurrentEntry(delegate, listener_callback);
}

void ZipReader::ExtractCurrentEntryToFilePathAsync(
    const base::FilePath& output_file_path,
    SuccessCallback success_callback,
    FailureCallback failure_callback,
    ProgressCallback progress_callback) {
  DCHECK(zip_file_);
  DCHECK_LT(0, next_index_);
  DCHECK(ok_);
  DCHECK(!reached_end_);

  // If this is a directory, just create it and return.
  if (entry_.is_directory) {
    if (base::CreateDirectory(output_file_path)) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, std::move(success_callback));
    } else {
      LOG(ERROR) << "Cannot create directory " << Redact(output_file_path);
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, std::move(failure_callback));
    }
    return;
  }

  // Use password only for encrypted files. For non-encrypted files, no password
  // is needed, and must be nullptr.
  const char* const password =
      entry_.is_encrypted ? password_.c_str() : nullptr;
  if (const UnzipError err{unzOpenCurrentFilePassword(zip_file_, password)};
      err != UNZ_OK) {
    LOG(ERROR) << "Cannot open file " << Redact(entry_.path)
               << " from ZIP: " << err;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(failure_callback));
    return;
  }

  base::FilePath output_dir_path = output_file_path.DirName();
  if (!base::CreateDirectory(output_dir_path)) {
    LOG(ERROR) << "Cannot create directory " << Redact(output_dir_path);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(failure_callback));
    return;
  }

  const int flags = base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE;
  base::File output_file(output_file_path, flags);

  if (!output_file.IsValid()) {
    LOG(ERROR) << "Cannot create file " << Redact(output_file_path);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(failure_callback));
    return;
  }

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ZipReader::ExtractChunk, weak_ptr_factory_.GetWeakPtr(),
                     std::move(output_file), std::move(success_callback),
                     std::move(failure_callback), std::move(progress_callback),
                     0 /* initial offset */));
}

bool ZipReader::ExtractCurrentEntryToString(uint64_t max_read_bytes,
                                            std::string* output) const {
  DCHECK(output);
  DCHECK(zip_file_);
  DCHECK_LT(0, next_index_);
  DCHECK(ok_);
  DCHECK(!reached_end_);

  output->clear();

  if (max_read_bytes == 0)
    return true;

  if (entry_.is_directory)
    return true;

  // The original_size is the best hint for the real size, so it saves doing
  // reallocations for the common case when the uncompressed size is correct.
  // However, we need to assume that the uncompressed size could be incorrect
  // therefore this function needs to read as much data as possible.
  output->reserve(base::checked_cast<size_t>(std::min<uint64_t>(
      max_read_bytes, base::checked_cast<uint64_t>(entry_.original_size))));

  StringWriterDelegate writer(output);
  return ExtractCurrentEntry(&writer, max_read_bytes);
}

bool ZipReader::OpenInternal() {
  DCHECK(zip_file_);

  unz_global_info zip_info = {};  // Zero-clear.
  if (const UnzipError err{unzGetGlobalInfo(zip_file_, &zip_info)};
      err != UNZ_OK) {
    LOG(ERROR) << "Cannot get ZIP info: " << err;
    return false;
  }

  num_entries_ = zip_info.number_entry;
  reached_end_ = (num_entries_ <= 0);
  ok_ = true;
  return true;
}

void ZipReader::Reset() {
  zip_file_ = nullptr;
  num_entries_ = 0;
  next_index_ = 0;
  reached_end_ = true;
  ok_ = false;
  delta_bytes_read_ = 0;
  entry_ = {};
}

void ZipReader::ExtractChunk(base::File output_file,
                             SuccessCallback success_callback,
                             FailureCallback failure_callback,
                             ProgressCallback progress_callback,
                             int64_t offset) {
  char buffer[internal::kZipBufSize];

  const int num_bytes_read =
      unzReadCurrentFile(zip_file_, buffer, internal::kZipBufSize);

  if (num_bytes_read == 0) {
    if (const UnzipError err{unzCloseCurrentFile(zip_file_)}; err != UNZ_OK) {
      LOG(ERROR) << "Cannot extract file " << Redact(entry_.path)
                 << " from ZIP: " << err;
      std::move(failure_callback).Run();
      return;
    }

    std::move(success_callback).Run();
    return;
  }

  if (num_bytes_read < 0) {
    LOG(ERROR) << "Cannot read file " << Redact(entry_.path)
               << " from ZIP: " << UnzipError(num_bytes_read);
    std::move(failure_callback).Run();
    return;
  }

  if (num_bytes_read != output_file.Write(offset, buffer, num_bytes_read)) {
    LOG(ERROR) << "Cannot write " << num_bytes_read
               << " bytes to file at offset " << offset;
    std::move(failure_callback).Run();
    return;
  }

  offset += num_bytes_read;
  progress_callback.Run(offset);

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&ZipReader::ExtractChunk, weak_ptr_factory_.GetWeakPtr(),
                     std::move(output_file), std::move(success_callback),
                     std::move(failure_callback), std::move(progress_callback),
                     offset));
}

// FileWriterDelegate ----------------------------------------------------------

FileWriterDelegate::FileWriterDelegate(base::File* file) : file_(file) {
  DCHECK(file_);
}

FileWriterDelegate::FileWriterDelegate(base::File owned_file)
    : owned_file_(std::move(owned_file)) {
  DCHECK_EQ(file_, &owned_file_);
}

FileWriterDelegate::~FileWriterDelegate() {}

bool FileWriterDelegate::PrepareOutput() {
  DCHECK(file_);

  if (!file_->IsValid()) {
    LOG(ERROR) << "File is not valid";
    return false;
  }

  const int64_t length = file_->GetLength();
  if (length < 0) {
    PLOG(ERROR) << "Cannot get length of file handle "
                << file_->GetPlatformFile();
    return false;
  }

  // Just log a warning if the file is not empty.
  // See crbug.com/1309879 and crbug.com/774762.
  LOG_IF(WARNING, length > 0)
      << "File handle " << file_->GetPlatformFile()
      << " is not empty: Its length is " << length << " bytes";

  return true;
}

bool FileWriterDelegate::WriteBytes(const char* data, int num_bytes) {
  int bytes_written = file_->WriteAtCurrentPos(data, num_bytes);
  if (bytes_written > 0)
    file_length_ += bytes_written;
  return bytes_written == num_bytes;
}

void FileWriterDelegate::SetTimeModified(const base::Time& time) {
  file_->SetTimes(base::Time::Now(), time);
}

void FileWriterDelegate::SetPosixFilePermissions(int mode) {
#if defined(OS_POSIX)
  zip::SetPosixFilePermissions(file_->GetPlatformFile(), mode);
#endif
}

void FileWriterDelegate::OnError() {
  file_length_ = 0;
  file_->SetLength(0);
}

// FilePathWriterDelegate ------------------------------------------------------

FilePathWriterDelegate::FilePathWriterDelegate(base::FilePath output_file_path)
    : FileWriterDelegate(base::File()),
      output_file_path_(std::move(output_file_path)) {}

FilePathWriterDelegate::~FilePathWriterDelegate() {}

bool FilePathWriterDelegate::PrepareOutput() {
  // We can't rely on parent directory entries being specified in the
  // zip, so we make sure they are created.
  if (const base::FilePath dir = output_file_path_.DirName();
      !base::CreateDirectory(dir)) {
    PLOG(ERROR) << "Cannot create directory " << Redact(dir);
    return false;
  }

  owned_file_.Initialize(output_file_path_,
                         base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  if (!owned_file_.IsValid()) {
    PLOG(ERROR) << "Cannot create file " << Redact(output_file_path_) << ": "
                << base::File::ErrorToString(owned_file_.error_details());
    return false;
  }

  const int64_t length = owned_file_.GetLength();
  if (length < 0) {
    PLOG(ERROR) << "Cannot get length of file " << Redact(output_file_path_);
    return false;
  }

  if (length > 0) {
    LOG(ERROR) << "File " << Redact(output_file_path_)
               << " is not empty: Its length is " << length << " bytes";
    return false;
  }

  return true;
}

void FilePathWriterDelegate::OnError() {
  FileWriterDelegate::OnError();
  owned_file_.Close();

  if (!base::DeleteFile(output_file_path_)) {
    LOG(ERROR) << "Cannot delete partially extracted file "
               << Redact(output_file_path_);
  }
}

}  // namespace zip
