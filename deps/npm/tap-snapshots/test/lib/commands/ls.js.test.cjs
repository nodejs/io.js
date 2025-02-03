/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/ls.js TAP ignore missing optional deps --json > ls --json problems 1`] = `
Array [
  "invalid: optional-wrong@3.2.1 {CWD}/prefix/node_modules/optional-wrong",
  "missing: peer-missing@1, required by test-npm-ls-ignore-missing-optional@1.2.3",
  "invalid: peer-optional-wrong@3.2.1 {CWD}/prefix/node_modules/peer-optional-wrong",
  "invalid: peer-wrong@3.2.1 {CWD}/prefix/node_modules/peer-wrong",
  "missing: prod-missing@1, required by test-npm-ls-ignore-missing-optional@1.2.3",
  "invalid: prod-wrong@3.2.1 {CWD}/prefix/node_modules/prod-wrong",
]
`

exports[`test/lib/commands/ls.js TAP ignore missing optional deps --parseable > ls --parseable result 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/optional-ok
{CWD}/prefix/node_modules/optional-wrong
{CWD}/prefix/node_modules/peer-ok
{CWD}/prefix/node_modules/peer-optional-ok
{CWD}/prefix/node_modules/peer-optional-wrong
{CWD}/prefix/node_modules/peer-wrong
{CWD}/prefix/node_modules/prod-ok
{CWD}/prefix/node_modules/prod-wrong
`

exports[`test/lib/commands/ls.js TAP ignore missing optional deps human output > ls result 1`] = `
test-npm-ls-ignore-missing-optional@1.2.3 {CWD}/prefix
+-- UNMET OPTIONAL DEPENDENCY optional-missing@1
+-- optional-ok@1.2.3
+-- optional-wrong@3.2.1 invalid: "1" from the root project
+-- UNMET DEPENDENCY peer-missing@1
+-- peer-ok@1.2.3
+-- UNMET OPTIONAL DEPENDENCY peer-optional-missing@1
+-- peer-optional-ok@1.2.3
+-- peer-optional-wrong@3.2.1 invalid: "1" from the root project
+-- peer-wrong@3.2.1 invalid: "1" from the root project
+-- UNMET DEPENDENCY prod-missing@1
+-- prod-ok@1.2.3
\`-- prod-wrong@3.2.1 invalid: "1" from the root project
`

