'use strict';

// This script sets up the context for shadow realms.

const {
  prepareShadowRealmExecution,
} = require('internal/process/pre_execution');
const {
  BuiltinModule,
} = require('internal/bootstrap/realm');

BuiltinModule.setRealmAllowRequireByUsers([
  /**
   * The built-in modules exposed in the ShadowRealm must each be providing
   * platform capabilities with no authority to cause side effects such as
   * I/O or mutation of values that are shared across different realms within
   * the same Node.js environment.
   */
]);

prepareShadowRealmExecution();
