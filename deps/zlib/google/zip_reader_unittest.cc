// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/zlib/google/zip_reader.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <iterator>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/hash/md5.h"
#include "base/i18n/time_formatting.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "third_party/zlib/google/zip_internal.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Return;
using ::testing::SizeIs;

namespace {

const static std::string kQuuxExpectedMD5 = "d1ae4ac8a17a0e09317113ab284b57a6";

class FileWrapper {
 public:
  typedef enum { READ_ONLY, READ_WRITE } AccessMode;

  FileWrapper(const base::FilePath& path, AccessMode mode) {
    int flags = base::File::FLAG_READ;
    if (mode == READ_ONLY)
      flags |= base::File::FLAG_OPEN;
    else
      flags |= base::File::FLAG_WRITE | base::File::FLAG_CREATE_ALWAYS;

    file_.Initialize(path, flags);
  }

  ~FileWrapper() {}

  base::PlatformFile platform_file() { return file_.GetPlatformFile(); }

  base::File* file() { return &file_; }

 private:
  base::File file_;
};

// A mock that provides methods that can be used as callbacks in asynchronous
// unzip functions.  Tracks the number of calls and number of bytes reported.
// Assumes that progress callbacks will be executed in-order.
class MockUnzipListener final {
 public:
  MockUnzipListener()
      : success_calls_(0),
        failure_calls_(0),
        progress_calls_(0),
        current_progress_(0) {}

  // Success callback for async functions.
  void OnUnzipSuccess() { success_calls_++; }

  // Failure callback for async functions.
  void OnUnzipFailure() { failure_calls_++; }

  // Progress callback for async functions.
  void OnUnzipProgress(int64_t progress) {
    DCHECK(progress > current_progress_);
    progress_calls_++;
    current_progress_ = progress;
  }

  int success_calls() { return success_calls_; }
  int failure_calls() { return failure_calls_; }
  int progress_calls() { return progress_calls_; }
  int current_progress() { return current_progress_; }

  base::WeakPtr<MockUnzipListener> AsWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  int success_calls_;
  int failure_calls_;
  int progress_calls_;

  int64_t current_progress_;

  base::WeakPtrFactory<MockUnzipListener> weak_ptr_factory_{this};
};

class MockWriterDelegate : public zip::WriterDelegate {
 public:
  MOCK_METHOD0(PrepareOutput, bool());
  MOCK_METHOD2(WriteBytes, bool(const char*, int));
  MOCK_METHOD1(SetTimeModified, void(const base::Time&));
  MOCK_METHOD1(SetPosixFilePermissions, void(int));
  MOCK_METHOD0(OnError, void());
};

bool ExtractCurrentEntryToFilePath(zip::ZipReader* reader,
                                   base::FilePath path) {
  zip::FilePathWriterDelegate writer(path);
  return reader->ExtractCurrentEntry(&writer);
}

const zip::ZipReader::Entry* LocateAndOpenEntry(
    zip::ZipReader* const reader,
    const base::FilePath& path_in_zip) {
  DCHECK(reader);
  EXPECT_TRUE(reader->ok());

  // The underlying library can do O(1) access, but ZipReader does not expose
  // that. O(N) access is acceptable for these tests.
  while (const zip::ZipReader::Entry* const entry = reader->Next()) {
    EXPECT_TRUE(reader->ok());
    if (entry->path == path_in_zip)
      return entry;
  }

  EXPECT_TRUE(reader->ok());
  return nullptr;
}

using Paths = std::vector<base::FilePath>;

}  // namespace

namespace zip {

// Make the test a PlatformTest to setup autorelease pools properly on Mac.
class ZipReaderTest : public PlatformTest {
 protected:
  void SetUp() override {
    PlatformTest::SetUp();

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();
  }

  static base::FilePath GetTestDataDirectory() {
    base::FilePath path;
    CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &path));
    return path.AppendASCII("third_party")
        .AppendASCII("zlib")
        .AppendASCII("google")
        .AppendASCII("test")
        .AppendASCII("data");
  }

  static Paths GetPaths(const base::FilePath& zip_path,
                        base::StringPiece encoding = {}) {
    Paths paths;

    if (ZipReader reader; reader.Open(zip_path)) {
      if (!encoding.empty())
        reader.SetEncoding(std::string(encoding));

      while (const ZipReader::Entry* const entry = reader.Next()) {
        EXPECT_TRUE(reader.ok());
        paths.push_back(entry->path);
      }

      EXPECT_TRUE(reader.ok());
    }

    return paths;
  }

  // The path to temporary directory used to contain the test operations.
  base::FilePath test_dir_;
  // The path to the test data directory where test.zip etc. are located.
  const base::FilePath data_dir_ = GetTestDataDirectory();
  // The path to test.zip in the test data directory.
  const base::FilePath test_zip_file_ = data_dir_.AppendASCII("test.zip");
  const Paths test_zip_contents_ = {
      base::FilePath(FILE_PATH_LITERAL("foo/")),
      base::FilePath(FILE_PATH_LITERAL("foo/bar/")),
      base::FilePath(FILE_PATH_LITERAL("foo/bar/baz.txt")),
      base::FilePath(FILE_PATH_LITERAL("foo/bar/quux.txt")),
      base::FilePath(FILE_PATH_LITERAL("foo/bar.txt")),
      base::FilePath(FILE_PATH_LITERAL("foo.txt")),
      base::FilePath(FILE_PATH_LITERAL("foo/bar/.hidden")),
  };
  base::ScopedTempDir temp_dir_;
  base::test::TaskEnvironment task_environment_;
};

