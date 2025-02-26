# Dependency update scripts

This folder contains scripts used to automatically update a Node.js dependency.
These scripts are usually run by CI (see `.github/workflows/tools.yml`) in order
to download a new dependency version, and replace the old version with it.

Since these scripts only update to the upstream code, changes might be needed in
this repository in order to successfully update (e.g: changing API calls to
conform to upstream changes, updating GYP build files, etc.)

## libuv

The `update-libuv.sh` script takes the target version to update as its only
argument, downloads it from the [GitHub repo](https://github.com/libuv/libuv)
and uses it to replace the contents of `deps/uv/`. The contents are replaced
entirely except for the `*.gyp` and `*.gypi` build files, which are part of the
Node.js build definitions and are not present in the upstream repo.

For example, in order to update to version `1.44.2`, the following command can
be run:

```bash
./tools/dep_updaters/update-libuv.sh 1.44.2
```

Once the script has run (either manually, or by CI in which case a PR will have
been created with the changes), do the following:

1. Check the [changelog](https://github.com/libuv/libuv/blob/v1.x/ChangeLog) for
   things that might require changes in Node.js.
2. If necessary, update `common.gypi` and `uv.gyp` with build-related changes.
3. Check that Node.js compiles without errors and the tests pass.
4. Create a commit for the update and in the commit message include the
   important/relevant items from the changelog (see [`c61870c`][] for an
   example).

[`c61870c`]: https://github.com/nodejs/node/commit/c61870c376e2f5b0dbaa939972c46745e21cdbdd

## OpenSSL

The `update-openssl.sh` script automates the steps described in
[`maintaining-openssl.md`][]. The main difference is that the script downloads
the release tarball from GitHub, instead of cloning the repo and using that as
the source code. This is useful since the release tarball does not include
development-specific files and directories (e.g the `.github` folder).

The script has to be run in two steps. The first one (using the `download`
sub-command) replaces the OpenSSL source code with the new version. The second
one (using the `regenerate` sub-command) regenerates the platform-specific
files. This makes it easier to create two separate git commits, making the git
history more descriptive.

For example, in order to update to version `3.0.7+quic1`, the following commands
should be run:

```bash
./tools/dep_updaters/update-openssl.sh download 3.0.7+quic1
git add -A deps/openssl/openssl
git commit -m "deps: upgrade openssl sources to quictls/openssl-3.0.7+quic1"

./tools/dep_updaters/update-openssl.sh regenerate 3.0.7+quic1
git add -A deps/openssl/config/archs deps/openssl/openssl
git commit -m "deps: update archs files for openssl"
```

Once the script has run (either manually, or by CI in which case a PR will have
been created with the changes), do the following:

1. Check the `CHANGES.md` file in the [repo](https://github.com/quictls/openssl)
   for things that might require changes in Node.js.
2. Check the diffs to ensure the changes are right. Even if there are no changes
   in the source, `buildinf.h` files will be updated because they have timestamp
   data in them.
3. Check that Node.js compiles without errors and the tests pass.
4. Create a commit for the update and in the commit message include the
   important/relevant items from the changelog.

## postject

The `update-postject.sh` script downloads postject from the [npm package](http://npmjs.com/package/postject)
and uses it to replace the contents of `test/fixtures/postject-copy`.

In order to update, the following command can be run:

```bash
./tools/dep_updaters/update-postject.sh
```

Once the script has run (either manually, or by CI in which case a PR will have
been created with the changes), do the following:

1. Check the [changelog](https://github.com/nodejs/postject/releases/tag/v1.0.0-alpha.4)
   for things that might require changes in Node.js.
2. Check that Node.js compiles without errors and the tests pass.
3. Create a commit for the update and in the commit message include the
   important/relevant items from the changelog.
   
[`maintaining-openssl.md`]: https://github.com/nodejs/node/blob/main/doc/contributing/maintaining/maintaining-openssl.md
