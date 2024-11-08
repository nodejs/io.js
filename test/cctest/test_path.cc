#include "debug_utils-inl.h"
#include "env-inl.h"
#include "gtest/gtest.h"
#include "node_options.h"
#include "node_test_fixture.h"
#include "path.h"
#include "util.h"
#include "v8.h"

using node::BufferValue;
using node::PathResolve;
using node::ToNamespacedPath;

class PathTest : public EnvironmentTestFixture {};

TEST_F(PathTest, PathResolve) {
  const v8::HandleScope handle_scope(isolate_);
  Argv argv;
  Env env{handle_scope, argv, node::EnvironmentFlags::kNoBrowserGlobals};
  auto cwd = (*env)->GetCwd((*env)->exec_path());
#ifdef _WIN32
  EXPECT_EQ(PathResolve(*env, {"c:/blah\\blah", "d:/games", "c:../a"}),
            "c:\\blah\\a");
  EXPECT_EQ(PathResolve(*env, {"c:/ignore", "d:\\a/b\\c/d", "\\e.exe"}),
            "d:\\e.exe");
  EXPECT_EQ(PathResolve(*env, {"c:/ignore", "c:/some/file"}), "c:\\some\\file");
  EXPECT_EQ(PathResolve(*env, {"d:/ignore", "d:some/dir//"}),
            "d:\\ignore\\some\\dir");
  EXPECT_EQ(PathResolve(*env, {"."}), cwd);
  EXPECT_EQ(PathResolve(*env, {"//server/share", "..", "relative\\"}),
            "\\\\server\\share\\relative");
  EXPECT_EQ(PathResolve(*env, {"c:/", "//"}), "c:\\");
  EXPECT_EQ(PathResolve(*env, {"c:/", "//dir"}), "c:\\dir");
  EXPECT_EQ(PathResolve(*env, {"c:/", "//server/share"}),
            "\\\\server\\share\\");
  EXPECT_EQ(PathResolve(*env, {"c:/", "//server//share"}),
            "\\\\server\\share\\");
  EXPECT_EQ(PathResolve(*env, {"c:/", "///some//dir"}), "c:\\some\\dir");
  EXPECT_EQ(
      PathResolve(*env, {"C:\\foo\\tmp.3\\", "..\\tmp.3\\cycles\\root.js"}),
      "C:\\foo\\tmp.3\\cycles\\root.js");
  EXPECT_EQ(PathResolve(*env, {"\\\\.\\PHYSICALDRIVE0"}),
            "\\\\.\\PHYSICALDRIVE0");
  EXPECT_EQ(PathResolve(*env, {"\\\\?\\PHYSICALDRIVE0"}),
            "\\\\?\\PHYSICALDRIVE0");
#else
  EXPECT_EQ(PathResolve(*env, {"/var/lib", "../", "file/"}), "/var/file");
  EXPECT_EQ(PathResolve(*env, {"/var/lib", "/../", "file/"}), "/file");
  EXPECT_EQ(PathResolve(*env, {"a/b/c/", "../../.."}), cwd);
  EXPECT_EQ(PathResolve(*env, {"."}), cwd);
  EXPECT_EQ(PathResolve(*env, {"/some/dir", ".", "/absolute/"}), "/absolute");
  EXPECT_EQ(PathResolve(*env, {"/foo/tmp.3/", "../tmp.3/cycles/root.js"}),
            "/foo/tmp.3/cycles/root.js");
#endif
}

TEST_F(PathTest, ToNamespacedPath) {
  const v8::HandleScope handle_scope(isolate_);
  Argv argv;
  Env env{handle_scope, argv, node::EnvironmentFlags::kNoBrowserGlobals};
#ifdef _WIN32
  BufferValue data(isolate_,
                   v8::String::NewFromUtf8(isolate_, "").ToLocalChecked());
  ToNamespacedPath(*env, &data);
  EXPECT_EQ(data.ToStringView(), "");  // Empty string should not be mutated
  BufferValue data_2(
      isolate_, v8::String::NewFromUtf8(isolate_, "C://").ToLocalChecked());
  ToNamespacedPath(*env, &data_2);
  EXPECT_EQ(data_2.ToStringView(), "\\\\?\\C:\\");
  BufferValue data_3(
      isolate_,
      v8::String::NewFromUtf8(
          isolate_,
          "C:\\workspace\\node-test-binary-windows-js-"
          "suites\\node\\test\\fixtures\\permission\\deny\\protected-file.md")
          .ToLocalChecked());
  ToNamespacedPath(*env, &data_3);
  EXPECT_EQ(
      data_3.ToStringView(),
      "\\\\?\\C:\\workspace\\node-test-binary-windows-js-"
      "suites\\node\\test\\fixtures\\permission\\deny\\protected-file.md");
  BufferValue data_4(
      isolate_,
      v8::String::NewFromUtf8(isolate_, "\\\\?\\c:\\Windows/System")
          .ToLocalChecked());
  ToNamespacedPath(*env, &data_4);
  EXPECT_EQ(data_4.ToStringView(), "\\\\?\\c:\\Windows\\System");
#else
  BufferValue data(
      isolate_,
      v8::String::NewFromUtf8(isolate_, "hello world").ToLocalChecked());
  ToNamespacedPath(*env, &data);
  EXPECT_EQ(data.ToStringView(), "hello world");  // Input should not be mutated
#endif
}
