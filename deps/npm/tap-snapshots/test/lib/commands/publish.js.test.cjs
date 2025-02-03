/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/publish.js TAP _auth config default registry > new package version 1`] = `
+ @npmcli/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP bare _auth and registry config > new package version 1`] = `
+ @npm/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP dry-run > must match snapshot 1`] = `
Array [
  "package: @npmcli/test-package@1.0.0",
  "Tarball Contents",
  "95B package.json",
  "Tarball Details",
  "name: @npmcli/test-package",
  "version: 1.0.0",
  "filename: npmcli-test-package-1.0.0.tgz",
  "package size: {size}",
  "unpacked size: 95 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 1",
  "Publishing to https://registry.npmjs.org/ with tag latest and default access (dry-run)",
]
`

exports[`test/lib/commands/publish.js TAP foreground-scripts can still be set to false > must match snapshot 1`] = `
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
  "Publishing to https://registry.npmjs.org/ with tag latest and default access (dry-run)",
]
`

exports[`test/lib/commands/publish.js TAP foreground-scripts defaults to true > must match snapshot 1`] = `
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
  "Publishing to https://registry.npmjs.org/ with tag latest and default access (dry-run)",
]
`

exports[`test/lib/commands/publish.js TAP has mTLS auth for scope configured registry > new package version 1`] = `
+ @npm/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP has token auth for scope configured registry > new package version 1`] = `
+ @npm/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP ignore-scripts > new package version 1`] = `
+ @npmcli/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP json > must match snapshot 1`] = `
Array [
  "Publishing to https://registry.npmjs.org/ with tag latest and default access",
]
`

exports[`test/lib/commands/publish.js TAP json > new package json 1`] = `
{
  "id": "@npmcli/test-package@1.0.0",
  "name": "@npmcli/test-package",
  "version": "1.0.0",
  "size": "{size}",
  "unpackedSize": 95,
  "shasum": "{sha}",
  "integrity": "{integrity}",
  "filename": "npmcli-test-package-1.0.0.tgz",
  "files": [
    {
      "path": "package.json",
      "size": "{size}",
      "mode": 420
    }
  ],
  "entryCount": 1,
  "bundled": []
}
`

exports[`test/lib/commands/publish.js TAP manifest > manifest 1`] = `
Object {
  "_id": "npm@{VERSION}",
  "author": Object {
    "name": "GitHub Inc.",
  },
  "bin": Object {
    "npm": "bin/npm-cli.js",
    "npx": "bin/npx-cli.js",
  },
  "bugs": Object {
    "url": "https://github.com/npm/cli/issues",
  },
  "description": "a package manager for JavaScript",
  "directories": Object {
    "doc": "./doc",
    "man": "./man",
  },
  "exports": Object {
    ".": Array [
      Object {
        "default": "./index.js",
      },
      "./index.js",
    ],
    "./package.json": "./package.json",
  },
  "files": Array [
    "bin/",
    "lib/",
    "index.js",
    "docs/content/",
    "docs/output/",
    "man/",
  ],
  "homepage": "https://docs.npmjs.com/",
  "keywords": Array [
    "install",
    "modules",
    "package manager",
    "package.json",
  ],
  "license": "Artistic-2.0",
  "main": "./index.js",
  "man": Array [
    "man/man1/npm-access.1",
    "man/man1/npm-adduser.1",
    "man/man1/npm-audit.1",
    "man/man1/npm-bugs.1",
    "man/man1/npm-cache.1",
    "man/man1/npm-ci.1",
    "man/man1/npm-completion.1",
    "man/man1/npm-config.1",
    "man/man1/npm-dedupe.1",
    "man/man1/npm-deprecate.1",
    "man/man1/npm-diff.1",
    "man/man1/npm-dist-tag.1",
    "man/man1/npm-docs.1",
    "man/man1/npm-doctor.1",
    "man/man1/npm-edit.1",
    "man/man1/npm-exec.1",
    "man/man1/npm-explain.1",
    "man/man1/npm-explore.1",
    "man/man1/npm-find-dupes.1",
    "man/man1/npm-fund.1",
    "man/man1/npm-help-search.1",
    "man/man1/npm-help.1",
    "man/man1/npm-init.1",
    "man/man1/npm-install-ci-test.1",
    "man/man1/npm-install-test.1",
    "man/man1/npm-install.1",
    "man/man1/npm-link.1",
    "man/man1/npm-login.1",
    "man/man1/npm-logout.1",
    "man/man1/npm-ls.1",
    "man/man1/npm-org.1",
    "man/man1/npm-outdated.1",
    "man/man1/npm-owner.1",
    "man/man1/npm-pack.1",
    "man/man1/npm-ping.1",
    "man/man1/npm-pkg.1",
    "man/man1/npm-prefix.1",
    "man/man1/npm-profile.1",
    "man/man1/npm-prune.1",
    "man/man1/npm-publish.1",
    "man/man1/npm-query.1",
    "man/man1/npm-rebuild.1",
    "man/man1/npm-repo.1",
    "man/man1/npm-restart.1",
    "man/man1/npm-root.1",
    "man/man1/npm-run-script.1",
    "man/man1/npm-sbom.1",
    "man/man1/npm-search.1",
    "man/man1/npm-shrinkwrap.1",
    "man/man1/npm-star.1",
    "man/man1/npm-stars.1",
    "man/man1/npm-start.1",
    "man/man1/npm-stop.1",
    "man/man1/npm-team.1",
    "man/man1/npm-test.1",
    "man/man1/npm-token.1",
    "man/man1/npm-undeprecate.1",
    "man/man1/npm-uninstall.1",
    "man/man1/npm-unpublish.1",
    "man/man1/npm-unstar.1",
    "man/man1/npm-update.1",
    "man/man1/npm-version.1",
    "man/man1/npm-view.1",
    "man/man1/npm-whoami.1",
    "man/man1/npm.1",
    "man/man1/npx.1",
    "man/man5/folders.5",
    "man/man5/install.5",
    "man/man5/npm-global.5",
    "man/man5/npm-json.5",
    "man/man5/npm-shrinkwrap-json.5",
    "man/man5/npmrc.5",
    "man/man5/package-json.5",
    "man/man5/package-lock-json.5",
    "man/man7/config.7",
    "man/man7/dependency-selectors.7",
    "man/man7/developers.7",
    "man/man7/logging.7",
    "man/man7/orgs.7",
    "man/man7/package-spec.7",
    "man/man7/registry.7",
    "man/man7/removal.7",
    "man/man7/scope.7",
    "man/man7/scripts.7",
    "man/man7/workspaces.7",
  ],
  "name": "npm",
  "readmeFilename": "README.md",
  "repository": Object {
    "type": "git",
    "url": "git+https://github.com/npm/cli.git",
  },
  "version": "{VERSION}",
}
`

exports[`test/lib/commands/publish.js TAP no auth dry-run > must match snapshot 1`] = `
+ @npmcli/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP no auth dry-run > warns about auth being needed 1`] = `
Array [
  "This command requires you to be logged in to https://registry.npmjs.org/ (dry-run)",
]
`

exports[`test/lib/commands/publish.js TAP prioritize CLI flags over publishConfig > new package version 1`] = `
+ @npmcli/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP public access > must match snapshot 1`] = `
Array [
  "package: @npm/test-package@1.0.0",
  "Tarball Contents",
  "55B package.json",
  "Tarball Details",
  "name: @npm/test-package",
  "version: 1.0.0",
  "filename: npm-test-package-1.0.0.tgz",
  "package size: {size}",
  "unpacked size: 55 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 1",
  "Publishing to https://registry.npmjs.org/ with tag latest and public access",
]
`

exports[`test/lib/commands/publish.js TAP public access > new package version 1`] = `
+ @npm/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP re-loads publishConfig.registry if added during script process > new package version 1`] = `
+ @npmcli/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP respects publishConfig.registry, runs appropriate scripts > new package version 1`] = `

