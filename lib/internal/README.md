# Internal Modules

The modules located in `lib/internal` directory are exclusively meant
for internal usage within the Node.js core. They are not intended to
be accessed via user modules `require()`. These modules may change at
any point in time. Relying on these internal modules outside the core
is not supported and can lead to unpredictable behavior.

In certain scenarios, accessing these internal modules for debugging or
experimental purposes might be necessary. Node.js provides the `--expose-internals`
flag to expose these modules to userland code. This flag only exists to
assist Node.js maintainers with debugging internals. It is not meant for
use outside the project.