TEST_F(ZipReaderTest, Open_ValidZipFile) {
  ZipReader reader;
  EXPECT_TRUE(reader.Open(test_zip_file_));
  EXPECT_TRUE(reader.ok());
}

TEST_F(ZipReaderTest, Open_ValidZipPlatformFile) {
  ZipReader reader;
  EXPECT_FALSE(reader.ok());
  FileWrapper zip_fd_wrapper(test_zip_file_, FileWrapper::READ_ONLY);
  EXPECT_TRUE(reader.OpenFromPlatformFile(zip_fd_wrapper.platform_file()));
  EXPECT_TRUE(reader.ok());
}

TEST_F(ZipReaderTest, Open_NonExistentFile) {
  ZipReader reader;
  EXPECT_FALSE(reader.ok());
  EXPECT_FALSE(reader.Open(data_dir_.AppendASCII("nonexistent.zip")));
  EXPECT_FALSE(reader.ok());
}

TEST_F(ZipReaderTest, Open_ExistentButNonZipFile) {
  ZipReader reader;
  EXPECT_FALSE(reader.ok());
  EXPECT_FALSE(reader.Open(data_dir_.AppendASCII("create_test_zip.sh")));
  EXPECT_FALSE(reader.ok());
}

TEST_F(ZipReaderTest, Open_EmptyFile) {
  ZipReader reader;
  EXPECT_FALSE(reader.ok());
  EXPECT_TRUE(reader.Open(data_dir_.AppendASCII("empty.zip")));
  EXPECT_TRUE(reader.ok());
  EXPECT_EQ(0, reader.num_entries());
  EXPECT_EQ(nullptr, reader.Next());
}

// Iterate through the contents in the test ZIP archive, and compare that the
// contents collected from the ZipReader matches the expected contents.
TEST_F(ZipReaderTest, Iteration) {
  Paths actual_contents;
  ZipReader reader;
  EXPECT_FALSE(reader.ok());
  EXPECT_TRUE(reader.Open(test_zip_file_));
  EXPECT_TRUE(reader.ok());
  while (const ZipReader::Entry* const entry = reader.Next()) {
    EXPECT_TRUE(reader.ok());
    actual_contents.push_back(entry->path);
  }

  EXPECT_TRUE(reader.ok());
  EXPECT_FALSE(reader.Next());  // Shouldn't go further.
  EXPECT_TRUE(reader.ok());

  EXPECT_THAT(actual_contents, SizeIs(reader.num_entries()));
  EXPECT_THAT(actual_contents, ElementsAreArray(test_zip_contents_));
}

// Open the test ZIP archive from a file descriptor, iterate through its
// contents, and compare that they match the expected contents.
TEST_F(ZipReaderTest, PlatformFileIteration) {
  Paths actual_contents;
  ZipReader reader;
  FileWrapper zip_fd_wrapper(test_zip_file_, FileWrapper::READ_ONLY);
  EXPECT_TRUE(reader.OpenFromPlatformFile(zip_fd_wrapper.platform_file()));
  EXPECT_TRUE(reader.ok());
  while (const ZipReader::Entry* const entry = reader.Next()) {
    EXPECT_TRUE(reader.ok());
    actual_contents.push_back(entry->path);
  }

  EXPECT_TRUE(reader.ok());
  EXPECT_FALSE(reader.Next());  // Shouldn't go further.
  EXPECT_TRUE(reader.ok());

  EXPECT_THAT(actual_contents, SizeIs(reader.num_entries()));
  EXPECT_THAT(actual_contents, ElementsAreArray(test_zip_contents_));
}

TEST_F(ZipReaderTest, RegularFile) {
  ZipReader reader;
  ASSERT_TRUE(reader.Open(test_zip_file_));
  base::FilePath target_path(FILE_PATH_LITERAL("foo/bar/quux.txt"));

  const ZipReader::Entry* entry = LocateAndOpenEntry(&reader, target_path);
  ASSERT_TRUE(entry);

  EXPECT_EQ(target_path, entry->path);
  EXPECT_EQ(13527, entry->original_size);
  EXPECT_EQ("2009-05-29 06:22:20.000",
            base::UnlocalizedTimeFormatWithPattern(entry->last_modified,
                                                   "y-MM-dd HH:mm:ss.SSS",
                                                   icu::TimeZone::getGMT()));
  EXPECT_FALSE(entry->is_unsafe);
  EXPECT_FALSE(entry->is_directory);
}

