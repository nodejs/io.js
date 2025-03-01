# Chromium inspector (devtools) protocol

This directory contains scripts to update the [Chromium `inspector_protocol`][]
to local at `deps/inspector_protocol`.

To run the `roll.py`, a local clone of the `inspector_protocol` project is required.
First, you will need to install Chromium's [`depot_tools`][], with `fetch` available
in your `PATH`.

```console
$ cd workspace
/workspace $ mkdir inspector_protocol
/workspace/inspector_protocol $ fetch inspector_protocol
# This will create a `src` directory in the current path.

# To update local clone.
/workspace/inspector_protocol $ cd src
/workspace/inspector_protocol/src $ git checkout main && git pull
```

With a local clone of the `inspector_protocol` project up to date, run the following
commands to roll the dep.

```console
$ cd workspace/node
/workspace/node $ python tools/inspector_protocol/roll.py \
  --ip_src_upstream /workspace/inspector_protocol/src \
  --node_src_downstream /workspace/node \
  --force
  # Add --force when you decided to take the update.
```

The `roll.py` requires the node repository to be a clean state (no unstaged changes)
to avoid unexpected overrides.

[`depot_tools`]: https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up
[Chromium `inspector_protocol`]: https://chromium.googlesource.com/deps/inspector_protocol/
