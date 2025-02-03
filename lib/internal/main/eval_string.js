'use strict';

// User passed `-e` or `--eval` arguments to Node without `-i` or
// `--interactive`.

const {
  ObjectDefineProperty,
  RegExpPrototypeExec,
  globalThis,
} = primordials;

const {
  prepareMainThreadExecution,
  markBootstrapComplete,
} = require('internal/process/pre_execution');
const {
  evalModuleEntryPoint,
  evalTypeScript,
  parseAndEvalCommonjsTypeScript,
  parseAndEvalModuleTypeScript,
  evalScript,
} = require('internal/process/execution');
const { addBuiltinLibsToObject } = require('internal/modules/helpers');
const { getOptionValue } = require('internal/options');

prepareMainThreadExecution();
addBuiltinLibsToObject(globalThis, '<eval>');
markBootstrapComplete();

const code = getOptionValue('--eval');

const print = getOptionValue('--print');
const shouldLoadESM = getOptionValue('--import').length > 0 || getOptionValue('--experimental-loader').length > 0;
const inputType = getOptionValue('--input-type');
const tsEnabled = getOptionValue('--experimental-strip-types');
if (inputType === 'module') {
  evalModuleEntryPoint(code, print);
} else if (inputType === 'module-typescript' && tsEnabled) {
  parseAndEvalModuleTypeScript(code, print);
} else {
  // For backward compatibility, we want the identifier crypto to be the
  // `node:crypto` module rather than WebCrypto.
  const isUsingCryptoIdentifier = RegExpPrototypeExec(/\bcrypto\b/, code) !== null;
  const shouldDefineCrypto = isUsingCryptoIdentifier && internalBinding('config').hasOpenSSL;

  if (isUsingCryptoIdentifier && !shouldDefineCrypto) {
    // This is taken from `addBuiltinLibsToObject`.
    const object = globalThis;
    const name = 'crypto';
    const setReal = (val) => {
      // Deleting the property before re-assigning it disables the
      // getter/setter mechanism.
      delete object[name];
      object[name] = val;
    };
    ObjectDefineProperty(object, name, { __proto__: null, set: setReal });
  }

  let evalFunction;
  if (inputType === 'commonjs') {
    evalFunction = evalScript;
  } else if (inputType === 'commonjs-typescript' && tsEnabled) {
    evalFunction = parseAndEvalCommonjsTypeScript;
  } else if (tsEnabled) {
    evalFunction = evalTypeScript;
  } else {
    // Default to commonjs.
    evalFunction = evalScript;
  }

  evalFunction('[eval]',
               shouldDefineCrypto ? (
                 print ? `let crypto=require("node:crypto");{${code}}` : `(crypto=>{{${code}}})(require('node:crypto'))`
               ) : code,
               getOptionValue('--inspect-brk'),
               print,
               shouldLoadESM);
}
