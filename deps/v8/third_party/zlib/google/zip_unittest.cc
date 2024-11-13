// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/zlib/google/zip.h"

#include <stddef.h>
#include <stdint.h>

#include <iomanip>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "third_party/zlib/google/zip_internal.h"
#include "third_party/zlib/google/zip_reader.h"

// Convenience macro to create a file path from a string literal.
#define FP(path) base::FilePath(FILE_PATH_LITERAL(path))

namespace {

using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

std::vector<std::string> GetRelativePaths(const base::FilePath& dir,
                                          base::FileEnumerator::FileType type) {
  std::vector<std::string> got_paths;
  base::FileEnumerator files(dir, true, type);
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    base::FilePath relative;
    EXPECT_TRUE(dir.AppendRelativePath(path, &relative));
    got_paths.push_back(relative.NormalizePathSeparatorsTo('/').AsUTF8Unsafe());
  }

  EXPECT_EQ(base::File::FILE_OK, files.GetError());
  return got_paths;
}

bool CreateFile(const std::string& content,
                base::FilePath* file_path,
                base::File* file) {
  if (!base::CreateTemporaryFile(file_path))
    return false;

  if (!base::WriteFile(*file_path, content)) {
    return false;
  }

  *file = base::File(
      *file_path, base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  return file->IsValid();
}

// A WriterDelegate that logs progress once per second.
class ProgressWriterDelegate : public zip::WriterDelegate {
 public:
  explicit ProgressWriterDelegate(int64_t expected_size)
      : expected_size_(expected_size) {
    CHECK_GT(expected_size_, 0);
  }

  bool WriteBytes(const char* data, int num_bytes) override {
    received_bytes_ += num_bytes;
    LogProgressIfNecessary();
    return true;
  }

  void SetTimeModified(const base::Time& time) override { LogProgress(); }

  int64_t received_bytes() const { return received_bytes_; }

 private:
  void LogProgressIfNecessary() {
    const base::TimeTicks now = base::TimeTicks::Now();
    if (next_progress_report_time_ > now)
      return;

    next_progress_report_time_ = now + progress_period_;
    LogProgress();
  }

  void LogProgress() const {
    LOG(INFO) << "Unzipping... " << std::setw(3)
              << (100 * received_bytes_ / expected_size_) << "%";
  }

  const base::TimeDelta progress_period_ = base::Seconds(1);
  base::TimeTicks next_progress_report_time_ =
      base::TimeTicks::Now() + progress_period_;
  const uint64_t expected_size_;
  int64_t received_bytes_ = 0;
};

// A virtual file system containing:
// /test
// /test/foo.txt
// /test/bar/bar1.txt
// /test/bar/bar2.txt
// Used to test providing a custom zip::FileAccessor when unzipping.
class VirtualFileSystem : public zip::FileAccessor {
 public:
  static constexpr char kFooContent[] = "This is foo.";
  static constexpr char kBar1Content[] = "This is bar.";
  static constexpr char kBar2Content[] = "This is bar too.";

  VirtualFileSystem() {
    base::FilePath test_dir;
    base::FilePath foo_txt_path = test_dir.AppendASCII("foo.txt");

    base::FilePath file_path;
    base::File file;
    bool success = CreateFile(kFooContent, &file_path, &file);
    DCHECK(success);
    files_[foo_txt_path] = std::move(file);

    base::FilePath bar_dir = test_dir.AppendASCII("bar");
    base::FilePath bar1_txt_path = bar_dir.AppendASCII("bar1.txt");
    success = CreateFile(kBar1Content, &file_path, &file);
    DCHECK(success);
    files_[bar1_txt_path] = std::move(file);

    base::FilePath bar2_txt_path = bar_dir.AppendASCII("bar2.txt");
    success = CreateFile(kBar2Content, &file_path, &file);
    DCHECK(success);
    files_[bar2_txt_path] = std::move(file);

    file_tree_[base::FilePath()] = {{foo_txt_path}, {bar_dir}};
    file_tree_[bar_dir] = {{bar1_txt_path, bar2_txt_path}};
    file_tree_[foo_txt_path] = {};
    file_tree_[bar1_txt_path] = {};
    file_tree_[bar2_txt_path] = {};
  }

  VirtualFileSystem(const VirtualFileSystem&) = delete;
  VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

  ~VirtualFileSystem() override = default;

 private:
  bool Open(const zip::Paths paths,
            std::vector<base::File>* const files) override {
    DCHECK(files);
    files->reserve(files->size() + paths.size());

    for (const base::FilePath& path : paths) {
      const auto it = files_.find(path);
      if (it == files_.end()) {
        files->emplace_back();
      } else {
        EXPECT_TRUE(it->second.IsValid());
        files->push_back(std::move(it->second));
      }
    }

    return true;
  }

  bool List(const base::FilePath& path,
            std::vector<base::FilePath>* const files,
            std::vector<base::FilePath>* const subdirs) override {
    DCHECK(!path.IsAbsolute());
    DCHECK(files);
    DCHECK(subdirs);

    const auto it = file_tree_.find(path);
    if (it == file_tree_.end())
      return false;

    for (const base::FilePath& file : it->second.files) {
      DCHECK(!file.empty());
      files->push_back(file);
    }

    for (const base::FilePath& subdir : it->second.subdirs) {
      DCHECK(!subdir.empty());
      subdirs->push_back(subdir);
    }

    return true;
  }

  bool GetInfo(const base::FilePath& path, Info* const info) override {
    DCHECK(!path.IsAbsolute());
    DCHECK(info);

    if (!file_tree_.count(path))
      return false;

    info->is_directory = !files_.count(path);
    info->last_modified =
        base::Time::FromSecondsSinceUnixEpoch(172097977);  // Some random date.

    return true;
  }

  struct DirContents {
    std::vector<base::FilePath> files, subdirs;
  };

  std::unordered_map<base::FilePath, DirContents> file_tree_;
  std::unordered_map<base::FilePath, base::File> files_;
};

// static
constexpr char VirtualFileSystem::kFooContent[];
constexpr char VirtualFileSystem::kBar1Content[];
constexpr char VirtualFileSystem::kBar2Content[];

// Make the test a PlatformTest to setup autorelease pools properly on Mac.
class ZipTest : public PlatformTest {
 protected:
  enum ValidYearType { VALID_YEAR, INVALID_YEAR };

  virtual void SetUp() {
    PlatformTest::SetUp();

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();

    base::FilePath zip_path(test_dir_);
    zip_contents_.insert(zip_path.AppendASCII("foo.txt"));
    zip_path = zip_path.AppendASCII("foo");
    zip_contents_.insert(zip_path);
    zip_contents_.insert(zip_path.AppendASCII("bar.txt"));
    zip_path = zip_path.AppendASCII("bar");
    zip_contents_.insert(zip_path);
    zip_contents_.insert(zip_path.AppendASCII("baz.txt"));
    zip_contents_.insert(zip_path.AppendASCII("quux.txt"));
    zip_contents_.insert(zip_path.AppendASCII(".hidden"));

    // Include a subset of files in |zip_file_list_| to test ZipFiles().
    zip_file_list_.push_back(FP("foo.txt"));
    zip_file_list_.push_back(FP("foo/bar/quux.txt"));
    zip_file_list_.push_back(FP("foo/bar/.hidden"));
  }

  virtual void TearDown() { PlatformTest::TearDown(); }

  static base::FilePath GetDataDirectory() {
    base::FilePath path;
    bool success = base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &path);
    EXPECT_TRUE(success);
    return std::move(path)
        .AppendASCII("third_party")
        .AppendASCII("zlib")
        .AppendASCII("google")
        .AppendASCII("test")
        .AppendASCII("data");
  }

  void TestUnzipFile(const base::FilePath::StringType& filename,
                     bool expect_hidden_files) {
    TestUnzipFile(GetDataDirectory().Append(filename), expect_hidden_files);
  }

  void TestUnzipFile(const base::FilePath& path, bool expect_hidden_files) {
    ASSERT_TRUE(base::PathExists(path)) << "no file " << path;
    ASSERT_TRUE(zip::Unzip(path, test_dir_));

    base::FilePath original_dir = GetDataDirectory().AppendASCII("test");

    base::FileEnumerator files(
        test_dir_, true,
        base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);

    size_t count = 0;
    for (base::FilePath unzipped_entry_path = files.Next();
         !unzipped_entry_path.empty(); unzipped_entry_path = files.Next()) {
      EXPECT_EQ(zip_contents_.count(unzipped_entry_path), 1U)
          << "Couldn't find " << unzipped_entry_path;
      count++;

      if (base::PathExists(unzipped_entry_path) &&
          !base::DirectoryExists(unzipped_entry_path)) {
        // It's a file, check its contents are what we zipped.
        base::FilePath relative_path;
        ASSERT_TRUE(
            test_dir_.AppendRelativePath(unzipped_entry_path, &relative_path))
            << "Cannot append relative path failed, params: '" << test_dir_
            << "' and '" << unzipped_entry_path << "'";
        base::FilePath original_path = original_dir.Append(relative_path);
        EXPECT_TRUE(base::ContentsEqual(original_path, unzipped_entry_path))
            << "Original file '" << original_path << "' and unzipped file '"
            << unzipped_entry_path << "' have different contents";
      }
    }
    EXPECT_EQ(base::File::FILE_OK, files.GetError());

    size_t expected_count = 0;
    for (const base::FilePath& path : zip_contents_) {
      if (expect_hidden_files || path.BaseName().value()[0] != '.')
        ++expected_count;
    }

    EXPECT_EQ(expected_count, count);
  }

  // This function does the following:
  // 1) Creates a test.txt file with the given last modification timestamp
  // 2) Zips test.txt and extracts it back into a different location.
  // 3) Confirms that test.txt in the output directory has the specified
  //    last modification timestamp if it is valid (|valid_year| is true).
  //    If the timestamp is not supported by the zip format, the last
  //    modification defaults to the current time.
  void TestTimeStamp(const char* date_time, ValidYearType valid_year) {
    SCOPED_TRACE(std::string("TestTimeStamp(") + date_time + ")");
    base::ScopedTempDir temp_dir;
    ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

    base::FilePath zip_file = temp_dir.GetPath().AppendASCII("out.zip");
    base::FilePath src_dir = temp_dir.GetPath().AppendASCII("input");
    base::FilePath out_dir = temp_dir.GetPath().AppendASCII("output");

    base::FilePath src_file = src_dir.AppendASCII("test.txt");
    base::FilePath out_file = out_dir.AppendASCII("test.txt");

    EXPECT_TRUE(base::CreateDirectory(src_dir));
    EXPECT_TRUE(base::CreateDirectory(out_dir));

    base::Time test_mtime;
    ASSERT_TRUE(base::Time::FromString(date_time, &test_mtime));

    // Adjusting the current timestamp to the resolution that the zip file
    // supports, which is 2 seconds. Note that between this call to Time::Now()
    // and zip::Zip() the clock can advance a bit, hence the use of EXPECT_GE.
    base::Time::Exploded now_parts;
    base::Time::Now().UTCExplode(&now_parts);
    now_parts.second = now_parts.second & ~1;
    now_parts.millisecond = 0;
    base::Time now_time;
    EXPECT_TRUE(base::Time::FromUTCExploded(now_parts, &now_time));

    EXPECT_TRUE(base::WriteFile(src_file, "1"));
    EXPECT_TRUE(base::TouchFile(src_file, base::Time::Now(), test_mtime));

    EXPECT_TRUE(zip::Zip(src_dir, zip_file, true));
    ASSERT_TRUE(zip::Unzip(zip_file, out_dir));

    base::File::Info file_info;
    EXPECT_TRUE(base::GetFileInfo(out_file, &file_info));
    EXPECT_EQ(file_info.size, 1);

    if (valid_year == VALID_YEAR) {
      EXPECT_EQ(file_info.last_modified, test_mtime);
    } else {
      // Invalid date means the modification time will default to 'now'.
      EXPECT_GE(file_info.last_modified, now_time);
    }
  }

  // The path to temporary directory used to contain the test operations.
  base::FilePath test_dir_;

  base::ScopedTempDir temp_dir_;

  // Hard-coded contents of a known zip file.
  std::unordered_set<base::FilePath> zip_contents_;

  // Hard-coded list of relative paths for a zip file created with ZipFiles.
  std::vector<base::FilePath> zip_file_list_;
};

