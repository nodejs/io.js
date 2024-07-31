// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

const {
  ArrayPrototypePush,
  Float64Array,
  NumberParseInt,
  ObjectDefineProperties,
  StringPrototypeEndsWith,
  StringPrototypeSlice,
  SymbolToPrimitive,
} = primordials;

const { safeGetenv } = internalBinding('credentials');
const constants = internalBinding('constants').os;
const isWindows = process.platform === 'win32';

const {
  codes: {
    ERR_SYSTEM_ERROR,
  },
  hideStackFrames,
} = require('internal/errors');
const { validateInt32 } = require('internal/validators');

const {
  getAvailableParallelism,
  getCPUs,
  getFreeMem,
  getHomeDirectory: _getHomeDirectory,
  getHostname: _getHostname,
  getInterfaceAddresses: _getInterfaceAddresses,
  getLoadAvg,
  getPriority: _getPriority,
  getOSInformation: _getOSInformation,
  getTotalMem,
  getUserInfo,
  getUptime: _getUptime,
  isBigEndian,
  setPriority: _setPriority,
} = internalBinding('os');

function getCheckedFunction(fn) {
  return hideStackFrames(function checkError() {
    const ctx = {};
    const ret = fn(ctx);
    if (ret === undefined) {
      throw new ERR_SYSTEM_ERROR.HideStackFramesError(ctx);
    }
    return ret;
  });
}

const {
  0: type,
  1: version,
  2: release,
  3: machine,
} = _getOSInformation();

const getHomeDirectory = getCheckedFunction(_getHomeDirectory);
const getHostname = getCheckedFunction(_getHostname);
const getInterfaceAddresses = getCheckedFunction(_getInterfaceAddresses);
const getUptime = getCheckedFunction(_getUptime);

/**
 * @returns {string}
 */
const getOSRelease = () => release;
/**
 * @returns {string}
 */
const getOSType = () => type;
/**
 * @returns {string}
 */
const getOSVersion = () => version;
/**
 * @returns {string}
 */
const getMachine = () => machine;

getAvailableParallelism[SymbolToPrimitive] = () => getAvailableParallelism();
getFreeMem[SymbolToPrimitive] = () => getFreeMem();
getHostname[SymbolToPrimitive] = () => getHostname();
getOSVersion[SymbolToPrimitive] = () => getOSVersion();
getOSType[SymbolToPrimitive] = () => getOSType();
getOSRelease[SymbolToPrimitive] = () => getOSRelease();
getMachine[SymbolToPrimitive] = () => getMachine();
getHomeDirectory[SymbolToPrimitive] = () => getHomeDirectory();
getTotalMem[SymbolToPrimitive] = () => getTotalMem();
getUptime[SymbolToPrimitive] = () => getUptime();

const kEndianness = isBigEndian ? 'BE' : 'LE';

const avgValues = new Float64Array(3);

/**
 * @returns {[number, number, number]}
 */
function loadavg() {
  getLoadAvg(avgValues);
  return [avgValues[0], avgValues[1], avgValues[2]];
}

/**
 * Returns an array of objects containing information about each
 * logical CPU core.
 * @returns {Array<{
 *  model: string,
 *  speed: number,
 *  times: {
 *    user: number,
 *    nice: number,
 *    sys: number,
 *    idle: number,
 *    irq: number,
 *  },
 * }>}
 */
function cpus() {
  // [] is a bugfix for a regression introduced in 51cea61
  const data = getCPUs() || [];
  const result = [];
  let i = 0;
  while (i < data.length) {
    ArrayPrototypePush(result, {
      model: data[i++],
      speed: data[i++],
      times: {
        user: data[i++],
        nice: data[i++],
        sys: data[i++],
        idle: data[i++],
        irq: data[i++],
      },
    });
  }
  return result;
}

/**
 * @returns {string}
 */
function arch() {
  return process.arch;
}
arch[SymbolToPrimitive] = () => process.arch;

/**
 * @returns {string}
 */
function platform() {
  return process.platform;
}
platform[SymbolToPrimitive] = () => process.platform;

/**
 * @returns {string}
 */
function tmpdir() {
  let path;
  if (isWindows) {
    path = process.env.TEMP ||
           process.env.TMP ||
           (process.env.SystemRoot || process.env.windir) + '\\temp';
    if (path.length > 1 && StringPrototypeEndsWith(path, '\\') &&
        !StringPrototypeEndsWith(path, ':\\'))
      path = StringPrototypeSlice(path, 0, -1);
  } else {
    path = safeGetenv('TMPDIR') ||
           safeGetenv('TMP') ||
           safeGetenv('TEMP') ||
           '/tmp';
    if (path.length > 1 && StringPrototypeEndsWith(path, '/'))
      path = StringPrototypeSlice(path, 0, -1);
  }

  return path;
}
tmpdir[SymbolToPrimitive] = () => tmpdir();

/**
 * @returns {'BE' | 'LE'}
 */
