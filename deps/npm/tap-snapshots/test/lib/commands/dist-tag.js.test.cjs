/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/dist-tag.js TAP add new tag > should return success msg 1`] = `
+c: @scoped/another@7.7.7
`

exports[`test/lib/commands/dist-tag.js TAP add using valid semver range as name > should return success msg 1`] = `
dist-tag add 1.0.0 to @scoped/another@7.7.7
`

exports[`test/lib/commands/dist-tag.js TAP ls in current package > should list available tags for current package 1`] = `
a: 0.0.1
b: 0.5.0
latest: 1.0.0
`

exports[`test/lib/commands/dist-tag.js TAP ls on missing package > should log no dist-tag found msg 1`] = `
dist-tag ls Couldn't get dist-tag data for Result {
dist-tag ls   type: 'range',
dist-tag ls   registry: true,
dist-tag ls   where: undefined,
dist-tag ls   raw: 'foo',
dist-tag ls   name: 'foo',
dist-tag ls   escapedName: 'foo',
dist-tag ls   scope: undefined,
dist-tag ls   rawSpec: '*',
dist-tag ls   saveSpec: null,
dist-tag ls   fetchSpec: '*',
dist-tag ls   gitRange: undefined,
dist-tag ls   gitCommittish: undefined,
dist-tag ls   gitSubdir: undefined,
dist-tag ls   hosted: undefined
dist-tag ls }
`

exports[`test/lib/commands/dist-tag.js TAP ls on named package > should list tags for the specified package 1`] = `
a: 0.0.2
b: 0.6.0
latest: 2.0.0
`

exports[`test/lib/commands/dist-tag.js TAP no args in current package > should default to listing available tags for current package 1`] = `
a: 0.0.1
b: 0.5.0
latest: 1.0.0
`

exports[`test/lib/commands/dist-tag.js TAP only named package arg > should default to listing tags for the specified package 1`] = `
a: 0.0.2
b: 0.6.0
latest: 2.0.0
`

exports[`test/lib/commands/dist-tag.js TAP remove existing tag > should log remove info 1`] = `
dist-tag del c from @scoped/another
`

exports[`test/lib/commands/dist-tag.js TAP remove existing tag > should return success msg 1`] = `
-c: @scoped/another@7.7.7
`

exports[`test/lib/commands/dist-tag.js TAP remove non-existing tag > should log error msg 1`] = `
dist-tag del nonexistent from @scoped/another
dist-tag del nonexistent is not a dist-tag on @scoped/another
`

exports[`test/lib/commands/dist-tag.js TAP set existing version > should log warn msg 1`] = `
dist-tag add b to @scoped/another@0.6.0
dist-tag add b is already set to version 0.6.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces no args > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
workspace-b:
latest-b: 2.0.0
latest: 2.0.0
workspace-c:
latest-c: 3.0.0
latest: 3.0.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces no args, one failing workspace sets exitCode to 1 > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
workspace-b:
latest-b: 2.0.0
latest: 2.0.0
workspace-c:
latest-c: 3.0.0
latest: 3.0.0
workspace-d:
`

exports[`test/lib/commands/dist-tag.js TAP workspaces no args, one workspace > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces one arg -- .@1, ignores version spec > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
workspace-b:
latest-b: 2.0.0
latest: 2.0.0
workspace-c:
latest-c: 3.0.0
latest: 3.0.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces one arg -- cwd > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
workspace-b:
latest-b: 2.0.0
latest: 2.0.0
workspace-c:
latest-c: 3.0.0
latest: 3.0.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces one arg -- list > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
workspace-b:
latest-b: 2.0.0
latest: 2.0.0
workspace-c:
latest-c: 3.0.0
latest: 3.0.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces two args -- list, .@1, ignores version spec > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
workspace-b:
latest-b: 2.0.0
latest: 2.0.0
workspace-c:
latest-c: 3.0.0
latest: 3.0.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces two args -- list, @scoped/pkg, logs a warning and ignores workspaces > printed the expected output 1`] = `
a: 0.0.1
b: 0.5.0
latest: 1.0.0
`

exports[`test/lib/commands/dist-tag.js TAP workspaces two args -- list, cwd > printed the expected output 1`] = `
workspace-a:
latest-a: 1.0.0
latest: 1.0.0
workspace-b:
latest-b: 2.0.0
latest: 2.0.0
workspace-c:
latest-c: 3.0.0
latest: 3.0.0
`