TEST_F(ZipReaderTest, DotDotFile) {
  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("evil.zip")));
  base::FilePath target_path(FILE_PATH_LITERAL(
      "UP/levilevilevilevilevilevilevilevilevilevilevilevil"));
  const ZipReader::Entry* entry = LocateAndOpenEntry(&reader, target_path);
  ASSERT_TRUE(entry);
  EXPECT_EQ(target_path, entry->path);
  EXPECT_FALSE(entry->is_unsafe);
  EXPECT_FALSE(entry->is_directory);
}

TEST_F(ZipReaderTest, InvalidUTF8File) {
  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("evil_via_invalid_utf8.zip")));
  base::FilePath target_path = base::FilePath::FromUTF8Unsafe(".�.�evil.txt");
  const ZipReader::Entry* entry = LocateAndOpenEntry(&reader, target_path);
  ASSERT_TRUE(entry);
  EXPECT_EQ(target_path, entry->path);
  EXPECT_FALSE(entry->is_unsafe);
  EXPECT_FALSE(entry->is_directory);
}

// By default, file paths in ZIPs are interpreted as UTF-8. But in this test,
// the ZIP archive contains file paths that are actually encoded in Shift JIS.
// The SJIS-encoded paths are thus wrongly interpreted as UTF-8, resulting in
// garbled paths. Invalid UTF-8 sequences are safely converted to the
// replacement character �.
TEST_F(ZipReaderTest, EncodingSjisAsUtf8) {
  EXPECT_THAT(
      GetPaths(data_dir_.AppendASCII("SJIS Bug 846195.zip")),
      ElementsAre(
          base::FilePath::FromUTF8Unsafe("�V�����t�H���_/SJIS_835C_��.txt"),
          base::FilePath::FromUTF8Unsafe(
              "�V�����t�H���_/�V�����e�L�X�g �h�L�������g.txt")));
}

// In this test, SJIS-encoded paths are interpreted as Code Page 1252. This
// results in garbled paths. Note the presence of C1 control codes U+0090 and
// U+0081 in the garbled paths.
TEST_F(ZipReaderTest, EncodingSjisAs1252) {
  EXPECT_THAT(
      GetPaths(data_dir_.AppendASCII("SJIS Bug 846195.zip"), "windows-1252"),
      ElementsAre(base::FilePath::FromUTF8Unsafe(
                      "\u0090V‚µ‚¢ƒtƒHƒ‹ƒ_/SJIS_835C_ƒ�.txt"),
                  base::FilePath::FromUTF8Unsafe(
                      "\u0090V‚µ‚¢ƒtƒHƒ‹ƒ_/\u0090V‚µ‚¢ƒeƒLƒXƒg "
                      "ƒhƒLƒ…ƒ\u0081ƒ“ƒg.txt")));
}

// In this test, SJIS-encoded paths are interpreted as Code Page 866. This
// results in garbled paths.
TEST_F(ZipReaderTest, EncodingSjisAsIbm866) {
  EXPECT_THAT(
      GetPaths(data_dir_.AppendASCII("SJIS Bug 846195.zip"), "IBM866"),
      ElementsAre(
          base::FilePath::FromUTF8Unsafe("РVВ╡ВвГtГHГЛГ_/SJIS_835C_Г�.txt"),
          base::FilePath::FromUTF8Unsafe(
              "РVВ╡ВвГtГHГЛГ_/РVВ╡ВвГeГLГXГg ГhГLГЕГБГУГg.txt")));
}

// Tests that SJIS-encoded paths are correctly converted to Unicode.
TEST_F(ZipReaderTest, EncodingSjis) {
  EXPECT_THAT(
      GetPaths(data_dir_.AppendASCII("SJIS Bug 846195.zip"), "Shift_JIS"),
      ElementsAre(
          base::FilePath::FromUTF8Unsafe("新しいフォルダ/SJIS_835C_ソ.txt"),
          base::FilePath::FromUTF8Unsafe(
              "新しいフォルダ/新しいテキスト ドキュメント.txt")));
}

TEST_F(ZipReaderTest, AbsoluteFile) {
  ZipReader reader;
  ASSERT_TRUE(
      reader.Open(data_dir_.AppendASCII("evil_via_absolute_file_name.zip")));
  base::FilePath target_path(FILE_PATH_LITERAL("ROOT/evil.txt"));
  const ZipReader::Entry* entry = LocateAndOpenEntry(&reader, target_path);
  ASSERT_TRUE(entry);
  EXPECT_EQ(target_path, entry->path);
  EXPECT_FALSE(entry->is_unsafe);
  EXPECT_FALSE(entry->is_directory);
}

