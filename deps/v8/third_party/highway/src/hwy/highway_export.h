// Pseudo-generated file to handle both cmake & bazel build system.

// Initial generation done using cmake code:
// include(GenerateExportHeader)
// generate_export_header(hwy EXPORT_MACRO_NAME HWY_DLLEXPORT EXPORT_FILE_NAME
// hwy/highway_export.h)
// code reformatted using clang-format --style=Google

#ifndef HWY_DLLEXPORT_H
#define HWY_DLLEXPORT_H

#if !defined(HWY_SHARED_DEFINE)
#define HWY_DLLEXPORT
#define HWY_CONTRIB_DLLEXPORT
#define HWY_TEST_DLLEXPORT
#else  // !HWY_SHARED_DEFINE

#ifndef HWY_DLLEXPORT
#if defined(hwy_EXPORTS)
/* We are building this library */
#ifdef _WIN32
#define HWY_DLLEXPORT __declspec(dllexport)
#else
#define HWY_DLLEXPORT __attribute__((visibility("default")))
#endif
#else  // defined(hwy_EXPORTS)
/* We are using this library */
#ifdef _WIN32
#define HWY_DLLEXPORT __declspec(dllimport)
#else
#define HWY_DLLEXPORT __attribute__((visibility("default")))
#endif
#endif  // defined(hwy_EXPORTS)
#endif  // HWY_DLLEXPORT

#ifndef HWY_CONTRIB_DLLEXPORT
#if defined(hwy_contrib_EXPORTS)
/* We are building this library */
#ifdef _WIN32
#define HWY_CONTRIB_DLLEXPORT __declspec(dllexport)
#else
#define HWY_CONTRIB_DLLEXPORT __attribute__((visibility("default")))
#endif
#else  // defined(hwy_contrib_EXPORTS)
/* We are using this library */
#ifdef _WIN32
#define HWY_CONTRIB_DLLEXPORT __declspec(dllimport)
#else
#define HWY_CONTRIB_DLLEXPORT __attribute__((visibility("default")))
#endif
#endif  // defined(hwy_contrib_EXPORTS)
#endif  // HWY_CONTRIB_DLLEXPORT

#ifndef HWY_TEST_DLLEXPORT
#if defined(hwy_test_EXPORTS)
/* We are building this library */
#ifdef _WIN32
#define HWY_TEST_DLLEXPORT __declspec(dllexport)
#else
#define HWY_TEST_DLLEXPORT __attribute__((visibility("default")))
#endif
#else  // defined(hwy_test_EXPORTS)
/* We are using this library */
#ifdef _WIN32
#define HWY_TEST_DLLEXPORT __declspec(dllimport)
#else
#define HWY_TEST_DLLEXPORT __attribute__((visibility("default")))
#endif
#endif  // defined(hwy_test_EXPORTS)
#endif  // HWY_TEST_DLLEXPORT

#endif  // !HWY_SHARED_DEFINE

#endif /* HWY_DLLEXPORT_H */
