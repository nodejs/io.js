/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/fund.js TAP fund a package with type and multiple sources > should print prompt select message 1`] = `
1: Foo funding available at the following URL: http://example.com/foo
2: Lorem funding available at the following URL: http://example.com/foo-lorem
Run \`npm fund [<package-spec>] --which=1\`, for example, to open the first funding URL listed in that package
`

exports[`test/lib/commands/fund.js TAP fund colors > should print output with color info 1`] = `
test-fund-colors@1.0.0
+-- [34mhttp://example.com/a[39m
|   \`-- a@1.0.0
\`-- [34mhttp://example.com/b[39m
  | \`-- b@1.0.0[2m,[22m c@1.0.0
  +-- [34mhttp://example.com/d[39m
  |   \`-- d@1.0.0
  \`-- [34mhttp://example.com/e[39m
      \`-- e@1.0.0
`

exports[`test/lib/commands/fund.js TAP fund containing multi-level nested deps with no funding > should omit dependencies with no funding declared 1`] = `
nested-no-funding-packages@1.0.0
+-- https://example.com/lorem
|   \`-- lorem@1.0.0
\`-- http://example.com/donate
    \`-- bar@1.0.0
`

exports[`test/lib/commands/fund.js TAP fund in which same maintainer owns all its deps > should print stack packages together 1`] = `
http://example.com/donate
  \`-- maintainer-owns-all-deps@1.0.0, dep-foo@1.0.0, dep-sub-foo@1.0.0, dep-bar@1.0.0
`

exports[`test/lib/commands/fund.js TAP fund pkg missing version number > should print name only 1`] = `
http://example.com/foo
  \`-- foo
`

exports[`test/lib/commands/fund.js TAP fund using bad which value: index too high > should print message about invalid which 1`] = `
--which=100 is not a valid index
1: Funding available at the following URL: http://example.com
2: Funding available at the following URL: http://sponsors.example.com/me
3: Funding available at the following URL: http://collective.example.com
Run \`npm fund [<package-spec>] --which=1\`, for example, to open the first funding URL listed in that package
`

exports[`test/lib/commands/fund.js TAP fund using nested packages with multiple sources > should prompt with all available URLs 1`] = `
1: Funding available at the following URL: https://one.example.com
2: Funding available at the following URL: https://two.example.com
Run \`npm fund [<package-spec>] --which=1\`, for example, to open the first funding URL listed in that package
`

exports[`test/lib/commands/fund.js TAP fund with no package containing funding > should print empty funding info 1`] = `
no-funding-package@0.0.0
`

exports[`test/lib/commands/fund.js TAP sub dep with fund info and a parent with no funding info > should nest sub dep as child of root 1`] = `
test-multiple-funding-sources@1.0.0
+-- http://example.com/b
|   \`-- b@1.0.0
\`-- http://example.com/c
    \`-- c@1.0.0
`

exports[`test/lib/commands/fund.js TAP workspaces filter funding info by a specific workspace name > should display only filtered workspace name and its deps 1`] = `
workspaces-support@1.0.0
\`-- https://example.com/a
  | \`-- a@1.0.0
  \`-- http://example.com/c
      \`-- c@1.0.0
`

exports[`test/lib/commands/fund.js TAP workspaces filter funding info by a specific workspace path > should display only filtered workspace name and its deps 1`] = `
workspaces-support@1.0.0
\`-- https://example.com/a
  | \`-- a@1.0.0
  \`-- http://example.com/c
      \`-- c@1.0.0
`
