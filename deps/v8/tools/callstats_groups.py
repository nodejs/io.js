# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import re

RUNTIME_CALL_STATS_GROUPS = [
    ('Group-IC', re.compile(".*IC_.*")),
    ('Group-OptimizeBackground', re.compile(".*OptimizeBackground.*")),
    ('Group-Optimize',
     re.compile("StackGuard|.*Optimize.*|.*Deoptimize.*|Recompile.*")),
    ('Group-CompileBackground', re.compile("(.*CompileBackground.*)")),
    ('Group-Compile', re.compile("(^Compile.*)|(.*_Compile.*)")),
    ('Group-ParseBackground', re.compile(".*ParseBackground.*")),
    ('Group-Parse', re.compile(".*Parse.*")),
    ('Group-Network-Data', re.compile(".*GetMoreDataCallback.*")),
    ('Group-Callback', re.compile(".*(Callback)|(Blink \+\+).*")),
    ('Group-API', re.compile(".*API.*")),
    ('Group-GC-Custom', re.compile("GC_Custom_.*")),
    ('Group-GC-Background', re.compile("GC_.*BACKGROUND.*")),
    ('Group-GC', re.compile("GC_.*|AllocateInTargetSpace")),
    ('Group-JavaScript', re.compile("JS_Execution")),
    ('Group-Runtime', re.compile(".*"))
]
