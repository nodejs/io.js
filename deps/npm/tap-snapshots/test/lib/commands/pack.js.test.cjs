/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/pack.js TAP dry run > logs pack contents 1`] = `
Array [
  "package: test-package@1.0.0",
  "Tarball Contents",
  "41B package.json",
  "Tarball Details",
  "name: test-package",
  "version: 1.0.0",
  "filename: test-package-1.0.0.tgz",
  "package size: {size}",
  "unpacked size: 41 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 1",
]
`

exports[`test/lib/commands/pack.js TAP foreground-scripts can still be set to false > logs pack contents 1`] = `
Array [
  "package: test-fg-scripts@0.0.0",
  "Tarball Contents",
  "110B package.json",
  "Tarball Details",
  "name: test-fg-scripts",
  "version: 0.0.0",
  "filename: test-fg-scripts-0.0.0.tgz",
  "package size: {size}",
  "unpacked size: 110 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 1",
]
`

exports[`test/lib/commands/pack.js TAP foreground-scripts defaults to true > logs pack contents 1`] = `
Array [
  "package: test-fg-scripts@0.0.0",
  "Tarball Contents",
  "110B package.json",
  "Tarball Details",
  "name: test-fg-scripts",
  "version: 0.0.0",
  "filename: test-fg-scripts-0.0.0.tgz",
  "package size: {size}",
  "unpacked size: 110 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 1",
]
`

exports[`test/lib/commands/pack.js TAP should log output as valid json > logs pack contents 1`] = `
Array []
`

exports[`test/lib/commands/pack.js TAP should log output as valid json > outputs as json 1`] = `
Array [
  Array [
    Object {
      "bundled": Array [],
      "entryCount": 1,
      "filename": "test-package-1.0.0.tgz",
      "files": Array [
        Object {
          "mode": 420,
          "path": "package.json",
          "size": "{size}",
        },
      ],
      "id": "test-package@1.0.0",
      "integrity": "{integrity}",
      "name": "test-package",
      "shasum": "{sha}",
      "size": "{size}",
      "unpackedSize": 41,
      "version": "1.0.0",
    },
  ],
]
`

exports[`test/lib/commands/pack.js TAP should log scoped package output as valid json > logs pack contents 1`] = `
Array []
`

exports[`test/lib/commands/pack.js TAP should log scoped package output as valid json > outputs as json 1`] = `
Array [
  Array [
    Object {
      "bundled": Array [],
      "entryCount": 1,
      "filename": "myscope-test-package-1.0.0.tgz",
      "files": Array [
        Object {
          "mode": 420,
          "path": "package.json",
          "size": "{size}",
        },
      ],
      "id": "@myscope/test-package@1.0.0",
      "integrity": "{integrity}",
      "name": "@myscope/test-package",
      "shasum": "{sha}",
      "size": "{size}",
      "unpackedSize": 88,
      "version": "1.0.0",
    },
  ],
]
`

exports[`test/lib/commands/pack.js TAP should log scoped package output as valid json > stderr has banners 1`] = `
Array [
  String(

    > @myscope/test-package@1.0.0 prepack
    > echo prepack!

  ),
]
`

exports[`test/lib/commands/pack.js TAP should pack current directory with no arguments > logs pack contents 1`] = `
Array [
  "package: test-package@1.0.0",
  "Tarball Contents",
  "41B package.json",
  "Tarball Details",
  "name: test-package",
  "version: 1.0.0",
  "filename: test-package-1.0.0.tgz",
  "package size: {size}",
  "unpacked size: 41 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 1",
]
`
