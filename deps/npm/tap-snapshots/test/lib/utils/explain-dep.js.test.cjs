/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/utils/explain-dep.js TAP basic > ellipses test one 1`] = `
manydep@1.0.0
  manydep@"1.0.0" from prod-dep@1.2.3
  node_modules/prod-dep
    prod-dep@"1.x" from the root project
  7 more (optdep, extra-neos, deep-dev, peer, the root project, ...)
`

exports[`test/lib/utils/explain-dep.js TAP basic > ellipses test two 1`] = `
manydep@1.0.0
  manydep@"1.0.0" from prod-dep@1.2.3
  node_modules/prod-dep
    prod-dep@"1.x" from the root project
  6 more (optdep, extra-neos, deep-dev, peer, the root project, a package with a pretty long name)
`

exports[`test/lib/utils/explain-dep.js TAP basic bundled > explain color deep 1`] = `
bundle-of-joy@1.0.0 [4m[36mbundled[39m[24m[2m[22m
[2mnode_modules/bundle-of-joy[22m
  [4m[36mbundled[39m[24m prod-dep@"1.x" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic bundled > explain nocolor shallow 1`] = `
bundle-of-joy@1.0.0 bundled
node_modules/bundle-of-joy
  bundled prod-dep@"1.x" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic bundled > print color 1`] = `
bundle-of-joy@1.0.0 [4m[36mbundled[39m[24m[2m[22m
[2mnode_modules/bundle-of-joy[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic bundled > print nocolor 1`] = `
bundle-of-joy@1.0.0 bundled
node_modules/bundle-of-joy
`

exports[`test/lib/utils/explain-dep.js TAP basic deepDev > explain color deep 1`] = `
deep-dev@2.3.4 [34mdev[39m[2m[22m
[2mnode_modules/deep-dev[22m
  deep-dev@"2.x" from metadev@3.4.5[2m[22m
  [2mnode_modules/dev/node_modules/metadev[22m
    metadev@"3.x" from topdev@4.5.6[2m[22m
    [2mnode_modules/topdev[22m
      [34mdev[39m topdev@"4.x" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic deepDev > explain nocolor shallow 1`] = `
deep-dev@2.3.4 dev
node_modules/deep-dev
  deep-dev@"2.x" from metadev@3.4.5
  node_modules/dev/node_modules/metadev
    metadev@"3.x" from topdev@4.5.6
    node_modules/topdev
`

exports[`test/lib/utils/explain-dep.js TAP basic deepDev > print color 1`] = `
deep-dev@2.3.4 [34mdev[39m[2m[22m
[2mnode_modules/deep-dev[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic deepDev > print nocolor 1`] = `
deep-dev@2.3.4 dev
node_modules/deep-dev
`

exports[`test/lib/utils/explain-dep.js TAP basic extraneous > explain color deep 1`] = `
extra-neos@1337.420.69-lol [31mextraneous[39m[2m[22m
[2mnode_modules/extra-neos[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic extraneous > explain nocolor shallow 1`] = `
extra-neos@1337.420.69-lol extraneous
node_modules/extra-neos
`

exports[`test/lib/utils/explain-dep.js TAP basic extraneous > print color 1`] = `
extra-neos@1337.420.69-lol [31mextraneous[39m[2m[22m
[2mnode_modules/extra-neos[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic extraneous > print nocolor 1`] = `
extra-neos@1337.420.69-lol extraneous
node_modules/extra-neos
`

exports[`test/lib/utils/explain-dep.js TAP basic manyDeps > explain color deep 1`] = `
manydep@1.0.0
  manydep@"1.0.0" from prod-dep@1.2.3[2m[22m
  [2mnode_modules/prod-dep[22m
    prod-dep@"1.x" from the root project
  [35moptional[39m manydep@"1.x" from optdep@1.0.0 [35moptional[39m[2m[22m
  [2mnode_modules/optdep[22m
    [35moptional[39m optdep@"1.0.0" from the root project
  manydep@"1.0.x" from extra-neos@1337.420.69-lol [31mextraneous[39m[2m[22m
  [2mnode_modules/extra-neos[22m
  [34mdev[39m manydep@"*" from deep-dev@2.3.4 [34mdev[39m[2m[22m
  [2mnode_modules/deep-dev[22m
    deep-dev@"2.x" from metadev@3.4.5[2m[22m
    [2mnode_modules/dev/node_modules/metadev[22m
      metadev@"3.x" from topdev@4.5.6[2m[22m
      [2mnode_modules/topdev[22m
        [34mdev[39m topdev@"4.x" from the root project
  [95mpeer[39m manydep@">1.0.0-beta <1.0.1" from peer@1.0.0 [95mpeer[39m[2m[22m
  [2mnode_modules/peer[22m
    [95mpeer[39m peer@"1.0.0" from the root project
  manydep@">1.0.0-beta <1.0.1" from the root project
  manydep@"1" from a package with a pretty long name@1.2.3
  manydep@"1" from another package with a pretty long name@1.2.3
  manydep@"1" from yet another a package with a pretty long name@1.2.3
`

exports[`test/lib/utils/explain-dep.js TAP basic manyDeps > explain nocolor shallow 1`] = `
manydep@1.0.0
  manydep@"1.0.0" from prod-dep@1.2.3
  node_modules/prod-dep
    prod-dep@"1.x" from the root project
  8 more (optdep, extra-neos, deep-dev, peer, the root project, ...)
`

exports[`test/lib/utils/explain-dep.js TAP basic manyDeps > print color 1`] = `
manydep@1.0.0
`

exports[`test/lib/utils/explain-dep.js TAP basic manyDeps > print nocolor 1`] = `
manydep@1.0.0
`

exports[`test/lib/utils/explain-dep.js TAP basic optional > explain color deep 1`] = `
optdep@1.0.0 [35moptional[39m[2m[22m
[2mnode_modules/optdep[22m
  [35moptional[39m optdep@"1.0.0" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic optional > explain nocolor shallow 1`] = `
optdep@1.0.0 optional
node_modules/optdep
  optional optdep@"1.0.0" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic optional > print color 1`] = `
optdep@1.0.0 [35moptional[39m[2m[22m
[2mnode_modules/optdep[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic optional > print nocolor 1`] = `
optdep@1.0.0 optional
node_modules/optdep
`

exports[`test/lib/utils/explain-dep.js TAP basic overridden > explain color deep 1`] = `
overridden-root@1.0.0 [2moverridden[22m[2m[22m
[2mnode_modules/overridden-root[22m
  [2moverridden[22m overridden-dep@"1.0.0" (was "^2.0.0") from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic overridden > explain nocolor shallow 1`] = `
overridden-root@1.0.0 overridden
node_modules/overridden-root
  overridden overridden-dep@"1.0.0" (was "^2.0.0") from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic overridden > print color 1`] = `
overridden-root@1.0.0 [2moverridden[22m[2m[22m
[2mnode_modules/overridden-root[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic overridden > print nocolor 1`] = `
overridden-root@1.0.0 overridden
node_modules/overridden-root
`

exports[`test/lib/utils/explain-dep.js TAP basic peer > explain color deep 1`] = `
peer@1.0.0 [95mpeer[39m[2m[22m
[2mnode_modules/peer[22m
  [95mpeer[39m peer@"1.0.0" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic peer > explain nocolor shallow 1`] = `
peer@1.0.0 peer
node_modules/peer
  peer peer@"1.0.0" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic peer > print color 1`] = `
peer@1.0.0 [95mpeer[39m[2m[22m
[2mnode_modules/peer[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic peer > print nocolor 1`] = `
peer@1.0.0 peer
node_modules/peer
`

exports[`test/lib/utils/explain-dep.js TAP basic prodDep > explain color deep 1`] = `
prod-dep@1.2.3[2m[22m
[2mnode_modules/prod-dep[22m
  prod-dep@"1.x" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic prodDep > explain nocolor shallow 1`] = `
prod-dep@1.2.3
node_modules/prod-dep
  prod-dep@"1.x" from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic prodDep > print color 1`] = `
prod-dep@1.2.3[2m[22m
[2mnode_modules/prod-dep[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic prodDep > print nocolor 1`] = `
prod-dep@1.2.3
node_modules/prod-dep
`

exports[`test/lib/utils/explain-dep.js TAP basic workspaces > explain color deep 1`] = `
[94ma@1.0.0[39m[2m[22m
[2ma[22m
  [94ma@1.0.0[39m[2m[22m
  [2mnode_modules/a[22m
    [94mworkspace[39m [1ma[22m from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic workspaces > explain nocolor shallow 1`] = `
a@1.0.0
a
  a@1.0.0
  node_modules/a
    workspace a from the root project
`

exports[`test/lib/utils/explain-dep.js TAP basic workspaces > print color 1`] = `
[94ma@1.0.0[39m[2m[22m
[2ma[22m
`

exports[`test/lib/utils/explain-dep.js TAP basic workspaces > print nocolor 1`] = `
a@1.0.0
a
`
