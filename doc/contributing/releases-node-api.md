# Node.js Node-API version release process

This document describes the technical aspects of the Node.js Node-API version
release process.

## Table of contents

* [How to create a release](#how-to-create-a-release)
  * [0. Pre-release steps](#0-pre-release-steps)
  * [1. Update the main branch](#1-update-the-main-branch)
  * [2. Create a new branch for the release](#2-create-a-new-branch-for-the-release)
  * [3. Update `NODE_API_SUPPORTED_VERSION_MAX`](#3-update-node_api_supported_version_max)
  * [4. Define `addon_context_register_func`](#4-define-addon_context_register_func)
  * [5. Update version guards](#5-update-version-guards)
  * [6. Create release commit](#6-create-release-commit)
  * [7. Propose release on GitHub](#7-propose-release-on-github)
  * [8. Ensure that the release branch is stable](#8-ensure-that-the-release-branch-is-stable)
  * [9. Land the release](#9-land-the-release)
  * [10. Backport the release](#10-backport-the-release)

## How to create a release

Notes:

* Version strings are listed below as _"vx"_ or _"x"_. Substitute for
  the release version.
* Examples will use the integer release version `10`.

### 0. Pre-release steps

Before preparing a Node.js Node-API version release, the Node-API Working Group
must be notified at least one business day in advance of the expected release.

Node-API Working Group can be contacted best by opening up an issue on the
[abi-stable-node issue tracker][].

### 1. Update the main Branch

Checkout the staging branch locally.

```bash
git remote update
git checkout main
git reset --hard upstream/main
```

If the staging branch is not up to date relative to `main`, bring the
appropriate PRs and commits into it.

### 2. Create a new branch for the release

Create a new branch named `node-api-x-proposal`, off the main branch.

```bash
git checkout -b node-api-10-proposal upstream/main
```

### 3. Update `NODE_API_SUPPORTED_VERSION_MAX`

Set the version for the proposed release using the following macros, which are
already defined in `src/node_version.h`:

```c
#define NODE_API_SUPPORTED_VERSION_MAX x
```

> Note: Do not update the `NAPI_VERSION` defined in `src/js_native_api.h`. It
> is a fixed constant baseline version of Node-API.

### 4. Define `addon_context_register_func`

For each new version of Node-API an `else if` case must be added to
`get_node_api_context_register_func` in `src/node_api.cc` and the numeric
literal used in the `static_assert` statement in that function must be updated
to the new Node-API version.

### 5. Update version guards

#### Step 1. Update define version guards

If this release includes new Node-APIs that were first released in this
version, the relevant commits should already include the `NAPI_EXPERIMENTAL`
define guards on the declaration of the new Node-API. Check for these guards
with:

```bash
grep                           \
  -E                           \
  'N(ODE_)?API_EXPERIMENTAL'   \
  src/js_native_api{_types,}.h \
  src/node_api{_types,}.h
```

and update the define version guards with the release version:

```diff
- #ifdef NAPI_EXPERIMENTAL
+ #if NAPI_VERSION >= 10

  NAPI_EXTERN napi_status NAPI_CDECL
  node_api_function(napi_env env);

- #endif  // NAPI_EXPERIMENTAL
+ #endif  // NAPI_VERSION >= 10
```

Remove any feature flags of the form `NODE_API_EXPERIMENTAL_HAS_<FEATURE>`.

Remove any additional `NODE_API_EXPERIMENTAL_*` guards along with
`NAPI_EXPERIMENTAL`.

Also, update the Node-API version value of the `napi_get_version` test in
`test/js-native-api/test_general/test.js` with the release version `x`:

```diff
  // Test version management functions
- assert.strictEqual(test_general.testGetVersion(), 9);
+ assert.strictEqual(test_general.testGetVersion(), 10);
```

#### Step 2. Update runtime version guards

If this release includes runtime behavior version guards, the relevant commits
should already include `NAPI_VERSION_EXPERIMENTAL` guard for the change. Check
for these guards with:

```bash
grep NAPI_VERSION_EXPERIMENTAL src/js_native_api_v8* src/node_api.cc
```

and substitute this guard version with the release version `x`.

#### Step 3. Update test version guards

If this release includes add-on tests for the new Node-APIs, the relevant
commits should already include `NAPI_EXPERIMENTAL` definition for the tests.
Check for these definitions with:

```bash
grep                                    \
  -E                                    \
  'N(ODE_)?API_EXPERIMENTAL'            \
  test/node-api/*/{*.{h,c},binding.gyp} \
  test/js-native-api/*/{*.{h,c},binding.gyp}
```

and substitute the `NAPI_EXPERIMENTAL` with the release version
`NAPI_VERSION x`;

```diff
- #define NAPI_EXPERIMENTAL
+ #define NAPI_VERSION 10
```

Remove any `NODE_API_EXPERIMENTAL_*` flags.

#### Step 4. Update document

If this release includes new Node-APIs that were first released in this
version and are necessary to document, the relevant commits should already
have documented the new Node-API.

For all Node-API functions and types with define guards updated in Step 1,
in `doc/api/n-api.md`, add the `napiVersion: x` metadata to the Node-API types
and functions that are released in the version, and remove the experimental
stability banner:

```diff
  #### node_api_function
  <!-- YAML
  added:
    - v1.2.3
+ napiVersion: 10
  -->

- > Stability: 1 - Experimental
```

#### Step 5. Update change history

If this release includes new Node-APIs runtime version guards that were first
released in this version and are necessary to document, the relevant commits
should already have documented the new behavior in a "Change History" section.

For all runtime version guards updated in Step 2, check for these definitions
with:

```bash
grep NAPI_EXPERIMENTAL doc/api/n-api.md
```

In `doc/api/n-api.md`, update the `experimental` change history item to be the
released version `x`:

```diff
  Change History:

- * experimental (`NAPI_EXPERIMENTAL` is defined):
+ * version 10:
```

### 6. Create release commit

When committing these to git, use the following message format:

```text
node-api: define version x
```

### 7. Propose release on GitHub

Create a pull request targeting the `main` branch. These PRs should be left
open for at least 24 hours, and can be updated as new commits land.

If you need any additional information about any of the commits, this PR is a
good place to @-mention the relevant contributors.

Tag the PR with the `notable-change` label, and @-mention the GitHub team
@nodejs/node-api and @nodejs/node-api-implementer.

### 8. Ensure that the release branch is stable

Run a **[`node-test-pull-request`](https://ci.nodejs.org/job/node-test-pull-request/)**
test run to ensure that the build is stable and the HEAD commit is ready for
release.

### 9. Land the release

See the steps documented in [Collaborator Guide - Landing a PR][] to land the
PR.

### 10. Backport the release

Consider backporting the release to all LTS versions following the steps
documented in the [backporting guide][].

[Collaborator Guide - Landing a PR]: ./collaborator-guide.md#landing-pull-requests
[abi-stable-node issue tracker]: https://github.com/nodejs/abi-stable-node/issues
[backporting guide]: backporting-to-release-lines.md
