// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/zlib/google/zip_internal.h"

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <string_view>

#include "base/containers/fixed_flat_set.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

#if defined(USE_SYSTEM_MINIZIP)
#include <minizip/ioapi.h>
#include <minizip/unzip.h>
#include <minizip/zip.h>
#else
#include "third_party/zlib/contrib/minizip/unzip.h"
#include "third_party/zlib/contrib/minizip/zip.h"
#if defined(OS_WIN)
#include "third_party/zlib/contrib/minizip/iowin32.h"
#elif defined(OS_POSIX)
#include "third_party/zlib/contrib/minizip/ioapi.h"
#endif  // defined(OS_POSIX)
#endif  // defined(USE_SYSTEM_MINIZIP)

namespace {

#if defined(OS_WIN)
typedef struct {
  HANDLE hf;
  int error;
} WIN32FILE_IOWIN;

// This function is derived from third_party/minizip/iowin32.c.
// Its only difference is that it treats the filename as UTF-8 and
// uses the Unicode version of CreateFile.
void* ZipOpenFunc(void* opaque, const void* filename, int mode) {
  DWORD desired_access = 0, creation_disposition = 0;
  DWORD share_mode = 0, flags_and_attributes = 0;
  HANDLE file = 0;
  void* ret = NULL;

  if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ) {
    desired_access = GENERIC_READ;
    creation_disposition = OPEN_EXISTING;
    share_mode = FILE_SHARE_READ;
  } else if (mode & ZLIB_FILEFUNC_MODE_EXISTING) {
    desired_access = GENERIC_WRITE | GENERIC_READ;
    creation_disposition = OPEN_EXISTING;
  } else if (mode & ZLIB_FILEFUNC_MODE_CREATE) {
    desired_access = GENERIC_WRITE | GENERIC_READ;
    creation_disposition = CREATE_ALWAYS;
  }

  if (filename != nullptr && desired_access != 0) {
    file = CreateFileW(
        base::UTF8ToWide(static_cast<const char*>(filename)).c_str(),
        desired_access, share_mode, nullptr, creation_disposition,
        flags_and_attributes, nullptr);
  }

  if (file == INVALID_HANDLE_VALUE)
    file = NULL;

  if (file != NULL) {
    WIN32FILE_IOWIN file_ret;
    file_ret.hf = file;
    file_ret.error = 0;
    ret = malloc(sizeof(WIN32FILE_IOWIN));
    if (ret == NULL)
      CloseHandle(file);
    else
      *(static_cast<WIN32FILE_IOWIN*>(ret)) = file_ret;
  }
  return ret;
}
#endif

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
// Callback function for zlib that opens a file stream from a file descriptor.
// Since we do not own the file descriptor, dup it so that we can fdopen/fclose
// a file stream.
void* FdOpenFileFunc(void* opaque, const void* filename, int mode) {
  FILE* file = NULL;
  const char* mode_fopen = NULL;

  if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) == ZLIB_FILEFUNC_MODE_READ)
    mode_fopen = "rb";
  else if (mode & ZLIB_FILEFUNC_MODE_EXISTING)
    mode_fopen = "r+b";
  else if (mode & ZLIB_FILEFUNC_MODE_CREATE)
    mode_fopen = "wb";

  if ((filename != NULL) && (mode_fopen != NULL)) {
    int fd = dup(*static_cast<int*>(opaque));
    if (fd != -1)
      file = fdopen(fd, mode_fopen);
  }

  return file;
}

int FdCloseFileFunc(void* opaque, void* stream) {
  fclose(static_cast<FILE*>(stream));
  free(opaque);  // malloc'ed in FillFdOpenFileFunc()
  return 0;
}

// Fills |pzlib_filecunc_def| appropriately to handle the zip file
// referred to by |fd|.
void FillFdOpenFileFunc(zlib_filefunc64_def* pzlib_filefunc_def, int fd) {
  fill_fopen64_filefunc(pzlib_filefunc_def);
  pzlib_filefunc_def->zopen64_file = FdOpenFileFunc;
  pzlib_filefunc_def->zclose_file = FdCloseFileFunc;
  int* ptr_fd = static_cast<int*>(malloc(sizeof(fd)));
  *ptr_fd = fd;
  pzlib_filefunc_def->opaque = ptr_fd;
}
#endif  // defined(OS_POSIX)

