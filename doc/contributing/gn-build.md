# GN Build

Similar to GYP, GN is a build system designed for building Chromium. The
official builds of Node.js are built with GYP, but GN build files are also
provided as an unofficial alternative build system.

The GN build files only support a subset of the Node.js build configurations.
It's not required for all pull requests to Node.js to support GN builds, and the
CI of Node.js does not currently test GN builds, though Node.js welcomes pull
requests that improve GN support or fix breakages introduced by other pull
requests.

Currently the GN build is used by:

1. Electron for building Node.js together with Chromium.
2. V8 for testing the integration of Node.js in CI.

## Files for GN support

Node.js contains following GN build files:

* `node.gni` - Public GN arguments for configuring the build.
* `*/BUILD.gn` - This is the file where GN looks for build rules, in Node.js it
  just includes the `unofficial.gni` file to avoid accidental modifications.
* `*/unofficial.gni` - The actual build rules for each component.

## Build with GN

Unlike GYP, the GN tool does not include any built-in rules for compiling a
project, which means projects building with GN must provide their own build
configurations for things like how to invoke a C++ compiler. Chromium related
projects like V8 and skia choose to reuse Chromium's build configurations, and
V8's Node.js integration testing repository
([node-ci](https://chromium.googlesource.com/v8/node-ci/)) can be reused for
building Node.js.

### 1. Install `depot_tools`

The `depot_tools` is a set of tools used by Chromium related projects for
checking out code and managing dependencies, and since this guide is reusing the
infra of V8, it needs to be installed and added to `PATH`:

```bash
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH=/path/to/depot_tools:$PATH
```

You can also follow the [official tutorial of
`depot_tools`](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html).

### 2. Check out code of Node.js

To check out the latest main branch of Node.js for building, use the `fetch`
tool from `depot_tools`:

```bash
mkdir node_gn
cd node_gn
fetch node
```

You can choose to save some time by omitting git history:

```bash
fetch --no-history node
```

After syncing is done, you will get a directory structure like this:

```console
node_gn/
├── .gclient
├── .gclient_entries
├── ...
└── node
    ├── DEPS
    ├── ...
    ├── build/
    ├── node/
    └── tools/
```

The `node_gn` is a workspace directory, which only contains configurations and
caches of the `gclient` tool from `depot_tools`, and the repository is checked
out at `node_gn/node`.

The `node_gn/node` directory is not a checkout of Node.js, it is actually
[node-ci](https://chromium.googlesource.com/v8/node-ci/), the repository used by
V8 for testing integration with Node.js. The source code of Node.js is checked
out at `node_gn/node/node`.

### 3. Build

GN only supports [`ninja`](https://ninja-build.org) for building, so to build
Node.js with GN, `ninja` build files should be generated first, and then
`ninja` can be invoked to do the building.

The `node-ci` repository provides a script for calling GN:

```bash
cd node  # Enter `node_gn/node` which contains a node-ci checkout
./tools/gn-gen.py out/Release
```

which writes `ninja` build files into the `out/Release` directory under
`node_gn/node`.

And then you can execute `ninja`:

```bash
ninja -C out/Release node
```

After the build is completed, the compiled Node.js executable can be found in
`out/Release/node`.

## Status of the GN build

Currently the GN build of Node.js is not fully functioning. It builds for macOS
and Linux, while the Windows build is still a work in progress. And some tests
are still failing with the GN build.

There are also efforts on making GN build work without using `depot_tools`,
which is tracked in the issue
[#51689](https://github.com/nodejs/node/issues/51689).
