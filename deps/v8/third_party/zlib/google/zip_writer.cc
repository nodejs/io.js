// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/zlib/google/zip_writer.h"

#include <algorithm>
#include <tuple>

#include "base/files/file.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "third_party/zlib/google/redact.h"
#include "third_party/zlib/google/zip_internal.h"

namespace zip {
namespace internal {

bool ZipWriter::ShouldContinue() {
  if (!progress_callback_)
    return true;

  const base::TimeTicks now = base::TimeTicks::Now();
  if (next_progress_report_time_ > now)
    return true;

  next_progress_report_time_ = now + progress_period_;
  if (progress_callback_.Run(progress_))
    return true;

  LOG(ERROR) << "Cancelling ZIP creation";
  return false;
}

bool ZipWriter::AddFileContent(const base::FilePath& path, base::File file) {
  char buf[zip::internal::kZipBufSize];

  while (ShouldContinue()) {
    const int num_bytes =
        file.ReadAtCurrentPos(buf, zip::internal::kZipBufSize);

    if (num_bytes < 0) {
      PLOG(ERROR) << "Cannot read file " << Redact(path);
      return false;
    }

    if (num_bytes == 0)
      return true;

    if (zipWriteInFileInZip(zip_file_, buf, num_bytes) != ZIP_OK) {
      PLOG(ERROR) << "Cannot write data from file " << Redact(path)
                  << " to ZIP";
      return false;
    }

    progress_.bytes += num_bytes;
  }

  return false;
}

bool ZipWriter::OpenNewFileEntry(const base::FilePath& path,
                                 bool is_directory,
                                 base::Time last_modified) {
  std::string str_path = path.AsUTF8Unsafe();

#if defined(OS_WIN)
  base::ReplaceSubstringsAfterOffset(&str_path, 0u, "\\", "/");
#endif

  Compression compression = kDeflated;

  if (is_directory) {
    str_path += "/";
  } else {
    compression = GetCompressionMethod(path);
  }

  return zip::internal::ZipOpenNewFileInZip(zip_file_, str_path, last_modified,
                                            compression);
}

bool ZipWriter::CloseNewFileEntry() {
  return zipCloseFileInZip(zip_file_) == ZIP_OK;
}

bool ZipWriter::AddFileEntry(const base::FilePath& path, base::File file) {
  base::File::Info info;
  if (!file.GetInfo(&info))
    return false;

  if (!OpenNewFileEntry(path, /*is_directory=*/false, info.last_modified))
    return false;

  if (!AddFileContent(path, std::move(file)))
    return false;

  progress_.files++;
  return CloseNewFileEntry();
}

bool ZipWriter::AddDirectoryEntry(const base::FilePath& path) {
  FileAccessor::Info info;
  if (!file_accessor_->GetInfo(path, &info) || !info.is_directory) {
    LOG(ERROR) << "Not a directory: " << Redact(path);
    progress_.errors++;
    return continue_on_error_;
  }

  if (!OpenNewFileEntry(path, /*is_directory=*/true, info.last_modified))
    return false;

  if (!CloseNewFileEntry())
    return false;

  progress_.directories++;
  if (!ShouldContinue())
    return false;

  if (!recursive_)
    return true;

  return AddDirectoryContents(path);
}

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
// static
std::unique_ptr<ZipWriter> ZipWriter::CreateWithFd(
    int zip_file_fd,
    FileAccessor* file_accessor) {
  DCHECK(zip_file_fd != base::kInvalidPlatformFile);
  zipFile zip_file =
      internal::OpenFdForZipping(zip_file_fd, APPEND_STATUS_CREATE);

  if (!zip_file) {
    DLOG(ERROR) << "Cannot create ZIP file for FD " << zip_file_fd;
    return nullptr;
  }

  return std::unique_ptr<ZipWriter>(new ZipWriter(zip_file, file_accessor));
}
#endif

// static
std::unique_ptr<ZipWriter> ZipWriter::Create(
    const base::FilePath& zip_file_path,
    FileAccessor* file_accessor) {
  DCHECK(!zip_file_path.empty());
  zipFile zip_file = internal::OpenForZipping(zip_file_path.AsUTF8Unsafe(),
                                              APPEND_STATUS_CREATE);

  if (!zip_file) {
    PLOG(ERROR) << "Cannot create ZIP file " << Redact(zip_file_path);
    return nullptr;
  }

  return std::unique_ptr<ZipWriter>(new ZipWriter(zip_file, file_accessor));
}

ZipWriter::ZipWriter(zipFile zip_file, FileAccessor* file_accessor)
    : zip_file_(zip_file), file_accessor_(file_accessor) {}

ZipWriter::~ZipWriter() {
  if (zip_file_)
    zipClose(zip_file_, nullptr);
}

bool ZipWriter::Close() {
  const bool success = zipClose(zip_file_, nullptr) == ZIP_OK;
  zip_file_ = nullptr;

  // Call the progress callback one last time with the final progress status.
  if (progress_callback_ && !progress_callback_.Run(progress_)) {
    LOG(ERROR) << "Cancelling ZIP creation at the end";
    return false;
  }

  return success;
}

bool ZipWriter::AddMixedEntries(Paths paths) {
  // Pointers to directory paths in |paths|.
  std::vector<const base::FilePath*> directories;

  // Declared outside loop to reuse internal buffer.
  std::vector<base::File> files;

  // First pass. We don't know which paths are files and which ones are
  // directories, and we want to avoid making a call to file_accessor_ for each
  // path. Try to open all of the paths as files. We'll get invalid file
  // descriptors for directories, and we'll process these directories in the
  // second pass.
  while (!paths.empty()) {
    // Work with chunks of 50 paths at most.
    const size_t n = std::min<size_t>(paths.size(), 50);
    Paths relative_paths;
    std::tie(relative_paths, paths) = paths.split_at(n);

    files.clear();
    if (!file_accessor_->Open(relative_paths, &files) || files.size() != n)
      return false;

    for (size_t i = 0; i < n; i++) {
      const base::FilePath& relative_path = relative_paths[i];
      DCHECK(!relative_path.empty());
      base::File& file = files[i];

      if (file.IsValid()) {
        // It's a file.
        if (!AddFileEntry(relative_path, std::move(file)))
          return false;
      } else {
        // It's probably a directory. Remember its path for the second pass.
        directories.push_back(&relative_path);
      }
    }
  }

  // Second pass for directories discovered during the first pass.
  for (const base::FilePath* const path : directories) {
    DCHECK(path);
    if (!AddDirectoryEntry(*path))
      return false;
  }

  return true;
}

bool ZipWriter::AddFileEntries(Paths paths) {
  // Declared outside loop to reuse internal buffer.
  std::vector<base::File> files;

  while (!paths.empty()) {
    // Work with chunks of 50 paths at most.
    const size_t n = std::min<size_t>(paths.size(), 50);
    Paths relative_paths;
    std::tie(relative_paths, paths) = paths.split_at(n);

    DCHECK_EQ(relative_paths.size(), n);

    files.clear();
    if (!file_accessor_->Open(relative_paths, &files) || files.size() != n)
      return false;

    for (size_t i = 0; i < n; i++) {
      const base::FilePath& relative_path = relative_paths[i];
      DCHECK(!relative_path.empty());
      base::File& file = files[i];

      if (!file.IsValid()) {
        LOG(ERROR) << "Cannot open " << Redact(relative_path);
        progress_.errors++;

        if (continue_on_error_)
          continue;

        return false;
      }

      if (!AddFileEntry(relative_path, std::move(file)))
        return false;
    }
  }

  return true;
}

bool ZipWriter::AddDirectoryEntries(Paths paths) {
  for (const base::FilePath& path : paths) {
    if (!AddDirectoryEntry(path))
      return false;
  }

  return true;
}

bool ZipWriter::AddDirectoryContents(const base::FilePath& path) {
  std::vector<base::FilePath> files, subdirs;

  if (!file_accessor_->List(path, &files, &subdirs)) {
    progress_.errors++;
    return continue_on_error_;
  }

  Filter(&files);
  Filter(&subdirs);

  if (!AddFileEntries(files))
    return false;

  return AddDirectoryEntries(subdirs);
}

void ZipWriter::Filter(std::vector<base::FilePath>* const paths) {
  DCHECK(paths);

  if (!filter_callback_)
    return;

  const auto end = std::remove_if(paths->begin(), paths->end(),
                                  [this](const base::FilePath& path) {
                                    return !filter_callback_.Run(path);
                                  });
  paths->erase(end, paths->end());
}

}  // namespace internal
}  // namespace zip
