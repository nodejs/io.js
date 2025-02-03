// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_EXPORT_H_
#define CRDTP_EXPORT_H_

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(CRDTP_IMPLEMENTATION)
#define CRDTP_EXPORT __declspec(dllexport)
#else
#define CRDTP_EXPORT __declspec(dllimport)
#endif  // defined(CRDTP_IMPLEMENTATION)

#else  // defined(WIN32)
#if defined(CRDTP_IMPLEMENTATION)
#define CRDTP_EXPORT __attribute__((visibility("default")))
#else
#define CRDTP_EXPORT
#endif
#endif

#else  // defined(COMPONENT_BUILD)
#define CRDTP_EXPORT
#endif

#endif  // CRDTP_EXPORT_H_
