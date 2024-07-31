// This module verifies that the module specifier is resolved relative to the
// current module and not the current working directory in the ShadowRealm.
export { getCounter } from "./state-counter.mjs";