TEST_F(ZipTest, UnzipNoSuchFile) {
  EXPECT_FALSE(zip::Unzip(GetDataDirectory().AppendASCII("No Such File.zip"),
                          test_dir_));
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre());
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::DIRECTORIES),
      UnorderedElementsAre());
}

TEST_F(ZipTest, Unzip) {
  TestUnzipFile(FILE_PATH_LITERAL("test.zip"), true);
}

TEST_F(ZipTest, UnzipUncompressed) {
  TestUnzipFile(FILE_PATH_LITERAL("test_nocompress.zip"), true);
}

TEST_F(ZipTest, UnzipEvil) {
  base::FilePath path = GetDataDirectory().AppendASCII("evil.zip");
  // Unzip the zip file into a sub directory of test_dir_ so evil.zip
  // won't create a persistent file outside test_dir_ in case of a
  // failure.
  base::FilePath output_dir = test_dir_.AppendASCII("out");
  EXPECT_TRUE(zip::Unzip(path, output_dir));
  EXPECT_TRUE(base::PathExists(output_dir.AppendASCII(
      "UP/levilevilevilevilevilevilevilevilevilevilevilevil")));
}

TEST_F(ZipTest, UnzipEvil2) {
  // The ZIP file contains a file with invalid UTF-8 in its file name.
  base::FilePath path =
      GetDataDirectory().AppendASCII("evil_via_invalid_utf8.zip");
  // See the comment at UnzipEvil() for why we do this.
  base::FilePath output_dir = test_dir_.AppendASCII("out");
  ASSERT_TRUE(zip::Unzip(path, output_dir));
  ASSERT_TRUE(base::PathExists(
      output_dir.Append(base::FilePath::FromUTF8Unsafe(".�.�evil.txt"))));
  ASSERT_FALSE(base::PathExists(output_dir.AppendASCII("../evil.txt")));
}