exports[`test/lib/commands/ls.js TAP ls --depth=0 > should output tree containing only top-level dependencies 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0
\`-- foo@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --depth=1 > should output tree containing top-level deps and their deps only 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- a@1.0.0
| \`-- b@1.0.0
\`-- e@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --dev > should output tree containing dev deps 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- dev-dep@1.0.0
  \`-- foo@1.0.0
    \`-- dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --link > should output tree containing linked deps 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- linked-dep@1.0.0 -> ./linked-dep
`

exports[`test/lib/commands/ls.js TAP ls --long --depth=0 > should output tree containing top-level deps with descriptions 1`] = `
test-npm-ls@1.0.0
| {CWD}/prefix
|
+-- chai@1.0.0
|
+-- dev-dep@1.0.0
|   A DEV dep kind of dep
+-- optional-dep@1.0.0
|   Maybe a dep?
+-- peer-dep@1.0.0
|   Peer-dep description here
\`-- prod-dep@1.0.0
    A PROD dep kind of dep
`

exports[`test/lib/commands/ls.js TAP ls --long > should output tree info with descriptions 1`] = `
test-npm-ls@1.0.0
| {CWD}/prefix
|
+-- chai@1.0.0
|
+-- dev-dep@1.0.0
| | A DEV dep kind of dep
| \`-- foo@1.0.0
|   |
|   \`-- dog@1.0.0
|
+-- optional-dep@1.0.0
|   Maybe a dep?
+-- peer-dep@1.0.0
|   Peer-dep description here
\`-- prod-dep@1.0.0
  | A PROD dep kind of dep
  \`-- dog@2.0.0
      A dep that bars
`

exports[`test/lib/commands/ls.js TAP ls --parseable --depth=0 > should output tree containing only top-level dependencies 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/foo
`

exports[`test/lib/commands/ls.js TAP ls --parseable --depth=1 > should output parseable containing top-level deps and their deps only 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/foo
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable --dev > should output tree containing dev deps 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/dev-dep
{CWD}/prefix/node_modules/foo
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable --link > should output tree containing linked deps 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/linked-dep
`

exports[`test/lib/commands/ls.js TAP ls --parseable --long --depth=0 > should output tree containing top-level deps with descriptions 1`] = `
{CWD}/prefix:test-npm-ls@1.0.0
{CWD}/prefix/node_modules/chai:chai@1.0.0
{CWD}/prefix/node_modules/dev-dep:dev-dep@1.0.0
{CWD}/prefix/node_modules/optional-dep:optional-dep@1.0.0
{CWD}/prefix/node_modules/peer-dep:peer-dep@1.0.0
{CWD}/prefix/node_modules/prod-dep:prod-dep@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --parseable --long > should output tree info with descriptions 1`] = `
{CWD}/prefix:test-npm-ls@1.0.0
{CWD}/prefix/node_modules/chai:chai@1.0.0
{CWD}/prefix/node_modules/dev-dep:dev-dep@1.0.0
{CWD}/prefix/node_modules/optional-dep:optional-dep@1.0.0
{CWD}/prefix/node_modules/peer-dep:peer-dep@1.0.0
{CWD}/prefix/node_modules/prod-dep:prod-dep@1.0.0
{CWD}/prefix/node_modules/foo:foo@1.0.0
{CWD}/prefix/node_modules/prod-dep/node_modules/dog:dog@2.0.0
{CWD}/prefix/node_modules/dog:dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --parseable --long missing/invalid/extraneous > should output parseable result containing EXTRANEOUS/INVALID labels 1`] = `
{CWD}/prefix:test-npm-ls@1.0.0
{CWD}/prefix/node_modules/chai:chai@1.0.0:EXTRANEOUS
{CWD}/prefix/node_modules/foo:foo@1.0.0:INVALID
{CWD}/prefix/node_modules/dog:dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --parseable --long print symlink target location > should output parseable results with symlink targets 1`] = `
{CWD}/prefix:test-npm-ls@1.0.0
{CWD}/prefix/node_modules/chai:chai@1.0.0
{CWD}/prefix/node_modules/dev-dep:dev-dep@1.0.0
{CWD}/prefix/node_modules/linked-dep:linked-dep@1.0.0:{CWD}/prefix/linked-dep
{CWD}/prefix/node_modules/optional-dep:optional-dep@1.0.0
{CWD}/prefix/node_modules/peer-dep:peer-dep@1.0.0
{CWD}/prefix/node_modules/prod-dep:prod-dep@1.0.0
{CWD}/prefix/node_modules/foo:foo@1.0.0
{CWD}/prefix/node_modules/prod-dep/node_modules/dog:dog@2.0.0
{CWD}/prefix/node_modules/dog:dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --parseable --long with extraneous deps > should output long parseable output with extraneous info 1`] = `
{CWD}/prefix:test-npm-ls@1.0.0
{CWD}/prefix/node_modules/chai:chai@1.0.0:EXTRANEOUS
{CWD}/prefix/node_modules/foo:foo@1.0.0
{CWD}/prefix/node_modules/dog:dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls --parseable --production > should output tree containing production deps 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/optional-dep
{CWD}/prefix/node_modules/prod-dep
{CWD}/prefix/node_modules/prod-dep/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable cycle deps > should print tree output omitting deduped ref 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/a
{CWD}/prefix/node_modules/b
`

