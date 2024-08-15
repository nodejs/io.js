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
  ArrayPrototypeJoin,
  ArrayPrototypeSlice,
  FunctionPrototypeBind,
  StringPrototypeCharCodeAt,
  StringPrototypeIndexOf,
  StringPrototypeLastIndexOf,
  StringPrototypeRepeat,
  StringPrototypeReplace,
  StringPrototypeSlice,
  StringPrototypeSplit,
  StringPrototypeToLowerCase,
} = primordials;

const {
  CHAR_UPPERCASE_A,
  CHAR_LOWERCASE_A,
  CHAR_UPPERCASE_Z,
  CHAR_LOWERCASE_Z,
  CHAR_DOT,
  CHAR_FORWARD_SLASH,
  CHAR_BACKWARD_SLASH,
  CHAR_COLON,
  CHAR_QUESTION_MARK,
} = require('internal/constants');
const {
  validateObject,
  validateString,
} = require('internal/validators');

const {
  getLazy,
  emitExperimentalWarning,
  isWindows,
  isMacOS,
} = require('internal/util');

const lazyMinimatch = getLazy(() => require('internal/deps/minimatch/index'));

function isPathSeparator(code) {
  return code === CHAR_FORWARD_SLASH || code === CHAR_BACKWARD_SLASH;
}

function isPosixPathSeparator(code) {
  return code === CHAR_FORWARD_SLASH;
}

function isWindowsDeviceRoot(code) {
  return (code >= CHAR_UPPERCASE_A && code <= CHAR_UPPERCASE_Z) ||
         (code >= CHAR_LOWERCASE_A && code <= CHAR_LOWERCASE_Z);
}

// Resolves . and .. elements in a path with directory names
function normalizeString(path, allowAboveRoot, separator, isPathSeparator) {
  let res = '';
  let lastSegmentLength = 0;
  let lastSlash = -1;
  let dots = 0;
  let code = 0;
  for (let i = 0; i <= path.length; ++i) {
    if (i < path.length)
      code = StringPrototypeCharCodeAt(path, i);
    else if (isPathSeparator(code))
      break;
    else
      code = CHAR_FORWARD_SLASH;

    if (isPathSeparator(code)) {
      if (lastSlash === i - 1 || dots === 1) {
        // NOOP
      } else if (dots === 2) {
        if (res.length < 2 || lastSegmentLength !== 2 ||
            StringPrototypeCharCodeAt(res, res.length - 1) !== CHAR_DOT ||
            StringPrototypeCharCodeAt(res, res.length - 2) !== CHAR_DOT) {
          if (res.length > 2) {
            const lastSlashIndex = StringPrototypeLastIndexOf(res, separator);
            if (lastSlashIndex === -1) {
              res = '';
              lastSegmentLength = 0;
            } else {
              res = StringPrototypeSlice(res, 0, lastSlashIndex);
              lastSegmentLength =
                res.length - 1 - StringPrototypeLastIndexOf(res, separator);
            }
            lastSlash = i;
            dots = 0;
            continue;
          } else if (res.length !== 0) {
            res = '';
            lastSegmentLength = 0;
            lastSlash = i;
            dots = 0;
            continue;
          }
        }
        if (allowAboveRoot) {
          res += res.length > 0 ? `${separator}..` : '..';
          lastSegmentLength = 2;
        }
      } else {
        if (res.length > 0)
          res += `${separator}${StringPrototypeSlice(path, lastSlash + 1, i)}`;
        else
          res = StringPrototypeSlice(path, lastSlash + 1, i);
        lastSegmentLength = i - lastSlash - 1;
      }
      lastSlash = i;
      dots = 0;
    } else if (code === CHAR_DOT && dots !== -1) {
      ++dots;
    } else {
      dots = -1;
    }
  }
  return res;
}

function formatExt(ext) {
  return ext ? `${ext[0] === '.' ? '' : '.'}${ext}` : '';
}

/**
 * @param {string} sep
 * @param {{
 *  dir?: string;
 *  root?: string;
 *  base?: string;
 *  name?: string;
 *  ext?: string;
 *  }} pathObject
 * @returns {string}
 */
function _format(sep, pathObject) {
  validateObject(pathObject, 'pathObject');
  const dir = pathObject.dir || pathObject.root;
  const base = pathObject.base ||
    `${pathObject.name || ''}${formatExt(pathObject.ext)}`;
  if (!dir) {
    return base;
  }
  return dir === pathObject.root ? `${dir}${base}` : `${dir}${sep}${base}`;
}

function glob(path, pattern, windows) {
  emitExperimentalWarning('glob');
  validateString(path, 'path');
  validateString(pattern, 'pattern');
  return lazyMinimatch().minimatch(path, pattern, {
    __proto__: null,
    nocase: isMacOS || isWindows,
    windowsPathsNoEscape: true,
    nonegate: true,
    nocomment: true,
    optimizationLevel: 2,
    platform: windows ? 'win32' : 'posix',
    nocaseMagicOnly: true,
  });
}