TEST_F(ZipTest, UnzipWithFilter) {
  auto filter = base::BindRepeating([](const base::FilePath& path) {
    return path.BaseName().MaybeAsASCII() == "foo.txt";
  });
  ASSERT_TRUE(zip::Unzip(GetDataDirectory().AppendASCII("test.zip"), test_dir_,
                         {.filter = std::move(filter)}));
  // Only foo.txt should have been extracted.
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("foo.txt"));
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::DIRECTORIES),
      UnorderedElementsAre());
}

TEST_F(ZipTest, UnzipEncryptedWithRightPassword) {
  // TODO(crbug.com/1296838) Also check the AES-encrypted files.
  auto filter = base::BindRepeating([](const base::FilePath& path) {
    return !base::StartsWith(path.MaybeAsASCII(), "Encrypted AES");
  });

  ASSERT_TRUE(zip::Unzip(
      GetDataDirectory().AppendASCII("Different Encryptions.zip"), test_dir_,
      {.filter = std::move(filter), .password = "password"}));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("ClearText.txt"),
                                     &contents));
  EXPECT_EQ("This is not encrypted.\n", contents);

  ASSERT_TRUE(base::ReadFileToString(
      test_dir_.AppendASCII("Encrypted ZipCrypto.txt"), &contents));
  EXPECT_EQ("This is encrypted with ZipCrypto.\n", contents);
}

TEST_F(ZipTest, UnzipEncryptedWithWrongPassword) {
  // TODO(crbug.com/1296838) Also check the AES-encrypted files.
  auto filter = base::BindRepeating([](const base::FilePath& path) {
    return !base::StartsWith(path.MaybeAsASCII(), "Encrypted AES");
  });

  ASSERT_FALSE(zip::Unzip(
      GetDataDirectory().AppendASCII("Different Encryptions.zip"), test_dir_,
      {.filter = std::move(filter), .password = "wrong"}));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("ClearText.txt"),
                                     &contents));
  EXPECT_EQ("This is not encrypted.\n", contents);

  // No rubbish file should be left behind.
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("ClearText.txt"));
}

TEST_F(ZipTest, UnzipEncryptedWithNoPassword) {
  // TODO(crbug.com/1296838) Also check the AES-encrypted files.
  auto filter = base::BindRepeating([](const base::FilePath& path) {
    return !base::StartsWith(path.MaybeAsASCII(), "Encrypted AES");
  });

  ASSERT_FALSE(
      zip::Unzip(GetDataDirectory().AppendASCII("Different Encryptions.zip"),
                 test_dir_, {.filter = std::move(filter)}));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("ClearText.txt"),
                                     &contents));
  EXPECT_EQ("This is not encrypted.\n", contents);

  // No rubbish file should be left behind.
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("ClearText.txt"));
}

TEST_F(ZipTest, UnzipEncryptedContinueOnError) {
  EXPECT_TRUE(
      zip::Unzip(GetDataDirectory().AppendASCII("Different Encryptions.zip"),
                 test_dir_, {.continue_on_error = true}));

  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("ClearText.txt"),
                                     &contents));
  EXPECT_EQ("This is not encrypted.\n", contents);

  // No rubbish file should be left behind.
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("ClearText.txt"));
}

TEST_F(ZipTest, UnzipWrongCrc) {
  ASSERT_FALSE(
      zip::Unzip(GetDataDirectory().AppendASCII("Wrong CRC.zip"), test_dir_));

  // No rubbish file should be left behind.
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre());
}

TEST_F(ZipTest, UnzipRepeatedDirName) {
  EXPECT_TRUE(zip::Unzip(
      GetDataDirectory().AppendASCII("Repeated Dir Name.zip"), test_dir_));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre());

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::DIRECTORIES),
      UnorderedElementsAre("repeated"));
}

TEST_F(ZipTest, UnzipRepeatedFileName) {
  EXPECT_FALSE(zip::Unzip(
      GetDataDirectory().AppendASCII("Repeated File Name.zip"), test_dir_));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("repeated"));

  std::string contents;
  EXPECT_TRUE(
      base::ReadFileToString(test_dir_.AppendASCII("repeated"), &contents));
  EXPECT_EQ("First file", contents);
}

TEST_F(ZipTest, UnzipCannotCreateEmptyDir) {
  EXPECT_FALSE(zip::Unzip(
      GetDataDirectory().AppendASCII("Empty Dir Same Name As File.zip"),
      test_dir_));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("repeated"));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::DIRECTORIES),
      UnorderedElementsAre());

  std::string contents;
  EXPECT_TRUE(
      base::ReadFileToString(test_dir_.AppendASCII("repeated"), &contents));
  EXPECT_EQ("First file", contents);
}

TEST_F(ZipTest, UnzipCannotCreateParentDir) {
  EXPECT_FALSE(zip::Unzip(
      GetDataDirectory().AppendASCII("Parent Dir Same Name As File.zip"),
      test_dir_));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("repeated"));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::DIRECTORIES),
      UnorderedElementsAre());

  std::string contents;
  EXPECT_TRUE(
      base::ReadFileToString(test_dir_.AppendASCII("repeated"), &contents));
  EXPECT_EQ("First file", contents);
}

