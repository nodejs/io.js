// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


d8.file.execute('../base.js');

d8.file.execute('ctor.js');
d8.file.execute('exec.js');
d8.file.execute('flags.js');
d8.file.execute('inline_test.js')
d8.file.execute('complex_case_test.js');
d8.file.execute('case_test.js');
d8.file.execute('match.js');
d8.file.execute('replaceall_emoji_g.js');
d8.file.execute('replaceall_emoji_gu.js');
d8.file.execute('replaceall_emoji_gv.js');
d8.file.execute('replace_emoji_g.js');
d8.file.execute('replace_emoji_gu.js');
d8.file.execute('replace_emoji_gv.js');
d8.file.execute('replace.js');
d8.file.execute('search.js');
d8.file.execute('slow_exec.js');
d8.file.execute('slow_flags.js');
d8.file.execute('slow_match.js');
d8.file.execute('slow_replaceall_emoji_g.js');
d8.file.execute('slow_replaceall_emoji_gu.js');
d8.file.execute('slow_replaceall_emoji_gv.js');
d8.file.execute('slow_replace_emoji_g.js');
d8.file.execute('slow_replace_emoji_gu.js');
d8.file.execute('slow_replace_emoji_gv.js');
d8.file.execute('slow_replace.js');
d8.file.execute('slow_search.js');
d8.file.execute('slow_split.js');
d8.file.execute('slow_test.js');
d8.file.execute('split.js');
d8.file.execute('test.js');

var success = true;

function PrintResult(name, result) {
  print(name + '-RegExp(Score): ' + result);
}


function PrintError(name, error) {
  PrintResult(name, error);
  success = false;
}


BenchmarkSuite.config.doWarmup = undefined;
BenchmarkSuite.config.doDeterministic = undefined;

BenchmarkSuite.RunSuites({ NotifyResult: PrintResult,
                           NotifyError: PrintError });