const win32 = {
  /**
   * path.resolve([from ...], to)
   * @param {...string} args
   * @returns {string}
   */
  resolve(...args) {
    let resolvedDevice = '';
    let resolvedTail = '';
    let resolvedAbsolute = false;

    for (let i = args.length - 1; i >= -1; i--) {
      let path;
      if (i >= 0) {
        path = args[i];
        validateString(path, `paths[${i}]`);

        // Skip empty entries
        if (path.length === 0) {
          continue;
        }
      } else if (resolvedDevice.length === 0) {
        path = process.cwd();
      } else {
        // Windows has the concept of drive-specific current working
        // directories. If we've resolved a drive letter but not yet an
        // absolute path, get cwd for that drive, or the process cwd if
        // the drive cwd is not available. We're sure the device is not
        // a UNC path at this points, because UNC paths are always absolute.
        path = process.env[`=${resolvedDevice}`] || process.cwd();

        // Verify that a cwd was found and that it actually points
        // to our drive. If not, default to the drive's root.
        if (path === undefined ||
            (StringPrototypeToLowerCase(StringPrototypeSlice(path, 0, 2)) !==
            StringPrototypeToLowerCase(resolvedDevice) &&
            StringPrototypeCharCodeAt(path, 2) === CHAR_BACKWARD_SLASH)) {
          path = `${resolvedDevice}\\`;
        }
      }

      const len = path.length;
      let rootEnd = 0;
      let device = '';
      let isAbsolute = false;
      const code = StringPrototypeCharCodeAt(path, 0);

      // Try to match a root
      if (len === 1) {
        if (isPathSeparator(code)) {
          // `path` contains just a path separator
          rootEnd = 1;
          isAbsolute = true;
        }
      } else if (isPathSeparator(code)) {
        // Possible UNC root

        // If we started with a separator, we know we at least have an
        // absolute path of some kind (UNC or otherwise)
        isAbsolute = true;

        if (isPathSeparator(StringPrototypeCharCodeAt(path, 1))) {
          // Matched double path separator at beginning
          let j = 2;
          let last = j;
          // Match 1 or more non-path separators
          while (j < len &&
                 !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
            j++;
          }
          if (j < len && j !== last) {
            const firstPart = StringPrototypeSlice(path, last, j);
            // Matched!
            last = j;
            // Match 1 or more path separators
            while (j < len &&
                   isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
              j++;
            }
            if (j < len && j !== last) {
              // Matched!
              last = j;
              // Match 1 or more non-path separators
              while (j < len &&
                     !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
                j++;
              }
              if (j === len || j !== last) {
                // We matched a UNC root
                device =
                  `\\\\${firstPart}\\${StringPrototypeSlice(path, last, j)}`;
                rootEnd = j;
              }
            }
          }
        } else {
          rootEnd = 1;
        }
      } else if (isWindowsDeviceRoot(code) &&
                  StringPrototypeCharCodeAt(path, 1) === CHAR_COLON) {
        // Possible device root
        device = StringPrototypeSlice(path, 0, 2);
        rootEnd = 2;
        if (len > 2 && isPathSeparator(StringPrototypeCharCodeAt(path, 2))) {
          // Treat separator following drive name as an absolute path
          // indicator
          isAbsolute = true;
          rootEnd = 3;
        }
      }

      if (device.length > 0) {
        if (resolvedDevice.length > 0) {
          if (StringPrototypeToLowerCase(device) !==
              StringPrototypeToLowerCase(resolvedDevice))
            // This path points to another device so it is not applicable
            continue;
        } else {
          resolvedDevice = device;
        }
      }

      if (resolvedAbsolute) {
        if (resolvedDevice.length > 0)
          break;
      } else {
        resolvedTail =
          `${StringPrototypeSlice(path, rootEnd)}\\${resolvedTail}`;
        resolvedAbsolute = isAbsolute;
        if (isAbsolute && resolvedDevice.length > 0) {
          break;
        }
      }
    }

    // At this point the path should be resolved to a full absolute path,
    // but handle relative paths to be safe (might happen when process.cwd()
    // fails)

    // Normalize the tail path
    resolvedTail = normalizeString(resolvedTail, !resolvedAbsolute, '\\',
                                   isPathSeparator);

    return resolvedAbsolute ?
      `${resolvedDevice}\\${resolvedTail}` :
      `${resolvedDevice}${resolvedTail}` || '.';
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  normalize(path) {
    validateString(path, 'path');
    const len = path.length;
    if (len === 0)
      return '.';
    let rootEnd = 0;
    let device;
    let isAbsolute = false;
    const code = StringPrototypeCharCodeAt(path, 0);

    // Try to match a root
    if (len === 1) {
      // `path` contains just a single char, exit early to avoid
      // unnecessary work
      return isPosixPathSeparator(code) ? '\\' : path;
    }
    if (isPathSeparator(code)) {
      // Possible UNC root

      // If we started with a separator, we know we at least have an absolute
      // path of some kind (UNC or otherwise)
      isAbsolute = true;

      if (isPathSeparator(StringPrototypeCharCodeAt(path, 1))) {
        // Matched double path separator at beginning
        let j = 2;
        let last = j;
        // Match 1 or more non-path separators
        while (j < len &&
               !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
          j++;
        }
        if (j < len && j !== last) {
          const firstPart = StringPrototypeSlice(path, last, j);
          // Matched!
          last = j;
          // Match 1 or more path separators
          while (j < len &&
                 isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
            j++;
          }
          if (j < len && j !== last) {
            // Matched!
            last = j;
            // Match 1 or more non-path separators
            while (j < len &&
                   !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
              j++;
            }
            if (j === len) {
              // We matched a UNC root only
              // Return the normalized version of the UNC root since there
              // is nothing left to process
              return `\\\\${firstPart}\\${StringPrototypeSlice(path, last)}\\`;
            }
            if (j !== last) {
              // We matched a UNC root with leftovers
              device =
                `\\\\${firstPart}\\${StringPrototypeSlice(path, last, j)}`;
              rootEnd = j;
            }
          }
        }
      } else {
        rootEnd = 1;
      }
    } else if (isWindowsDeviceRoot(code) &&
               StringPrototypeCharCodeAt(path, 1) === CHAR_COLON) {
      // Possible device root
      device = StringPrototypeSlice(path, 0, 2);
      rootEnd = 2;
      if (len > 2 && isPathSeparator(StringPrototypeCharCodeAt(path, 2))) {
        // Treat separator following drive name as an absolute path
        // indicator
        isAbsolute = true;
        rootEnd = 3;
      }
    }

    let tail = rootEnd < len ?
      normalizeString(StringPrototypeSlice(path, rootEnd),
                      !isAbsolute, '\\', isPathSeparator) :
      '';
    if (tail.length === 0 && !isAbsolute)
      tail = '.';
    if (tail.length > 0 &&
        isPathSeparator(StringPrototypeCharCodeAt(path, len - 1)))
      tail += '\\';
    if (device === undefined) {
      return isAbsolute ? `\\${tail}` : tail;
    }
    return isAbsolute ? `${device}\\${tail}` : `${device}${tail}`;
  },

  /**
   * @param {string} path
   * @returns {boolean}
   */
  isAbsolute(path) {
    validateString(path, 'path');
    const len = path.length;
    if (len === 0)
      return false;

    const code = StringPrototypeCharCodeAt(path, 0);
    return isPathSeparator(code) ||
      // Possible device root
      (len > 2 &&
      isWindowsDeviceRoot(code) &&
      StringPrototypeCharCodeAt(path, 1) === CHAR_COLON &&
      isPathSeparator(StringPrototypeCharCodeAt(path, 2)));
  },

  /**
   * @param {...string} args
   * @returns {string}
   */
  join(...args) {
    if (args.length === 0)
      return '.';

    let joined;
    let firstPart;
    for (let i = 0; i < args.length; ++i) {
      const arg = args[i];
      validateString(arg, 'path');
      if (arg.length > 0) {
        if (joined === undefined)
          joined = firstPart = arg;
        else
          joined += `\\${arg}`;
      }
    }

    if (joined === undefined)
      return '.';

    // Make sure that the joined path doesn't start with two slashes, because
    // normalize() will mistake it for a UNC path then.
    //
    // This step is skipped when it is very clear that the user actually
    // intended to point at a UNC path. This is assumed when the first
    // non-empty string arguments starts with exactly two slashes followed by
    // at least one more non-slash character.
    //
    // Note that for normalize() to treat a path as a UNC path it needs to
    // have at least 2 components, so we don't filter for that here.
    // This means that the user can use join to construct UNC paths from
    // a server name and a share name; for example:
    //   path.join('//server', 'share') -> '\\\\server\\share\\')
    let needsReplace = true;
    let slashCount = 0;
    if (isPathSeparator(StringPrototypeCharCodeAt(firstPart, 0))) {
      ++slashCount;
      const firstLen = firstPart.length;
      if (firstLen > 1 &&
          isPathSeparator(StringPrototypeCharCodeAt(firstPart, 1))) {
        ++slashCount;
        if (firstLen > 2) {
          if (isPathSeparator(StringPrototypeCharCodeAt(firstPart, 2)))
            ++slashCount;
          else {
            // We matched a UNC path in the first part
            needsReplace = false;
          }
        }
      }
    }
    if (needsReplace) {
      // Find any more consecutive slashes we need to replace
      while (slashCount < joined.length &&
             isPathSeparator(StringPrototypeCharCodeAt(joined, slashCount))) {
        slashCount++;
      }

      // Replace the slashes if needed
      if (slashCount >= 2)
        joined = `\\${StringPrototypeSlice(joined, slashCount)}`;
    }

    return win32.normalize(joined);
  },

  /**
   * It will solve the relative path from `from` to `to`, for instance
   * from = 'C:\\orandea\\test\\aaa'
   * to = 'C:\\orandea\\impl\\bbb'
   * The output of the function should be: '..\\..\\impl\\bbb'
   * @param {string} from
   * @param {string} to
   * @returns {string}
   */
  relative(from, to) {
    validateString(from, 'from');
    validateString(to, 'to');

    if (from === to)
      return '';

    const fromOrig = win32.resolve(from);
    const toOrig = win32.resolve(to);

    if (fromOrig === toOrig)
      return '';

    from = StringPrototypeToLowerCase(fromOrig);
    to = StringPrototypeToLowerCase(toOrig);

    if (from === to)
      return '';

    if (fromOrig.length !== from.length || toOrig.length !== to.length) {
      const fromSplit = StringPrototypeSplit(fromOrig, '\\');
      const toSplit = StringPrototypeSplit(toOrig, '\\');
      if (fromSplit[fromSplit.length - 1] === '') {
        fromSplit.pop();
      }
      if (toSplit[toSplit.length - 1] === '') {
        toSplit.pop();
      }

      const fromLen = fromSplit.length;
      const toLen = toSplit.length;
      const length = fromLen < toLen ? fromLen : toLen;

      let i;
      for (i = 0; i < length; i++) {
        if (StringPrototypeToLowerCase(fromSplit[i]) !== StringPrototypeToLowerCase(toSplit[i])) {
          break;
        }
      }

      if (i === 0) {
        return toOrig;
      } else if (i === length) {
        if (toLen > length) {
          return ArrayPrototypeJoin(ArrayPrototypeSlice(toSplit, i), '\\');
        }
        if (fromLen > length) {
          return StringPrototypeRepeat('..\\', fromLen - 1 - i) + '..';
        }
        return '';
      }

      return StringPrototypeRepeat('..\\', fromLen - i) + ArrayPrototypeJoin(ArrayPrototypeSlice(toSplit, i), '\\');
    }

    // Trim any leading backslashes
    let fromStart = 0;
    while (fromStart < from.length &&
           StringPrototypeCharCodeAt(from, fromStart) === CHAR_BACKWARD_SLASH) {
      fromStart++;
    }
    // Trim trailing backslashes (applicable to UNC paths only)
    let fromEnd = from.length;
    while (
      fromEnd - 1 > fromStart &&
      StringPrototypeCharCodeAt(from, fromEnd - 1) === CHAR_BACKWARD_SLASH
    ) {
      fromEnd--;
    }
    const fromLen = fromEnd - fromStart;

    // Trim any leading backslashes
    let toStart = 0;
    while (toStart < to.length &&
           StringPrototypeCharCodeAt(to, toStart) === CHAR_BACKWARD_SLASH) {
      toStart++;
    }
    // Trim trailing backslashes (applicable to UNC paths only)
    let toEnd = to.length;
    while (toEnd - 1 > toStart &&
           StringPrototypeCharCodeAt(to, toEnd - 1) === CHAR_BACKWARD_SLASH) {
      toEnd--;
    }
    const toLen = toEnd - toStart;

    // Compare paths to find the longest common path from root
    const length = fromLen < toLen ? fromLen : toLen;
    let lastCommonSep = -1;
    let i = 0;
    for (; i < length; i++) {
      const fromCode = StringPrototypeCharCodeAt(from, fromStart + i);
      if (fromCode !== StringPrototypeCharCodeAt(to, toStart + i))
        break;
      else if (fromCode === CHAR_BACKWARD_SLASH)
        lastCommonSep = i;
    }

    // We found a mismatch before the first common path separator was seen, so
    // return the original `to`.
    if (i !== length) {
      if (lastCommonSep === -1)
        return toOrig;
    } else {
      if (toLen > length) {
        if (StringPrototypeCharCodeAt(to, toStart + i) ===
            CHAR_BACKWARD_SLASH) {
          // We get here if `from` is the exact base path for `to`.
          // For example: from='C:\\foo\\bar'; to='C:\\foo\\bar\\baz'
          return StringPrototypeSlice(toOrig, toStart + i + 1);
        }
        if (i === 2) {
          // We get here if `from` is the device root.
          // For example: from='C:\\'; to='C:\\foo'
          return StringPrototypeSlice(toOrig, toStart + i);
        }
      }
      if (fromLen > length) {
        if (StringPrototypeCharCodeAt(from, fromStart + i) ===
            CHAR_BACKWARD_SLASH) {
          // We get here if `to` is the exact base path for `from`.
          // For example: from='C:\\foo\\bar'; to='C:\\foo'
          lastCommonSep = i;
        } else if (i === 2) {
          // We get here if `to` is the device root.
          // For example: from='C:\\foo\\bar'; to='C:\\'
          lastCommonSep = 3;
        }
      }
      if (lastCommonSep === -1)
        lastCommonSep = 0;
    }

    let out = '';
    // Generate the relative path based on the path difference between `to` and
    // `from`
    for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {
      if (i === fromEnd ||
          StringPrototypeCharCodeAt(from, i) === CHAR_BACKWARD_SLASH) {
        out += out.length === 0 ? '..' : '\\..';
      }
    }

    toStart += lastCommonSep;

    // Lastly, append the rest of the destination (`to`) path that comes after
    // the common path parts
    if (out.length > 0)
      return `${out}${StringPrototypeSlice(toOrig, toStart, toEnd)}`;

    if (StringPrototypeCharCodeAt(toOrig, toStart) === CHAR_BACKWARD_SLASH)
      ++toStart;
    return StringPrototypeSlice(toOrig, toStart, toEnd);
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  toNamespacedPath(path) {
    // Note: this will *probably* throw somewhere.
    if (typeof path !== 'string' || path.length === 0)
      return path;

    const resolvedPath = win32.resolve(path);

    if (resolvedPath.length <= 2)
      return path;

    if (StringPrototypeCharCodeAt(resolvedPath, 0) === CHAR_BACKWARD_SLASH) {
      // Possible UNC root
      if (StringPrototypeCharCodeAt(resolvedPath, 1) === CHAR_BACKWARD_SLASH) {
        const code = StringPrototypeCharCodeAt(resolvedPath, 2);
        if (code !== CHAR_QUESTION_MARK && code !== CHAR_DOT) {
          // Matched non-long UNC root, convert the path to a long UNC path
          return `\\\\?\\UNC\\${StringPrototypeSlice(resolvedPath, 2)}`;
        }
      }
    } else if (
      isWindowsDeviceRoot(StringPrototypeCharCodeAt(resolvedPath, 0)) &&
      StringPrototypeCharCodeAt(resolvedPath, 1) === CHAR_COLON &&
      StringPrototypeCharCodeAt(resolvedPath, 2) === CHAR_BACKWARD_SLASH
    ) {
      // Matched device root, convert the path to a long UNC path
      return `\\\\?\\${resolvedPath}`;
    }

    return resolvedPath;
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  dirname(path) {
    validateString(path, 'path');
    const len = path.length;
    if (len === 0)
      return '.';
    let rootEnd = -1;
    let offset = 0;
    const code = StringPrototypeCharCodeAt(path, 0);

    if (len === 1) {
      // `path` contains just a path separator, exit early to avoid
      // unnecessary work or a dot.
      return isPathSeparator(code) ? path : '.';
    }

    // Try to match a root
    if (isPathSeparator(code)) {
      // Possible UNC root

      rootEnd = offset = 1;

      if (isPathSeparator(StringPrototypeCharCodeAt(path, 1))) {
        // Matched double path separator at beginning
        let j = 2;
        let last = j;
        // Match 1 or more non-path separators
        while (j < len &&
               !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
          j++;
        }
        if (j < len && j !== last) {
          // Matched!
          last = j;
          // Match 1 or more path separators
          while (j < len &&
                 isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
            j++;
          }
          if (j < len && j !== last) {
            // Matched!
            last = j;
            // Match 1 or more non-path separators
            while (j < len &&
                   !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
              j++;
            }
            if (j === len) {
              // We matched a UNC root only
              return path;
            }
            if (j !== last) {
              // We matched a UNC root with leftovers

              // Offset by 1 to include the separator after the UNC root to
              // treat it as a "normal root" on top of a (UNC) root
              rootEnd = offset = j + 1;
            }
          }
        }
      }
    // Possible device root
    } else if (isWindowsDeviceRoot(code) &&
               StringPrototypeCharCodeAt(path, 1) === CHAR_COLON) {
      rootEnd =
        len > 2 && isPathSeparator(StringPrototypeCharCodeAt(path, 2)) ? 3 : 2;
      offset = rootEnd;
    }

    let end = -1;
    let matchedSlash = true;
    for (let i = len - 1; i >= offset; --i) {
      if (isPathSeparator(StringPrototypeCharCodeAt(path, i))) {
        if (!matchedSlash) {
          end = i;
          break;
        }
      } else {
        // We saw the first non-path separator
        matchedSlash = false;
      }
    }

    if (end === -1) {
      if (rootEnd === -1)
        return '.';

      end = rootEnd;
    }
    return StringPrototypeSlice(path, 0, end);
  },

  /**
   * @param {string} path
   * @param {string} [suffix]
   * @returns {string}
   */
  basename(path, suffix) {
    if (suffix !== undefined)
      validateString(suffix, 'suffix');
    validateString(path, 'path');
    let start = 0;
    let end = -1;
    let matchedSlash = true;

    // Check for a drive letter prefix so as not to mistake the following
    // path separator as an extra separator at the end of the path that can be
    // disregarded
    if (path.length >= 2 &&
        isWindowsDeviceRoot(StringPrototypeCharCodeAt(path, 0)) &&
        StringPrototypeCharCodeAt(path, 1) === CHAR_COLON) {
      start = 2;
    }

    if (suffix !== undefined && suffix.length > 0 && suffix.length <= path.length) {
      if (suffix === path)
        return '';
      let extIdx = suffix.length - 1;
      let firstNonSlashEnd = -1;
      for (let i = path.length - 1; i >= start; --i) {
        const code = StringPrototypeCharCodeAt(path, i);
        if (isPathSeparator(code)) {
          // If we reached a path separator that was not part of a set of path
          // separators at the end of the string, stop now
          if (!matchedSlash) {
            start = i + 1;
            break;
          }
        } else {
          if (firstNonSlashEnd === -1) {
            // We saw the first non-path separator, remember this index in case
            // we need it if the extension ends up not matching
            matchedSlash = false;
            firstNonSlashEnd = i + 1;
          }
          if (extIdx >= 0) {
            // Try to match the explicit extension
            if (code === StringPrototypeCharCodeAt(suffix, extIdx)) {
              if (--extIdx === -1) {
                // We matched the extension, so mark this as the end of our path
                // component
                end = i;
              }
            } else {
              // Extension does not match, so our result is the entire path
              // component
              extIdx = -1;
              end = firstNonSlashEnd;
            }
          }
        }
      }

      if (start === end)
        end = firstNonSlashEnd;
      else if (end === -1)
        end = path.length;
      return StringPrototypeSlice(path, start, end);
    }
    for (let i = path.length - 1; i >= start; --i) {
      if (isPathSeparator(StringPrototypeCharCodeAt(path, i))) {
        // If we reached a path separator that was not part of a set of path
        // separators at the end of the string, stop now
        if (!matchedSlash) {
          start = i + 1;
          break;
        }
      } else if (end === -1) {
        // We saw the first non-path separator, mark this as the end of our
        // path component
        matchedSlash = false;
        end = i + 1;
      }
    }

    if (end === -1)
      return '';
    return StringPrototypeSlice(path, start, end);
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  extname(path) {
    validateString(path, 'path');
    let start = 0;
    let startDot = -1;
    let startPart = 0;
    let end = -1;
    let matchedSlash = true;
    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    let preDotState = 0;

    // Check for a drive letter prefix so as not to mistake the following
    // path separator as an extra separator at the end of the path that can be
    // disregarded

    if (path.length >= 2 &&
        StringPrototypeCharCodeAt(path, 1) === CHAR_COLON &&
        isWindowsDeviceRoot(StringPrototypeCharCodeAt(path, 0))) {
      start = startPart = 2;
    }

    for (let i = path.length - 1; i >= start; --i) {
      const code = StringPrototypeCharCodeAt(path, i);
      if (isPathSeparator(code)) {
        // If we reached a path separator that was not part of a set of path
        // separators at the end of the string, stop now
        if (!matchedSlash) {
          startPart = i + 1;
          break;
        }
        continue;
      }
      if (end === -1) {
        // We saw the first non-path separator, mark this as the end of our
        // extension
        matchedSlash = false;
        end = i + 1;
      }
      if (code === CHAR_DOT) {
        // If this is our first dot, mark it as the start of our extension
        if (startDot === -1)
          startDot = i;
        else if (preDotState !== 1)
          preDotState = 1;
      } else if (startDot !== -1) {
        // We saw a non-dot and non-path separator before our dot, so we should
        // have a good chance at having a non-empty extension
        preDotState = -1;
      }
    }

    if (startDot === -1 ||
        end === -1 ||
        // We saw a non-dot character immediately before the dot
        preDotState === 0 ||
        // The (right-most) trimmed path component is exactly '..'
        (preDotState === 1 &&
         startDot === end - 1 &&
         startDot === startPart + 1)) {
      return '';
    }
    return StringPrototypeSlice(path, startDot, end);
  },

  format: FunctionPrototypeBind(_format, null, '\\'),

  /**
   * @param {string} path
   * @returns {{
   *  dir: string;
   *  root: string;
   *  base: string;
   *  name: string;
   *  ext: string;
   *  }}
   */
  parse(path) {
    validateString(path, 'path');

    const ret = { root: '', dir: '', base: '', ext: '', name: '' };
    if (path.length === 0)
      return ret;

    const len = path.length;
    let rootEnd = 0;
    let code = StringPrototypeCharCodeAt(path, 0);

    if (len === 1) {
      if (isPathSeparator(code)) {
        // `path` contains just a path separator, exit early to avoid
        // unnecessary work
        ret.root = ret.dir = path;
        return ret;
      }
      ret.base = ret.name = path;
      return ret;
    }
    // Try to match a root
    if (isPathSeparator(code)) {
      // Possible UNC root

      rootEnd = 1;
      if (isPathSeparator(StringPrototypeCharCodeAt(path, 1))) {
        // Matched double path separator at beginning
        let j = 2;
        let last = j;
        // Match 1 or more non-path separators
        while (j < len &&
               !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
          j++;
        }
        if (j < len && j !== last) {
          // Matched!
          last = j;
          // Match 1 or more path separators
          while (j < len &&
                 isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
            j++;
          }
          if (j < len && j !== last) {
            // Matched!
            last = j;
            // Match 1 or more non-path separators
            while (j < len &&
                   !isPathSeparator(StringPrototypeCharCodeAt(path, j))) {
              j++;
            }
            if (j === len) {
              // We matched a UNC root only
              rootEnd = j;
            } else if (j !== last) {
              // We matched a UNC root with leftovers
              rootEnd = j + 1;
            }
          }
        }
      }
    } else if (isWindowsDeviceRoot(code) &&
               StringPrototypeCharCodeAt(path, 1) === CHAR_COLON) {
      // Possible device root
      if (len <= 2) {
        // `path` contains just a drive root, exit early to avoid
        // unnecessary work
        ret.root = ret.dir = path;
        return ret;
      }
      rootEnd = 2;
      if (isPathSeparator(StringPrototypeCharCodeAt(path, 2))) {
        if (len === 3) {
          // `path` contains just a drive root, exit early to avoid
          // unnecessary work
          ret.root = ret.dir = path;
          return ret;
        }
        rootEnd = 3;
      }
    }
    if (rootEnd > 0)
      ret.root = StringPrototypeSlice(path, 0, rootEnd);

    let startDot = -1;
    let startPart = rootEnd;
    let end = -1;
    let matchedSlash = true;
    let i = path.length - 1;

    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    let preDotState = 0;

    // Get non-dir info
    for (; i >= rootEnd; --i) {
      code = StringPrototypeCharCodeAt(path, i);
      if (isPathSeparator(code)) {
        // If we reached a path separator that was not part of a set of path
        // separators at the end of the string, stop now
        if (!matchedSlash) {
          startPart = i + 1;
          break;
        }
        continue;
      }
      if (end === -1) {
        // We saw the first non-path separator, mark this as the end of our
        // extension
        matchedSlash = false;
        end = i + 1;
      }
      if (code === CHAR_DOT) {
        // If this is our first dot, mark it as the start of our extension
        if (startDot === -1)
          startDot = i;
        else if (preDotState !== 1)
          preDotState = 1;
      } else if (startDot !== -1) {
        // We saw a non-dot and non-path separator before our dot, so we should
        // have a good chance at having a non-empty extension
        preDotState = -1;
      }
    }

    if (end !== -1) {
      if (startDot === -1 ||
          // We saw a non-dot character immediately before the dot
          preDotState === 0 ||
          // The (right-most) trimmed path component is exactly '..'
          (preDotState === 1 &&
           startDot === end - 1 &&
           startDot === startPart + 1)) {
        ret.base = ret.name = StringPrototypeSlice(path, startPart, end);
      } else {
        ret.name = StringPrototypeSlice(path, startPart, startDot);
        ret.base = StringPrototypeSlice(path, startPart, end);
        ret.ext = StringPrototypeSlice(path, startDot, end);
      }
    }

    // If the directory is the root, use the entire root as the `dir` including
    // the trailing slash if any (`C:\abc` -> `C:\`). Otherwise, strip out the
    // trailing slash (`C:\abc\def` -> `C:\abc`).
    if (startPart > 0 && startPart !== rootEnd)
      ret.dir = StringPrototypeSlice(path, 0, startPart - 1);
    else
      ret.dir = ret.root;

    return ret;
  },

  matchesGlob(path, pattern) {
    return glob(path, pattern, true);
  },

  sep: '\\',
  delimiter: ';',
  win32: null,
  posix: null,
};

const posixCwd = (() => {
  if (isWindows) {
    // Converts Windows' backslash path separators to POSIX forward slashes
    // and truncates any drive indicator
    const regexp = /\\/g;
    return () => {
      const cwd = StringPrototypeReplace(process.cwd(), regexp, '/');
      return StringPrototypeSlice(cwd, StringPrototypeIndexOf(cwd, '/'));
    };
  }

  // We're already on POSIX, no need for any transformations
  return () => process.cwd();
})();

const posix = {
  /**
   * path.resolve([from ...], to)
   * @param {...string} args
   * @returns {string}
   */
  resolve(...args) {
    let resolvedPath = '';
    let resolvedAbsolute = false;

    for (let i = args.length - 1; i >= -1 && !resolvedAbsolute; i--) {
      const path = i >= 0 ? args[i] : posixCwd();
      validateString(path, `paths[${i}]`);

      // Skip empty entries
      if (path.length === 0) {
        continue;
      }

      resolvedPath = `${path}/${resolvedPath}`;
      resolvedAbsolute =
        StringPrototypeCharCodeAt(path, 0) === CHAR_FORWARD_SLASH;
    }

    // At this point the path should be resolved to a full absolute path, but
    // handle relative paths to be safe (might happen when process.cwd() fails)

    // Normalize the path
    resolvedPath = normalizeString(resolvedPath, !resolvedAbsolute, '/',
                                   isPosixPathSeparator);

    if (resolvedAbsolute) {
      return `/${resolvedPath}`;
    }
    return resolvedPath.length > 0 ? resolvedPath : '.';
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  normalize(path) {
    validateString(path, 'path');

    if (path.length === 0)
      return '.';

    const isAbsolute =
      StringPrototypeCharCodeAt(path, 0) === CHAR_FORWARD_SLASH;
    const trailingSeparator =
      StringPrototypeCharCodeAt(path, path.length - 1) === CHAR_FORWARD_SLASH;

    // Normalize the path
    path = normalizeString(path, !isAbsolute, '/', isPosixPathSeparator);

    if (path.length === 0) {
      if (isAbsolute)
        return '/';
      return trailingSeparator ? './' : '.';
    }
    if (trailingSeparator)
      path += '/';

    return isAbsolute ? `/${path}` : path;
  },

  /**
   * @param {string} path
   * @returns {boolean}
   */
  isAbsolute(path) {
    validateString(path, 'path');
    return path.length > 0 &&
           StringPrototypeCharCodeAt(path, 0) === CHAR_FORWARD_SLASH;
  },

  /**
   * @param {...string} args
   * @returns {string}
   */
  join(...args) {
    if (args.length === 0)
      return '.';

    const path = [];
    for (let i = 0; i < args.length; ++i) {
      const arg = args[i];
      validateString(arg, 'path');
      if (arg.length > 0) {
        path.push(arg);
      }
    }

    if (path.length === 0)
      return '.';

    return posix.normalize(ArrayPrototypeJoin(path, '/'));
  },

  /**
   * @param {string} from
   * @param {string} to
   * @returns {string}
   */
  relative(from, to) {
    validateString(from, 'from');
    validateString(to, 'to');

    if (from === to)
      return '';

    // Trim leading forward slashes.
    from = posix.resolve(from);
    to = posix.resolve(to);

    if (from === to)
      return '';

    const fromStart = 1;
    const fromEnd = from.length;
    const fromLen = fromEnd - fromStart;
    const toStart = 1;
    const toLen = to.length - toStart;

    // Compare paths to find the longest common path from root
    const length = (fromLen < toLen ? fromLen : toLen);
    let lastCommonSep = -1;
    let i = 0;
    for (; i < length; i++) {
      const fromCode = StringPrototypeCharCodeAt(from, fromStart + i);
      if (fromCode !== StringPrototypeCharCodeAt(to, toStart + i))
        break;
      else if (fromCode === CHAR_FORWARD_SLASH)
        lastCommonSep = i;
    }
    if (i === length) {
      if (toLen > length) {
        if (StringPrototypeCharCodeAt(to, toStart + i) === CHAR_FORWARD_SLASH) {
          // We get here if `from` is the exact base path for `to`.
          // For example: from='/foo/bar'; to='/foo/bar/baz'
          return StringPrototypeSlice(to, toStart + i + 1);
        }
        if (i === 0) {
          // We get here if `from` is the root
          // For example: from='/'; to='/foo'
          return StringPrototypeSlice(to, toStart + i);
        }
      } else if (fromLen > length) {
        if (StringPrototypeCharCodeAt(from, fromStart + i) ===
            CHAR_FORWARD_SLASH) {
          // We get here if `to` is the exact base path for `from`.
          // For example: from='/foo/bar/baz'; to='/foo/bar'
          lastCommonSep = i;
        } else if (i === 0) {
          // We get here if `to` is the root.
          // For example: from='/foo/bar'; to='/'
          lastCommonSep = 0;
        }
      }
    }

    let out = '';
    // Generate the relative path based on the path difference between `to`
    // and `from`.
    for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {
      if (i === fromEnd ||
          StringPrototypeCharCodeAt(from, i) === CHAR_FORWARD_SLASH) {
        out += out.length === 0 ? '..' : '/..';
      }
    }

    // Lastly, append the rest of the destination (`to`) path that comes after
    // the common path parts.
    return `${out}${StringPrototypeSlice(to, toStart + lastCommonSep)}`;
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  toNamespacedPath(path) {
    // Non-op on posix systems
    return path;
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  dirname(path) {
    validateString(path, 'path');
    if (path.length === 0)
      return '.';
    const hasRoot = StringPrototypeCharCodeAt(path, 0) === CHAR_FORWARD_SLASH;
    let end = -1;
    let matchedSlash = true;
    for (let i = path.length - 1; i >= 1; --i) {
      if (StringPrototypeCharCodeAt(path, i) === CHAR_FORWARD_SLASH) {
        if (!matchedSlash) {
          end = i;
          break;
        }
      } else {
        // We saw the first non-path separator
        matchedSlash = false;
      }
    }

    if (end === -1)
      return hasRoot ? '/' : '.';
    if (hasRoot && end === 1)
      return '//';
    return StringPrototypeSlice(path, 0, end);
  },

  /**
   * @param {string} path
   * @param {string} [suffix]
   * @returns {string}
   */
  basename(path, suffix) {
    if (suffix !== undefined)
      validateString(suffix, 'suffix');
    validateString(path, 'path');

    let start = 0;
    let end = -1;
    let matchedSlash = true;

    if (suffix !== undefined && suffix.length > 0 && suffix.length <= path.length) {
      if (suffix === path)
        return '';
      let extIdx = suffix.length - 1;
      let firstNonSlashEnd = -1;
      for (let i = path.length - 1; i >= 0; --i) {
        const code = StringPrototypeCharCodeAt(path, i);
        if (code === CHAR_FORWARD_SLASH) {
          // If we reached a path separator that was not part of a set of path
          // separators at the end of the string, stop now
          if (!matchedSlash) {
            start = i + 1;
            break;
          }
        } else {
          if (firstNonSlashEnd === -1) {
            // We saw the first non-path separator, remember this index in case
            // we need it if the extension ends up not matching
            matchedSlash = false;
            firstNonSlashEnd = i + 1;
          }
          if (extIdx >= 0) {
            // Try to match the explicit extension
            if (code === StringPrototypeCharCodeAt(suffix, extIdx)) {
              if (--extIdx === -1) {
                // We matched the extension, so mark this as the end of our path
                // component
                end = i;
              }
            } else {
              // Extension does not match, so our result is the entire path
              // component
              extIdx = -1;
              end = firstNonSlashEnd;
            }
          }
        }
      }

      if (start === end)
        end = firstNonSlashEnd;
      else if (end === -1)
        end = path.length;
      return StringPrototypeSlice(path, start, end);
    }
    for (let i = path.length - 1; i >= 0; --i) {
      if (StringPrototypeCharCodeAt(path, i) === CHAR_FORWARD_SLASH) {
        // If we reached a path separator that was not part of a set of path
        // separators at the end of the string, stop now
        if (!matchedSlash) {
          start = i + 1;
          break;
        }
      } else if (end === -1) {
        // We saw the first non-path separator, mark this as the end of our
        // path component
        matchedSlash = false;
        end = i + 1;
      }
    }

    if (end === -1)
      return '';
    return StringPrototypeSlice(path, start, end);
  },

  /**
   * @param {string} path
   * @returns {string}
   */
  extname(path) {
    validateString(path, 'path');
    let startDot = -1;
    let startPart = 0;
    let end = -1;
    let matchedSlash = true;
    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    let preDotState = 0;
    for (let i = path.length - 1; i >= 0; --i) {
      const code = StringPrototypeCharCodeAt(path, i);
      if (code === CHAR_FORWARD_SLASH) {
        // If we reached a path separator that was not part of a set of path
        // separators at the end of the string, stop now
        if (!matchedSlash) {
          startPart = i + 1;
          break;
        }
        continue;
      }
      if (end === -1) {
        // We saw the first non-path separator, mark this as the end of our
        // extension
        matchedSlash = false;
        end = i + 1;
      }
      if (code === CHAR_DOT) {
        // If this is our first dot, mark it as the start of our extension
        if (startDot === -1)
          startDot = i;
        else if (preDotState !== 1)
          preDotState = 1;
      } else if (startDot !== -1) {
        // We saw a non-dot and non-path separator before our dot, so we should
        // have a good chance at having a non-empty extension
        preDotState = -1;
      }
    }

    if (startDot === -1 ||
        end === -1 ||
        // We saw a non-dot character immediately before the dot
        preDotState === 0 ||
        // The (right-most) trimmed path component is exactly '..'
        (preDotState === 1 &&
         startDot === end - 1 &&
         startDot === startPart + 1)) {
      return '';
    }
    return StringPrototypeSlice(path, startDot, end);
  },

  format: FunctionPrototypeBind(_format, null, '/'),

  /**
   * @param {string} path
   * @returns {{
   *   dir: string;
   *   root: string;
   *   base: string;
   *   name: string;
   *   ext: string;
   *   }}
   */
  parse(path) {
    validateString(path, 'path');

    const ret = { root: '', dir: '', base: '', ext: '', name: '' };
    if (path.length === 0)
      return ret;
    const isAbsolute =
      StringPrototypeCharCodeAt(path, 0) === CHAR_FORWARD_SLASH;
    let start;
    if (isAbsolute) {
      ret.root = '/';
      start = 1;
    } else {
      start = 0;
    }
    let startDot = -1;
    let startPart = 0;
    let end = -1;
    let matchedSlash = true;
    let i = path.length - 1;

    // Track the state of characters (if any) we see before our first dot and
    // after any path separator we find
    let preDotState = 0;

    // Get non-dir info
    for (; i >= start; --i) {
      const code = StringPrototypeCharCodeAt(path, i);
      if (code === CHAR_FORWARD_SLASH) {
        // If we reached a path separator that was not part of a set of path
        // separators at the end of the string, stop now
        if (!matchedSlash) {
          startPart = i + 1;
          break;
        }
        continue;
      }
      if (end === -1) {
        // We saw the first non-path separator, mark this as the end of our
        // extension
        matchedSlash = false;
        end = i + 1;
      }
      if (code === CHAR_DOT) {
        // If this is our first dot, mark it as the start of our extension
        if (startDot === -1)
          startDot = i;
        else if (preDotState !== 1)
          preDotState = 1;
      } else if (startDot !== -1) {
        // We saw a non-dot and non-path separator before our dot, so we should
        // have a good chance at having a non-empty extension
        preDotState = -1;
      }
    }

    if (end !== -1) {
      const start = startPart === 0 && isAbsolute ? 1 : startPart;
      if (startDot === -1 ||
          // We saw a non-dot character immediately before the dot
          preDotState === 0 ||
          // The (right-most) trimmed path component is exactly '..'
          (preDotState === 1 &&
          startDot === end - 1 &&
          startDot === startPart + 1)) {
        ret.base = ret.name = StringPrototypeSlice(path, start, end);
      } else {
        ret.name = StringPrototypeSlice(path, start, startDot);
        ret.base = StringPrototypeSlice(path, start, end);
        ret.ext = StringPrototypeSlice(path, startDot, end);
      }
    }

    if (startPart > 0)
      ret.dir = StringPrototypeSlice(path, 0, startPart - 1);
    else if (isAbsolute)
      ret.dir = '/';

    return ret;
  },

  matchesGlob(path, pattern) {
    return glob(path, pattern, false);
  },

  sep: '/',
  delimiter: ':',
  win32: null,
  posix: null,
};

posix.win32 = win32.win32 = win32;
posix.posix = win32.posix = posix;

// Legacy internal API, docs-only deprecated: DEP0080
win32._makeLong = win32.toNamespacedPath;
posix._makeLong = posix.toNamespacedPath;

module.exports = isWindows ? win32 : posix;