// TODO(crbug.com/1311140) Detect and rename reserved file names on Windows.
TEST_F(ZipTest, UnzipWindowsSpecialNames) {
  EXPECT_TRUE(
      zip::Unzip(GetDataDirectory().AppendASCII("Windows Special Names.zip"),
                 test_dir_, {.continue_on_error = true}));

  std::unordered_set<std::string> want_paths = {
      "First",
      "Last",
      "CLOCK$",
      " NUL.txt",
#ifndef OS_WIN
      "NUL",
      "NUL ",
      "NUL.",
      "NUL .",
      "NUL.txt",
      "NUL.tar.gz",
      "NUL..txt",
      "NUL...txt",
      "NUL .txt",
      "NUL  .txt",
      "NUL  ..txt",
#ifndef OS_APPLE
      "Nul.txt",
#endif
      "nul.very long extension",
      "a/NUL",
      "CON",
      "PRN",
      "AUX",
      "COM1",
      "COM2",
      "COM3",
      "COM4",
      "COM5",
      "COM6",
      "COM7",
      "COM8",
      "COM9",
      "LPT1",
      "LPT2",
      "LPT3",
      "LPT4",
      "LPT5",
      "LPT6",
      "LPT7",
      "LPT8",
      "LPT9",
#endif
  };

  const std::vector<std::string> got_paths =
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES);

  for (const std::string& path : got_paths) {
    const bool ok = want_paths.erase(path);

#ifdef OS_WIN
    if (!ok) {
      // See crbug.com/1313991: Different versions of Windows treat these
      // filenames differently. No hard error here if there is an unexpected
      // file.
      LOG(WARNING) << "Found unexpected file: " << std::quoted(path);
      continue;
    }
#else
    EXPECT_TRUE(ok) << "Found unexpected file: " << std::quoted(path);
#endif

    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII(path), &contents));
    EXPECT_EQ(base::StrCat({"This is: ", path}), contents);
  }

  for (const std::string& path : want_paths) {
    EXPECT_TRUE(false) << "Cannot find expected file: " << std::quoted(path);
  }
}

TEST_F(ZipTest, UnzipDifferentCases) {
#if defined(OS_WIN) || defined(OS_APPLE)
  // Only the first file (with mixed case) is extracted.
  EXPECT_FALSE(zip::Unzip(GetDataDirectory().AppendASCII(
                              "Repeated File Name With Different Cases.zip"),
                          test_dir_));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("Case"));

  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("Case"), &contents));
  EXPECT_EQ("Mixed case 111", contents);
#else
  // All the files are extracted.
  EXPECT_TRUE(zip::Unzip(GetDataDirectory().AppendASCII(
                             "Repeated File Name With Different Cases.zip"),
                         test_dir_));

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("Case", "case", "CASE"));

  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("Case"), &contents));
  EXPECT_EQ("Mixed case 111", contents);

  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("case"), &contents));
  EXPECT_EQ("Lower case 22", contents);

  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("CASE"), &contents));
  EXPECT_EQ("Upper case 3", contents);
#endif
}

TEST_F(ZipTest, UnzipDifferentCasesContinueOnError) {
  EXPECT_TRUE(zip::Unzip(GetDataDirectory().AppendASCII(
                             "Repeated File Name With Different Cases.zip"),
                         test_dir_, {.continue_on_error = true}));

  std::string contents;

#if defined(OS_WIN) || defined(OS_APPLE)
  // Only the first file (with mixed case) has been extracted.
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("Case"));

  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("Case"), &contents));
  EXPECT_EQ("Mixed case 111", contents);
#else
  // All the files have been extracted.
  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES),
      UnorderedElementsAre("Case", "case", "CASE"));

  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("Case"), &contents));
  EXPECT_EQ("Mixed case 111", contents);

  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("case"), &contents));
  EXPECT_EQ("Lower case 22", contents);

  EXPECT_TRUE(base::ReadFileToString(test_dir_.AppendASCII("CASE"), &contents));
  EXPECT_EQ("Upper case 3", contents);
#endif
}

