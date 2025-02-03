// Copyright 2024 Nathan Whitaker. All rights reserved. MIT license.

export default async () => {
  // depends on `skip-common.mjs` which contains a TLA
  const a = import("./skip-a.mjs");

  // this resolves, resetting the async ordinal counter
  await import("./skip-imports-async-module.mjs");

  // start evaluating more modules that depend on `skip-common.mjs`.
  // one of these will receive a duplicate ordinal to
  // `skip-a.mjs`
  await Promise.all([
    a,
    import("./skip-b.mjs"),
    import("./skip-c.mjs"),
    import("./skip-d.mjs"),
    import("./skip-e.mjs"),
    import("./skip-f.mjs"),
  ]);
  // when `common.mjs` settles, we go to update the state
  // of the modules waiting on it. Due to the duplicate
  // ordinal, one of the modules will be skipped and
  // be stuck pending forever.
};
