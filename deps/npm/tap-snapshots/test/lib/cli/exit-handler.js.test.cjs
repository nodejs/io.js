/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/cli/exit-handler.js TAP handles unknown error with logs and debug file > debug file contents 1`] = `
XX timing npm:load:whichnode Completed in {TIME}ms
XX silly config load:file:{CWD}/npmrc
XX silly config load:file:{CWD}/prefix/.npmrc
XX silly config load:file:{CWD}/home/.npmrc
XX silly config load:file:{CWD}/global/etc/npmrc
XX timing npm:load:configload Completed in {TIME}ms
XX timing npm:load:mkdirpcache Completed in {TIME}ms
XX timing npm:load:mkdirplogs Completed in {TIME}ms
XX verbose title npm
XX verbose argv "--fetch-retries" "0" "--cache" "{CWD}/cache" "--loglevel" "silly" "--color" "false" "--timing" "true"
XX timing npm:load:setTitle Completed in {TIME}ms
XX verbose logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-
XX verbose logfile {CWD}/cache/_logs/{DATE}-debug-0.log
XX timing npm:load Completed in {TIME}ms
XX timing command:root Completed in {TIME}ms
XX verbose stack Error: Unknown error
XX error code ECODE
XX error Unknown error
XX timing npm Completed in {TIME}ms
XX info timing Timing info written to: {CWD}/cache/_logs/{DATE}-timing.json
XX verbose cwd {CWD}/prefix
XX verbose os {OS}
XX verbose {NODE-VERSION}
XX verbose npm  {NPM-VERSION}
XX verbose exit 1
XX verbose code 1
XX error A complete log of this run can be found in: {CWD}/cache/_logs/{DATE}-debug-0.log
`

exports[`test/lib/cli/exit-handler.js TAP handles unknown error with logs and debug file > logs 1`] = `
timing npm:load:whichnode Completed in {TIME}ms
silly config load:file:{CWD}/npmrc
silly config load:file:{CWD}/prefix/.npmrc
silly config load:file:{CWD}/home/.npmrc
silly config load:file:{CWD}/global/etc/npmrc
timing npm:load:configload Completed in {TIME}ms
timing npm:load:mkdirpcache Completed in {TIME}ms
timing npm:load:mkdirplogs Completed in {TIME}ms
verbose title npm
verbose argv "--fetch-retries" "0" "--cache" "{CWD}/cache" "--loglevel" "silly" "--color" "false" "--timing" "true"
timing npm:load:setTitle Completed in {TIME}ms
verbose logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-
verbose logfile {CWD}/cache/_logs/{DATE}-debug-0.log
timing npm:load Completed in {TIME}ms
timing command:root Completed in {TIME}ms
verbose stack Error: Unknown error
error code ECODE
error Unknown error
timing npm Completed in {TIME}ms
info timing Timing info written to: {CWD}/cache/_logs/{DATE}-timing.json
verbose cwd {CWD}/prefix
verbose os {OS}
verbose {NODE-VERSION}
verbose npm  {NPM-VERSION}
verbose exit 1
verbose code 1
error A complete log of this run can be found in: {CWD}/cache/_logs/{DATE}-debug-0.log
`