TEST_F(ZipTest, UnzipMixedPaths) {
  EXPECT_TRUE(zip::Unzip(GetDataDirectory().AppendASCII("Mixed Paths.zip"),
                         test_dir_, {.continue_on_error = true}));

  std::unordered_set<std::string> want_paths = {
#ifdef OS_WIN
      "Dot",     //
      "Space→",  //
#else
      " ",                        //
      "AUX",                      // Disappears on Windows
      "COM1",                     // Disappears on Windows
      "COM2",                     // Disappears on Windows
      "COM3",                     // Disappears on Windows
      "COM4",                     // Disappears on Windows
      "COM5",                     // Disappears on Windows
      "COM6",                     // Disappears on Windows
      "COM7",                     // Disappears on Windows
      "COM8",                     // Disappears on Windows
      "COM9",                     // Disappears on Windows
      "CON",                      // Disappears on Windows
      "Dot .",                    //
      "LPT1",                     // Disappears on Windows
      "LPT2",                     // Disappears on Windows
      "LPT3",                     // Disappears on Windows
      "LPT4",                     // Disappears on Windows
      "LPT5",                     // Disappears on Windows
      "LPT6",                     // Disappears on Windows
      "LPT7",                     // Disappears on Windows
      "LPT8",                     // Disappears on Windows
      "LPT9",                     // Disappears on Windows
      "NUL  ..txt",               // Disappears on Windows
      "NUL  .txt",                // Disappears on Windows
      "NUL ",                     // Disappears on Windows
      "NUL .",                    // Disappears on Windows
      "NUL .txt",                 // Disappears on Windows
      "NUL",                      // Disappears on Windows
      "NUL.",                     // Disappears on Windows
      "NUL...txt",                // Disappears on Windows
      "NUL..txt",                 // Disappears on Windows
      "NUL.tar.gz",               // Disappears on Windows
      "NUL.txt",                  // Disappears on Windows
      "PRN",                      // Disappears on Windows
      "Space→ ",                  //
      "c/NUL",                    // Disappears on Windows
      "nul.very long extension",  // Disappears on Windows
#ifndef OS_APPLE
      "CASE",                     // Conflicts with "Case"
      "case",                     // Conflicts with "Case"
#endif
#endif
      " NUL.txt",                  //
      " ←Space",                   //
      "$HOME",                     //
      "%TMP",                      //
      "-",                         //
      "...Three",                  //
      "..Two",                     //
      ".One",                      //
      "Ampersand &",               //
      "Angle ��",                  //
      "At @",                      //
      "Backslash1→�",              //
      "Backslash3→�←Backslash4",   //
      "Backspace �",               //
      "Backtick `",                //
      "Bell �",                    //
      "CLOCK$",                    //
      "Caret ^",                   //
      "Carriage Return �",         //
      "Case",                      //
      "Colon �",                   //
      "Comma ,",                   //
      "Curly {}",                  //
      "C�",                        //
      "C��",                       //
      "C��Temp",                   //
      "C��Temp�",                  //
      "C��Temp�File",              //
      "Dash -",                    //
      "Delete \x7F",               //
      "Dollar $",                  //
      "Double quote �",            //
      "Equal =",                   //
      "Escape �",                  //
      "Euro €",                    //
      "Exclamation !",             //
      "FileOrDir",                 //
      "First",                     //
      "Hash #",                    //
      "Last",                      //
      "Line Feed �",               //
      "Percent %",                 //
      "Pipe �",                    //
      "Plus +",                    //
      "Question �",                //
      "Quote '",                   //
      "ROOT/At The Top",           //
      "ROOT/UP/Over The Top",      //
      "ROOT/dev/null",             //
      "Round ()",                  //
      "Semicolon ;",               //
      "Smile \U0001F642",          //
      "Square []",                 //
      "Star �",                    //
      "String Terminator \u009C",  //
      "Tab �",                     //
      "Tilde ~",                   //
      "UP/One Level Up",           //
      "UP/UP/Two Levels Up",       //
      "Underscore _",              //
      "a/DOT/b",                   //
      "a/UP/b",                    //
      "u/v/w/x/y/z",               //
      "~",                         //
      "�←Backslash2",              //
      "��server�share�file",       //
  };

  const std::vector<std::string> got_paths =
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::FILES);

  for (const std::string& path : got_paths) {
    const bool ok = want_paths.erase(path);
#ifdef OS_WIN
    // See crbug.com/1313991: Different versions of Windows treat reserved
    // Windows filenames differently. No hard error here if there is an
    // unexpected file.
    LOG_IF(WARNING, !ok) << "Found unexpected file: " << std::quoted(path);
#else
    EXPECT_TRUE(ok) << "Found unexpected file: " << std::quoted(path);
#endif
  }

  for (const std::string& path : want_paths) {
    EXPECT_TRUE(false) << "Cannot find expected file: " << std::quoted(path);
  }

  EXPECT_THAT(
      GetRelativePaths(test_dir_, base::FileEnumerator::FileType::DIRECTORIES),
      UnorderedElementsAreArray({
          "Empty",
          "ROOT",
          "ROOT/Empty",
          "ROOT/UP",
          "ROOT/dev",
          "UP",
          "UP/UP",
          "a",
          "a/DOT",
          "a/UP",
          "c",
          "u",
          "u/v",
          "u/v/w",
          "u/v/w/x",
          "u/v/w/x/y",
      }));
}

TEST_F(ZipTest, UnzipWithDelegates) {
  auto dir_creator =
      base::BindLambdaForTesting([this](const base::FilePath& entry_path) {
        return base::CreateDirectory(test_dir_.Append(entry_path));
      });
  auto writer =
      base::BindLambdaForTesting([this](const base::FilePath& entry_path)
                                     -> std::unique_ptr<zip::WriterDelegate> {
        return std::make_unique<zip::FilePathWriterDelegate>(
            test_dir_.Append(entry_path));
      });

  base::File file(GetDataDirectory().AppendASCII("test.zip"),
                  base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  EXPECT_TRUE(zip::Unzip(file.GetPlatformFile(), writer, dir_creator));
  base::FilePath dir = test_dir_;
  base::FilePath dir_foo = dir.AppendASCII("foo");
  base::FilePath dir_foo_bar = dir_foo.AppendASCII("bar");
  EXPECT_TRUE(base::PathExists(dir.AppendASCII("foo.txt")));
  EXPECT_TRUE(base::DirectoryExists(dir_foo));
  EXPECT_TRUE(base::PathExists(dir_foo.AppendASCII("bar.txt")));
  EXPECT_TRUE(base::DirectoryExists(dir_foo_bar));
  EXPECT_TRUE(base::PathExists(dir_foo_bar.AppendASCII(".hidden")));
  EXPECT_TRUE(base::PathExists(dir_foo_bar.AppendASCII("baz.txt")));
  EXPECT_TRUE(base::PathExists(dir_foo_bar.AppendASCII("quux.txt")));
}

TEST_F(ZipTest, UnzipOnlyDirectories) {
  auto dir_creator =
      base::BindLambdaForTesting([this](const base::FilePath& entry_path) {
        return base::CreateDirectory(test_dir_.Append(entry_path));
      });

  // Always return a null WriterDelegate.
  auto writer =
      base::BindLambdaForTesting([](const base::FilePath& entry_path) {
        return std::unique_ptr<zip::WriterDelegate>();
      });

  base::File file(GetDataDirectory().AppendASCII("test.zip"),
                  base::File::Flags::FLAG_OPEN | base::File::Flags::FLAG_READ);
  EXPECT_TRUE(zip::Unzip(file.GetPlatformFile(), writer, dir_creator,
                         {.continue_on_error = true}));
  base::FilePath dir = test_dir_;
  base::FilePath dir_foo = dir.AppendASCII("foo");
  base::FilePath dir_foo_bar = dir_foo.AppendASCII("bar");
  EXPECT_FALSE(base::PathExists(dir.AppendASCII("foo.txt")));
  EXPECT_TRUE(base::DirectoryExists(dir_foo));
  EXPECT_FALSE(base::PathExists(dir_foo.AppendASCII("bar.txt")));
  EXPECT_TRUE(base::DirectoryExists(dir_foo_bar));
  EXPECT_FALSE(base::PathExists(dir_foo_bar.AppendASCII(".hidden")));
  EXPECT_FALSE(base::PathExists(dir_foo_bar.AppendASCII("baz.txt")));
  EXPECT_FALSE(base::PathExists(dir_foo_bar.AppendASCII("quux.txt")));
}

// Tests that a ZIP archive containing SJIS-encoded file names can be correctly
// extracted if the encoding is specified.
TEST_F(ZipTest, UnzipSjis) {
  ASSERT_TRUE(zip::Unzip(GetDataDirectory().AppendASCII("SJIS Bug 846195.zip"),
                         test_dir_, {.encoding = "Shift_JIS"}));

  const base::FilePath dir =
      test_dir_.Append(base::FilePath::FromUTF8Unsafe("新しいフォルダ"));
  EXPECT_TRUE(base::DirectoryExists(dir));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(
      dir.Append(base::FilePath::FromUTF8Unsafe("SJIS_835C_ソ.txt")),
      &contents));
  EXPECT_EQ(
      "This file's name contains 0x5c (backslash) as the 2nd byte of Japanese "
      "characater \"\x83\x5c\" when encoded in Shift JIS.",
      contents);

  ASSERT_TRUE(base::ReadFileToString(dir.Append(base::FilePath::FromUTF8Unsafe(
                                         "新しいテキスト ドキュメント.txt")),
                                     &contents));
  EXPECT_EQ("This file name is coded in Shift JIS in the archive.", contents);
}