TEST_F(ZipReaderTest, Directory) {
  ZipReader reader;
  ASSERT_TRUE(reader.Open(test_zip_file_));
  base::FilePath target_path(FILE_PATH_LITERAL("foo/bar/"));
  const ZipReader::Entry* entry = LocateAndOpenEntry(&reader, target_path);
  ASSERT_TRUE(entry);
  EXPECT_EQ(target_path, entry->path);
  // The directory size should be zero.
  EXPECT_EQ(0, entry->original_size);
  EXPECT_EQ("2009-05-31 15:49:52.000",
            base::UnlocalizedTimeFormatWithPattern(entry->last_modified,
                                                   "y-MM-dd HH:mm:ss.SSS",
                                                   icu::TimeZone::getGMT()));
  EXPECT_FALSE(entry->is_unsafe);
  EXPECT_TRUE(entry->is_directory);
}

TEST_F(ZipReaderTest, EncryptedFile_WrongPassword) {
  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("Different Encryptions.zip")));
  reader.SetPassword("wrong password");

  {
    const ZipReader::Entry* entry = reader.Next();
    ASSERT_TRUE(entry);
    EXPECT_EQ(base::FilePath::FromASCII("ClearText.txt"), entry->path);
    EXPECT_FALSE(entry->is_directory);
    EXPECT_FALSE(entry->is_encrypted);
    std::string contents = "dummy";
    EXPECT_TRUE(reader.ExtractCurrentEntryToString(&contents));
    EXPECT_EQ("This is not encrypted.\n", contents);
  }

  for (const base::StringPiece path : {
           "Encrypted AES-128.txt",
           "Encrypted AES-192.txt",
           "Encrypted AES-256.txt",
           "Encrypted ZipCrypto.txt",
       }) {
    const ZipReader::Entry* entry = reader.Next();
    ASSERT_TRUE(entry);
    EXPECT_EQ(base::FilePath::FromASCII(path), entry->path);
    EXPECT_FALSE(entry->is_directory);
    EXPECT_TRUE(entry->is_encrypted);
    std::string contents = "dummy";
    EXPECT_FALSE(reader.ExtractCurrentEntryToString(&contents));
  }

  EXPECT_FALSE(reader.Next());
  EXPECT_TRUE(reader.ok());
}

TEST_F(ZipReaderTest, EncryptedFile_RightPassword) {
  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("Different Encryptions.zip")));
  reader.SetPassword("password");

  {
    const ZipReader::Entry* entry = reader.Next();
    ASSERT_TRUE(entry);
    EXPECT_EQ(base::FilePath::FromASCII("ClearText.txt"), entry->path);
    EXPECT_FALSE(entry->is_directory);
    EXPECT_FALSE(entry->is_encrypted);
    std::string contents = "dummy";
    EXPECT_TRUE(reader.ExtractCurrentEntryToString(&contents));
    EXPECT_EQ("This is not encrypted.\n", contents);
  }

  // TODO(crbug.com/1296838) Support AES encryption.
  for (const base::StringPiece path : {
           "Encrypted AES-128.txt",
           "Encrypted AES-192.txt",
           "Encrypted AES-256.txt",
       }) {
    const ZipReader::Entry* entry = reader.Next();
    ASSERT_TRUE(entry);
    EXPECT_EQ(base::FilePath::FromASCII(path), entry->path);
    EXPECT_FALSE(entry->is_directory);
    EXPECT_TRUE(entry->is_encrypted);
    std::string contents = "dummy";
    EXPECT_FALSE(reader.ExtractCurrentEntryToString(&contents));
    EXPECT_EQ("", contents);
  }

  {
    const ZipReader::Entry* entry = reader.Next();
    ASSERT_TRUE(entry);
    EXPECT_EQ(base::FilePath::FromASCII("Encrypted ZipCrypto.txt"),
              entry->path);
    EXPECT_FALSE(entry->is_directory);
    EXPECT_TRUE(entry->is_encrypted);
    std::string contents = "dummy";
    EXPECT_TRUE(reader.ExtractCurrentEntryToString(&contents));
    EXPECT_EQ("This is encrypted with ZipCrypto.\n", contents);
  }

  EXPECT_FALSE(reader.Next());
  EXPECT_TRUE(reader.ok());
}

