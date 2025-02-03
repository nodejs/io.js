# Node.js startup snapshot builder

This is the V8 startup snapshot builder of Node.js. Not to be confused with
V8's own snapshot builder, which builds a snapshot containing JavaScript
builtins, this builds a snapshot containing Node.js builtins that can be
deserialized on top of V8's own startup snapshot. When Node.js is launched,
instead of executing code to bootstrap, it can deserialize the context from
an embedded snapshot, which readily contains the result of the bootstrap, so
that Node.js can start up faster.

The built-in snapshot consists of the following snapshots:

## Isolate snapshot

Which is used whenever a `v8::Isolate` is created using the data returned by
`node::SnapshotBuilder::GetEmbeddedSnapshotData`.

## Context snapshots

Which are used when a `v8::Context` is created from a `v8::Isolate` deserialized
from the snapshot. There are four context snapshots in the snapshot blob.

1. The default context snapshot, used for contexts created by
  `v8::Context::New()`, it only contains V8 built-ins, matching the
  layout of the isolate snapshot.
1. The vm context snapshot, which can be deserialized using 
  `v8::Context::FromSnapshot(isolate, node::SnapshotData::kNodeVMContextIndex, ...)`.
  It captures initializations specific to vm contexts done by
  `node::contextify::ContextifyContext::CreateV8Context()`.
1. The base context snapshot, which can be deserialized using
  `v8::Context::FromSnapshot(isolate, node::SnapshotData::kNodeBaseContextIndex, ...)`.
  It currently captures initializations done by `node::NewContext()`
  but is intended to include more as a basis for worker thread
  contexts.
1. The main context snapshot, which can be deserialized using
  `v8::Context::FromSnapshot(isolate, node::SnapshotData::kNodeMainContextIndex, ...)`.
  This is the snapshot for the main context on the main thread, and
  captures initializations done by `node::CommonEnvironmentSetup::CreateForSnapshotting()`,
  most notably `node::CreateEnvironment()`, which runs the following scripts via
  `node::Realm::RunBootstrapping()` for the main context as a principal realm,
  so that at runtime, these scripts do not need to be run. Instead only the context
  initialized by them is deserialized at runtime.
     1. `internal/bootstrap/realm`
     2. `internal/bootstrap/node`
     3. `internal/bootstrap/web/exposed-wildcard`
     4. `internal/bootstrap/web/exposed-window-or-worker`
     5. `internal/bootstrap/switches/is_main_thread`
     6. `internal/bootstrap/switches/does_own_process_state`

For more information about these contexts, see the comment in `src/node_context_data.h`.

## How it's built and used

The snapshot builder is built with the `node_mksnapshot` target in `node.gyp`
when `node_use_node_snapshot` is set to true, which is currently done by
default.

In the default build of the Node.js executable, to embed a V8 startup snapshot
into the Node.js executable, `libnode` is first built with these unresolved
symbols:

- `node::SnapshotBuilder::GetEmbeddedSnapshotData`

Then the `node_mksnapshot` executable is built with C++ files in this
directory, as well as `src/node_snapshot_stub.cc` which defines the unresolved
symbols.

`node_mksnapshot` is run to generate a C++ file
`<(SHARED_INTERMEDIATE_DIR)/node_snapshot.cc` that is similar to
`src/node_snapshot_stub.cc` in structure, but contains the snapshot data
written as static char array literals. Then `libnode` is built with
`node_snapshot.cc` to produce the final Node.js executable with the snapshot
data embedded.

For debugging, Node.js can be built without Node.js's own snapshot if
`--without-node-snapshot` is passed to `configure`. A Node.js executable
with Node.js snapshot embedded can also be launched without deserializing
from it if the command line argument `--no-node-snapshot` is passed.

### When `node_mksnapshot` crashes..

Due to this two-phase building process, sometimes when there is an issue
in the code, the build may crash early at executing `node_mksnapshot` instead of crashing
at executing the final executable `node`. If the crash can be reproduced when running
the `node` executable built with `--without-node-snapshot`, it means the crash likely
has nothing to do with snapshots, and only shows up in `node_mksnapshot` because it's
the first Node.js executable being run.

If the crash comes from a `mksnapshot` executable (notice that it doesn't have the `node_`
prefix), that comes from V8's own snapshot building process, not the one in Node.js, and the
fix likely needs to be in V8 or the build configurations of V8.

If it `node_mksnapshot` crashes with an error message containing
something like `Unknown external reference 0x107769200`, see
[Registering binding functions used in bootstrap][] on how to fix it.

[Registering binding functions used in bootstrap]: ../../src/README.md#registering-binding-functions-used-in-bootstrap
