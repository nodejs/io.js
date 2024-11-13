# Node.js Core Development Flags

These flags are specifically designed for use in Node.js core development and are not intended for general
application usage.

> \[!NOTE]
> These APIs are not bound by semantic versioning rules, and they can be altered or removed in any version of Node.js

## Command Line Interface (CLI)

### Flags

#### `--debug-arraybuffer-allocations`

Enables debugging of `ArrayBuffer` allocations.

#### `--expose-internals`

Allows the usage of `internal/*` modules, granting access to internal Node.js functionality.

#### `--inspect-brk-node[=[host:]port]`

Pauses execution at the start of Node.js application code, waiting for a debugger to connect on the specified
`host` and `port`. This is useful for debugging application startup issues. If `host` and `port` are not
provided, it defaults to `127.0.0.1:9229`.

#### `--node-snapshot`

Enables the use of Node.js snapshots, potentially improving startup performance.

#### `--test-udp-no-try-send`

Used for testing UDP functionality without attempting to send data.

#### `--trace-promises`

Enables tracing of promises for debugging and performance analysis.

#### `--verify-base-objects`

Allows verification of base objects for debugging purposes.