// Verifies that the ZipReader class can extract a file from a zip archive
// stored in memory. This test opens a zip archive in a std::string object,
// extracts its content, and verifies the content is the same as the expected
// text.
TEST_F(ZipReaderTest, OpenFromString) {
  // A zip archive consisting of one file "test.txt", which is a 16-byte text
  // file that contains "This is a test.\n".
  const char kTestData[] =
      "\x50\x4b\x03\x04\x0a\x00\x00\x00\x00\x00\xa4\x66\x24\x41\x13\xe8"
      "\xcb\x27\x10\x00\x00\x00\x10\x00\x00\x00\x08\x00\x1c\x00\x74\x65"
      "\x73\x74\x2e\x74\x78\x74\x55\x54\x09\x00\x03\x34\x89\x45\x50\x34"
      "\x89\x45\x50\x75\x78\x0b\x00\x01\x04\x8e\xf0\x00\x00\x04\x88\x13"
      "\x00\x00\x54\x68\x69\x73\x20\x69\x73\x20\x61\x20\x74\x65\x73\x74"
      "\x2e\x0a\x50\x4b\x01\x02\x1e\x03\x0a\x00\x00\x00\x00\x00\xa4\x66"
      "\x24\x41\x13\xe8\xcb\x27\x10\x00\x00\x00\x10\x00\x00\x00\x08\x00"
      "\x18\x00\x00\x00\x00\x00\x01\x00\x00\x00\xa4\x81\x00\x00\x00\x00"
      "\x74\x65\x73\x74\x2e\x74\x78\x74\x55\x54\x05\x00\x03\x34\x89\x45"
      "\x50\x75\x78\x0b\x00\x01\x04\x8e\xf0\x00\x00\x04\x88\x13\x00\x00"
      "\x50\x4b\x05\x06\x00\x00\x00\x00\x01\x00\x01\x00\x4e\x00\x00\x00"
      "\x52\x00\x00\x00\x00\x00";
  std::string data(kTestData, std::size(kTestData));
  ZipReader reader;
  ASSERT_TRUE(reader.OpenFromString(data));
  base::FilePath target_path(FILE_PATH_LITERAL("test.txt"));
  ASSERT_TRUE(LocateAndOpenEntry(&reader, target_path));
  ASSERT_TRUE(ExtractCurrentEntryToFilePath(&reader,
                                            test_dir_.AppendASCII("test.txt")));

  std::string actual;
  ASSERT_TRUE(
      base::ReadFileToString(test_dir_.AppendASCII("test.txt"), &actual));
  EXPECT_EQ(std::string("This is a test.\n"), actual);
}

// Verifies that the asynchronous extraction to a file works.
TEST_F(ZipReaderTest, ExtractToFileAsync_RegularFile) {
  MockUnzipListener listener;

  ZipReader reader;
  base::FilePath target_file = test_dir_.AppendASCII("quux.txt");
  base::FilePath target_path(FILE_PATH_LITERAL("foo/bar/quux.txt"));
  ASSERT_TRUE(reader.Open(test_zip_file_));
  ASSERT_TRUE(LocateAndOpenEntry(&reader, target_path));
  reader.ExtractCurrentEntryToFilePathAsync(
      target_file,
      base::BindOnce(&MockUnzipListener::OnUnzipSuccess, listener.AsWeakPtr()),
      base::BindOnce(&MockUnzipListener::OnUnzipFailure, listener.AsWeakPtr()),
      base::BindRepeating(&MockUnzipListener::OnUnzipProgress,
                          listener.AsWeakPtr()));

  EXPECT_EQ(0, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_EQ(0, listener.progress_calls());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_LE(1, listener.progress_calls());

  std::string output;
  ASSERT_TRUE(
      base::ReadFileToString(test_dir_.AppendASCII("quux.txt"), &output));
  const std::string md5 = base::MD5String(output);
  EXPECT_EQ(kQuuxExpectedMD5, md5);

  int64_t file_size = 0;
  ASSERT_TRUE(base::GetFileSize(target_file, &file_size));

  EXPECT_EQ(file_size, listener.current_progress());
}

TEST_F(ZipReaderTest, ExtractToFileAsync_Encrypted_NoPassword) {
  MockUnzipListener listener;

  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("Different Encryptions.zip")));
  ASSERT_TRUE(LocateAndOpenEntry(
      &reader, base::FilePath::FromASCII("Encrypted ZipCrypto.txt")));
  const base::FilePath target_path = test_dir_.AppendASCII("extracted");
  reader.ExtractCurrentEntryToFilePathAsync(
      target_path,
      base::BindOnce(&MockUnzipListener::OnUnzipSuccess, listener.AsWeakPtr()),
      base::BindOnce(&MockUnzipListener::OnUnzipFailure, listener.AsWeakPtr()),
      base::BindRepeating(&MockUnzipListener::OnUnzipProgress,
                          listener.AsWeakPtr()));

  EXPECT_EQ(0, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_EQ(0, listener.progress_calls());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, listener.success_calls());
  EXPECT_EQ(1, listener.failure_calls());
  EXPECT_LE(1, listener.progress_calls());

  // The extracted file contains rubbish data.
  // We probably shouldn't even look at it.
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(target_path, &contents));
  EXPECT_NE("", contents);
  EXPECT_EQ(contents.size(), listener.current_progress());
}

