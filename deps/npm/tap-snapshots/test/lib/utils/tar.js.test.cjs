/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/utils/tar.js TAP should log tarball contents > must match snapshot 1`] = `


package: my-cool-pkg@1.0.0
Tarball Contents

4B cat
4B chai
4B dog
114B package.json
Bundled Dependencies

bundle-dep
Tarball Details

name: my-cool-pkg

version: 1.0.0

filename: my-cool-pkg-1.0.0.tgz

package size: {size}

unpacked size: 126 B

shasum: {sha}

integrity: {integrity}

bundled deps: 1

bundled files: 0

own files: 5

total files: 5


`

exports[`test/lib/utils/tar.js TAP should log tarball contents of a scoped package > must match snapshot 1`] = `


package: @myscope/my-cool-pkg@1.0.0
Tarball Contents

4B cat
4B chai
4B dog
123B package.json
Bundled Dependencies

bundle-dep
Tarball Details

name: @myscope/my-cool-pkg

version: 1.0.0

filename: myscope-my-cool-pkg-1.0.0.tgz

package size: {size}

unpacked size: 135 B

shasum: {sha}

integrity: {integrity}

bundled deps: 1

bundled files: 0

own files: 5

total files: 5


`
