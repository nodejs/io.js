// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import typescript from "rollup-plugin-typescript2";
import node from "rollup-plugin-node-resolve";
import path from "path";

const warningHandler = (warning) => {
  // Silence circular dependency warning for moment package
  const node_modules = path.normalize('node_modules/');
  if (warning.code === 'CIRCULAR_DEPENDENCY' && warning.ids.some(id => id.indexOf(node_modules) !== -1)) {
    return;
  }
  console.warn(`(!) ${warning.message}`)
}

export default {
  input: "src/turbo-visualizer.ts",
  plugins: [node(), typescript({
    abortOnError: false
  })],
  output: {
    file: "build/turbolizer.js",
    format: "iife",
    sourcemap: true
  },
  onwarn: warningHandler
};
