These flags are for Node.js core development usage only. Do not use these flags
in your own applications. These flags are not subjected to semantic versioning
rules. The core developers may remove these flags in any version of Node.js.

# Internal documentation of Node.js

## CLI

### Flags

#### `--debug-arraybuffer-allocations`

#### `--expose-internals`

Allows to require the `internal/*` modules.

#### `--inspect-brk-node[=[host:]port]`

<!-- YAML
added: v7.6.0
-->

Activate inspector on `host:port` and break at start of the first internal
JavaScript script executed when the inspector is available.
Default `host:port` is `127.0.0.1:9229`.

#### `--node-snapshot`

#### `--test-udp-no-try-send`

#### `--trace-promises`

#### `--verify-base-objects`
