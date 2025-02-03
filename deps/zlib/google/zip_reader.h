// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef THIRD_PARTY_ZLIB_GOOGLE_ZIP_READER_H_
#define THIRD_PARTY_ZLIB_GOOGLE_ZIP_READER_H_

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/time/time.h"

#if defined(USE_SYSTEM_MINIZIP)
#include <minizip/unzip.h>
#else
#include "third_party/zlib/contrib/minizip/unzip.h"
#endif

namespace zip {

// A delegate interface used to stream out an entry; see
// ZipReader::ExtractCurrentEntry.
class WriterDelegate {
 public:
  virtual ~WriterDelegate() {}

  // Invoked once before any data is streamed out to pave the way (e.g., to open
  // the output file). Return false on failure to cancel extraction.
  virtual bool PrepareOutput() { return true; }

  // Invoked to write the next chunk of data. Return false on failure to cancel
  // extraction.
  virtual bool WriteBytes(const char* data, int num_bytes) { return true; }

  // Sets the last-modified time of the data.
  virtual void SetTimeModified(const base::Time& time) {}

  // Called with the POSIX file permissions of the data; POSIX implementations
  // may apply some of the permissions (for example, the executable bit) to the
  // output file.
  virtual void SetPosixFilePermissions(int mode) {}

  // Called if an error occurred while extracting the file. The WriterDelegate
  // can then remove and clean up the partially extracted data.
  virtual void OnError() {}
};

// This class is used for reading ZIP archives. A typical use case of this class
// is to scan entries in a ZIP archive and extract them. The code will look
// like:
//
//   ZipReader reader;
//   if (!reader.Open(zip_path)) {
//     // Cannot open
//     return;
//   }
//
//   while (const ZipReader::entry* entry = reader.Next()) {
//     auto writer = CreateFilePathWriterDelegate(extract_dir, entry->path);
//     if (!reader.ExtractCurrentEntry(writer)) {
//           // Cannot extract
//           return;
//     }
//   }
//
//   if (!reader.ok()) {
//     // Error while enumerating entries
//     return;
//   }
//
class ZipReader {
 public:
  // A callback that is called when the operation is successful.
  using SuccessCallback = base::OnceClosure;
  // A callback that is called when the operation fails.
  using FailureCallback = base::OnceClosure;
  // A callback that is called periodically during the operation with the number
  // of bytes that have been processed so far.
  using ProgressCallback = base::RepeatingCallback<void(int64_t)>;
  // A callback that is called periodically during the operation with the number
  // of bytes that have been processed since the previous call (i.e. delta).
  using ListenerCallback = base::RepeatingCallback<void(uint64_t)>;

  // Information of an entry (file or directory) in a ZIP archive.
  struct Entry {
    // Path of this entry, in its original encoding as it is stored in the ZIP
    // archive. The encoding is not specified here. It might or might not be
    // UTF-8, and the caller needs to use other means to determine the encoding
    // if it wants to interpret this path correctly.
    std::string path_in_original_encoding;

    // Path of the entry, converted to Unicode. This path is relative (eg
    // "foo/bar.txt"). Absolute paths (eg "/foo/bar.txt") or paths containing
    // ".." or "." components (eg "../foo/bar.txt") are converted to safe
    // relative paths. Eg:
    // (In ZIP) -> (Entry.path)
    // /foo/bar -> ROOT/foo/bar
    // ../a     -> UP/a
    // ./a      -> DOT/a
    base::FilePath path;

    // Size of the original uncompressed file, or 0 if the entry is a directory.
    // This value should not be trusted, because it is stored as metadata in the
    // ZIP archive and can be different from the real uncompressed size.
    int64_t original_size;

    // Last modified time. If the timestamp stored in the ZIP archive is not
    // valid, the Unix epoch will be returned.
    //
    // The timestamp stored in the ZIP archive uses the MS-DOS date and time
    // format.
    //
    // http://msdn.microsoft.com/en-us/library/ms724247(v=vs.85).aspx
    //
    // As such the following limitations apply:
    // * Only years from 1980 to 2107 can be represented.
    // * The timestamp has a 2-second resolution.
    // * There is no timezone information, so the time is interpreted as UTC.
    base::Time last_modified;

    // True if the entry is a directory.
    // False if the entry is a file.
    bool is_directory = false;

    // True if the entry path cannot be converted to a safe relative path. This
    // happens if a file entry (not a directory) has a filename "." or "..".
    bool is_unsafe = false;

    // True if the file content is encrypted.
    bool is_encrypted = false;

    // True if the encryption scheme is AES.
    bool uses_aes_encryption = false;