TEST_F(ZipReaderTest, ExtractToFileAsync_Encrypted_RightPassword) {
  MockUnzipListener listener;

  ZipReader reader;
  reader.SetPassword("password");
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("Different Encryptions.zip")));
  ASSERT_TRUE(LocateAndOpenEntry(
      &reader, base::FilePath::FromASCII("Encrypted ZipCrypto.txt")));
  const base::FilePath target_path = test_dir_.AppendASCII("extracted");
  reader.ExtractCurrentEntryToFilePathAsync(
      target_path,
      base::BindOnce(&MockUnzipListener::OnUnzipSuccess, listener.AsWeakPtr()),
      base::BindOnce(&MockUnzipListener::OnUnzipFailure, listener.AsWeakPtr()),
      base::BindRepeating(&MockUnzipListener::OnUnzipProgress,
                          listener.AsWeakPtr()));

  EXPECT_EQ(0, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_EQ(0, listener.progress_calls());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_LE(1, listener.progress_calls());

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(target_path, &contents));
  EXPECT_EQ("This is encrypted with ZipCrypto.\n", contents);
  EXPECT_EQ(contents.size(), listener.current_progress());
}

TEST_F(ZipReaderTest, ExtractToFileAsync_WrongCrc) {
  MockUnzipListener listener;

  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("Wrong CRC.zip")));
  ASSERT_TRUE(
      LocateAndOpenEntry(&reader, base::FilePath::FromASCII("Corrupted.txt")));
  const base::FilePath target_path = test_dir_.AppendASCII("extracted");
  reader.ExtractCurrentEntryToFilePathAsync(
      target_path,
      base::BindOnce(&MockUnzipListener::OnUnzipSuccess, listener.AsWeakPtr()),
      base::BindOnce(&MockUnzipListener::OnUnzipFailure, listener.AsWeakPtr()),
      base::BindRepeating(&MockUnzipListener::OnUnzipProgress,
                          listener.AsWeakPtr()));

  EXPECT_EQ(0, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_EQ(0, listener.progress_calls());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, listener.success_calls());
  EXPECT_EQ(1, listener.failure_calls());
  EXPECT_LE(1, listener.progress_calls());

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(target_path, &contents));
  EXPECT_EQ("This file has been changed after its CRC was computed.\n",
            contents);
  EXPECT_EQ(contents.size(), listener.current_progress());
}

// Verifies that the asynchronous extraction to a file works.
TEST_F(ZipReaderTest, ExtractToFileAsync_Directory) {
  MockUnzipListener listener;

  ZipReader reader;
  base::FilePath target_file = test_dir_.AppendASCII("foo");
  base::FilePath target_path(FILE_PATH_LITERAL("foo/"));
  ASSERT_TRUE(reader.Open(test_zip_file_));
  ASSERT_TRUE(LocateAndOpenEntry(&reader, target_path));
  reader.ExtractCurrentEntryToFilePathAsync(
      target_file,
      base::BindOnce(&MockUnzipListener::OnUnzipSuccess, listener.AsWeakPtr()),
      base::BindOnce(&MockUnzipListener::OnUnzipFailure, listener.AsWeakPtr()),
      base::BindRepeating(&MockUnzipListener::OnUnzipProgress,
                          listener.AsWeakPtr()));

  EXPECT_EQ(0, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_EQ(0, listener.progress_calls());

  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, listener.success_calls());
  EXPECT_EQ(0, listener.failure_calls());
  EXPECT_GE(0, listener.progress_calls());

  ASSERT_TRUE(base::DirectoryExists(target_file));
}

TEST_F(ZipReaderTest, ExtractCurrentEntryToString) {
  // test_mismatch_size.zip contains files with names from 0.txt to 7.txt with
  // sizes from 0 to 7 bytes respectively, being the contents of each file a
  // substring of "0123456" starting at '0'.
  base::FilePath test_zip_file =
      data_dir_.AppendASCII("test_mismatch_size.zip");

  ZipReader reader;
  std::string contents;
  ASSERT_TRUE(reader.Open(test_zip_file));

  for (size_t i = 0; i < 8; i++) {
    SCOPED_TRACE(base::StringPrintf("Processing %d.txt", static_cast<int>(i)));

    base::FilePath file_name = base::FilePath::FromUTF8Unsafe(
        base::StringPrintf("%d.txt", static_cast<int>(i)));
    ASSERT_TRUE(LocateAndOpenEntry(&reader, file_name));

    if (i > 1) {
      // Off by one byte read limit: must fail.
      EXPECT_FALSE(reader.ExtractCurrentEntryToString(i - 1, &contents));
    }

    if (i > 0) {
      // Exact byte read limit: must pass.
      EXPECT_TRUE(reader.ExtractCurrentEntryToString(i, &contents));
      EXPECT_EQ(std::string(base::StringPiece("0123456", i)), contents);
    }

    // More than necessary byte read limit: must pass.
    EXPECT_TRUE(reader.ExtractCurrentEntryToString(&contents));
    EXPECT_EQ(std::string(base::StringPiece("0123456", i)), contents);
  }
  reader.Close();
}