#if defined(OS_WIN)
// Callback function for zlib that opens a file stream from a Windows handle.
// Does not take ownership of the handle.
void* HandleOpenFileFunc(void* opaque, const void* /*filename*/, int mode) {
  WIN32FILE_IOWIN file_ret;
  file_ret.hf = static_cast<HANDLE>(opaque);
  file_ret.error = 0;
  if (file_ret.hf == INVALID_HANDLE_VALUE)
    return NULL;

  void* ret = malloc(sizeof(WIN32FILE_IOWIN));
  if (ret != NULL)
    *(static_cast<WIN32FILE_IOWIN*>(ret)) = file_ret;
  return ret;
}

int HandleCloseFileFunc(void* opaque, void* stream) {
  free(stream);  // malloc'ed in HandleOpenFileFunc()
  return 0;
}
#endif

// A struct that contains data required for zlib functions to extract files from
// a zip archive stored in memory directly. The following I/O API functions
// expect their opaque parameters refer to this struct.
struct ZipBuffer {
  const char* data;  // weak
  ZPOS64_T length;
  ZPOS64_T offset;
};

// Opens the specified file. When this function returns a non-NULL pointer, zlib
// uses this pointer as a stream parameter while compressing or uncompressing
// data. (Returning NULL represents an error.) This function initializes the
// given opaque parameter and returns it because this parameter stores all
// information needed for uncompressing data. (This function does not support
// writing compressed data and it returns NULL for this case.)
void* OpenZipBuffer(void* opaque, const void* /*filename*/, int mode) {
  if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ) {
    NOTREACHED_IN_MIGRATION();
    return NULL;
  }
  ZipBuffer* buffer = static_cast<ZipBuffer*>(opaque);
  if (!buffer || !buffer->data || !buffer->length)
    return NULL;
  buffer->offset = 0;
  return opaque;
}

// Reads compressed data from the specified stream. This function copies data
// refered by the opaque parameter and returns the size actually copied.
uLong ReadZipBuffer(void* opaque, void* /*stream*/, void* buf, uLong size) {
  ZipBuffer* buffer = static_cast<ZipBuffer*>(opaque);
  DCHECK_LE(buffer->offset, buffer->length);
  ZPOS64_T remaining_bytes = buffer->length - buffer->offset;
  if (!buffer || !buffer->data || !remaining_bytes)
    return 0;
  if (size > remaining_bytes)
    size = remaining_bytes;
  memcpy(buf, &buffer->data[buffer->offset], size);
  buffer->offset += size;
  return size;
}

// Writes compressed data to the stream. This function always returns zero
// because this implementation is only for reading compressed data.
uLong WriteZipBuffer(void* /*opaque*/,
                     void* /*stream*/,
                     const void* /*buf*/,
                     uLong /*size*/) {
  NOTREACHED_IN_MIGRATION();
  return 0;
}

// Returns the offset from the beginning of the data.
ZPOS64_T GetOffsetOfZipBuffer(void* opaque, void* /*stream*/) {
  ZipBuffer* buffer = static_cast<ZipBuffer*>(opaque);
  if (!buffer)
    return -1;
  return buffer->offset;
}

// Moves the current offset to the specified position.
long SeekZipBuffer(void* opaque,
                   void* /*stream*/,
                   ZPOS64_T offset,
                   int origin) {
  ZipBuffer* buffer = static_cast<ZipBuffer*>(opaque);
  if (!buffer)
    return -1;
  if (origin == ZLIB_FILEFUNC_SEEK_CUR) {
    buffer->offset = std::min(buffer->offset + offset, buffer->length);
    return 0;
  }
  if (origin == ZLIB_FILEFUNC_SEEK_END) {
    buffer->offset = (buffer->length > offset) ? buffer->length - offset : 0;
    return 0;
  }
  if (origin == ZLIB_FILEFUNC_SEEK_SET) {
    buffer->offset = std::min(buffer->length, offset);
    return 0;
  }
  NOTREACHED_IN_MIGRATION();
  return -1;
}

// Closes the input offset and deletes all resources used for compressing or
// uncompressing data. This function deletes the ZipBuffer object referred by
// the opaque parameter since zlib deletes the unzFile object and it does not
// use this object any longer.
int CloseZipBuffer(void* opaque, void* /*stream*/) {
  if (opaque)
    free(opaque);
  return 0;
}

// Returns the last error happened when reading or writing data. This function
// always returns zero, which means there are not any errors.
int GetErrorOfZipBuffer(void* /*opaque*/, void* /*stream*/) {
  return 0;
}