    // Entry POSIX permissions (POSIX systems only).
    int posix_mode;
  };

  ZipReader();

  ZipReader(const ZipReader&) = delete;
  ZipReader& operator=(const ZipReader&) = delete;

  ~ZipReader();

  // Opens the ZIP archive specified by |zip_path|. Returns true on
  // success.
  bool Open(const base::FilePath& zip_path);

  // Opens the ZIP archive referred to by the platform file |zip_fd|, without
  // taking ownership of |zip_fd|. Returns true on success.
  bool OpenFromPlatformFile(base::PlatformFile zip_fd);

  // Opens the zip data stored in |data|. This class uses a weak reference to
  // the given sring while extracting files, i.e. the caller should keep the
  // string until it finishes extracting files.
  bool OpenFromString(const std::string& data);

  // Closes the currently opened ZIP archive. This function is called in the
  // destructor of the class, so you usually don't need to call this.
  void Close();

  // Sets the encoding of entry paths in the ZIP archive.
  // By default, paths are assumed to be in UTF-8.
  void SetEncoding(std::string encoding) { encoding_ = std::move(encoding); }

  // Sets the decryption password that will be used to decrypt encrypted file in
  // the ZIP archive.
  void SetPassword(std::string password) { password_ = std::move(password); }

  // Gets the next entry. Returns null if there is no more entry, or if an error
  // occurred while scanning entries. The returned Entry is owned by this
  // ZipReader, and is valid until Next() is called again or until this
  // ZipReader is closed.
  //
  // This function should be called before operations over the current entry
  // like ExtractCurrentEntryToFile().
  //
  // while (const ZipReader::Entry* entry = reader.Next()) {
  //   // Do something with the current entry here.
  //   ...
  // }
  //
  // // Finished scanning entries.
  // // Check if the scanning stopped because of an error.
  // if (!reader.ok()) {
  //   // There was an error.
  //   ...
  // }
  const Entry* Next();

  // Returns true if the enumeration of entries was successful, or false if it
  // stopped because of an error.
  bool ok() const { return ok_; }

  // Extracts |num_bytes_to_extract| bytes of the current entry to |delegate|,
  // starting from the beginning of the entry.
  //
  // Returns true if the entire file was extracted without error.
  //
  // Precondition: Next() returned a non-null Entry.
  bool ExtractCurrentEntry(WriterDelegate* delegate,
                           uint64_t num_bytes_to_extract =
                               std::numeric_limits<uint64_t>::max()) const;

  // Extracts the current entry to |delegate|, starting from the beginning
  // of the entry, calling |listener_callback| regularly with the number of
  // bytes extracted.
  //
  // Returns true if the entire file was extracted without error.
  //
  // Precondition: Next() returned a non-null Entry.
  bool ExtractCurrentEntryWithListener(
      WriterDelegate* delegate,
      ListenerCallback listener_callback) const;

  // Asynchronously extracts the current entry to the given output file path. If
  // the current entry is a directory it just creates the directory
  // synchronously instead.
  //
  // |success_callback| will be called on success and |failure_callback| will be
  // called on failure. |progress_callback| will be called at least once.
  // Callbacks will be posted to the current MessageLoop in-order.
  //
  // Precondition: Next() returned a non-null Entry.
  void ExtractCurrentEntryToFilePathAsync(
      const base::FilePath& output_file_path,
      SuccessCallback success_callback,
      FailureCallback failure_callback,
      ProgressCallback progress_callback);

  // Extracts the current entry into memory. If the current entry is a
  // directory, |*output| is set to the empty string. If the current entry is a
  // file, |*output| is filled with its contents.
  //
  // The value in |Entry::original_size| cannot be trusted, so the real size of
  // the uncompressed contents can be different. |max_read_bytes| limits the
  // amount of memory used to carry the entry.
  //
  // Returns true if the entire content is read without error. If the content is
  // bigger than |max_read_bytes|, this function returns false and |*output| is
  // filled with |max_read_bytes| of data. If an error occurs, this function
  // returns false and |*output| contains the content extracted so far, which
  // might be garbage data.
  //
  // Precondition: Next() returned a non-null Entry.
  bool ExtractCurrentEntryToString(uint64_t max_read_bytes,
                                   std::string* output) const;

  bool ExtractCurrentEntryToString(std::string* output) const {
    return ExtractCurrentEntryToString(
        base::checked_cast<uint64_t>(output->max_size()), output);
  }

  // Returns the number of entries in the ZIP archive.
  //
  // Precondition: one of the Open() methods returned true.
  int num_entries() const { return num_entries_; }

 private:
  // Common code used both in Open and OpenFromFd.
  bool OpenInternal();

