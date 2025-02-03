/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/utils/log-file.js TAP snapshot > must match snapshot 1`] = `
0 verbose logfile logs-max:10 dir:{CWD}/{DATE}-
1 verbose logfile {CWD}/{DATE}-debug-0.log
2 silly logfile done cleaning log files
3 error no prefix
4 error prefix with prefix
5 error prefix 1 2 3
6 verbose { obj: { with: { many: [Object] } } }
7 verbose {"obj":{"with":{"many":{"props":1}}}}
8 verbose {
8 verbose   "obj": {
8 verbose     "with": {
8 verbose       "many": {
8 verbose         "props": 1
8 verbose       }
8 verbose     }
8 verbose   }
8 verbose }
9 verbose [ 'test', 'with', 'an', 'array' ]
10 verbose ["test","with","an","array"]
11 verbose [
11 verbose   "test",
11 verbose   "with",
11 verbose   "an",
11 verbose   "array"
11 verbose ]
12 verbose [ 'test', [ 'with', [ 'an', [Array] ] ] ]
13 verbose ["test",["with",["an",["array"]]]]
14 verbose [
14 verbose   "test",
14 verbose   [
14 verbose     "with",
14 verbose     [
14 verbose       "an",
14 verbose       [
14 verbose         "array"
14 verbose       ]
14 verbose     ]
14 verbose   ]
14 verbose ]
15 error pre has many errors Error: message
15 error pre     at stack trace line 0
15 error pre     at stack trace line 1
15 error pre     at stack trace line 2
15 error pre     at stack trace line 3
15 error pre     at stack trace line 4
15 error pre     at stack trace line 5
15 error pre     at stack trace line 6
15 error pre     at stack trace line 7
15 error pre     at stack trace line 8
15 error pre     at stack trace line 9 Error: message2
15 error pre     at stack trace line 0
15 error pre     at stack trace line 1
15 error pre     at stack trace line 2
15 error pre     at stack trace line 3
15 error pre     at stack trace line 4
15 error pre     at stack trace line 5
15 error pre     at stack trace line 6
15 error pre     at stack trace line 7
15 error pre     at stack trace line 8
15 error pre     at stack trace line 9
16 error nostack [Error: message]

`
