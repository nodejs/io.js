// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


const firstSet = new Set();
firstSet.add(42);

const otherSet = new Set();

const resultSet = new Set();
resultSet.add(42);

const resultArray = Array.from(resultSet);
const differenceArray = Array.from(firstSet.difference(otherSet));

assertEquals(resultArray, differenceArray);
