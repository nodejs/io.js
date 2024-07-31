# Building Node.js with Ninja

The purpose of this guide is to show how to build Node.js using [Ninja][], as
doing so can be significantly quicker than using `make`. Please see
[Ninja's site][Ninja] for installation instructions (Unix only).

[Ninja][] is supported in the Makefile. Run `./configure --ninja` to configure
the project to run the regular `make` commands with Ninja.

When modifying only the JS layer in `lib`, you can use:

```bash
./configure --ninja --node-builtin-modules-path "$(pwd)"
```

For example, `make` will execute `ninja -C out/Release` internally
to produce a compiled release binary, It will also execute
`ln -fs out/Release/node node`, so that you can execute `./node` at
the project's root.

When running `make`, you will see output similar to the following
if the build has succeeded:

```console
ninja: Entering directory `out/Release`
[4/4] LINK node, POSTBUILDS
```

The bottom line will change while building, showing the progress as
`[finished/total]` build steps. This is useful output that `make` does not
produce and is one of the benefits of using Ninja. When using Ninja, builds
are always run in parallel, based by default on the number of CPUs your
system has. You can use the `-j` parameter to override this behavior,
which is equivalent to the `-j` parameter in the regular `make`:

```bash
make -j4 # With this flag, Ninja will limit itself to 4 parallel jobs,
         # regardless of the number of cores on the current machine.
```

Note: if you are on macOS and use GNU Make version `3.x`, the `-jn` flag
will not work. You can either upgrade to `v4.x` (e.g. using a package manager
such as [Homebrew](https://formulae.brew.sh/formula/make#default)) or use `make JOBS=n`.

## Producing a debug build

To create a debug build rather than a release build:

```bash
./configure --ninja --debug && make
```

## Customizing `ninja` path

On some systems (such as RHEL7 and below), the Ninja binary might be installed
with a different name. For these systems use the `NINJA` env var:

```bash
./configure --ninja && NINJA="ninja-build" make
```

[Ninja]: https://ninja-build.org/
