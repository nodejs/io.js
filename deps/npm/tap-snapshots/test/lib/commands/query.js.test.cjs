/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/query.js TAP global > should return global package 1`] = `
[
  {
    "name": "lorem",
    "version": "2.0.0",
    "_id": "lorem@2.0.0",
    "pkgid": "lorem@2.0.0",
    "location": "node_modules/lorem",
    "path": "{CWD}/global/node_modules/lorem",
    "realpath": "{CWD}/global/node_modules/lorem",
    "resolved": null,
    "from": [
      ""
    ],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  }
]
`

exports[`test/lib/commands/query.js TAP include-workspace-root > should return workspace object and root object 1`] = `
[
  {
    "name": "project",
    "workspaces": [
      "c"
    ],
    "dependencies": {
      "a": "^1.0.0",
      "b": "^1.0.0"
    },
    "pkgid": "project@",
    "location": "",
    "path": "{CWD}/prefix",
    "realpath": "{CWD}/prefix",
    "resolved": null,
    "from": [],
    "to": [
      "node_modules/c",
      "node_modules/a",
      "node_modules/b"
    ],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  },
  {
    "name": "c",
    "version": "1.0.0",
    "_id": "c@1.0.0",
    "pkgid": "c@1.0.0",
    "location": "c",
    "path": "{CWD}/prefix/c",
    "realpath": "{CWD}/prefix/c",
    "resolved": null,
    "from": [],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  }
]
`

exports[`test/lib/commands/query.js TAP linked node > should return linked node res 1`] = `
[
  {
    "name": "a",
    "version": "1.0.0",
    "_id": "a@1.0.0",
    "pkgid": "a@1.0.0",
    "location": "a",
    "path": "{CWD}/prefix/a",
    "realpath": "{CWD}/prefix/a",
    "resolved": null,
    "from": [],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  }
]
`

exports[`test/lib/commands/query.js TAP missing > should return missing node 1`] = `
[
  {
    "name": "b",
    "version": "^1.0.0",
    "_id": "b@^1.0.0",
    "pkgid": "b@^1.0.0",
    "path": null,
    "realpath": null,
    "resolved": null,
    "from": [
      ""
    ],
    "to": [],
    "dev": true,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {
      "missing": true
    }
  }
]
`

exports[`test/lib/commands/query.js TAP package-lock-only with package lock > should return valid response with only lock info 1`] = `
[
  {
    "name": "project",
    "dependencies": {
      "a": "^1.0.0"
    },
    "pkgid": "project@",
    "location": "",
    "path": "{CWD}/prefix",
    "realpath": "{CWD}/prefix",
    "resolved": null,
    "from": [],
    "to": [
      "node_modules/a"
    ],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  },
  {
    "version": "1.2.3",
    "resolved": "https://dummy.npmjs.org/a/-/a-1.2.3.tgz",
    "integrity": "sha512-dummy",
    "engines": {
      "node": ">=14.17"
    },
    "name": "a",
    "_id": "a@1.2.3",
    "pkgid": "a@1.2.3",
    "location": "node_modules/a",
    "path": "{CWD}/prefix/node_modules/a",
    "realpath": "{CWD}/prefix/node_modules/a",
    "from": [
      ""
    ],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  }
]
`

exports[`test/lib/commands/query.js TAP recursive tree > should return everything in the tree, accounting for recursion 1`] = `
[
  {
    "name": "project",
    "dependencies": {
      "a": "^1.0.0",
      "b": "^1.0.0"
    },
    "pkgid": "project@",
    "location": "",
    "path": "{CWD}/prefix",
    "realpath": "{CWD}/prefix",
    "resolved": null,
    "from": [],
    "to": [
      "node_modules/a",
      "node_modules/b"
    ],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  },
  {
    "pkgid": "a@",
    "location": "node_modules/a",
    "path": "{CWD}/prefix/node_modules/a",
    "realpath": "{CWD}/prefix/node_modules/a",
    "resolved": null,
    "from": [
      ""
    ],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  },
  {
    "pkgid": "b@",
    "location": "node_modules/b",
    "path": "{CWD}/prefix/node_modules/b",
    "realpath": "{CWD}/prefix/node_modules/b",
    "resolved": null,
    "from": [
      ""
    ],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  }
]
`

exports[`test/lib/commands/query.js TAP simple query > should return root object and direct children 1`] = `
[
  {
    "name": "project",
    "dependencies": {
      "a": "^1.0.0",
      "b": "^1.0.0"
    },
    "peerDependencies": {
      "c": "1.0.0"
    },
    "pkgid": "project@",
    "location": "",
    "path": "{CWD}/prefix",
    "realpath": "{CWD}/prefix",
    "resolved": null,
    "from": [],
    "to": [
      "node_modules/a",
      "node_modules/b"
    ],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  },
  {
    "pkgid": "a@",
    "location": "node_modules/a",
    "path": "{CWD}/prefix/node_modules/a",
    "realpath": "{CWD}/prefix/node_modules/a",
    "resolved": null,
    "from": [
      ""
    ],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  },
  {
    "pkgid": "b@",
    "location": "node_modules/b",
    "path": "{CWD}/prefix/node_modules/b",
    "realpath": "{CWD}/prefix/node_modules/b",
    "resolved": null,
    "from": [
      ""
    ],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  }
]
`

exports[`test/lib/commands/query.js TAP workspace query > should return workspace object 1`] = `
[
  {
    "name": "c",
    "version": "1.0.0",
    "_id": "c@1.0.0",
    "pkgid": "c@1.0.0",
    "location": "c",
    "path": "{CWD}/prefix/c",
    "realpath": "{CWD}/prefix/c",
    "resolved": null,
    "from": [],
    "to": [],
    "dev": false,
    "inBundle": false,
    "deduped": false,
    "overridden": false,
    "queryContext": {}
  }
]
`