exports[`test/lib/commands/ls.js TAP ls --parseable default --depth value should be 0 > should output parseable output containing only top-level dependencies 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/foo
`

exports[`test/lib/commands/ls.js TAP ls --parseable empty location > should print empty result 1`] = `
{CWD}/prefix
`

exports[`test/lib/commands/ls.js TAP ls --parseable extraneous deps > should output containing problems info 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/foo
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable from and resolved properties > should not be printed in tree output 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/simple-output
`

exports[`test/lib/commands/ls.js TAP ls --parseable global > should print parseable output for global deps 1`] = `
{CWD}/global
{CWD}/global/node_modules/a
{CWD}/global/node_modules/b
{CWD}/global/node_modules/b/node_modules/c
`

exports[`test/lib/commands/ls.js TAP ls --parseable json read problems > should print empty result 1`] = `
{CWD}/prefix
`

exports[`test/lib/commands/ls.js TAP ls --parseable missing package.json > should output parseable missing name/version of top-level package 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/dog
{CWD}/prefix/node_modules/foo
`

exports[`test/lib/commands/ls.js TAP ls --parseable missing/invalid/extraneous > should output parseable containing top-level deps and their deps only 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/foo
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable no args > should output parseable representation of dependencies structure 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/foo
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable overridden dep > should contain overridden outout 1`] = `
{CWD}/prefix:test-overridden@1.0.0
{CWD}/prefix/node_modules/foo:foo@1.0.0
{CWD}/prefix/node_modules/bar:bar@1.0.0:OVERRIDDEN
`

exports[`test/lib/commands/ls.js TAP ls --parseable resolved points to git ref > should output tree containing git refs 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/abbrev
`

exports[`test/lib/commands/ls.js TAP ls --parseable unmet optional dep > should output parseable with empty entry for missing optional deps 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/dev-dep
{CWD}/prefix/node_modules/optional-dep
{CWD}/prefix/node_modules/peer-dep
{CWD}/prefix/node_modules/prod-dep
{CWD}/prefix/node_modules/foo
{CWD}/prefix/node_modules/prod-dep/node_modules/dog
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable unmet peer dep > should output parseable signaling missing peer dep in problems 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/dev-dep
{CWD}/prefix/node_modules/optional-dep
{CWD}/prefix/node_modules/peer-dep
{CWD}/prefix/node_modules/prod-dep
{CWD}/prefix/node_modules/foo
{CWD}/prefix/node_modules/prod-dep/node_modules/dog
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable using aliases > should output tree containing aliases 1`] = `
{CWD}/prefix
{CWD}/prefix/node_modules/a
`

exports[`test/lib/commands/ls.js TAP ls --parseable with filter arg > should output parseable contaning only occurrences of filtered by package 1`] = `
{CWD}/prefix/node_modules/chai
`

exports[`test/lib/commands/ls.js TAP ls --parseable with filter arg nested dep > should output parseable contaning only occurrences of filtered package 1`] = `
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --parseable with missing filter arg > should output parseable output containing no dependencies info 1`] = `

`

exports[`test/lib/commands/ls.js TAP ls --parseable with multiple filter args > should output parseable contaning only occurrences of multiple filtered packages and their ancestors 1`] = `
{CWD}/prefix/node_modules/chai
{CWD}/prefix/node_modules/dog
`

exports[`test/lib/commands/ls.js TAP ls --production > should output tree containing production deps 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0
+-- optional-dep@1.0.0
\`-- prod-dep@1.0.0
  \`-- dog@2.0.0
`

exports[`test/lib/commands/ls.js TAP ls broken resolved field > should NOT print git refs in output tree 1`] = `
npm-broken-resolved-field-test@1.0.0 {CWD}/prefix
\`-- a@1.0.1
`

exports[`test/lib/commands/ls.js TAP ls colored output > should output tree containing color info 1`] = `
[0mtest-npm-ls@1.0.0 {CWD}/prefix[0m
[0m+-- chai@1.0.0 [31mextraneous[39m[0m
[0m+-- foo@1.0.0 [31minvalid: "^2.0.0" from the root project[39m[0m
[0m| \`-- dog@1.0.0[0m
[0m\`-- [31mUNMET DEPENDENCY[39m ipsum@^1.0.0[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls cycle deps > should print tree output containing deduped ref 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- a@1.0.0
  \`-- b@1.0.0
    \`-- a@1.0.0 deduped
`

exports[`test/lib/commands/ls.js TAP ls cycle deps with filter args > should print tree output containing deduped ref 1`] = `
[0mtest-npm-ls@1.0.0 {CWD}/prefix[0m
[0m\`-- [33ma@1.0.0[39m[0m
[0m  \`-- b@1.0.0[0m
[0m    \`-- [33ma@1.0.0[39m [2mdeduped[22m[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls deduped missing dep > should output parseable signaling missing peer dep in problems 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- a@1.0.0
| \`-- UNMET DEPENDENCY b@^1.0.0
\`-- UNMET DEPENDENCY b@^1.0.0
`

exports[`test/lib/commands/ls.js TAP ls default --depth value should be 0 > should output tree containing only top-level dependencies 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0
\`-- foo@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls empty location > should print empty result 1`] = `
{CWD}/prefix
\`-- (empty)
`

exports[`test/lib/commands/ls.js TAP ls extraneous deps > should output containing problems info 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0 extraneous
\`-- foo@1.0.0
  \`-- dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls filter pkg arg using depth option should list a in top-level only > output 1`] = `
test-pkg-arg-filter-with-depth-opt@1.0.0 {CWD}/prefix
\`-- a@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls filter pkg arg using depth option should print empty results msg > output 1`] = `
test-pkg-arg-filter-with-depth-opt@1.0.0 {CWD}/prefix
\`-- (empty)
`

exports[`test/lib/commands/ls.js TAP ls filter pkg arg using depth option should print expected result > output 1`] = `
test-pkg-arg-filter-with-depth-opt@1.0.0 {CWD}/prefix
\`-- b@1.0.0
  \`-- c@1.0.0
    \`-- d@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls filtering by child of missing dep > should print tree and not duplicate child of missing items 1`] = `
filter-by-child-of-missing-dep@1.0.0 {CWD}/prefix
+-- b@1.0.0 extraneous
| \`-- c@1.0.0 deduped
+-- c@1.0.0 extraneous
\`-- d@1.0.0 extraneous
  \`-- c@2.0.0 extraneous
`

exports[`test/lib/commands/ls.js TAP ls from and resolved properties > should not be printed in tree output 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- simple-output@2.1.1
`

exports[`test/lib/commands/ls.js TAP ls global > should print tree and not mark top-level items extraneous 1`] = `
{CWD}/global
+-- a@1.0.0
\`-- b@1.0.0
  \`-- c@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls invalid deduped dep > should output tree signaling mismatching peer dep in problems 1`] = `
[0minvalid-deduped-dep@1.0.0 {CWD}/prefix[0m
[0m+-- a@1.0.0[0m
[0m| \`-- b@1.0.0 [2mdeduped[22m [31minvalid: "^2.0.0" from the root project, "^2.0.0" from node_modules/a[39m[0m
[0m\`-- b@1.0.0 [31minvalid: "^2.0.0" from the root project, "^2.0.0" from node_modules/a[39m[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls invalid peer dep > should output tree signaling mismatching peer dep in problems 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0
+-- dev-dep@1.0.0
| \`-- foo@1.0.0
|   \`-- dog@1.0.0
+-- optional-dep@1.0.0
+-- peer-dep@1.0.0 invalid: "^2.0.0" from the root project
\`-- prod-dep@1.0.0
  \`-- dog@2.0.0
`

exports[`test/lib/commands/ls.js TAP ls json read problems > should print empty result 1`] = `
{CWD}/prefix
\`-- (empty)
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should filter by parent folder workspace config > output 1`] = `
workspaces-tree@1.0.0 {CWD}/prefix
+-- e@1.0.0 -> ./group/e
\`-- f@1.0.0 -> ./group/f
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should filter single workspace > output 1`] = `
workspaces-tree@1.0.0 {CWD}/prefix
+-- a@1.0.0 -> ./a
| \`-- d@1.0.0 deduped -> ./d
\`-- d@1.0.0 -> ./d
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should filter using workspace config > output 1`] = `
workspaces-tree@1.0.0 {CWD}/prefix
\`-- a@1.0.0 -> ./a
  +-- baz@1.0.0
  +-- c@1.0.0
  \`-- d@1.0.0 -> ./d
    \`-- foo@1.1.1
      \`-- bar@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should inlude root and specified workspace > output 1`] = `
workspaces-tree@1.0.0 {CWD}/prefix
+-- d@1.0.0 -> ./d
| \`-- foo@1.1.1
|   \`-- bar@1.0.0
\`-- pacote@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should list --all workspaces properly > output 1`] = `
workspaces-tree@1.0.0 {CWD}/prefix
+-- a@1.0.0 -> ./a
| +-- baz@1.0.0
| +-- c@1.0.0
| \`-- d@1.0.0 deduped -> ./d
+-- b@1.0.0 -> ./b
+-- d@1.0.0 -> ./d
| \`-- foo@1.1.1
|   \`-- bar@1.0.0
+-- e@1.0.0 -> ./group/e
+-- f@1.0.0 -> ./group/f
\`-- pacote@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should list only prod deps of workspaces > output 1`] = `
workspaces-tree@1.0.0 {CWD}/prefix
+-- a@1.0.0 -> ./a
| +-- c@1.0.0
| \`-- d@1.0.0 deduped -> ./d
+-- b@1.0.0 -> ./b
+-- d@1.0.0 -> ./d
| \`-- foo@1.1.1
|   \`-- bar@1.0.0
+-- e@1.0.0 -> ./group/e
+-- f@1.0.0 -> ./group/f
\`-- pacote@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should list workspaces properly with default configs > output 1`] = `
[0mworkspaces-tree@1.0.0 {CWD}/prefix[0m
[0m+-- [94ma@1.0.0[39m -> ./a[0m
[0m| +-- baz@1.0.0[0m
[0m| +-- c@1.0.0[0m
[0m| \`-- d@1.0.0 [2mdeduped[22m -> ./d[0m
[0m+-- [94mb@1.0.0[39m -> ./b[0m
[0m+-- [94md@1.0.0[39m -> ./d[0m
[0m| \`-- foo@1.1.1[0m
[0m+-- [94me@1.0.0[39m -> ./group/e[0m
[0m+-- [94mf@1.0.0[39m -> ./group/f[0m
[0m\`-- pacote@1.0.0[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should not list workspaces with --no-workspaces > output 1`] = `
[0mworkspaces-tree@1.0.0 {CWD}/prefix[0m
[0m\`-- pacote@1.0.0[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls loading a tree containing workspaces should print all tree and filter by dep within only the ws subtree > output 1`] = `
workspaces-tree@1.0.0 {CWD}/prefix
\`-- d@1.0.0 -> ./d
  \`-- foo@1.1.1
    \`-- bar@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls missing package.json > should output tree missing name/version of top-level package 1`] = `
{CWD}/prefix
+-- chai@1.0.0 extraneous
+-- dog@1.0.0 extraneous
\`-- foo@1.0.0 extraneous
  \`-- dog@1.0.0 deduped
`