// Tests that a ZIP archive containing SJIS-encoded file names can be extracted
// even if the encoding is not specified. In this case, file names are
// interpreted as UTF-8, which leads to garbled names where invalid UTF-8
// sequences are replaced with the character �. Nevertheless, the files are
// safely extracted and readable.
TEST_F(ZipTest, UnzipSjisAsUtf8) {
  ASSERT_TRUE(zip::Unzip(GetDataDirectory().AppendASCII("SJIS Bug 846195.zip"),
                         test_dir_));

  EXPECT_FALSE(base::DirectoryExists(
      test_dir_.Append(base::FilePath::FromUTF8Unsafe("新しいフォルダ"))));

  const base::FilePath dir =
      test_dir_.Append(base::FilePath::FromUTF8Unsafe("�V�����t�H���_"));
  EXPECT_TRUE(base::DirectoryExists(dir));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(
      dir.Append(base::FilePath::FromUTF8Unsafe("SJIS_835C_��.txt")),
      &contents));
  EXPECT_EQ(
      "This file's name contains 0x5c (backslash) as the 2nd byte of Japanese "
      "characater \"\x83\x5c\" when encoded in Shift JIS.",
      contents);

  ASSERT_TRUE(base::ReadFileToString(dir.Append(base::FilePath::FromUTF8Unsafe(
                                         "�V�����e�L�X�g �h�L�������g.txt")),
                                     &contents));
  EXPECT_EQ("This file name is coded in Shift JIS in the archive.", contents);
}

TEST_F(ZipTest, Zip) {
  base::FilePath src_dir = GetDataDirectory().AppendASCII("test");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath zip_file = temp_dir.GetPath().AppendASCII("out.zip");

  EXPECT_TRUE(zip::Zip(src_dir, zip_file, /*include_hidden_files=*/true));
  TestUnzipFile(zip_file, true);
}

TEST_F(ZipTest, ZipIgnoreHidden) {
  base::FilePath src_dir = GetDataDirectory().AppendASCII("test");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath zip_file = temp_dir.GetPath().AppendASCII("out.zip");

  EXPECT_TRUE(zip::Zip(src_dir, zip_file, /*include_hidden_files=*/false));
  TestUnzipFile(zip_file, false);
}

TEST_F(ZipTest, ZipNonASCIIDir) {
  base::FilePath src_dir = GetDataDirectory().AppendASCII("test");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // Append 'Тест' (in cyrillic).
  base::FilePath src_dir_russian = temp_dir.GetPath().Append(
      base::FilePath::FromUTF8Unsafe("\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82"));
  base::CopyDirectory(src_dir, src_dir_russian, true);
  base::FilePath zip_file = temp_dir.GetPath().AppendASCII("out_russian.zip");

  EXPECT_TRUE(zip::Zip(src_dir_russian, zip_file, true));
  TestUnzipFile(zip_file, true);
}

TEST_F(ZipTest, ZipTimeStamp) {
  // The dates tested are arbitrary, with some constraints. The zip format can
  // only store years from 1980 to 2107 and an even number of seconds, due to it
  // using the ms dos date format.

  // Valid arbitrary date.
  TestTimeStamp("23 Oct 1997 23:22:20", VALID_YEAR);

  // Date before 1980, zip format limitation, must default to unix epoch.
  TestTimeStamp("29 Dec 1979 21:00:10", INVALID_YEAR);

  // Despite the minizip headers telling the maximum year should be 2044, it
  // can actually go up to 2107. Beyond that, the dos date format cannot store
  // the year (2107-1980=127). To test that limit, the input file needs to be
  // touched, but the code that modifies the file access and modification times
  // relies on time_t which is defined as long, therefore being in many
  // platforms just a 4-byte integer, like 32-bit Mac OSX or linux. As such, it
  // suffers from the year-2038 bug. Therefore 2038 is the highest we can test
  // in all platforms reliably.
  TestTimeStamp("02 Jan 2038 23:59:58", VALID_YEAR);
}

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
TEST_F(ZipTest, ZipFiles) {
  base::FilePath src_dir = GetDataDirectory().AppendASCII("test");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath zip_name = temp_dir.GetPath().AppendASCII("out.zip");

  base::File zip_file(zip_name,
                      base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(zip_file.IsValid());
  EXPECT_TRUE(
      zip::ZipFiles(src_dir, zip_file_list_, zip_file.GetPlatformFile()));
  zip_file.Close();

  zip::ZipReader reader;
  EXPECT_TRUE(reader.Open(zip_name));
  EXPECT_EQ(zip_file_list_.size(), static_cast<size_t>(reader.num_entries()));
  for (size_t i = 0; i < zip_file_list_.size(); ++i) {
    const zip::ZipReader::Entry* const entry = reader.Next();
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->path, zip_file_list_[i]);
  }
}
#endif  // defined(OS_POSIX) || defined(OS_FUCHSIA)

TEST_F(ZipTest, UnzipFilesWithIncorrectSize) {
  // test_mismatch_size.zip contains files with names from 0.txt to 7.txt with
  // sizes from 0 to 7 bytes respectively, but the metadata in the zip file says
  // the uncompressed size is 3 bytes. The ZipReader and minizip code needs to
  // be clever enough to get all the data out.
  base::FilePath test_zip_file =
      GetDataDirectory().AppendASCII("test_mismatch_size.zip");

  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const base::FilePath& temp_dir = scoped_temp_dir.GetPath();

  ASSERT_TRUE(zip::Unzip(test_zip_file, temp_dir));
  EXPECT_TRUE(base::DirectoryExists(temp_dir.AppendASCII("d")));

  for (int i = 0; i < 8; i++) {
    SCOPED_TRACE(base::StringPrintf("Processing %d.txt", i));
    base::FilePath file_path =
        temp_dir.AppendASCII(base::StringPrintf("%d.txt", i));
    int64_t file_size = -1;
    EXPECT_TRUE(base::GetFileSize(file_path, &file_size));
    EXPECT_EQ(static_cast<int64_t>(i), file_size);
  }
}

