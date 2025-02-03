// Copyright 2024 Nathan Whitaker. All rights reserved. MIT license.

// make sure it takes >1 tick to evaluate the module
await import("./dummy").catch(() => {});
await import("./dummy").catch(() => {});

export default () => {};