TEST_F(ZipReaderTest, ExtractPartOfCurrentEntry) {
  // test_mismatch_size.zip contains files with names from 0.txt to 7.txt with
  // sizes from 0 to 7 bytes respectively, being the contents of each file a
  // substring of "0123456" starting at '0'.
  base::FilePath test_zip_file =
      data_dir_.AppendASCII("test_mismatch_size.zip");

  ZipReader reader;
  std::string contents;
  ASSERT_TRUE(reader.Open(test_zip_file));

  base::FilePath file_name0 = base::FilePath::FromUTF8Unsafe("0.txt");
  ASSERT_TRUE(LocateAndOpenEntry(&reader, file_name0));
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(0, &contents));
  EXPECT_EQ("", contents);
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(1, &contents));
  EXPECT_EQ("", contents);

  base::FilePath file_name1 = base::FilePath::FromUTF8Unsafe("1.txt");
  ASSERT_TRUE(LocateAndOpenEntry(&reader, file_name1));
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(0, &contents));
  EXPECT_EQ("", contents);
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(1, &contents));
  EXPECT_EQ("0", contents);
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(2, &contents));
  EXPECT_EQ("0", contents);

  base::FilePath file_name4 = base::FilePath::FromUTF8Unsafe("4.txt");
  ASSERT_TRUE(LocateAndOpenEntry(&reader, file_name4));
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(0, &contents));
  EXPECT_EQ("", contents);
  EXPECT_FALSE(reader.ExtractCurrentEntryToString(2, &contents));
  EXPECT_EQ("01", contents);
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(4, &contents));
  EXPECT_EQ("0123", contents);
  // Checks that entire file is extracted and function returns true when
  // |max_read_bytes| is larger than file size.
  EXPECT_TRUE(reader.ExtractCurrentEntryToString(5, &contents));
  EXPECT_EQ("0123", contents);

  reader.Close();
}

TEST_F(ZipReaderTest, ExtractPosixPermissions) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("test_posix_permissions.zip")));
  for (auto entry : {"0.txt", "1.txt", "2.txt", "3.txt"}) {
    ASSERT_TRUE(LocateAndOpenEntry(&reader, base::FilePath::FromASCII(entry)));
    FilePathWriterDelegate delegate(temp_dir.GetPath().AppendASCII(entry));
    ASSERT_TRUE(reader.ExtractCurrentEntry(&delegate));
  }
  reader.Close();

#if defined(OS_POSIX)
  // This assumes a umask of at least 0400.
  int mode = 0;
  EXPECT_TRUE(base::GetPosixFilePermissions(
      temp_dir.GetPath().AppendASCII("0.txt"), &mode));
  EXPECT_EQ(mode & 0700, 0700);
  EXPECT_TRUE(base::GetPosixFilePermissions(
      temp_dir.GetPath().AppendASCII("1.txt"), &mode));
  EXPECT_EQ(mode & 0700, 0600);
  EXPECT_TRUE(base::GetPosixFilePermissions(
      temp_dir.GetPath().AppendASCII("2.txt"), &mode));
  EXPECT_EQ(mode & 0700, 0700);
  EXPECT_TRUE(base::GetPosixFilePermissions(
      temp_dir.GetPath().AppendASCII("3.txt"), &mode));
  EXPECT_EQ(mode & 0700, 0600);
#endif
}

// This test exposes http://crbug.com/430959, at least on OS X
TEST_F(ZipReaderTest, DISABLED_LeakDetectionTest) {
  for (int i = 0; i < 100000; ++i) {
    FileWrapper zip_fd_wrapper(test_zip_file_, FileWrapper::READ_ONLY);
    ZipReader reader;
    ASSERT_TRUE(reader.OpenFromPlatformFile(zip_fd_wrapper.platform_file()));
  }
}

// Test that when WriterDelegate::PrepareMock returns false, no other methods on
// the delegate are called and the extraction fails.
TEST_F(ZipReaderTest, ExtractCurrentEntryPrepareFailure) {
  testing::StrictMock<MockWriterDelegate> mock_writer;

  EXPECT_CALL(mock_writer, PrepareOutput()).WillOnce(Return(false));

  base::FilePath target_path(FILE_PATH_LITERAL("foo/bar/quux.txt"));
  ZipReader reader;

  ASSERT_TRUE(reader.Open(test_zip_file_));
  ASSERT_TRUE(LocateAndOpenEntry(&reader, target_path));
  ASSERT_FALSE(reader.ExtractCurrentEntry(&mock_writer));
}

