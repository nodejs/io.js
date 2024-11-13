'use strict';

const {
  ObjectDefineProperties,
  ObjectFreeze,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  StringPrototypeToUpperCase,
  Symbol,
} = primordials;

const {
  ERR_ILLEGAL_CONSTRUCTOR,
} = require('internal/errors').codes;

const {
  kEnumerableProperty,
} = require('internal/util');

const {
  getAvailableParallelism,
} = internalBinding('os');

const kInitialize = Symbol('kInitialize');

const {
  platform,
  arch,
  version: nodeVersion,
} = require('internal/process/per_thread');

const {
  getDefaultLocale,
} = internalBinding('config');

/**
 * @param {string} arch
 * @param {string} platform
 * @returns {string}
 */
function getNavigatorPlatform(arch, platform) {
  if (platform === 'darwin') {
    // On macOS, modern browsers return 'MacIntel' even if running on Apple Silicon.
    return 'MacIntel';
  } else if (platform === 'win32') {
    // On Windows, modern browsers return 'Win32' even if running on a 64-bit version of Windows.
    // https://developer.mozilla.org/en-US/docs/Web/API/Navigator/platform#usage_notes
    return 'Win32';
  } else if (platform === 'linux') {
    if (arch === 'ia32') {
      return 'Linux i686';
    } else if (arch === 'x64') {
      return 'Linux x86_64';
    }
    return `Linux ${arch}`;
  } else if (platform === 'freebsd') {
    if (arch === 'ia32') {
      return 'FreeBSD i386';
    } else if (arch === 'x64') {
      return 'FreeBSD amd64';
    }
    return `FreeBSD ${arch}`;
  } else if (platform === 'openbsd') {
    if (arch === 'ia32') {
      return 'OpenBSD i386';
    } else if (arch === 'x64') {
      return 'OpenBSD amd64';
    }
    return `OpenBSD ${arch}`;
  } else if (platform === 'sunos') {
    if (arch === 'ia32') {
      return 'SunOS i86pc';
    }
    return `SunOS ${arch}`;
  } else if (platform === 'aix') {
    return 'AIX';
  }
  return `${StringPrototypeToUpperCase(platform[0])}${StringPrototypeSlice(platform, 1)} ${arch}`;
}

class Navigator {
  // Private properties are used to avoid brand validations.
  #availableParallelism;
  #userAgent;
  #platform;
  #languages;

  constructor() {
    if (arguments[0] === kInitialize) {
      return;
    }
    throw new ERR_ILLEGAL_CONSTRUCTOR();
  }

  /**
   * @return {number}
   */
  get hardwareConcurrency() {
    this.#availableParallelism ??= getAvailableParallelism();
    return this.#availableParallelism;
  }

  /**
   * @return {string}
   */
  get language() {
    // The default locale might be changed dynamically, so always invoke the
    // binding.
    return getDefaultLocale() || 'en-US';
  }

  /**
   * @return {Array<string>}
   */
  get languages() {
    this.#languages ??= ObjectFreeze([this.language]);
    return this.#languages;
  }

  /**
   * @return {string}
   */
  get userAgent() {
    this.#userAgent ??= `Node.js/${StringPrototypeSlice(nodeVersion, 1, StringPrototypeIndexOf(nodeVersion, '.'))}`;
    return this.#userAgent;
  }

  /**
   * @return {string}
   */
  get platform() {
    this.#platform ??= getNavigatorPlatform(arch, platform);
    return this.#platform;
  }
}

ObjectDefineProperties(Navigator.prototype, {
  hardwareConcurrency: kEnumerableProperty,
  language: kEnumerableProperty,
  languages: kEnumerableProperty,
  userAgent: kEnumerableProperty,
  platform: kEnumerableProperty,
});

module.exports = {
  getNavigatorPlatform,
  navigator: new Navigator(kInitialize),
  Navigator,
};