  // Resets the internal state.
  void Reset();

  // Opens the current entry in the ZIP archive. On success, returns true and
  // updates the current entry state |entry_|.
  //
  // Note that there is no matching CloseEntry(). The current entry state is
  // reset automatically as needed.
  bool OpenEntry();

  // Normalizes the given path passed as UTF-16 string piece. Sets entry_.path,
  // entry_.is_directory and entry_.is_unsafe.
  void Normalize(std::u16string_view in);

  // Runs the ListenerCallback at a throttled rate.
  void ReportProgress(ListenerCallback listener_callback, uint64_t bytes) const;

  // Extracts |num_bytes_to_extract| bytes of the current entry to |delegate|,
  // starting from the beginning of the entry calling |listener_callback| if
  // its supplied.
  //
  // Returns true if the entire file was extracted without error.
  //
  // Precondition: Next() returned a non-null Entry.
  bool ExtractCurrentEntry(WriterDelegate* delegate,
                           ListenerCallback listener_callback,
                           uint64_t num_bytes_to_extract =
                               std::numeric_limits<uint64_t>::max()) const;

  // Extracts a chunk of the file to the target.  Will post a task for the next
  // chunk and success/failure/progress callbacks as necessary.
  void ExtractChunk(base::File target_file,
                    SuccessCallback success_callback,
                    FailureCallback failure_callback,
                    ProgressCallback progress_callback,
                    const int64_t offset);

  std::string encoding_;
  std::string password_;
  unzFile zip_file_;
  int num_entries_;
  int next_index_;
  bool reached_end_;
  bool ok_;
  Entry entry_;

  // Next time to report progress.
  mutable base::TimeTicks next_progress_report_time_ = base::TimeTicks::Now();

  // Progress time delta.
  // TODO(crbug.com/953256) Add this as parameter to the unzip options.
  base::TimeDelta progress_period_ = base::Milliseconds(1000);

  // Number of bytes read since last progress report callback executed.
  mutable uint64_t delta_bytes_read_ = 0;

  base::WeakPtrFactory<ZipReader> weak_ptr_factory_{this};
};

// A writer delegate that writes to a given File. It is recommended that this
// file be initially empty.
class FileWriterDelegate : public WriterDelegate {
 public:
  // Constructs a FileWriterDelegate that manipulates |file|. The delegate will
  // not own |file|, therefore the caller must guarantee |file| will outlive the
  // delegate.
  explicit FileWriterDelegate(base::File* file);

  // Constructs a FileWriterDelegate that takes ownership of |file|.
  explicit FileWriterDelegate(base::File owned_file);

  FileWriterDelegate(const FileWriterDelegate&) = delete;
  FileWriterDelegate& operator=(const FileWriterDelegate&) = delete;

  ~FileWriterDelegate() override;

  // Returns true if the file handle passed to the constructor is valid.
  bool PrepareOutput() override;

  // Writes |num_bytes| bytes of |data| to the file, returning false on error or
  // if not all bytes could be written.
  bool WriteBytes(const char* data, int num_bytes) override;

  // Sets the last-modified time of the data.
  void SetTimeModified(const base::Time& time) override;

  // On POSIX systems, sets the file to be executable if the source file was
  // executable.
  void SetPosixFilePermissions(int mode) override;

  // Empties the file to avoid leaving garbage data in it.
  void OnError() override;

  // Gets the number of bytes written into the file.
  int64_t file_length() { return file_length_; }

 protected:
  // The delegate can optionally own the file it modifies, in which case
  // owned_file_ is set and file_ is an alias for owned_file_.
  base::File owned_file_;

  // The file the delegate modifies.
  base::File* const file_ = &owned_file_;

  int64_t file_length_ = 0;
};

// A writer delegate that creates and writes a file at a given path. This does
// not overwrite any existing file.
class FilePathWriterDelegate : public FileWriterDelegate {
 public:
  explicit FilePathWriterDelegate(base::FilePath output_file_path);

  FilePathWriterDelegate(const FilePathWriterDelegate&) = delete;
  FilePathWriterDelegate& operator=(const FilePathWriterDelegate&) = delete;

  ~FilePathWriterDelegate() override;

  // Creates the output file and any necessary intermediate directories. Does
  // not overwrite any existing file, and returns false if the output file
  // cannot be created because another file conflicts with it.
  bool PrepareOutput() override;

  // Deletes the output file.
  void OnError() override;

 private:
  const base::FilePath output_file_path_;
};

}  // namespace zip

#endif  // THIRD_PARTY_ZLIB_GOOGLE_ZIP_READER_H_