`

exports[`test/lib/commands/publish.js TAP restricted access > must match snapshot 1`] = `
Array [
  "package: @npm/test-package@1.0.0",
  "Tarball Contents",
  "55B package.json",
  "Tarball Details",
  "name: @npm/test-package",
  "version: 1.0.0",
  "filename: npm-test-package-1.0.0.tgz",
  "package size: {size}",
  "unpacked size: 55 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 1",
  "Publishing to https://registry.npmjs.org/ with tag latest and restricted access",
]
`

exports[`test/lib/commands/publish.js TAP restricted access > new package version 1`] = `
+ @npm/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP scoped _auth config scoped registry > new package version 1`] = `
+ @npm/test-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP tarball > must match snapshot 1`] = `
Array [
  "package: test-tar-package@1.0.0",
  "Tarball Contents",
  String(
    26B index.js
    98B package.json
  ),
  "Tarball Details",
  "name: test-tar-package",
  "version: 1.0.0",
  "filename: test-tar-package-1.0.0.tgz",
  "package size: {size}",
  "unpacked size: 124 B",
  "shasum: {sha}",
  "integrity: {integrity}
  "total files: 2",
  "Publishing to https://registry.npmjs.org/ with tag latest and default access",
]
`

exports[`test/lib/commands/publish.js TAP tarball > new package json 1`] = `
+ test-tar-package@1.0.0
`

exports[`test/lib/commands/publish.js TAP workspaces all workspaces - color > all public workspaces 1`] = `
+ workspace-a@1.2.3-a
+ workspace-b@1.2.3-n
+ workspace-n@1.2.3-n
`

exports[`test/lib/commands/publish.js TAP workspaces all workspaces - color > warns about skipped private workspace in color 1`] = `
Array [
  "\\u001b[94mpublish\\u001b[39m npm auto-corrected some errors in your package.json when publishing.  Please run \\"npm pkg fix\\" to address these errors.",
  String(
    \\u001b[94mpublish\\u001b[39m errors corrected:
    \\u001b[94mpublish\\u001b[39m "repository" was changed from a string to an object
  ),
  "\\u001b[94mpublish\\u001b[39m npm auto-corrected some errors in your package.json when publishing.  Please run \\"npm pkg fix\\" to address these errors.",
  String(
    \\u001b[94mpublish\\u001b[39m errors corrected:
    \\u001b[94mpublish\\u001b[39m "repository" was changed from a string to an object
    \\u001b[94mpublish\\u001b[39m "repository.url" was normalized to "git+https://github.com/npm/workspace-b.git"
  ),
  "\\u001b[94mpublish\\u001b[39m Skipping workspace \\u001b[36mworkspace-p\\u001b[39m, marked as \\u001b[1mprivate\\u001b[22m",
]
`

exports[`test/lib/commands/publish.js TAP workspaces all workspaces - no color > all public workspaces 1`] = `
+ workspace-a@1.2.3-a
+ workspace-b@1.2.3-n
+ workspace-n@1.2.3-n
`

exports[`test/lib/commands/publish.js TAP workspaces all workspaces - no color > warns about skipped private workspace 1`] = `
Array [
  "publish npm auto-corrected some errors in your package.json when publishing.  Please run \\"npm pkg fix\\" to address these errors.",
  String(
    publish errors corrected:
    publish "repository" was changed from a string to an object
  ),
  "publish npm auto-corrected some errors in your package.json when publishing.  Please run \\"npm pkg fix\\" to address these errors.",
  String(
    publish errors corrected:
    publish "repository" was changed from a string to an object
    publish "repository.url" was normalized to "git+https://github.com/npm/workspace-b.git"
  ),
  "publish Skipping workspace workspace-p, marked as private",
]
`

exports[`test/lib/commands/publish.js TAP workspaces all workspaces - some marked private > one marked private 1`] = `
+ workspace-a@1.2.3-a
`

exports[`test/lib/commands/publish.js TAP workspaces differet package spec > publish different package spec 1`] = `
+ pkg@1.2.3
`

exports[`test/lib/commands/publish.js TAP workspaces json > all workspaces in json 1`] = `
{
  "workspace-a": {
    "id": "workspace-a@1.2.3-a",
    "name": "workspace-a",
    "version": "1.2.3-a",
    "size": "{size}",
    "unpackedSize": 82,
    "shasum": "{sha}",
    "integrity": "{integrity}",
    "filename": "workspace-a-1.2.3-a.tgz",
    "files": [
      {
        "path": "package.json",
        "size": "{size}",
        "mode": 420
      }
    ],
    "entryCount": 1,
    "bundled": []
  },
  "workspace-b": {
    "id": "workspace-b@1.2.3-n",
    "name": "workspace-b",
    "version": "1.2.3-n",
    "size": "{size}",
    "unpackedSize": 92,
    "shasum": "{sha}",
    "integrity": "{integrity}",
    "filename": "workspace-b-1.2.3-n.tgz",
    "files": [
      {
        "path": "package.json",
        "size": "{size}",
        "mode": 420
      }
    ],
    "entryCount": 1,
    "bundled": []
  },
  "workspace-n": {
    "id": "workspace-n@1.2.3-n",
    "name": "workspace-n",
    "version": "1.2.3-n",
    "size": "{size}",
    "unpackedSize": 42,
    "shasum": "{sha}",
    "integrity": "{integrity}",
    "filename": "workspace-n-1.2.3-n.tgz",
    "files": [
      {
        "path": "package.json",
        "size": "{size}",
        "mode": 420
      }
    ],
    "entryCount": 1,
    "bundled": []
  }
}
`

exports[`test/lib/commands/publish.js TAP workspaces one workspace - success > single workspace 1`] = `
+ workspace-a@1.2.3-a
`