exports[`test/lib/commands/ls.js TAP ls missing/invalid/extraneous > should output tree containing missing, invalid, extraneous labels 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0 extraneous
+-- foo@1.0.0 invalid: "^2.0.0" from the root project
| \`-- dog@1.0.0
\`-- UNMET DEPENDENCY ipsum@^1.0.0
`

exports[`test/lib/commands/ls.js TAP ls no args > should output tree representation of dependencies structure 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0
\`-- foo@1.0.0
  \`-- dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls overridden dep > should contain overridden outout 1`] = `
test-overridden@1.0.0 {CWD}/prefix
\`-- foo@1.0.0
  \`-- bar@1.0.0 overridden
`

exports[`test/lib/commands/ls.js TAP ls overridden dep w/ color > should contain overridden outout 1`] = `
[0mtest-overridden@1.0.0 {CWD}/prefix[0m
[0m\`-- foo@1.0.0[0m
[0m  \`-- bar@1.0.0 [2moverridden[22m[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls print deduped symlinks > should output tree containing linked deps 1`] = `
print-deduped-symlinks@1.0.0 {CWD}/prefix
+-- a@1.0.0
| \`-- b@1.0.0 deduped -> ./b
\`-- b@1.0.0 -> ./b
`

exports[`test/lib/commands/ls.js TAP ls resolved points to git ref > should output tree containing git refs 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- abbrev@1.1.1 (git+ssh://git@github.com/isaacs/abbrev-js.git#b8f3a2fc0c3bb8ffd8b0d0072cc6b5a3667e963c)
`

exports[`test/lib/commands/ls.js TAP ls unmet optional dep > should output tree with empty entry for missing optional deps 1`] = `
[0mtest-npm-ls@1.0.0 {CWD}/prefix[0m
[0m+-- chai@1.0.0[0m
[0m+-- dev-dep@1.0.0[0m
[0m| \`-- foo@1.0.0[0m
[0m|   \`-- dog@1.0.0[0m
[0m+-- [33mUNMET OPTIONAL DEPENDENCY[39m missing-optional-dep@^1.0.0[0m
[0m+-- optional-dep@1.0.0 [31minvalid: "^2.0.0" from the root project[39m[0m
[0m+-- peer-dep@1.0.0[0m
[0m\`-- prod-dep@1.0.0[0m
[0m  \`-- dog@2.0.0[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls unmet peer dep > should output tree signaling missing peer dep in problems 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- UNMET DEPENDENCY peer-dep@*
`

exports[`test/lib/commands/ls.js TAP ls using aliases > should output tree containing aliases 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- a@npm:b@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls with args and dedupe entries > should print tree output containing deduped ref 1`] = `
[0mdedupe-entries@1.0.0 {CWD}/prefix[0m
[0m+-- @npmcli/a@1.0.0[0m
[0m| \`-- [33m@npmcli/b@1.1.2[39m [2mdeduped[22m[0m
[0m+-- [33m@npmcli/b@1.1.2[39m[0m
[0m\`-- @npmcli/c@1.0.0[0m
[0m  \`-- [33m@npmcli/b@1.1.2[39m [2mdeduped[22m[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls with args and different order of items > should print tree output containing deduped ref 1`] = `
dedupe-entries@1.0.0 {CWD}/prefix
+-- @npmcli/a@1.0.0
| \`-- @npmcli/c@1.0.0 deduped
+-- @npmcli/b@1.1.2
| \`-- @npmcli/c@1.0.0 deduped
\`-- @npmcli/c@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls with dot filter arg > should output tree contaning only occurrences of filtered by package and colored output 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- (empty)
`