TEST_F(ZipTest, ZipWithFileAccessor) {
  base::FilePath zip_file;
  ASSERT_TRUE(base::CreateTemporaryFile(&zip_file));
  VirtualFileSystem file_accessor;
  const zip::ZipParams params{.file_accessor = &file_accessor,
                              .dest_file = zip_file};
  ASSERT_TRUE(zip::Zip(params));

  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const base::FilePath& temp_dir = scoped_temp_dir.GetPath();
  ASSERT_TRUE(zip::Unzip(zip_file, temp_dir));
  base::FilePath bar_dir = temp_dir.AppendASCII("bar");
  EXPECT_TRUE(base::DirectoryExists(bar_dir));
  std::string file_content;
  EXPECT_TRUE(
      base::ReadFileToString(temp_dir.AppendASCII("foo.txt"), &file_content));
  EXPECT_EQ(VirtualFileSystem::kFooContent, file_content);
  EXPECT_TRUE(
      base::ReadFileToString(bar_dir.AppendASCII("bar1.txt"), &file_content));
  EXPECT_EQ(VirtualFileSystem::kBar1Content, file_content);
  EXPECT_TRUE(
      base::ReadFileToString(bar_dir.AppendASCII("bar2.txt"), &file_content));
  EXPECT_EQ(VirtualFileSystem::kBar2Content, file_content);
}

// Tests progress reporting while zipping files.
TEST_F(ZipTest, ZipProgress) {
  base::FilePath src_dir = GetDataDirectory().AppendASCII("test");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath zip_file = temp_dir.GetPath().AppendASCII("out.zip");

  int progress_count = 0;
  zip::Progress last_progress;

  zip::ProgressCallback progress_callback =
      base::BindLambdaForTesting([&](const zip::Progress& progress) {
        progress_count++;
        LOG(INFO) << "Progress #" << progress_count << ": " << progress;

        // Progress should only go forwards.
        EXPECT_GE(progress.bytes, last_progress.bytes);
        EXPECT_GE(progress.files, last_progress.files);
        EXPECT_GE(progress.directories, last_progress.directories);

        last_progress = progress;
        return true;
      });

  EXPECT_TRUE(zip::Zip({.src_dir = src_dir,
                        .dest_file = zip_file,
                        .progress_callback = std::move(progress_callback)}));

  EXPECT_EQ(progress_count, 14);
  EXPECT_EQ(last_progress.bytes, 13546);
  EXPECT_EQ(last_progress.files, 5);
  EXPECT_EQ(last_progress.directories, 2);

  TestUnzipFile(zip_file, true);
}

// Tests throttling of progress reporting while zipping files.
TEST_F(ZipTest, ZipProgressPeriod) {
  base::FilePath src_dir = GetDataDirectory().AppendASCII("test");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath zip_file = temp_dir.GetPath().AppendASCII("out.zip");

  int progress_count = 0;
  zip::Progress last_progress;

  zip::ProgressCallback progress_callback =
      base::BindLambdaForTesting([&](const zip::Progress& progress) {
        progress_count++;
        LOG(INFO) << "Progress #" << progress_count << ": " << progress;

        // Progress should only go forwards.
        EXPECT_GE(progress.bytes, last_progress.bytes);
        EXPECT_GE(progress.files, last_progress.files);
        EXPECT_GE(progress.directories, last_progress.directories);

        last_progress = progress;
        return true;
      });

  EXPECT_TRUE(zip::Zip({.src_dir = src_dir,
                        .dest_file = zip_file,
                        .progress_callback = std::move(progress_callback),
                        .progress_period = base::Hours(1)}));

  // We expect only 2 progress reports: the first one, and the last one.
  EXPECT_EQ(progress_count, 2);
  EXPECT_EQ(last_progress.bytes, 13546);
  EXPECT_EQ(last_progress.files, 5);
  EXPECT_EQ(last_progress.directories, 2);

  TestUnzipFile(zip_file, true);
}

// Tests cancellation while zipping files.
TEST_F(ZipTest, ZipCancel) {
  base::FilePath src_dir = GetDataDirectory().AppendASCII("test");

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath zip_file = temp_dir.GetPath().AppendASCII("out.zip");

  // First: establish the number of possible interruption points.
  int progress_count = 0;

  EXPECT_TRUE(zip::Zip({.src_dir = src_dir,
                        .dest_file = zip_file,
                        .progress_callback = base::BindLambdaForTesting(
                            [&progress_count](const zip::Progress&) {
                              progress_count++;
                              return true;
                            })}));

  EXPECT_EQ(progress_count, 14);

  // Second: exercise each and every interruption point.
  for (int i = progress_count; i > 0; i--) {
    int j = 0;
    EXPECT_FALSE(zip::Zip({.src_dir = src_dir,
                           .dest_file = zip_file,
                           .progress_callback = base::BindLambdaForTesting(
                               [i, &j](const zip::Progress&) {
                                 j++;
                                 // Callback shouldn't be called again after
                                 // having returned false once.
                                 EXPECT_LE(j, i);
                                 return j < i;
                               })}));

    EXPECT_EQ(j, i);
  }
}

// Tests zip::internal::GetCompressionMethod()
TEST_F(ZipTest, GetCompressionMethod) {
  using zip::internal::GetCompressionMethod;
  using zip::internal::kDeflated;
  using zip::internal::kStored;

  EXPECT_EQ(GetCompressionMethod(FP("")), kDeflated);
  EXPECT_EQ(GetCompressionMethod(FP("NoExtension")), kDeflated);
  EXPECT_EQ(GetCompressionMethod(FP("Folder.zip").Append(FP("NoExtension"))),
            kDeflated);
  EXPECT_EQ(GetCompressionMethod(FP("Name.txt")), kDeflated);
  EXPECT_EQ(GetCompressionMethod(FP("Name.zip")), kStored);
  EXPECT_EQ(GetCompressionMethod(FP("Name....zip")), kStored);
  EXPECT_EQ(GetCompressionMethod(FP("Name.zip")), kStored);
  EXPECT_EQ(GetCompressionMethod(FP("NAME.ZIP")), kStored);
  EXPECT_EQ(GetCompressionMethod(FP("Name.gz")), kStored);
  EXPECT_EQ(GetCompressionMethod(FP("Name.tar.gz")), kStored);
  EXPECT_EQ(GetCompressionMethod(FP("Name.tar")), kDeflated);

  // This one is controversial.
  EXPECT_EQ(GetCompressionMethod(FP(".zip")), kStored);
}

