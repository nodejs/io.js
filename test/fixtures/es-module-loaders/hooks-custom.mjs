import { pathToFileURL } from 'node:url';
import count from '../es-modules/stateful.mjs';


// Arbitrary instance of manipulating a module's internal state
// used to assert node-land and user-land have different contexts
count();

export function resolve(specifier, { importAttributes }, next) {
  let format = '';

  if (specifier === 'esmHook/format.false') {
    format = false;
  }
  if (specifier === 'esmHook/format.true') {
    format = true;
  }
  if (specifier === 'esmHook/preknownFormat.pre') {
    format = 'module';
  }

  if (specifier.startsWith('esmHook')) {
    return {
      format,
      shortCircuit: true,
      url: pathToFileURL(specifier).href,
      importAttributes,
    };
  }

  return next(specifier);
}

/**
 * @param {string} url A fully resolved file url.
 * @param {object} context Additional info.
 * @param {function} next for now, next is defaultLoad a wrapper for
 * defaultGetFormat + defaultGetSource
 * @returns {{ format: string, source: (string|SharedArrayBuffer|Uint8Array) }}
 */
export function load(url, context, next) {
  // Load all .js files as ESM, regardless of package scope
  if (url.endsWith('.js')) {
    return next(url, {
      ...context,
      format: 'module',
    });
  }

  if (url.endsWith('.ext')) {
    return next(url, {
      ...context,
      format: 'module',
    });
  }

  if (url.endsWith('esmHook/badReturnVal.mjs')) {
    return 'export function returnShouldBeObject() {}';
  }

  if (url.endsWith('esmHook/badReturnFormatVal.mjs')) {
    return {
      format: Array(0),
      shortCircuit: true,
      source: '',
    };
  }
  if (url.endsWith('esmHook/unsupportedReturnFormatVal.mjs')) {
    return {
      format: 'foo', // Not one of the allowable inputs: no translator named 'foo'
      shortCircuit: true,
      source: '',
    };
  }

  if (url.endsWith('esmHook/badReturnSourceVal.mjs')) {
    return {
      format: 'module',
      shortCircuit: true,
      source: Array(0),
    };
  }

  if (url.endsWith('esmHook/preknownFormat.pre')) {
    return {
      format: context.format,
      shortCircuit: true,
      source: `const msg = 'hello world'; export default msg;`
    };
  }

  if (url.endsWith('esmHook/virtual.mjs')) {
    return {
      format: 'module',
      shortCircuit: true,
      source: `export const message = 'Woohoo!'.toUpperCase();`,
    };
  }

  if (url.endsWith('esmHook/commonJsNullSource.mjs')) {
    return {
      format: 'commonjs',
      shortCircuit: true,
      source: 1n,
    };
  }

  if (url.endsWith('esmHook/maximumCallStack.mjs')) {
    function recurse() {
      recurse();
    }
    recurse();
  }

  return next(url);
}