exports[`test/lib/commands/ls.js TAP ls with filter arg > should output tree contaning only occurrences of filtered by package and colored output 1`] = `
[0mtest-npm-ls@1.0.0 {CWD}/prefix[0m
[0m\`-- [33mchai@1.0.0[39m[0m
[0m[0m
`

exports[`test/lib/commands/ls.js TAP ls with filter arg nested dep > should output tree contaning only occurrences of filtered package and its ancestors 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- foo@1.0.0
  \`-- dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls with missing filter arg > should output tree containing no dependencies info 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
\`-- (empty)
`

exports[`test/lib/commands/ls.js TAP ls with multiple filter args > should output tree contaning only occurrences of multiple filtered packages and their ancestors 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- chai@1.0.0
\`-- foo@1.0.0
  \`-- dog@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls with no args dedupe entries > should print tree output containing deduped ref 1`] = `
dedupe-entries@1.0.0 {CWD}/prefix
+-- @npmcli/a@1.0.0
| \`-- @npmcli/b@1.1.2 deduped
+-- @npmcli/b@1.1.2
\`-- @npmcli/c@1.0.0
  \`-- @npmcli/b@1.1.2 deduped
`

exports[`test/lib/commands/ls.js TAP ls with no args dedupe entries and not displaying all > should print tree output containing deduped ref 1`] = `
dedupe-entries@1.0.0 {CWD}/prefix
+-- @npmcli/a@1.0.0
+-- @npmcli/b@1.1.2
\`-- @npmcli/c@1.0.0
`

exports[`test/lib/commands/ls.js TAP ls workspace and missing optional dep > should omit missing optional dep 1`] = `
root@ {CWD}/prefix
+-- baz@1.0.0 -> ./baz
\`-- foo@1.0.0
`

exports[`test/lib/commands/ls.js TAP show multiple invalid reasons > ls result 1`] = `
test-npm-ls@1.0.0 {CWD}/prefix
+-- cat@1.0.0 invalid: "^2.0.0" from the root project
| \`-- dog@1.0.0 deduped invalid: "^1.2.3" from the root project, "^2.0.0" from node_modules/cat
+-- chai@1.0.0 extraneous
| \`-- dog@1.0.0 deduped invalid: "^1.2.3" from the root project, "^2.0.0" from node_modules/cat, "2.x" from node_modules/chai
\`-- dog@1.0.0 invalid: "^1.2.3" from the root project, "^2.0.0" from node_modules/cat, "2.x" from node_modules/chai
  \`-- cat@1.0.0 deduped invalid: "^2.0.0" from the root project
`
