/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/outdated.js TAP aliases > should display aliased outdated dep output 1`] = `
Package         Current  Wanted  Latest  Location          Depended by
cat:dog@latest    1.0.0   2.0.0   2.0.0  node_modules/cat  prefix
`

exports[`test/lib/commands/outdated.js TAP aliases with version range > should display aliased outdated dep output with correct wanted values 1`] = `
Package         Current  Wanted  Latest  Location          Depended by
cat:dog@^1.0.0    1.0.0   1.0.1   2.0.0  node_modules/cat  prefix
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --all > must match snapshot 1`] = `
Package  Current  Wanted  Latest  Location           Depended by
cat        1.0.0   1.0.1   1.0.1  node_modules/cat   prefix
chai       1.0.0   1.0.1   1.0.1  node_modules/chai  prefix
dog        1.0.1   1.0.1   2.0.0  node_modules/dog   prefix
theta    MISSING   1.0.1   1.0.1  -                  prefix
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --json --long > must match snapshot 1`] = `
{
  "cat": {
    "current": "1.0.0",
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "prefix",
    "location": "{CWD}/prefix/node_modules/cat",
    "type": "dependencies"
  },
  "chai": {
    "current": "1.0.0",
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "prefix",
    "location": "{CWD}/prefix/node_modules/chai",
    "type": "peerDependencies"
  },
  "dog": {
    "current": "1.0.1",
    "wanted": "1.0.1",
    "latest": "2.0.0",
    "dependent": "prefix",
    "location": "{CWD}/prefix/node_modules/dog",
    "type": "dependencies"
  },
  "theta": {
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "prefix",
    "type": "dependencies"
  }
}
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --json > must match snapshot 1`] = `
{
  "cat": {
    "current": "1.0.0",
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "prefix",
    "location": "{CWD}/prefix/node_modules/cat"
  },
  "chai": {
    "current": "1.0.0",
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "prefix",
    "location": "{CWD}/prefix/node_modules/chai"
  },
  "dog": {
    "current": "1.0.1",
    "wanted": "1.0.1",
    "latest": "2.0.0",
    "dependent": "prefix",
    "location": "{CWD}/prefix/node_modules/dog"
  },
  "theta": {
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "prefix"
  }
}
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --long > must match snapshot 1`] = `
Package  Current  Wanted  Latest  Location           Depended by  Package Type      Homepage
cat        1.0.0   1.0.1   1.0.1  node_modules/cat   prefix       dependencies
chai       1.0.0   1.0.1   1.0.1  node_modules/chai  prefix       peerDependencies
dog        1.0.1   1.0.1   2.0.0  node_modules/dog   prefix       dependencies
theta    MISSING   1.0.1   1.0.1  -                  prefix       dependencies
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --omit=dev --omit=peer > must match snapshot 1`] = `
[1m[4mPackage[24m[22m  [1m[4mCurrent[24m[22m  [1m[4mWanted[24m[22m  [1m[4mLatest[24m[22m  [1m[4mLocation[24m[22m          [1m[4mDepended by[24m[22m
[31mcat[39m        1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/cat  prefix
[33mdog[39m        1.0.1   [36m1.0.1[39m   [34m2.0.0[39m  node_modules/dog  prefix
[31mtheta[39m    MISSING   [36m1.0.1[39m   [34m1.0.1[39m  -                 prefix
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --omit=dev > must match snapshot 1`] = `
[1m[4mPackage[24m[22m  [1m[4mCurrent[24m[22m  [1m[4mWanted[24m[22m  [1m[4mLatest[24m[22m  [1m[4mLocation[24m[22m           [1m[4mDepended by[24m[22m
[31mcat[39m        1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/cat   prefix
[31mchai[39m       1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/chai  prefix
[33mdog[39m        1.0.1   [36m1.0.1[39m   [34m2.0.0[39m  node_modules/dog   prefix
[31mtheta[39m    MISSING   [36m1.0.1[39m   [34m1.0.1[39m  -                  prefix
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --omit=prod > must match snapshot 1`] = `
[1m[4mPackage[24m[22m  [1m[4mCurrent[24m[22m  [1m[4mWanted[24m[22m  [1m[4mLatest[24m[22m  [1m[4mLocation[24m[22m           [1m[4mDepended by[24m[22m
[31mcat[39m        1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/cat   prefix
[31mchai[39m       1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/chai  prefix
[33mdog[39m        1.0.1   [36m1.0.1[39m   [34m2.0.0[39m  node_modules/dog   prefix
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --parseable --long > must match snapshot 1`] = `
{CWD}/prefix/node_modules/cat:cat@1.0.1:cat@1.0.0:cat@1.0.1:prefix:dependencies:
{CWD}/prefix/node_modules/chai:chai@1.0.1:chai@1.0.0:chai@1.0.1:prefix:peerDependencies:
{CWD}/prefix/node_modules/dog:dog@1.0.1:dog@1.0.1:dog@2.0.0:prefix:dependencies:
:theta@1.0.1:MISSING:theta@1.0.1:prefix:dependencies:
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated --parseable > must match snapshot 1`] = `
{CWD}/prefix/node_modules/cat:cat@1.0.1:cat@1.0.0:cat@1.0.1:prefix
{CWD}/prefix/node_modules/chai:chai@1.0.1:chai@1.0.0:chai@1.0.1:prefix
{CWD}/prefix/node_modules/dog:dog@1.0.1:dog@1.0.1:dog@2.0.0:prefix
:theta@1.0.1:MISSING:theta@1.0.1:prefix
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated > must match snapshot 1`] = `
[1m[4mPackage[24m[22m  [1m[4mCurrent[24m[22m  [1m[4mWanted[24m[22m  [1m[4mLatest[24m[22m  [1m[4mLocation[24m[22m           [1m[4mDepended by[24m[22m
[31mcat[39m        1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/cat   prefix
[31mchai[39m       1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/chai  prefix
[33mdog[39m        1.0.1   [36m1.0.1[39m   [34m2.0.0[39m  node_modules/dog   prefix
[31mtheta[39m    MISSING   [36m1.0.1[39m   [34m1.0.1[39m  -                  prefix
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated global > must match snapshot 1`] = `
Package  Current  Wanted  Latest  Location          Depended by
cat        1.0.0   1.0.1   1.0.1  node_modules/cat  global
`

exports[`test/lib/commands/outdated.js TAP should display outdated deps outdated specific dep > must match snapshot 1`] = `
Package  Current  Wanted  Latest  Location          Depended by
cat        1.0.0   1.0.1   1.0.1  node_modules/cat  prefix
`

exports[`test/lib/commands/outdated.js TAP workspaces should display all dependencies > output 1`] = `
Package  Current  Wanted  Latest  Location           Depended by
cat        1.0.0   1.0.1   1.0.1  node_modules/cat   a@1.0.0
chai       1.0.0   1.0.1   1.0.1  node_modules/chai  foo
dog        1.0.1   1.0.1   2.0.0  node_modules/dog   prefix
theta    MISSING   1.0.1   1.0.1  -                  c@1.0.0
theta    MISSING   1.0.1   1.0.1  -                  d@1.0.0
theta    MISSING   1.0.1   1.0.1  -                  e@1.0.0
`

exports[`test/lib/commands/outdated.js TAP workspaces should display json results filtered by ws > output 1`] = `
{
  "cat": {
    "current": "1.0.0",
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "a",
    "location": "{CWD}/prefix/node_modules/cat"
  }
}
`

exports[`test/lib/commands/outdated.js TAP workspaces should display missing deps when filtering by ws > output 1`] = `
Package  Current  Wanted  Latest  Location  Depended by
theta    MISSING   1.0.1   1.0.1  -         c@1.0.0
`

exports[`test/lib/commands/outdated.js TAP workspaces should display nested deps when filtering by ws and using --all > output 1`] = `
Package  Current  Wanted  Latest  Location           Depended by
cat        1.0.0   1.0.1   1.0.1  node_modules/cat   a@1.0.0
chai       1.0.0   1.0.1   1.0.1  node_modules/chai  foo
`

exports[`test/lib/commands/outdated.js TAP workspaces should display no results if ws has no deps to display > output 1`] = `

`

exports[`test/lib/commands/outdated.js TAP workspaces should display only root outdated when ws disabled > output 1`] = `

`

exports[`test/lib/commands/outdated.js TAP workspaces should display parseable results filtered by ws > output 1`] = `
{CWD}/prefix/node_modules/cat:cat@1.0.1:cat@1.0.0:cat@1.0.1:a
`

exports[`test/lib/commands/outdated.js TAP workspaces should display results filtered by ws > output 1`] = `
Package  Current  Wanted  Latest  Location          Depended by
cat        1.0.0   1.0.1   1.0.1  node_modules/cat  a@1.0.0
`

exports[`test/lib/commands/outdated.js TAP workspaces should display ws outdated deps human output > output 1`] = `
Package  Current  Wanted  Latest  Location          Depended by
cat        1.0.0   1.0.1   1.0.1  node_modules/cat  a@1.0.0
dog        1.0.1   1.0.1   2.0.0  node_modules/dog  prefix
theta    MISSING   1.0.1   1.0.1  -                 c@1.0.0
theta    MISSING   1.0.1   1.0.1  -                 d@1.0.0
theta    MISSING   1.0.1   1.0.1  -                 e@1.0.0
`

exports[`test/lib/commands/outdated.js TAP workspaces should display ws outdated deps json output > output 1`] = `
{
  "cat": {
    "current": "1.0.0",
    "wanted": "1.0.1",
    "latest": "1.0.1",
    "dependent": "a",
    "location": "{CWD}/prefix/node_modules/cat"
  },
  "dog": {
    "current": "1.0.1",
    "wanted": "1.0.1",
    "latest": "2.0.0",
    "dependent": "prefix",
    "location": "{CWD}/prefix/node_modules/dog"
  },
  "theta": [
    {
      "wanted": "1.0.1",
      "latest": "1.0.1",
      "dependent": "c"
    },
    {
      "wanted": "1.0.1",
      "latest": "1.0.1",
      "dependent": "d"
    },
    {
      "wanted": "1.0.1",
      "latest": "1.0.1",
      "dependent": "e"
    }
  ]
}
`

exports[`test/lib/commands/outdated.js TAP workspaces should display ws outdated deps parseable output > output 1`] = `
{CWD}/prefix/node_modules/cat:cat@1.0.1:cat@1.0.0:cat@1.0.1:a
{CWD}/prefix/node_modules/dog:dog@1.0.1:dog@1.0.1:dog@2.0.0:prefix
:theta@1.0.1:MISSING:theta@1.0.1:c
:theta@1.0.1:MISSING:theta@1.0.1:d
:theta@1.0.1:MISSING:theta@1.0.1:e
`

exports[`test/lib/commands/outdated.js TAP workspaces should highlight ws in dependend by section > output 1`] = `
[1m[4mPackage[24m[22m  [1m[4mCurrent[24m[22m  [1m[4mWanted[24m[22m  [1m[4mLatest[24m[22m  [1m[4mLocation[24m[22m          [1m[4mDepended by[24m[22m
[31mcat[39m        1.0.0   [36m1.0.1[39m   [34m1.0.1[39m  node_modules/cat  [34ma@1.0.0[39m
[33mdog[39m        1.0.1   [36m1.0.1[39m   [34m2.0.0[39m  node_modules/dog  prefix
[31mtheta[39m    MISSING   [36m1.0.1[39m   [34m1.0.1[39m  -                 [34mc@1.0.0[39m
[31mtheta[39m    MISSING   [36m1.0.1[39m   [34m1.0.1[39m  -                 [34md@1.0.0[39m
[31mtheta[39m    MISSING   [36m1.0.1[39m   [34m1.0.1[39m  -                 [34me@1.0.0[39m
`