// Test that when WriterDelegate::WriteBytes returns false, only the OnError
// method on the delegate is called and the extraction fails.
TEST_F(ZipReaderTest, ExtractCurrentEntryWriteBytesFailure) {
  testing::StrictMock<MockWriterDelegate> mock_writer;

  EXPECT_CALL(mock_writer, PrepareOutput()).WillOnce(Return(true));
  EXPECT_CALL(mock_writer, WriteBytes(_, _)).WillOnce(Return(false));
  EXPECT_CALL(mock_writer, OnError());

  base::FilePath target_path(FILE_PATH_LITERAL("foo/bar/quux.txt"));
  ZipReader reader;

  ASSERT_TRUE(reader.Open(test_zip_file_));
  ASSERT_TRUE(LocateAndOpenEntry(&reader, target_path));
  ASSERT_FALSE(reader.ExtractCurrentEntry(&mock_writer));
}

// Test that extraction succeeds when the writer delegate reports all is well.
TEST_F(ZipReaderTest, ExtractCurrentEntrySuccess) {
  testing::StrictMock<MockWriterDelegate> mock_writer;

  EXPECT_CALL(mock_writer, PrepareOutput()).WillOnce(Return(true));
  EXPECT_CALL(mock_writer, WriteBytes(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_writer, SetPosixFilePermissions(_));
  EXPECT_CALL(mock_writer, SetTimeModified(_));

  base::FilePath target_path(FILE_PATH_LITERAL("foo/bar/quux.txt"));
  ZipReader reader;

  ASSERT_TRUE(reader.Open(test_zip_file_));
  ASSERT_TRUE(LocateAndOpenEntry(&reader, target_path));
  ASSERT_TRUE(reader.ExtractCurrentEntry(&mock_writer));
}

TEST_F(ZipReaderTest, WrongCrc) {
  ZipReader reader;
  ASSERT_TRUE(reader.Open(data_dir_.AppendASCII("Wrong CRC.zip")));

  const ZipReader::Entry* const entry =
      LocateAndOpenEntry(&reader, base::FilePath::FromASCII("Corrupted.txt"));
  ASSERT_TRUE(entry);

  std::string contents = "dummy";
  EXPECT_FALSE(reader.ExtractCurrentEntryToString(&contents));
  EXPECT_EQ("This file has been changed after its CRC was computed.\n",
            contents);

  contents = "dummy";
  EXPECT_FALSE(
      reader.ExtractCurrentEntryToString(entry->original_size + 1, &contents));
  EXPECT_EQ("This file has been changed after its CRC was computed.\n",
            contents);

  contents = "dummy";
  EXPECT_FALSE(
      reader.ExtractCurrentEntryToString(entry->original_size, &contents));
  EXPECT_EQ("This file has been changed after its CRC was computed.\n",
            contents);

  contents = "dummy";
  EXPECT_FALSE(
      reader.ExtractCurrentEntryToString(entry->original_size - 1, &contents));
  EXPECT_EQ("This file has been changed after its CRC was computed.", contents);
}

class FileWriterDelegateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(base::CreateTemporaryFile(&temp_file_path_));
    file_.Initialize(temp_file_path_,
                     (base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                      base::File::FLAG_WRITE | base::File::FLAG_WIN_TEMPORARY |
                      base::File::FLAG_DELETE_ON_CLOSE));
    ASSERT_TRUE(file_.IsValid());
  }

  base::FilePath temp_file_path_;
  base::File file_;
};

TEST_F(FileWriterDelegateTest, WriteToEnd) {
  const std::string payload = "This is the actualy payload data.\n";

  {
    FileWriterDelegate writer(&file_);
    EXPECT_EQ(0, writer.file_length());
    ASSERT_TRUE(writer.PrepareOutput());
    ASSERT_TRUE(writer.WriteBytes(payload.data(), payload.size()));
    EXPECT_EQ(payload.size(), writer.file_length());
  }

  EXPECT_EQ(payload.size(), file_.GetLength());
}

TEST_F(FileWriterDelegateTest, EmptyOnError) {
  const std::string payload = "This is the actualy payload data.\n";

  {
    FileWriterDelegate writer(&file_);
    EXPECT_EQ(0, writer.file_length());
    ASSERT_TRUE(writer.PrepareOutput());
    ASSERT_TRUE(writer.WriteBytes(payload.data(), payload.size()));
    EXPECT_EQ(payload.size(), writer.file_length());
    EXPECT_EQ(payload.size(), file_.GetLength());
    writer.OnError();
    EXPECT_EQ(0, writer.file_length());
  }

  EXPECT_EQ(0, file_.GetLength());
}

}  // namespace zip
