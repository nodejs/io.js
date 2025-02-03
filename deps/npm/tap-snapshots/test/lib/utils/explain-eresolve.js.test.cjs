/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/utils/explain-eresolve.js TAP basic chain-conflict > explain with color, depth of 2 1`] = `
While resolving: project@1.2.3
Found: @isaacs/testing-peer-dep-conflict-chain-d@2.0.0[2m[22m
[2mnode_modules/@isaacs/testing-peer-dep-conflict-chain-d[22m
  @isaacs/testing-peer-dep-conflict-chain-d@"2" from the root project

Could not resolve dependency:
[95mpeer[39m @isaacs/testing-peer-dep-conflict-chain-d@"1" from @isaacs/testing-peer-dep-conflict-chain-c@1.0.0[2m[22m
[2mnode_modules/@isaacs/testing-peer-dep-conflict-chain-c[22m
  @isaacs/testing-peer-dep-conflict-chain-c@"1" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic chain-conflict > explain with no color, depth of 6 1`] = `
While resolving: project@1.2.3
Found: @isaacs/testing-peer-dep-conflict-chain-d@2.0.0
node_modules/@isaacs/testing-peer-dep-conflict-chain-d
  @isaacs/testing-peer-dep-conflict-chain-d@"2" from the root project

Could not resolve dependency:
peer @isaacs/testing-peer-dep-conflict-chain-d@"1" from @isaacs/testing-peer-dep-conflict-chain-c@1.0.0
node_modules/@isaacs/testing-peer-dep-conflict-chain-c
  @isaacs/testing-peer-dep-conflict-chain-c@"1" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic chain-conflict > report from color 1`] = `
# npm resolution error report

While resolving: project@1.2.3
Found: @isaacs/testing-peer-dep-conflict-chain-d@2.0.0
node_modules/@isaacs/testing-peer-dep-conflict-chain-d
  @isaacs/testing-peer-dep-conflict-chain-d@"2" from the root project

Could not resolve dependency:
peer @isaacs/testing-peer-dep-conflict-chain-d@"1" from @isaacs/testing-peer-dep-conflict-chain-c@1.0.0
node_modules/@isaacs/testing-peer-dep-conflict-chain-c
  @isaacs/testing-peer-dep-conflict-chain-c@"1" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic chain-conflict > report with color 1`] = `
While resolving: project@1.2.3
Found: @isaacs/testing-peer-dep-conflict-chain-d@2.0.0[2m[22m
[2mnode_modules/@isaacs/testing-peer-dep-conflict-chain-d[22m
  @isaacs/testing-peer-dep-conflict-chain-d@"2" from the root project

Could not resolve dependency:
[95mpeer[39m @isaacs/testing-peer-dep-conflict-chain-d@"1" from @isaacs/testing-peer-dep-conflict-chain-c@1.0.0[2m[22m
[2mnode_modules/@isaacs/testing-peer-dep-conflict-chain-c[22m
  @isaacs/testing-peer-dep-conflict-chain-c@"1" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic chain-conflict > report with no color 1`] = `
While resolving: project@1.2.3
Found: @isaacs/testing-peer-dep-conflict-chain-d@2.0.0
node_modules/@isaacs/testing-peer-dep-conflict-chain-d
  @isaacs/testing-peer-dep-conflict-chain-d@"2" from the root project

Could not resolve dependency:
peer @isaacs/testing-peer-dep-conflict-chain-d@"1" from @isaacs/testing-peer-dep-conflict-chain-c@1.0.0
node_modules/@isaacs/testing-peer-dep-conflict-chain-c
  @isaacs/testing-peer-dep-conflict-chain-c@"1" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic cycleNested > explain with color, depth of 2 1`] = `
Found: @isaacs/peer-dep-cycle-c@2.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-c[22m
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
[95mpeer[39m @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-a[22m
  @isaacs/peer-dep-cycle-a@"1.x" from the root project

Conflicting peer dependency: @isaacs/peer-dep-cycle-c@1.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-c[22m
  [95mpeer[39m @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0[2m[22m
  [2mnode_modules/@isaacs/peer-dep-cycle-b[22m
    [95mpeer[39m @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0[2m[22m
    [2mnode_modules/@isaacs/peer-dep-cycle-a[22m
`

exports[`test/lib/utils/explain-eresolve.js TAP basic cycleNested > explain with no color, depth of 6 1`] = `
Found: @isaacs/peer-dep-cycle-c@2.0.0
node_modules/@isaacs/peer-dep-cycle-c
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
node_modules/@isaacs/peer-dep-cycle-a
  @isaacs/peer-dep-cycle-a@"1.x" from the root project

Conflicting peer dependency: @isaacs/peer-dep-cycle-c@1.0.0
node_modules/@isaacs/peer-dep-cycle-c
  peer @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0
  node_modules/@isaacs/peer-dep-cycle-b
    peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
    node_modules/@isaacs/peer-dep-cycle-a
      @isaacs/peer-dep-cycle-a@"1.x" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic cycleNested > report from color 1`] = `
# npm resolution error report

Found: @isaacs/peer-dep-cycle-c@2.0.0
node_modules/@isaacs/peer-dep-cycle-c
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
node_modules/@isaacs/peer-dep-cycle-a
  @isaacs/peer-dep-cycle-a@"1.x" from the root project

Conflicting peer dependency: @isaacs/peer-dep-cycle-c@1.0.0
node_modules/@isaacs/peer-dep-cycle-c
  peer @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0
  node_modules/@isaacs/peer-dep-cycle-b
    peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
    node_modules/@isaacs/peer-dep-cycle-a
      @isaacs/peer-dep-cycle-a@"1.x" from the root project

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic cycleNested > report with color 1`] = `
Found: @isaacs/peer-dep-cycle-c@2.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-c[22m
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
[95mpeer[39m @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-a[22m
  @isaacs/peer-dep-cycle-a@"1.x" from the root project

Conflicting peer dependency: @isaacs/peer-dep-cycle-c@1.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-c[22m
  [95mpeer[39m @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0[2m[22m
  [2mnode_modules/@isaacs/peer-dep-cycle-b[22m
    [95mpeer[39m @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0[2m[22m
    [2mnode_modules/@isaacs/peer-dep-cycle-a[22m
      @isaacs/peer-dep-cycle-a@"1.x" from the root project

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic cycleNested > report with no color 1`] = `
Found: @isaacs/peer-dep-cycle-c@2.0.0
node_modules/@isaacs/peer-dep-cycle-c
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
node_modules/@isaacs/peer-dep-cycle-a
  @isaacs/peer-dep-cycle-a@"1.x" from the root project

Conflicting peer dependency: @isaacs/peer-dep-cycle-c@1.0.0
node_modules/@isaacs/peer-dep-cycle-c
  peer @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0
  node_modules/@isaacs/peer-dep-cycle-b
    peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
    node_modules/@isaacs/peer-dep-cycle-a
      @isaacs/peer-dep-cycle-a@"1.x" from the root project

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic eslint-plugin case > explain with color, depth of 2 1`] = `
While resolving: eslint-plugin-react@7.24.0
Found: eslint@6.8.0[2m[22m
[2mnode_modules/eslint[22m
  [34mdev[39m eslint@"^3 || ^4 || ^5 || ^6 || ^7" from the root project
  3 more (@typescript-eslint/parser, ...)

Could not resolve dependency:
[34mdev[39m eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Conflicting peer dependency: eslint@7.31.0[2m[22m
[2mnode_modules/eslint[22m
  [95mpeer[39m eslint@"^7.0.0" from eslint-plugin-eslint-plugin@3.5.1[2m[22m
  [2mnode_modules/eslint-plugin-eslint-plugin[22m
    [34mdev[39m eslint-plugin-eslint-plugin@"^3.1.0" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic eslint-plugin case > explain with no color, depth of 6 1`] = `
While resolving: eslint-plugin-react@7.24.0
Found: eslint@6.8.0
node_modules/eslint
  dev eslint@"^3 || ^4 || ^5 || ^6 || ^7" from the root project
  peer eslint@"^5.0.0 || ^6.0.0" from @typescript-eslint/parser@2.34.0
  node_modules/@typescript-eslint/parser
    dev @typescript-eslint/parser@"^2.34.0" from the root project
  peer eslint@"^5.16.0 || ^6.8.0 || ^7.2.0" from eslint-config-airbnb-base@14.2.1
  node_modules/eslint-config-airbnb-base
    dev eslint-config-airbnb-base@"^14.2.1" from the root project
  1 more (eslint-plugin-import)

Could not resolve dependency:
dev eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Conflicting peer dependency: eslint@7.31.0
node_modules/eslint
  peer eslint@"^7.0.0" from eslint-plugin-eslint-plugin@3.5.1
  node_modules/eslint-plugin-eslint-plugin
    dev eslint-plugin-eslint-plugin@"^3.1.0" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic eslint-plugin case > report from color 1`] = `
# npm resolution error report

While resolving: eslint-plugin-react@7.24.0
Found: eslint@6.8.0
node_modules/eslint
  dev eslint@"^3 || ^4 || ^5 || ^6 || ^7" from the root project
  peer eslint@"^5.0.0 || ^6.0.0" from @typescript-eslint/parser@2.34.0
  node_modules/@typescript-eslint/parser
    dev @typescript-eslint/parser@"^2.34.0" from the root project
  peer eslint@"^5.16.0 || ^6.8.0 || ^7.2.0" from eslint-config-airbnb-base@14.2.1
  node_modules/eslint-config-airbnb-base
    dev eslint-config-airbnb-base@"^14.2.1" from the root project
  peer eslint@"^2 || ^3 || ^4 || ^5 || ^6 || ^7.2.0" from eslint-plugin-import@2.23.4
  node_modules/eslint-plugin-import
    dev eslint-plugin-import@"^2.23.4" from the root project
    peer eslint-plugin-import@"^2.22.1" from eslint-config-airbnb-base@14.2.1
    node_modules/eslint-config-airbnb-base
      dev eslint-config-airbnb-base@"^14.2.1" from the root project

Could not resolve dependency:
dev eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Conflicting peer dependency: eslint@7.31.0
node_modules/eslint
  peer eslint@"^7.0.0" from eslint-plugin-eslint-plugin@3.5.1
  node_modules/eslint-plugin-eslint-plugin
    dev eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic eslint-plugin case > report with color 1`] = `
While resolving: eslint-plugin-react@7.24.0
Found: eslint@6.8.0[2m[22m
[2mnode_modules/eslint[22m
  [34mdev[39m eslint@"^3 || ^4 || ^5 || ^6 || ^7" from the root project
  [95mpeer[39m eslint@"^5.0.0 || ^6.0.0" from @typescript-eslint/parser@2.34.0[2m[22m
  [2mnode_modules/@typescript-eslint/parser[22m
    [34mdev[39m @typescript-eslint/parser@"^2.34.0" from the root project
  2 more (eslint-config-airbnb-base, eslint-plugin-import)

Could not resolve dependency:
[34mdev[39m eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Conflicting peer dependency: eslint@7.31.0[2m[22m
[2mnode_modules/eslint[22m
  [95mpeer[39m eslint@"^7.0.0" from eslint-plugin-eslint-plugin@3.5.1[2m[22m
  [2mnode_modules/eslint-plugin-eslint-plugin[22m
    [34mdev[39m eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic eslint-plugin case > report with no color 1`] = `
While resolving: eslint-plugin-react@7.24.0
Found: eslint@6.8.0
node_modules/eslint
  dev eslint@"^3 || ^4 || ^5 || ^6 || ^7" from the root project
  peer eslint@"^5.0.0 || ^6.0.0" from @typescript-eslint/parser@2.34.0
  node_modules/@typescript-eslint/parser
    dev @typescript-eslint/parser@"^2.34.0" from the root project
  2 more (eslint-config-airbnb-base, eslint-plugin-import)

Could not resolve dependency:
dev eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Conflicting peer dependency: eslint@7.31.0
node_modules/eslint
  peer eslint@"^7.0.0" from eslint-plugin-eslint-plugin@3.5.1
  node_modules/eslint-plugin-eslint-plugin
    dev eslint-plugin-eslint-plugin@"^3.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic gatsby > explain with color, depth of 2 1`] = `
While resolving: gatsby-recipes@0.2.31
Found: ink@3.0.0-7[2m[22m
[2mnode_modules/ink[22m
  [34mdev[39m ink@"next" from gatsby-recipes@0.2.31[2m[22m
  [2mnode_modules/gatsby-recipes[22m
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107[2m[22m
    [2mnode_modules/gatsby-cli[22m

Could not resolve dependency:
[95mpeer[39m ink@">=2.0.0" from ink-box@1.0.0[2m[22m
[2mnode_modules/ink-box[22m
  ink-box@"^1.0.0" from gatsby-recipes@0.2.31[2m[22m
  [2mnode_modules/gatsby-recipes[22m
`

exports[`test/lib/utils/explain-eresolve.js TAP basic gatsby > explain with no color, depth of 6 1`] = `
While resolving: gatsby-recipes@0.2.31
Found: ink@3.0.0-7
node_modules/ink
  dev ink@"next" from gatsby-recipes@0.2.31
  node_modules/gatsby-recipes
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107
    node_modules/gatsby-cli
      gatsby-cli@"^2.12.107" from gatsby@2.24.74
      node_modules/gatsby
        gatsby@"" from the root project

Could not resolve dependency:
peer ink@">=2.0.0" from ink-box@1.0.0
node_modules/ink-box
  ink-box@"^1.0.0" from gatsby-recipes@0.2.31
  node_modules/gatsby-recipes
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107
    node_modules/gatsby-cli
      gatsby-cli@"^2.12.107" from gatsby@2.24.74
      node_modules/gatsby
        gatsby@"" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic gatsby > report from color 1`] = `
# npm resolution error report

While resolving: gatsby-recipes@0.2.31
Found: ink@3.0.0-7
node_modules/ink
  dev ink@"next" from gatsby-recipes@0.2.31
  node_modules/gatsby-recipes
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107
    node_modules/gatsby-cli
      gatsby-cli@"^2.12.107" from gatsby@2.24.74
      node_modules/gatsby
        gatsby@"" from the root project

Could not resolve dependency:
peer ink@">=2.0.0" from ink-box@1.0.0
node_modules/ink-box
  ink-box@"^1.0.0" from gatsby-recipes@0.2.31
  node_modules/gatsby-recipes
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107
    node_modules/gatsby-cli
      gatsby-cli@"^2.12.107" from gatsby@2.24.74
      node_modules/gatsby
        gatsby@"" from the root project

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic gatsby > report with color 1`] = `
While resolving: gatsby-recipes@0.2.31
Found: ink@3.0.0-7[2m[22m
[2mnode_modules/ink[22m
  [34mdev[39m ink@"next" from gatsby-recipes@0.2.31[2m[22m
  [2mnode_modules/gatsby-recipes[22m
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107[2m[22m
    [2mnode_modules/gatsby-cli[22m
      gatsby-cli@"^2.12.107" from gatsby@2.24.74[2m[22m
      [2mnode_modules/gatsby[22m
        gatsby@"" from the root project

Could not resolve dependency:
[95mpeer[39m ink@">=2.0.0" from ink-box@1.0.0[2m[22m
[2mnode_modules/ink-box[22m
  ink-box@"^1.0.0" from gatsby-recipes@0.2.31[2m[22m
  [2mnode_modules/gatsby-recipes[22m
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107[2m[22m
    [2mnode_modules/gatsby-cli[22m
      gatsby-cli@"^2.12.107" from gatsby@2.24.74[2m[22m
      [2mnode_modules/gatsby[22m

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic gatsby > report with no color 1`] = `
While resolving: gatsby-recipes@0.2.31
Found: ink@3.0.0-7
node_modules/ink
  dev ink@"next" from gatsby-recipes@0.2.31
  node_modules/gatsby-recipes
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107
    node_modules/gatsby-cli
      gatsby-cli@"^2.12.107" from gatsby@2.24.74
      node_modules/gatsby
        gatsby@"" from the root project

Could not resolve dependency:
peer ink@">=2.0.0" from ink-box@1.0.0
node_modules/ink-box
  ink-box@"^1.0.0" from gatsby-recipes@0.2.31
  node_modules/gatsby-recipes
    gatsby-recipes@"^0.2.31" from gatsby-cli@2.12.107
    node_modules/gatsby-cli
      gatsby-cli@"^2.12.107" from gatsby@2.24.74
      node_modules/gatsby

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, but has current edge > explain with color, depth of 2 1`] = `
While resolving: eslint@7.22.0
Found: [34mdev[39m eslint@"file:." from the root project

Could not resolve dependency:
[95mpeer[39m eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0[2m[22m
[2mnode_modules/eslint-plugin-jsdoc[22m
  [34mdev[39m eslint-plugin-jsdoc@"^22.1.0" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, but has current edge > explain with no color, depth of 6 1`] = `
While resolving: eslint@7.22.0
Found: dev eslint@"file:." from the root project

Could not resolve dependency:
peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, but has current edge > report from color 1`] = `
# npm resolution error report

While resolving: eslint@7.22.0
Found: dev eslint@"file:." from the root project

Could not resolve dependency:
peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, but has current edge > report with color 1`] = `
While resolving: eslint@7.22.0
Found: [34mdev[39m eslint@"file:." from the root project

Could not resolve dependency:
[95mpeer[39m eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0[2m[22m
[2mnode_modules/eslint-plugin-jsdoc[22m
  [34mdev[39m eslint-plugin-jsdoc@"^22.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, but has current edge > report with no color 1`] = `
While resolving: eslint@7.22.0
Found: dev eslint@"file:." from the root project

Could not resolve dependency:
peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, no current edge, idk > explain with color, depth of 2 1`] = `
While resolving: eslint@7.22.0
Found: [95mpeer[39m eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0[2m[22m
[2mnode_modules/eslint-plugin-jsdoc[22m
  [34mdev[39m eslint-plugin-jsdoc@"^22.1.0" from the root project

Could not resolve dependency:
[95mpeer[39m eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0[2m[22m
[2mnode_modules/eslint-plugin-jsdoc[22m
  [34mdev[39m eslint-plugin-jsdoc@"^22.1.0" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, no current edge, idk > explain with no color, depth of 6 1`] = `
While resolving: eslint@7.22.0
Found: peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project

Could not resolve dependency:
peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, no current edge, idk > report from color 1`] = `
# npm resolution error report

While resolving: eslint@7.22.0
Found: peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project

Could not resolve dependency:
peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, no current edge, idk > report with color 1`] = `
While resolving: eslint@7.22.0
Found: [95mpeer[39m eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0[2m[22m
[2mnode_modules/eslint-plugin-jsdoc[22m
  [34mdev[39m eslint-plugin-jsdoc@"^22.1.0" from the root project

Could not resolve dependency:
[95mpeer[39m eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0[2m[22m
[2mnode_modules/eslint-plugin-jsdoc[22m
  [34mdev[39m eslint-plugin-jsdoc@"^22.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic no current node, no current edge, idk > report with no color 1`] = `
While resolving: eslint@7.22.0
Found: peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project

Could not resolve dependency:
peer eslint@"^6.0.0" from eslint-plugin-jsdoc@22.2.0
node_modules/eslint-plugin-jsdoc
  dev eslint-plugin-jsdoc@"^22.1.0" from the root project

Fix the upstream dependency conflict, or retry
this command with --force or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic withShrinkwrap > explain with color, depth of 2 1`] = `
While resolving: @isaacs/peer-dep-cycle-b@1.0.0
Found: @isaacs/peer-dep-cycle-c@2.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-c[22m
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
[95mpeer[39m @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-b[22m
  [95mpeer[39m @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0[2m[22m
  [2mnode_modules/@isaacs/peer-dep-cycle-a[22m
`

exports[`test/lib/utils/explain-eresolve.js TAP basic withShrinkwrap > explain with no color, depth of 6 1`] = `
While resolving: @isaacs/peer-dep-cycle-b@1.0.0
Found: @isaacs/peer-dep-cycle-c@2.0.0
node_modules/@isaacs/peer-dep-cycle-c
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
peer @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0
node_modules/@isaacs/peer-dep-cycle-b
  peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
  node_modules/@isaacs/peer-dep-cycle-a
    @isaacs/peer-dep-cycle-a@"1.x" from the root project
`

exports[`test/lib/utils/explain-eresolve.js TAP basic withShrinkwrap > report from color 1`] = `
# npm resolution error report

While resolving: @isaacs/peer-dep-cycle-b@1.0.0
Found: @isaacs/peer-dep-cycle-c@2.0.0
node_modules/@isaacs/peer-dep-cycle-c
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
peer @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0
node_modules/@isaacs/peer-dep-cycle-b
  peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
  node_modules/@isaacs/peer-dep-cycle-a
    @isaacs/peer-dep-cycle-a@"1.x" from the root project

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic withShrinkwrap > report with color 1`] = `
While resolving: @isaacs/peer-dep-cycle-b@1.0.0
Found: @isaacs/peer-dep-cycle-c@2.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-c[22m
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
[95mpeer[39m @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0[2m[22m
[2mnode_modules/@isaacs/peer-dep-cycle-b[22m
  [95mpeer[39m @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0[2m[22m
  [2mnode_modules/@isaacs/peer-dep-cycle-a[22m
    @isaacs/peer-dep-cycle-a@"1.x" from the root project

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`

exports[`test/lib/utils/explain-eresolve.js TAP basic withShrinkwrap > report with no color 1`] = `
While resolving: @isaacs/peer-dep-cycle-b@1.0.0
Found: @isaacs/peer-dep-cycle-c@2.0.0
node_modules/@isaacs/peer-dep-cycle-c
  @isaacs/peer-dep-cycle-c@"2.x" from the root project

Could not resolve dependency:
peer @isaacs/peer-dep-cycle-c@"1" from @isaacs/peer-dep-cycle-b@1.0.0
node_modules/@isaacs/peer-dep-cycle-b
  peer @isaacs/peer-dep-cycle-b@"1" from @isaacs/peer-dep-cycle-a@1.0.0
  node_modules/@isaacs/peer-dep-cycle-a
    @isaacs/peer-dep-cycle-a@"1.x" from the root project

Fix the upstream dependency conflict, or retry
this command with --no-strict-peer-deps, --force, or --legacy-peer-deps
to accept an incorrect (and potentially broken) dependency resolution.
`
