// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-import-attributes

import {life} from 'modules-skip-imports-attributes-json-1.mjs';

assertEquals(42, life());