// Tests that files put inside a ZIP are effectively compressed.
TEST_F(ZipTest, Compressed) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const base::FilePath src_dir = temp_dir.GetPath().AppendASCII("input");
  EXPECT_TRUE(base::CreateDirectory(src_dir));

  // Create some dummy source files.
  for (const std::string_view s : {"foo", "bar.txt", ".hidden"}) {
    base::File f(src_dir.AppendASCII(s),
                 base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(f.SetLength(5000));
  }

  // Zip the source files.
  const base::FilePath dest_file = temp_dir.GetPath().AppendASCII("dest.zip");
  EXPECT_TRUE(zip::Zip({.src_dir = src_dir,
                        .dest_file = dest_file,
                        .include_hidden_files = true}));

  // Since the source files compress well, the destination ZIP file should be
  // smaller than the source files.
  int64_t dest_file_size;
  ASSERT_TRUE(base::GetFileSize(dest_file, &dest_file_size));
  EXPECT_GT(dest_file_size, 300);
  EXPECT_LT(dest_file_size, 1000);
}

// Tests that a ZIP put inside a ZIP is simply stored instead of being
// compressed.
TEST_F(ZipTest, NestedZip) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const base::FilePath src_dir = temp_dir.GetPath().AppendASCII("input");
  EXPECT_TRUE(base::CreateDirectory(src_dir));

  // Create a dummy ZIP file. This is not a valid ZIP file, but for the purpose
  // of this test, it doesn't really matter.
  const int64_t src_size = 5000;

  {
    base::File f(src_dir.AppendASCII("src.zip"),
                 base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(f.SetLength(src_size));
  }

  // Zip the dummy ZIP file.
  const base::FilePath dest_file = temp_dir.GetPath().AppendASCII("dest.zip");
  EXPECT_TRUE(zip::Zip({.src_dir = src_dir, .dest_file = dest_file}));

  // Since the dummy source (inner) ZIP file should simply be stored in the
  // destination (outer) ZIP file, the destination file should be bigger than
  // the source file, but not much bigger.
  int64_t dest_file_size;
  ASSERT_TRUE(base::GetFileSize(dest_file, &dest_file_size));
  EXPECT_GT(dest_file_size, src_size + 100);
  EXPECT_LT(dest_file_size, src_size + 300);
}

// Tests that there is no 2GB or 4GB limits. Tests that big files can be zipped
// (crbug.com/1207737) and that big ZIP files can be created
// (crbug.com/1221447). Tests that the big ZIP can be opened, that its entries
// are correctly enumerated (crbug.com/1298347), and that the big file can be
// extracted.
//
// Because this test is dealing with big files, it tends to take a lot of disk
// space and time (crbug.com/1299736). Therefore, it only gets run on a few bots
// (ChromeOS and Windows).
//
// This test is too slow with TSAN.
// OS Fuchsia does not seem to support large files.
// Some 32-bit Android waterfall and CQ try bots are running out of space when
// performing this test (android-asan, android-11-x86-rel,
// android-marshmallow-x86-rel-non-cq).
// Some Mac, Linux and Debug (dbg) bots tend to time out when performing this
// test (crbug.com/1299736, crbug.com/1300448, crbug.com/1369958).
#if defined(THREAD_SANITIZER) || BUILDFLAG(IS_FUCHSIA) ||                \
    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || !defined(NDEBUG)
TEST_F(ZipTest, DISABLED_BigFile) {
#else
TEST_F(ZipTest, BigFile) {
#endif
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const base::FilePath src_dir = temp_dir.GetPath().AppendASCII("input");
  EXPECT_TRUE(base::CreateDirectory(src_dir));

  // Create a big dummy ZIP file. This is not a valid ZIP file, but for the
  // purpose of this test, it doesn't really matter.
  const int64_t src_size = 5'000'000'000;

  const base::FilePath src_file = src_dir.AppendASCII("src.zip");
  LOG(INFO) << "Creating big file " << src_file;
  {
    base::File f(src_file, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(f.SetLength(src_size));
  }

  // Zip the dummy ZIP file.
  const base::FilePath dest_file = temp_dir.GetPath().AppendASCII("dest.zip");
  LOG(INFO) << "Zipping big file into " << dest_file;
  zip::ProgressCallback progress_callback =
      base::BindLambdaForTesting([&](const zip::Progress& progress) {
        LOG(INFO) << "Zipping... " << std::setw(3)
                  << (100 * progress.bytes / src_size) << "%";
        return true;
      });
  EXPECT_TRUE(zip::Zip({.src_dir = src_dir,
                        .dest_file = dest_file,
                        .progress_callback = std::move(progress_callback),
                        .progress_period = base::Seconds(1)}));

  // Since the dummy source (inner) ZIP file should simply be stored in the
  // destination (outer) ZIP file, the destination file should be bigger than
  // the source file, but not much bigger.
  int64_t dest_file_size;
  ASSERT_TRUE(base::GetFileSize(dest_file, &dest_file_size));
  EXPECT_GT(dest_file_size, src_size + 100);
  EXPECT_LT(dest_file_size, src_size + 300);

  LOG(INFO) << "Reading big ZIP " << dest_file;
  zip::ZipReader reader;
  ASSERT_TRUE(reader.Open(dest_file));

  const zip::ZipReader::Entry* const entry = reader.Next();
  ASSERT_TRUE(entry);
  EXPECT_EQ(FP("src.zip"), entry->path);
  EXPECT_EQ(src_size, entry->original_size);
  EXPECT_FALSE(entry->is_directory);
  EXPECT_FALSE(entry->is_encrypted);

  ProgressWriterDelegate writer(src_size);
  EXPECT_TRUE(reader.ExtractCurrentEntry(&writer,
                                         std::numeric_limits<uint64_t>::max()));
  EXPECT_EQ(src_size, writer.received_bytes());

  EXPECT_FALSE(reader.Next());
  EXPECT_TRUE(reader.ok());
}

}  // namespace
