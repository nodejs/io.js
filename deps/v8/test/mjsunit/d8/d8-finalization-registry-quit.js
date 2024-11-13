// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --expose-gc

// Quitting from within a FR cleanup task should not hang d8.

const reg = new FinalizationRegistry(() => {
    console.log("Quitting");
    quit(0);
});

reg.register({});

function gcLoop() {
    console.log("Running GC loop");
    gc();
    setTimeout(gcLoop);
}
gcLoop();