function endianness() {
  return kEndianness;
}
endianness[SymbolToPrimitive] = () => kEndianness;

// Returns the number of ones in the binary representation of the decimal
// number.
function countBinaryOnes(n) {
  // Count the number of bits set in parallel, which is faster than looping
  n = n - ((n >>> 1) & 0x55555555);
  n = (n & 0x33333333) + ((n >>> 2) & 0x33333333);
  return ((n + (n >>> 4) & 0xF0F0F0F) * 0x1010101) >>> 24;
}

function getCIDR(address, netmask, family) {
  let ones = 0;
  let split = '.';
  let range = 10;
  let groupLength = 8;
  let hasZeros = false;
  let lastPos = 0;

  if (family === 'IPv6') {
    split = ':';
    range = 16;
    groupLength = 16;
  }

  for (let i = 0; i < netmask.length; i++) {
    if (netmask[i] !== split) {
      if (i + 1 < netmask.length) {
        continue;
      }
      i++;
    }
    const part = StringPrototypeSlice(netmask, lastPos, i);
    lastPos = i + 1;
    if (part !== '') {
      if (hasZeros) {
        if (part !== '0') {
          return null;
        }
      } else {
        const binary = NumberParseInt(part, range);
        const binaryOnes = countBinaryOnes(binary);
        ones += binaryOnes;
        if (binaryOnes !== groupLength) {
          if ((binary & 1) !== 0) {
            return null;
          }
          hasZeros = true;
        }
      }
    }
  }

  return `${address}/${ones}`;
}

/**
 * @returns {Record<string, Array<{
 *  address: string,
 *  netmask: string,
 *  family: 'IPv4' | 'IPv6',
 *  mac: string,
 *  internal: boolean,
 *  scopeid: number,
 *  cidr: string | null,
 * }>>}
 */
function networkInterfaces() {
  const data = getInterfaceAddresses();
  const result = {};

  if (data === undefined)
    return result;
  for (let i = 0; i < data.length; i += 7) {
    const name = data[i];
    const entry = {
      address: data[i + 1],
      netmask: data[i + 2],
      family: data[i + 3],
      mac: data[i + 4],
      internal: data[i + 5],
      cidr: getCIDR(data[i + 1], data[i + 2], data[i + 3]),
    };
    const scopeid = data[i + 6];
    if (scopeid !== -1)
      entry.scopeid = scopeid;

    const existing = result[name];
    if (existing !== undefined)
      ArrayPrototypePush(existing, entry);
    else
      result[name] = [entry];
  }

  return result;
}

/**
 * @param {number} [pid=0]
 * @param {number} priority
 * @returns {void}
 */
function setPriority(pid, priority) {
  if (priority === undefined) {
    priority = pid;
    pid = 0;
  }

  validateInt32(pid, 'pid');
  validateInt32(priority, 'priority', -20, 19);

  const ctx = {};

  if (_setPriority(pid, priority, ctx) !== 0)
    throw new ERR_SYSTEM_ERROR(ctx);
}

/**
 * @param {number} [pid=0]
 * @returns {number}
 */
function getPriority(pid) {
  if (pid === undefined)
    pid = 0;
  else
    validateInt32(pid, 'pid');

  const ctx = {};
  const priority = _getPriority(pid, ctx);

  if (priority === undefined)
    throw new ERR_SYSTEM_ERROR(ctx);

  return priority;
}

/**
 * @param {{ encoding?: string }} [options=utf8] If `encoding` is set to
 * `'buffer'`, the `username`, `shell`, and `homedir` values will
 * be `Buffer` instances.
 * @returns {{
 *   uid: number,
 *   gid: number,
 *   username: string,
 *   homedir: string,
 *   shell: string | null,
 * }}
 */
function userInfo(options) {
  if (typeof options !== 'object')
    options = null;

  const ctx = {};
  const user = getUserInfo(options, ctx);

  if (user === undefined)
    throw new ERR_SYSTEM_ERROR(ctx);

  if (isWindows) {
    user.uid |= 0;
    user.gid |= 0;
  }

  return user;
}

module.exports = {
  arch,
  availableParallelism: getAvailableParallelism,
  cpus,
  endianness,
  freemem: getFreeMem,
  getPriority,
  homedir: getHomeDirectory,
  hostname: getHostname,
  loadavg,
  networkInterfaces,
  platform,
  release: getOSRelease,
  setPriority,
  tmpdir,
  totalmem: getTotalMem,
  type: getOSType,
  userInfo,
  uptime: getUptime,
  version: getOSVersion,
  machine: getMachine,
};

ObjectDefineProperties(module.exports, {
  constants: {
    __proto__: null,
    configurable: false,
    enumerable: true,
    value: constants,
  },

  EOL: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    writable: false,
    value: isWindows ? '\r\n' : '\n',
  },

  devNull: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    writable: false,
    value: isWindows ? '\\\\.\\nul' : '/dev/null',
  },
});