// Returns a zip_fileinfo struct with the time represented by |file_time|.
zip_fileinfo TimeToZipFileInfo(const base::Time& file_time) {
  base::Time::Exploded file_time_parts;
  file_time.UTCExplode(&file_time_parts);

  zip_fileinfo zip_info = {};
  if (file_time_parts.year >= 1980) {
    // This if check works around the handling of the year value in
    // contrib/minizip/zip.c in function zip64local_TmzDateToDosDate
    // It assumes that dates below 1980 are in the double digit format.
    // Hence the fail safe option is to leave the date unset. Some programs
    // might show the unset date as 1980-0-0 which is invalid.
    zip_info.tmz_date.tm_year = file_time_parts.year;
    zip_info.tmz_date.tm_mon = file_time_parts.month - 1;
    zip_info.tmz_date.tm_mday = file_time_parts.day_of_month;
    zip_info.tmz_date.tm_hour = file_time_parts.hour;
    zip_info.tmz_date.tm_min = file_time_parts.minute;
    zip_info.tmz_date.tm_sec = file_time_parts.second;
  }

  return zip_info;
}
}  // namespace

namespace zip {
namespace internal {

unzFile OpenForUnzipping(const std::string& file_name_utf8) {
  zlib_filefunc64_def* zip_func_ptrs = nullptr;
#if defined(OS_WIN)
  zlib_filefunc64_def zip_funcs;
  fill_win32_filefunc64(&zip_funcs);
  zip_funcs.zopen64_file = ZipOpenFunc;
  zip_func_ptrs = &zip_funcs;
#endif
  return unzOpen2_64(file_name_utf8.c_str(), zip_func_ptrs);
}

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
unzFile OpenFdForUnzipping(int zip_fd) {
  zlib_filefunc64_def zip_funcs;
  FillFdOpenFileFunc(&zip_funcs, zip_fd);
  // Passing dummy "fd" filename to zlib.
  return unzOpen2_64("fd", &zip_funcs);
}
#endif

#if defined(OS_WIN)
unzFile OpenHandleForUnzipping(HANDLE zip_handle) {
  zlib_filefunc64_def zip_funcs;
  fill_win32_filefunc64(&zip_funcs);
  zip_funcs.zopen64_file = HandleOpenFileFunc;
  zip_funcs.zclose_file = HandleCloseFileFunc;
  zip_funcs.opaque = zip_handle;
  return unzOpen2_64("fd", &zip_funcs);
}
#endif

// static
unzFile PrepareMemoryForUnzipping(const std::string& data) {
  if (data.empty())
    return NULL;

  ZipBuffer* buffer = static_cast<ZipBuffer*>(malloc(sizeof(ZipBuffer)));
  if (!buffer)
    return NULL;
  buffer->data = data.data();
  buffer->length = data.length();
  buffer->offset = 0;

  zlib_filefunc64_def zip_functions;
  zip_functions.zopen64_file = OpenZipBuffer;
  zip_functions.zread_file = ReadZipBuffer;
  zip_functions.zwrite_file = WriteZipBuffer;
  zip_functions.ztell64_file = GetOffsetOfZipBuffer;
  zip_functions.zseek64_file = SeekZipBuffer;
  zip_functions.zclose_file = CloseZipBuffer;
  zip_functions.zerror_file = GetErrorOfZipBuffer;
  zip_functions.opaque = buffer;
  return unzOpen2_64(nullptr, &zip_functions);
}

zipFile OpenForZipping(const std::string& file_name_utf8, int append_flag) {
  zlib_filefunc64_def* zip_func_ptrs = nullptr;
#if defined(OS_WIN)
  zlib_filefunc64_def zip_funcs;
  fill_win32_filefunc64(&zip_funcs);
  zip_funcs.zopen64_file = ZipOpenFunc;
  zip_func_ptrs = &zip_funcs;
#endif
  return zipOpen2_64(file_name_utf8.c_str(), append_flag, nullptr,
                     zip_func_ptrs);
}

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
zipFile OpenFdForZipping(int zip_fd, int append_flag) {
  zlib_filefunc64_def zip_funcs;
  FillFdOpenFileFunc(&zip_funcs, zip_fd);
  // Passing dummy "fd" filename to zlib.
  return zipOpen2_64("fd", append_flag, nullptr, &zip_funcs);
}
#endif

bool ZipOpenNewFileInZip(zipFile zip_file,
                         const std::string& str_path,
                         base::Time last_modified_time,
                         Compression compression) {
  // Section 4.4.4 http://www.pkware.com/documents/casestudies/APPNOTE.TXT
  // Setting the Language encoding flag so the file is told to be in utf-8.
  const uLong LANGUAGE_ENCODING_FLAG = 0x1 << 11;

  const zip_fileinfo file_info = TimeToZipFileInfo(last_modified_time);
  const int err = zipOpenNewFileInZip4_64(
      /*file=*/zip_file,
      /*filename=*/str_path.c_str(),
      /*zip_fileinfo=*/&file_info,
      /*extrafield_local=*/nullptr,
      /*size_extrafield_local=*/0u,
      /*extrafield_global=*/nullptr,
      /*size_extrafield_global=*/0u,
      /*comment=*/nullptr,
      /*method=*/compression,
      /*level=*/Z_DEFAULT_COMPRESSION,
      /*raw=*/0,
      /*windowBits=*/-MAX_WBITS,
      /*memLevel=*/DEF_MEM_LEVEL,
      /*strategy=*/Z_DEFAULT_STRATEGY,
      /*password=*/nullptr,
      /*crcForCrypting=*/0,
      /*versionMadeBy=*/0,
      /*flagBase=*/LANGUAGE_ENCODING_FLAG,
      /*zip64=*/1);

  if (err != ZIP_OK) {
    DLOG(ERROR) << "Cannot open ZIP file entry '" << str_path
                << "': zipOpenNewFileInZip4_64 returned " << err;
    return false;
  }

  return true;
}

Compression GetCompressionMethod(const base::FilePath& path) {
  // Get the filename extension in lower case.
  const base::FilePath::StringType ext =
      base::ToLowerASCII(path.FinalExtension());

  if (ext.empty())
    return kDeflated;


  // Skip the leading dot.
  base::FilePath::StringPieceType ext_without_dot = ext;
  DCHECK_EQ(ext_without_dot.front(), FILE_PATH_LITERAL('.'));
  ext_without_dot.remove_prefix(1);

  // Well known filename extensions of files that a likely to be already
  // compressed. The extensions are in lower case without the leading dot.
  static constexpr auto kExts =
      base::MakeFixedFlatSet<base::FilePath::StringPieceType>({
          FILE_PATH_LITERAL("3g2"),   //
          FILE_PATH_LITERAL("3gp"),   //
          FILE_PATH_LITERAL("7z"),    //
          FILE_PATH_LITERAL("7zip"),  //
          FILE_PATH_LITERAL("aac"),   //
          FILE_PATH_LITERAL("avi"),   //
          FILE_PATH_LITERAL("bz"),    //
          FILE_PATH_LITERAL("bz2"),   //
          FILE_PATH_LITERAL("crx"),   //
          FILE_PATH_LITERAL("gif"),   //
          FILE_PATH_LITERAL("gz"),    //
          FILE_PATH_LITERAL("jar"),   //
          FILE_PATH_LITERAL("jpeg"),  //
          FILE_PATH_LITERAL("jpg"),   //
          FILE_PATH_LITERAL("lz"),    //
          FILE_PATH_LITERAL("m2v"),   //
          FILE_PATH_LITERAL("m4p"),   //
          FILE_PATH_LITERAL("m4v"),   //
          FILE_PATH_LITERAL("mng"),   //
          FILE_PATH_LITERAL("mov"),   //
          FILE_PATH_LITERAL("mp2"),   //
          FILE_PATH_LITERAL("mp3"),   //
          FILE_PATH_LITERAL("mp4"),   //
          FILE_PATH_LITERAL("mpe"),   //
          FILE_PATH_LITERAL("mpeg"),  //
          FILE_PATH_LITERAL("mpg"),   //
          FILE_PATH_LITERAL("mpv"),   //
          FILE_PATH_LITERAL("ogg"),   //
          FILE_PATH_LITERAL("ogv"),   //
          FILE_PATH_LITERAL("png"),   //
          FILE_PATH_LITERAL("qt"),    //
          FILE_PATH_LITERAL("rar"),   //
          FILE_PATH_LITERAL("taz"),   //
          FILE_PATH_LITERAL("tb2"),   //
          FILE_PATH_LITERAL("tbz"),   //
          FILE_PATH_LITERAL("tbz2"),  //
          FILE_PATH_LITERAL("tgz"),   //
          FILE_PATH_LITERAL("tlz"),   //
          FILE_PATH_LITERAL("tz"),    //
          FILE_PATH_LITERAL("tz2"),   //
          FILE_PATH_LITERAL("vob"),   //
          FILE_PATH_LITERAL("webm"),  //
          FILE_PATH_LITERAL("wma"),   //
          FILE_PATH_LITERAL("wmv"),   //
          FILE_PATH_LITERAL("xz"),    //
          FILE_PATH_LITERAL("z"),     //
          FILE_PATH_LITERAL("zip"),   //
      });

  if (kExts.count(ext_without_dot)) {
    return kStored;
  }

  return kDeflated;
}

}  // namespace internal
}  // namespace zip
