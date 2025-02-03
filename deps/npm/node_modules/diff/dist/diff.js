/*!

 diff v7.0.0

BSD 3-Clause License

Copyright (c) 2009-2015, Kevin Decker <kpdecker@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

@license
*/
(function (global, factory) {
  typeof exports === 'object' && typeof module !== 'undefined' ? factory(exports) :
  typeof define === 'function' && define.amd ? define(['exports'], factory) :
  (global = typeof globalThis !== 'undefined' ? globalThis : global || self, factory(global.Diff = {}));
})(this, (function (exports) { 'use strict';

  function Diff() {}
  Diff.prototype = {
    diff: function diff(oldString, newString) {
      var _options$timeout;
      var options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
      var callback = options.callback;
      if (typeof options === 'function') {
        callback = options;
        options = {};
      }
      var self = this;
      function done(value) {
        value = self.postProcess(value, options);
        if (callback) {
          setTimeout(function () {
            callback(value);
          }, 0);
          return true;
        } else {
          return value;
        }
      }

      // Allow subclasses to massage the input prior to running
      oldString = this.castInput(oldString, options);
      newString = this.castInput(newString, options);
      oldString = this.removeEmpty(this.tokenize(oldString, options));
      newString = this.removeEmpty(this.tokenize(newString, options));
      var newLen = newString.length,
        oldLen = oldString.length;
      var editLength = 1;
      var maxEditLength = newLen + oldLen;
      if (options.maxEditLength != null) {
        maxEditLength = Math.min(maxEditLength, options.maxEditLength);
      }
      var maxExecutionTime = (_options$timeout = options.timeout) !== null && _options$timeout !== void 0 ? _options$timeout : Infinity;
      var abortAfterTimestamp = Date.now() + maxExecutionTime;
      var bestPath = [{
        oldPos: -1,
        lastComponent: undefined
      }];

      // Seed editLength = 0, i.e. the content starts with the same values
      var newPos = this.extractCommon(bestPath[0], newString, oldString, 0, options);
      if (bestPath[0].oldPos + 1 >= oldLen && newPos + 1 >= newLen) {
        // Identity per the equality and tokenizer
        return done(buildValues(self, bestPath[0].lastComponent, newString, oldString, self.useLongestToken));
      }

      // Once we hit the right edge of the edit graph on some diagonal k, we can
      // definitely reach the end of the edit graph in no more than k edits, so
      // there's no point in considering any moves to diagonal k+1 any more (from
      // which we're guaranteed to need at least k+1 more edits).
      // Similarly, once we've reached the bottom of the edit graph, there's no
      // point considering moves to lower diagonals.
      // We record this fact by setting minDiagonalToConsider and
      // maxDiagonalToConsider to some finite value once we've hit the edge of
      // the edit graph.
      // This optimization is not faithful to the original algorithm presented in
      // Myers's paper, which instead pointlessly extends D-paths off the end of
      // the edit graph - see page 7 of Myers's paper which notes this point
      // explicitly and illustrates it with a diagram. This has major performance
      // implications for some common scenarios. For instance, to compute a diff
      // where the new text simply appends d characters on the end of the
      // original text of length n, the true Myers algorithm will take O(n+d^2)
      // time while this optimization needs only O(n+d) time.
      var minDiagonalToConsider = -Infinity,
        maxDiagonalToConsider = Infinity;

      // Main worker method. checks all permutations of a given edit length for acceptance.
      function execEditLength() {
        for (var diagonalPath = Math.max(minDiagonalToConsider, -editLength); diagonalPath <= Math.min(maxDiagonalToConsider, editLength); diagonalPath += 2) {
          var basePath = void 0;
          var removePath = bestPath[diagonalPath - 1],
            addPath = bestPath[diagonalPath + 1];
          if (removePath) {
            // No one else is going to attempt to use this value, clear it
            bestPath[diagonalPath - 1] = undefined;
          }
          var canAdd = false;
          if (addPath) {
            // what newPos will be after we do an insertion:
            var addPathNewPos = addPath.oldPos - diagonalPath;
            canAdd = addPath && 0 <= addPathNewPos && addPathNewPos < newLen;
          }
          var canRemove = removePath && removePath.oldPos + 1 < oldLen;
          if (!canAdd && !canRemove) {
            // If this path is a terminal then prune
            bestPath[diagonalPath] = undefined;
            continue;
          }

          // Select the diagonal that we want to branch from. We select the prior
          // path whose position in the old string is the farthest from the origin
          // and does not pass the bounds of the diff graph
          if (!canRemove || canAdd && removePath.oldPos < addPath.oldPos) {
            basePath = self.addToPath(addPath, true, false, 0, options);
          } else {
            basePath = self.addToPath(removePath, false, true, 1, options);
          }
          newPos = self.extractCommon(basePath, newString, oldString, diagonalPath, options);
          if (basePath.oldPos + 1 >= oldLen && newPos + 1 >= newLen) {
            // If we have hit the end of both strings, then we are done
            return done(buildValues(self, basePath.lastComponent, newString, oldString, self.useLongestToken));
          } else {
            bestPath[diagonalPath] = basePath;
            if (basePath.oldPos + 1 >= oldLen) {
              maxDiagonalToConsider = Math.min(maxDiagonalToConsider, diagonalPath - 1);
            }
            if (newPos + 1 >= newLen) {
              minDiagonalToConsider = Math.max(minDiagonalToConsider, diagonalPath + 1);
            }
          }
        }
        editLength++;
      }

      // Performs the length of edit iteration. Is a bit fugly as this has to support the
      // sync and async mode which is never fun. Loops over execEditLength until a value
      // is produced, or until the edit length exceeds options.maxEditLength (if given),
      // in which case it will return undefined.
      if (callback) {
        (function exec() {
          setTimeout(function () {
            if (editLength > maxEditLength || Date.now() > abortAfterTimestamp) {
              return callback();
            }
            if (!execEditLength()) {
              exec();
            }
          }, 0);
        })();
      } else {
        while (editLength <= maxEditLength && Date.now() <= abortAfterTimestamp) {
          var ret = execEditLength();
          if (ret) {
            return ret;
          }
        }
      }
    },
    addToPath: function addToPath(path, added, removed, oldPosInc, options) {
      var last = path.lastComponent;
      if (last && !options.oneChangePerToken && last.added === added && last.removed === removed) {
        return {
          oldPos: path.oldPos + oldPosInc,
          lastComponent: {
            count: last.count + 1,
            added: added,
            removed: removed,
            previousComponent: last.previousComponent
          }
        };
      } else {
        return {
          oldPos: path.oldPos + oldPosInc,
          lastComponent: {
            count: 1,
            added: added,
            removed: removed,
            previousComponent: last
          }
        };
      }
    },
    extractCommon: function extractCommon(basePath, newString, oldString, diagonalPath, options) {
      var newLen = newString.length,
        oldLen = oldString.length,
        oldPos = basePath.oldPos,
        newPos = oldPos - diagonalPath,
        commonCount = 0;
      while (newPos + 1 < newLen && oldPos + 1 < oldLen && this.equals(oldString[oldPos + 1], newString[newPos + 1], options)) {
        newPos++;
        oldPos++;
        commonCount++;
        if (options.oneChangePerToken) {
          basePath.lastComponent = {
            count: 1,
            previousComponent: basePath.lastComponent,
            added: false,
            removed: false
          };
        }
      }
      if (commonCount && !options.oneChangePerToken) {
        basePath.lastComponent = {
          count: commonCount,
          previousComponent: basePath.lastComponent,
          added: false,
          removed: false
        };
      }
      basePath.oldPos = oldPos;
      return newPos;
    },
    equals: function equals(left, right, options) {
      if (options.comparator) {
        return options.comparator(left, right);
      } else {
        return left === right || options.ignoreCase && left.toLowerCase() === right.toLowerCase();
      }
    },
    removeEmpty: function removeEmpty(array) {
      var ret = [];
      for (var i = 0; i < array.length; i++) {
        if (array[i]) {
          ret.push(array[i]);
        }
      }
      return ret;
    },
    castInput: function castInput(value) {
      return value;
    },
    tokenize: function tokenize(value) {
      return Array.from(value);
    },
    join: function join(chars) {
      return chars.join('');
    },
    postProcess: function postProcess(changeObjects) {
      return changeObjects;
    }
  };
  function buildValues(diff, lastComponent, newString, oldString, useLongestToken) {
    // First we convert our linked list of components in reverse order to an
    // array in the right order:
    var components = [];
    var nextComponent;
    while (lastComponent) {
      components.push(lastComponent);
      nextComponent = lastComponent.previousComponent;
      delete lastComponent.previousComponent;
      lastComponent = nextComponent;
    }
    components.reverse();
    var componentPos = 0,
      componentLen = components.length,
      newPos = 0,
      oldPos = 0;
    for (; componentPos < componentLen; componentPos++) {
      var component = components[componentPos];
      if (!component.removed) {
        if (!component.added && useLongestToken) {
          var value = newString.slice(newPos, newPos + component.count);
          value = value.map(function (value, i) {
            var oldValue = oldString[oldPos + i];
            return oldValue.length > value.length ? oldValue : value;
          });
          component.value = diff.join(value);
        } else {
          component.value = diff.join(newString.slice(newPos, newPos + component.count));
        }
        newPos += component.count;

        // Common case
        if (!component.added) {
          oldPos += component.count;
        }
      } else {
        component.value = diff.join(oldString.slice(oldPos, oldPos + component.count));
        oldPos += component.count;
      }
    }
    return components;
  }

  var characterDiff = new Diff();
  function diffChars(oldStr, newStr, options) {
    return characterDiff.diff(oldStr, newStr, options);
  }

  function longestCommonPrefix(str1, str2) {
    var i;
    for (i = 0; i < str1.length && i < str2.length; i++) {
      if (str1[i] != str2[i]) {
        return str1.slice(0, i);
      }
    }
    return str1.slice(0, i);
  }
  function longestCommonSuffix(str1, str2) {
    var i;

    // Unlike longestCommonPrefix, we need a special case to handle all scenarios
    // where we return the empty string since str1.slice(-0) will return the
    // entire string.
    if (!str1 || !str2 || str1[str1.length - 1] != str2[str2.length - 1]) {
      return '';
    }
    for (i = 0; i < str1.length && i < str2.length; i++) {
      if (str1[str1.length - (i + 1)] != str2[str2.length - (i + 1)]) {
        return str1.slice(-i);
      }
    }
    return str1.slice(-i);
  }
  function replacePrefix(string, oldPrefix, newPrefix) {
    if (string.slice(0, oldPrefix.length) != oldPrefix) {
      throw Error("string ".concat(JSON.stringify(string), " doesn't start with prefix ").concat(JSON.stringify(oldPrefix), "; this is a bug"));
    }
    return newPrefix + string.slice(oldPrefix.length);
  }
  function replaceSuffix(string, oldSuffix, newSuffix) {
    if (!oldSuffix) {
      return string + newSuffix;
    }
    if (string.slice(-oldSuffix.length) != oldSuffix) {
      throw Error("string ".concat(JSON.stringify(string), " doesn't end with suffix ").concat(JSON.stringify(oldSuffix), "; this is a bug"));
    }
    return string.slice(0, -oldSuffix.length) + newSuffix;
  }
  function removePrefix(string, oldPrefix) {
    return replacePrefix(string, oldPrefix, '');
  }
  function removeSuffix(string, oldSuffix) {
    return replaceSuffix(string, oldSuffix, '');
  }
  function maximumOverlap(string1, string2) {
    return string2.slice(0, overlapCount(string1, string2));
  }

  // Nicked from https://stackoverflow.com/a/60422853/1709587
  function overlapCount(a, b) {
    // Deal with cases where the strings differ in length
    var startA = 0;
    if (a.length > b.length) {
      startA = a.length - b.length;
    }
    var endB = b.length;
    if (a.length < b.length) {
      endB = a.length;
    }
    // Create a back-reference for each index
    //   that should be followed in case of a mismatch.
    //   We only need B to make these references:
    var map = Array(endB);
    var k = 0; // Index that lags behind j
    map[0] = 0;
    for (var j = 1; j < endB; j++) {
      if (b[j] == b[k]) {
        map[j] = map[k]; // skip over the same character (optional optimisation)
      } else {
        map[j] = k;
      }
      while (k > 0 && b[j] != b[k]) {
        k = map[k];
      }
      if (b[j] == b[k]) {
        k++;
      }
    }
    // Phase 2: use these references while iterating over A
    k = 0;
    for (var i = startA; i < a.length; i++) {
      while (k > 0 && a[i] != b[k]) {
        k = map[k];
      }
      if (a[i] == b[k]) {
        k++;
      }
    }
    return k;
  }

  /**
   * Returns true if the string consistently uses Windows line endings.
   */
  function hasOnlyWinLineEndings(string) {
    return string.includes('\r\n') && !string.startsWith('\n') && !string.match(/[^\r]\n/);
  }

  /**
   * Returns true if the string consistently uses Unix line endings.
   */
  function hasOnlyUnixLineEndings(string) {
    return !string.includes('\r\n') && string.includes('\n');
  }

  // Based on https://en.wikipedia.org/wiki/Latin_script_in_Unicode
  //
  // Ranges and exceptions:
  // Latin-1 Supplement, 0080–00FF
  //  - U+00D7  × Multiplication sign
  //  - U+00F7  ÷ Division sign
  // Latin Extended-A, 0100–017F
  // Latin Extended-B, 0180–024F
  // IPA Extensions, 0250–02AF
  // Spacing Modifier Letters, 02B0–02FF
  //  - U+02C7  ˇ &#711;  Caron
  //  - U+02D8  ˘ &#728;  Breve
  //  - U+02D9  ˙ &#729;  Dot Above
  //  - U+02DA  ˚ &#730;  Ring Above
  //  - U+02DB  ˛ &#731;  Ogonek
  //  - U+02DC  ˜ &#732;  Small Tilde
  //  - U+02DD  ˝ &#733;  Double Acute Accent
  // Latin Extended Additional, 1E00–1EFF
  var extendedWordChars = "a-zA-Z0-9_\\u{C0}-\\u{FF}\\u{D8}-\\u{F6}\\u{F8}-\\u{2C6}\\u{2C8}-\\u{2D7}\\u{2DE}-\\u{2FF}\\u{1E00}-\\u{1EFF}";

  // Each token is one of the following:
  // - A punctuation mark plus the surrounding whitespace
  // - A word plus the surrounding whitespace
  // - Pure whitespace (but only in the special case where this the entire text
  //   is just whitespace)
  //
  // We have to include surrounding whitespace in the tokens because the two
  // alternative approaches produce horribly broken results:
  // * If we just discard the whitespace, we can't fully reproduce the original
  //   text from the sequence of tokens and any attempt to render the diff will
  //   get the whitespace wrong.
  // * If we have separate tokens for whitespace, then in a typical text every
  //   second token will be a single space character. But this often results in
  //   the optimal diff between two texts being a perverse one that preserves
  //   the spaces between words but deletes and reinserts actual common words.
  //   See https://github.com/kpdecker/jsdiff/issues/160#issuecomment-1866099640
  //   for an example.
  //
  // Keeping the surrounding whitespace of course has implications for .equals
  // and .join, not just .tokenize.

  // This regex does NOT fully implement the tokenization rules described above.
  // Instead, it gives runs of whitespace their own "token". The tokenize method
  // then handles stitching whitespace tokens onto adjacent word or punctuation
  // tokens.
  var tokenizeIncludingWhitespace = new RegExp("[".concat(extendedWordChars, "]+|\\s+|[^").concat(extendedWordChars, "]"), 'ug');
  var wordDiff = new Diff();
  wordDiff.equals = function (left, right, options) {
    if (options.ignoreCase) {
      left = left.toLowerCase();
      right = right.toLowerCase();
    }
    return left.trim() === right.trim();
  };
  wordDiff.tokenize = function (value) {
    var options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
    var parts;
    if (options.intlSegmenter) {
      if (options.intlSegmenter.resolvedOptions().granularity != 'word') {
        throw new Error('The segmenter passed must have a granularity of "word"');
      }
      parts = Array.from(options.intlSegmenter.segment(value), function (segment) {
        return segment.segment;
      });
    } else {
      parts = value.match(tokenizeIncludingWhitespace) || [];
    }
    var tokens = [];
    var prevPart = null;
    parts.forEach(function (part) {
      if (/\s/.test(part)) {
        if (prevPart == null) {
          tokens.push(part);
        } else {
          tokens.push(tokens.pop() + part);
        }
      } else if (/\s/.test(prevPart)) {
        if (tokens[tokens.length - 1] == prevPart) {
          tokens.push(tokens.pop() + part);
        } else {
          tokens.push(prevPart + part);
        }
      } else {
        tokens.push(part);
      }
      prevPart = part;
    });
    return tokens;
  };
  wordDiff.join = function (tokens) {
    // Tokens being joined here will always have appeared consecutively in the
    // same text, so we can simply strip off the leading whitespace from all the
    // tokens except the first (and except any whitespace-only tokens - but such
    // a token will always be the first and only token anyway) and then join them
    // and the whitespace around words and punctuation will end up correct.
    return tokens.map(function (token, i) {
      if (i == 0) {
        return token;
      } else {
        return token.replace(/^\s+/, '');
      }
    }).join('');
  };
  wordDiff.postProcess = function (changes, options) {
    if (!changes || options.oneChangePerToken) {
      return changes;
    }
    var lastKeep = null;
    // Change objects representing any insertion or deletion since the last
    // "keep" change object. There can be at most one of each.
    var insertion = null;
    var deletion = null;
    changes.forEach(function (change) {
      if (change.added) {
        insertion = change;
      } else if (change.removed) {
        deletion = change;
      } else {
        if (insertion || deletion) {
          // May be false at start of text
          dedupeWhitespaceInChangeObjects(lastKeep, deletion, insertion, change);
        }
        lastKeep = change;
        insertion = null;
        deletion = null;
      }
    });
    if (insertion || deletion) {
      dedupeWhitespaceInChangeObjects(lastKeep, deletion, insertion, null);
    }
    return changes;
  };
  function diffWords(oldStr, newStr, options) {
    // This option has never been documented and never will be (it's clearer to
    // just call `diffWordsWithSpace` directly if you need that behavior), but
    // has existed in jsdiff for a long time, so we retain support for it here
    // for the sake of backwards compatibility.
    if ((options === null || options === void 0 ? void 0 : options.ignoreWhitespace) != null && !options.ignoreWhitespace) {
      return diffWordsWithSpace(oldStr, newStr, options);
    }
    return wordDiff.diff(oldStr, newStr, options);
  }
  function dedupeWhitespaceInChangeObjects(startKeep, deletion, insertion, endKeep) {
    // Before returning, we tidy up the leading and trailing whitespace of the
    // change objects to eliminate cases where trailing whitespace in one object
    // is repeated as leading whitespace in the next.
    // Below are examples of the outcomes we want here to explain the code.
    // I=insert, K=keep, D=delete
    // 1. diffing 'foo bar baz' vs 'foo baz'
    //    Prior to cleanup, we have K:'foo ' D:' bar ' K:' baz'
    //    After cleanup, we want:   K:'foo ' D:'bar ' K:'baz'
    //
    // 2. Diffing 'foo bar baz' vs 'foo qux baz'
    //    Prior to cleanup, we have K:'foo ' D:' bar ' I:' qux ' K:' baz'
    //    After cleanup, we want K:'foo ' D:'bar' I:'qux' K:' baz'
    //
    // 3. Diffing 'foo\nbar baz' vs 'foo baz'
    //    Prior to cleanup, we have K:'foo ' D:'\nbar ' K:' baz'
    //    After cleanup, we want K'foo' D:'\nbar' K:' baz'
    //
    // 4. Diffing 'foo baz' vs 'foo\nbar baz'
    //    Prior to cleanup, we have K:'foo\n' I:'\nbar ' K:' baz'
    //    After cleanup, we ideally want K'foo' I:'\nbar' K:' baz'
    //    but don't actually manage this currently (the pre-cleanup change
    //    objects don't contain enough information to make it possible).
    //
    // 5. Diffing 'foo   bar baz' vs 'foo  baz'
    //    Prior to cleanup, we have K:'foo  ' D:'   bar ' K:'  baz'
    //    After cleanup, we want K:'foo  ' D:' bar ' K:'baz'
    //
    // Our handling is unavoidably imperfect in the case where there's a single
    // indel between keeps and the whitespace has changed. For instance, consider
    // diffing 'foo\tbar\nbaz' vs 'foo baz'. Unless we create an extra change
    // object to represent the insertion of the space character (which isn't even
    // a token), we have no way to avoid losing information about the texts'
    // original whitespace in the result we return. Still, we do our best to
    // output something that will look sensible if we e.g. print it with
    // insertions in green and deletions in red.

    // Between two "keep" change objects (or before the first or after the last
    // change object), we can have either:
    // * A "delete" followed by an "insert"
    // * Just an "insert"
    // * Just a "delete"
    // We handle the three cases separately.
    if (deletion && insertion) {
      var oldWsPrefix = deletion.value.match(/^\s*/)[0];
      var oldWsSuffix = deletion.value.match(/\s*$/)[0];
      var newWsPrefix = insertion.value.match(/^\s*/)[0];
      var newWsSuffix = insertion.value.match(/\s*$/)[0];
      if (startKeep) {
        var commonWsPrefix = longestCommonPrefix(oldWsPrefix, newWsPrefix);
        startKeep.value = replaceSuffix(startKeep.value, newWsPrefix, commonWsPrefix);
        deletion.value = removePrefix(deletion.value, commonWsPrefix);
        insertion.value = removePrefix(insertion.value, commonWsPrefix);
      }
      if (endKeep) {
        var commonWsSuffix = longestCommonSuffix(oldWsSuffix, newWsSuffix);
        endKeep.value = replacePrefix(endKeep.value, newWsSuffix, commonWsSuffix);
        deletion.value = removeSuffix(deletion.value, commonWsSuffix);
        insertion.value = removeSuffix(insertion.value, commonWsSuffix);
      }
    } else if (insertion) {
      // The whitespaces all reflect what was in the new text rather than
      // the old, so we essentially have no information about whitespace
      // insertion or deletion. We just want to dedupe the whitespace.
      // We do that by having each change object keep its trailing
      // whitespace and deleting duplicate leading whitespace where
      // present.
      if (startKeep) {
        insertion.value = insertion.value.replace(/^\s*/, '');
      }
      if (endKeep) {
        endKeep.value = endKeep.value.replace(/^\s*/, '');
      }
      // otherwise we've got a deletion and no insertion
    } else if (startKeep && endKeep) {
      var newWsFull = endKeep.value.match(/^\s*/)[0],
        delWsStart = deletion.value.match(/^\s*/)[0],
        delWsEnd = deletion.value.match(/\s*$/)[0];

      // Any whitespace that comes straight after startKeep in both the old and
      // new texts, assign to startKeep and remove from the deletion.
      var newWsStart = longestCommonPrefix(newWsFull, delWsStart);
      deletion.value = removePrefix(deletion.value, newWsStart);

      // Any whitespace that comes straight before endKeep in both the old and
      // new texts, and hasn't already been assigned to startKeep, assign to
      // endKeep and remove from the deletion.
      var newWsEnd = longestCommonSuffix(removePrefix(newWsFull, newWsStart), delWsEnd);
      deletion.value = removeSuffix(deletion.value, newWsEnd);
      endKeep.value = replacePrefix(endKeep.value, newWsFull, newWsEnd);

      // If there's any whitespace from the new text that HASN'T already been
      // assigned, assign it to the start:
      startKeep.value = replaceSuffix(startKeep.value, newWsFull, newWsFull.slice(0, newWsFull.length - newWsEnd.length));
    } else if (endKeep) {
      // We are at the start of the text. Preserve all the whitespace on
      // endKeep, and just remove whitespace from the end of deletion to the
      // extent that it overlaps with the start of endKeep.
      var endKeepWsPrefix = endKeep.value.match(/^\s*/)[0];
      var deletionWsSuffix = deletion.value.match(/\s*$/)[0];
      var overlap = maximumOverlap(deletionWsSuffix, endKeepWsPrefix);
      deletion.value = removeSuffix(deletion.value, overlap);
    } else if (startKeep) {
      // We are at the END of the text. Preserve all the whitespace on
      // startKeep, and just remove whitespace from the start of deletion to
      // the extent that it overlaps with the end of startKeep.
      var startKeepWsSuffix = startKeep.value.match(/\s*$/)[0];
      var deletionWsPrefix = deletion.value.match(/^\s*/)[0];
      var _overlap = maximumOverlap(startKeepWsSuffix, deletionWsPrefix);
      deletion.value = removePrefix(deletion.value, _overlap);
    }
  }
  var wordWithSpaceDiff = new Diff();
  wordWithSpaceDiff.tokenize = function (value) {
    // Slightly different to the tokenizeIncludingWhitespace regex used above in
    // that this one treats each individual newline as a distinct tokens, rather
    // than merging them into other surrounding whitespace. This was requested
    // in https://github.com/kpdecker/jsdiff/issues/180 &
    //    https://github.com/kpdecker/jsdiff/issues/211
    var regex = new RegExp("(\\r?\\n)|[".concat(extendedWordChars, "]+|[^\\S\\n\\r]+|[^").concat(extendedWordChars, "]"), 'ug');
    return value.match(regex) || [];
  };
  function diffWordsWithSpace(oldStr, newStr, options) {
    return wordWithSpaceDiff.diff(oldStr, newStr, options);
  }

  function generateOptions(options, defaults) {
    if (typeof options === 'function') {
      defaults.callback = options;
    } else if (options) {
      for (var name in options) {
        /* istanbul ignore else */
        if (options.hasOwnProperty(name)) {
          defaults[name] = options[name];
        }
      }
    }
    return defaults;
  }

  var lineDiff = new Diff();
  lineDiff.tokenize = function (value, options) {
    if (options.stripTrailingCr) {
      // remove one \r before \n to match GNU diff's --strip-trailing-cr behavior
      value = value.replace(/\r\n/g, '\n');
    }
    var retLines = [],
      linesAndNewlines = value.split(/(\n|\r\n)/);

    // Ignore the final empty token that occurs if the string ends with a new line
    if (!linesAndNewlines[linesAndNewlines.length - 1]) {
      linesAndNewlines.pop();
    }

    // Merge the content and line separators into single tokens
    for (var i = 0; i < linesAndNewlines.length; i++) {
      var line = linesAndNewlines[i];
      if (i % 2 && !options.newlineIsToken) {
        retLines[retLines.length - 1] += line;
      } else {
        retLines.push(line);
      }
    }
    return retLines;
  };
  lineDiff.equals = function (left, right, options) {
    // If we're ignoring whitespace, we need to normalise lines by stripping
    // whitespace before checking equality. (This has an annoying interaction
    // with newlineIsToken that requires special handling: if newlines get their
    // own token, then we DON'T want to trim the *newline* tokens down to empty
    // strings, since this would cause us to treat whitespace-only line content
    // as equal to a separator between lines, which would be weird and
    // inconsistent with the documented behavior of the options.)
    if (options.ignoreWhitespace) {
      if (!options.newlineIsToken || !left.includes('\n')) {
        left = left.trim();
      }
      if (!options.newlineIsToken || !right.includes('\n')) {
        right = right.trim();
      }
    } else if (options.ignoreNewlineAtEof && !options.newlineIsToken) {
      if (left.endsWith('\n')) {
        left = left.slice(0, -1);
      }
      if (right.endsWith('\n')) {
        right = right.slice(0, -1);
      }
    }
    return Diff.prototype.equals.call(this, left, right, options);
  };
  function diffLines(oldStr, newStr, callback) {
    return lineDiff.diff(oldStr, newStr, callback);
  }

  // Kept for backwards compatibility. This is a rather arbitrary wrapper method
  // that just calls `diffLines` with `ignoreWhitespace: true`. It's confusing to
  // have two ways to do exactly the same thing in the API, so we no longer
  // document this one (library users should explicitly use `diffLines` with
  // `ignoreWhitespace: true` instead) but we keep it around to maintain
  // compatibility with code that used old versions.
  function diffTrimmedLines(oldStr, newStr, callback) {
    var options = generateOptions(callback, {
      ignoreWhitespace: true
    });
    return lineDiff.diff(oldStr, newStr, options);
  }

  var sentenceDiff = new Diff();
  sentenceDiff.tokenize = function (value) {
    return value.split(/(\S.+?[.!?])(?=\s+|$)/);
  };
  function diffSentences(oldStr, newStr, callback) {
    return sentenceDiff.diff(oldStr, newStr, callback);
  }

  var cssDiff = new Diff();
  cssDiff.tokenize = function (value) {
    return value.split(/([{}:;,]|\s+)/);
  };
  function diffCss(oldStr, newStr, callback) {
    return cssDiff.diff(oldStr, newStr, callback);
  }

  function ownKeys(e, r) {
    var t = Object.keys(e);
    if (Object.getOwnPropertySymbols) {
      var o = Object.getOwnPropertySymbols(e);
      r && (o = o.filter(function (r) {
        return Object.getOwnPropertyDescriptor(e, r).enumerable;
      })), t.push.apply(t, o);
    }
    return t;
  }
  function _objectSpread2(e) {
    for (var r = 1; r < arguments.length; r++) {
      var t = null != arguments[r] ? arguments[r] : {};
      r % 2 ? ownKeys(Object(t), !0).forEach(function (r) {
        _defineProperty(e, r, t[r]);
      }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) {
        Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r));
      });
    }
    return e;
  }
  function _toPrimitive(t, r) {
    if ("object" != typeof t || !t) return t;
    var e = t[Symbol.toPrimitive];
    if (void 0 !== e) {
      var i = e.call(t, r || "default");
      if ("object" != typeof i) return i;
      throw new TypeError("@@toPrimitive must return a primitive value.");
    }
    return ("string" === r ? String : Number)(t);
  }
  function _toPropertyKey(t) {
    var i = _toPrimitive(t, "string");
    return "symbol" == typeof i ? i : i + "";
  }
  function _typeof(o) {
    "@babel/helpers - typeof";

    return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) {
      return typeof o;
    } : function (o) {
      return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o;
    }, _typeof(o);
  }
  function _defineProperty(obj, key, value) {
    key = _toPropertyKey(key);
    if (key in obj) {
      Object.defineProperty(obj, key, {
        value: value,
        enumerable: true,
        configurable: true,
        writable: true
      });
    } else {
      obj[key] = value;
    }
    return obj;
  }
  function _toConsumableArray(arr) {
    return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread();
  }
  function _arrayWithoutHoles(arr) {
    if (Array.isArray(arr)) return _arrayLikeToArray(arr);
  }
  function _iterableToArray(iter) {
    if (typeof Symbol !== "undefined" && iter[Symbol.iterator] != null || iter["@@iterator"] != null) return Array.from(iter);
  }
  function _unsupportedIterableToArray(o, minLen) {
    if (!o) return;
    if (typeof o === "string") return _arrayLikeToArray(o, minLen);
    var n = Object.prototype.toString.call(o).slice(8, -1);
    if (n === "Object" && o.constructor) n = o.constructor.name;
    if (n === "Map" || n === "Set") return Array.from(o);
    if (n === "Arguments" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n)) return _arrayLikeToArray(o, minLen);
  }
  function _arrayLikeToArray(arr, len) {
    if (len == null || len > arr.length) len = arr.length;
    for (var i = 0, arr2 = new Array(len); i < len; i++) arr2[i] = arr[i];
    return arr2;
  }
  function _nonIterableSpread() {
    throw new TypeError("Invalid attempt to spread non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.");
  }

  var jsonDiff = new Diff();
  // Discriminate between two lines of pretty-printed, serialized JSON where one of them has a
  // dangling comma and the other doesn't. Turns out including the dangling comma yields the nicest output:
  jsonDiff.useLongestToken = true;
  jsonDiff.tokenize = lineDiff.tokenize;
  jsonDiff.castInput = function (value, options) {
    var undefinedReplacement = options.undefinedReplacement,
      _options$stringifyRep = options.stringifyReplacer,
      stringifyReplacer = _options$stringifyRep === void 0 ? function (k, v) {
        return typeof v === 'undefined' ? undefinedReplacement : v;
      } : _options$stringifyRep;
    return typeof value === 'string' ? value : JSON.stringify(canonicalize(value, null, null, stringifyReplacer), stringifyReplacer, '  ');
  };
  jsonDiff.equals = function (left, right, options) {
    return Diff.prototype.equals.call(jsonDiff, left.replace(/,([\r\n])/g, '$1'), right.replace(/,([\r\n])/g, '$1'), options);
  };
  function diffJson(oldObj, newObj, options) {
    return jsonDiff.diff(oldObj, newObj, options);
  }

  // This function handles the presence of circular references by bailing out when encountering an
  // object that is already on the "stack" of items being processed. Accepts an optional replacer
  function canonicalize(obj, stack, replacementStack, replacer, key) {
    stack = stack || [];
    replacementStack = replacementStack || [];
    if (replacer) {
      obj = replacer(key, obj);
    }
    var i;
    for (i = 0; i < stack.length; i += 1) {
      if (stack[i] === obj) {
        return replacementStack[i];
      }
    }
    var canonicalizedObj;
    if ('[object Array]' === Object.prototype.toString.call(obj)) {
      stack.push(obj);
      canonicalizedObj = new Array(obj.length);
      replacementStack.push(canonicalizedObj);
      for (i = 0; i < obj.length; i += 1) {
        canonicalizedObj[i] = canonicalize(obj[i], stack, replacementStack, replacer, key);
      }
      stack.pop();
      replacementStack.pop();
      return canonicalizedObj;
    }
    if (obj && obj.toJSON) {
      obj = obj.toJSON();
    }
    if (_typeof(obj) === 'object' && obj !== null) {
      stack.push(obj);
      canonicalizedObj = {};
      replacementStack.push(canonicalizedObj);
      var sortedKeys = [],
        _key;
      for (_key in obj) {
        /* istanbul ignore else */
        if (Object.prototype.hasOwnProperty.call(obj, _key)) {
          sortedKeys.push(_key);
        }
      }
      sortedKeys.sort();
      for (i = 0; i < sortedKeys.length; i += 1) {
        _key = sortedKeys[i];
        canonicalizedObj[_key] = canonicalize(obj[_key], stack, replacementStack, replacer, _key);
      }
      stack.pop();
      replacementStack.pop();
    } else {
      canonicalizedObj = obj;
    }
    return canonicalizedObj;
  }

  var arrayDiff = new Diff();
  arrayDiff.tokenize = function (value) {
    return value.slice();
  };
  arrayDiff.join = arrayDiff.removeEmpty = function (value) {
    return value;
  };
  function diffArrays(oldArr, newArr, callback) {
    return arrayDiff.diff(oldArr, newArr, callback);
  }

  function unixToWin(patch) {
    if (Array.isArray(patch)) {
      return patch.map(unixToWin);
    }
    return _objectSpread2(_objectSpread2({}, patch), {}, {
      hunks: patch.hunks.map(function (hunk) {
        return _objectSpread2(_objectSpread2({}, hunk), {}, {
          lines: hunk.lines.map(function (line, i) {
            var _hunk$lines;
            return line.startsWith('\\') || line.endsWith('\r') || (_hunk$lines = hunk.lines[i + 1]) !== null && _hunk$lines !== void 0 && _hunk$lines.startsWith('\\') ? line : line + '\r';
          })
        });
      })
    });
  }
  function winToUnix(patch) {
    if (Array.isArray(patch)) {
      return patch.map(winToUnix);
    }
    return _objectSpread2(_objectSpread2({}, patch), {}, {
      hunks: patch.hunks.map(function (hunk) {
        return _objectSpread2(_objectSpread2({}, hunk), {}, {
          lines: hunk.lines.map(function (line) {
            return line.endsWith('\r') ? line.substring(0, line.length - 1) : line;
          })
        });
      })
    });
  }

  /**
   * Returns true if the patch consistently uses Unix line endings (or only involves one line and has
   * no line endings).
   */
  function isUnix(patch) {
    if (!Array.isArray(patch)) {
      patch = [patch];
    }
    return !patch.some(function (index) {
      return index.hunks.some(function (hunk) {
        return hunk.lines.some(function (line) {
          return !line.startsWith('\\') && line.endsWith('\r');
        });
      });
    });
  }

  /**
   * Returns true if the patch uses Windows line endings and only Windows line endings.
   */
  function isWin(patch) {
    if (!Array.isArray(patch)) {
      patch = [patch];
    }
    return patch.some(function (index) {
      return index.hunks.some(function (hunk) {
        return hunk.lines.some(function (line) {
          return line.endsWith('\r');
        });
      });
    }) && patch.every(function (index) {
      return index.hunks.every(function (hunk) {
        return hunk.lines.every(function (line, i) {
          var _hunk$lines2;
          return line.startsWith('\\') || line.endsWith('\r') || ((_hunk$lines2 = hunk.lines[i + 1]) === null || _hunk$lines2 === void 0 ? void 0 : _hunk$lines2.startsWith('\\'));
        });
      });
    });
  }

  function parsePatch(uniDiff) {
    var diffstr = uniDiff.split(/\n/),
      list = [],
      i = 0;
    function parseIndex() {
      var index = {};
      list.push(index);

      // Parse diff metadata
      while (i < diffstr.length) {
        var line = diffstr[i];

        // File header found, end parsing diff metadata
        if (/^(\-\-\-|\+\+\+|@@)\s/.test(line)) {
          break;
        }

        // Diff index
        var header = /^(?:Index:|diff(?: -r \w+)+)\s+(.+?)\s*$/.exec(line);
        if (header) {
          index.index = header[1];
        }
        i++;
      }

      // Parse file headers if they are defined. Unified diff requires them, but
      // there's no technical issues to have an isolated hunk without file header
      parseFileHeader(index);
      parseFileHeader(index);

      // Parse hunks
      index.hunks = [];
      while (i < diffstr.length) {
        var _line = diffstr[i];
        if (/^(Index:\s|diff\s|\-\-\-\s|\+\+\+\s|===================================================================)/.test(_line)) {
          break;
        } else if (/^@@/.test(_line)) {
          index.hunks.push(parseHunk());
        } else if (_line) {
          throw new Error('Unknown line ' + (i + 1) + ' ' + JSON.stringify(_line));
        } else {
          i++;
        }
      }
    }

    // Parses the --- and +++ headers, if none are found, no lines
    // are consumed.
    function parseFileHeader(index) {
      var fileHeader = /^(---|\+\+\+)\s+(.*)\r?$/.exec(diffstr[i]);
      if (fileHeader) {
        var keyPrefix = fileHeader[1] === '---' ? 'old' : 'new';
        var data = fileHeader[2].split('\t', 2);
        var fileName = data[0].replace(/\\\\/g, '\\');
        if (/^".*"$/.test(fileName)) {
          fileName = fileName.substr(1, fileName.length - 2);
        }
        index[keyPrefix + 'FileName'] = fileName;
        index[keyPrefix + 'Header'] = (data[1] || '').trim();
        i++;
      }
    }

    // Parses a hunk
    // This assumes that we are at the start of a hunk.
    function parseHunk() {
      var chunkHeaderIndex = i,
        chunkHeaderLine = diffstr[i++],
        chunkHeader = chunkHeaderLine.split(/@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/);
      var hunk = {
        oldStart: +chunkHeader[1],
        oldLines: typeof chunkHeader[2] === 'undefined' ? 1 : +chunkHeader[2],
        newStart: +chunkHeader[3],
        newLines: typeof chunkHeader[4] === 'undefined' ? 1 : +chunkHeader[4],
        lines: []
      };

      // Unified Diff Format quirk: If the chunk size is 0,
      // the first number is one lower than one would expect.
      // https://www.artima.com/weblogs/viewpost.jsp?thread=164293
      if (hunk.oldLines === 0) {
        hunk.oldStart += 1;
      }
      if (hunk.newLines === 0) {
        hunk.newStart += 1;
      }
      var addCount = 0,
        removeCount = 0;
      for (; i < diffstr.length && (removeCount < hunk.oldLines || addCount < hunk.newLines || (_diffstr$i = diffstr[i]) !== null && _diffstr$i !== void 0 && _diffstr$i.startsWith('\\')); i++) {
        var _diffstr$i;
        var operation = diffstr[i].length == 0 && i != diffstr.length - 1 ? ' ' : diffstr[i][0];
        if (operation === '+' || operation === '-' || operation === ' ' || operation === '\\') {
          hunk.lines.push(diffstr[i]);
          if (operation === '+') {
            addCount++;
          } else if (operation === '-') {
            removeCount++;
          } else if (operation === ' ') {
            addCount++;
            removeCount++;
          }
        } else {
          throw new Error("Hunk at line ".concat(chunkHeaderIndex + 1, " contained invalid line ").concat(diffstr[i]));
        }
      }

      // Handle the empty block count case
      if (!addCount && hunk.newLines === 1) {
        hunk.newLines = 0;
      }
      if (!removeCount && hunk.oldLines === 1) {
        hunk.oldLines = 0;
      }

      // Perform sanity checking
      if (addCount !== hunk.newLines) {
        throw new Error('Added line count did not match for hunk at line ' + (chunkHeaderIndex + 1));
      }
      if (removeCount !== hunk.oldLines) {
        throw new Error('Removed line count did not match for hunk at line ' + (chunkHeaderIndex + 1));
      }
      return hunk;
    }
    while (i < diffstr.length) {
      parseIndex();
    }
    return list;
  }

  // Iterator that traverses in the range of [min, max], stepping
  // by distance from a given start position. I.e. for [0, 4], with
  // start of 2, this will iterate 2, 3, 1, 4, 0.
  function distanceIterator (start, minLine, maxLine) {
    var wantForward = true,
      backwardExhausted = false,
      forwardExhausted = false,
      localOffset = 1;
    return function iterator() {
      if (wantForward && !forwardExhausted) {
        if (backwardExhausted) {
          localOffset++;
        } else {
          wantForward = false;
        }

        // Check if trying to fit beyond text length, and if not, check it fits
        // after offset location (or desired location on first iteration)
        if (start + localOffset <= maxLine) {
          return start + localOffset;
        }
        forwardExhausted = true;
      }
      if (!backwardExhausted) {
        if (!forwardExhausted) {
          wantForward = true;
        }

        // Check if trying to fit before text beginning, and if not, check it fits
        // before offset location
        if (minLine <= start - localOffset) {
          return start - localOffset++;
        }
        backwardExhausted = true;
        return iterator();
      }

      // We tried to fit hunk before text beginning and beyond text length, then
      // hunk can't fit on the text. Return undefined
    };
  }

  function applyPatch(source, uniDiff) {
    var options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
    if (typeof uniDiff === 'string') {
      uniDiff = parsePatch(uniDiff);
    }
    if (Array.isArray(uniDiff)) {
      if (uniDiff.length > 1) {
        throw new Error('applyPatch only works with a single input.');
      }
      uniDiff = uniDiff[0];
    }
    if (options.autoConvertLineEndings || options.autoConvertLineEndings == null) {
      if (hasOnlyWinLineEndings(source) && isUnix(uniDiff)) {
        uniDiff = unixToWin(uniDiff);
      } else if (hasOnlyUnixLineEndings(source) && isWin(uniDiff)) {
        uniDiff = winToUnix(uniDiff);
      }
    }

    // Apply the diff to the input
    var lines = source.split('\n'),
      hunks = uniDiff.hunks,
      compareLine = options.compareLine || function (lineNumber, line, operation, patchContent) {
        return line === patchContent;
      },
      fuzzFactor = options.fuzzFactor || 0,
      minLine = 0;
    if (fuzzFactor < 0 || !Number.isInteger(fuzzFactor)) {
      throw new Error('fuzzFactor must be a non-negative integer');
    }

    // Special case for empty patch.
    if (!hunks.length) {
      return source;
    }

    // Before anything else, handle EOFNL insertion/removal. If the patch tells us to make a change
    // to the EOFNL that is redundant/impossible - i.e. to remove a newline that's not there, or add a
    // newline that already exists - then we either return false and fail to apply the patch (if
    // fuzzFactor is 0) or simply ignore the problem and do nothing (if fuzzFactor is >0).
    // If we do need to remove/add a newline at EOF, this will always be in the final hunk:
    var prevLine = '',
      removeEOFNL = false,
      addEOFNL = false;
    for (var i = 0; i < hunks[hunks.length - 1].lines.length; i++) {
      var line = hunks[hunks.length - 1].lines[i];
      if (line[0] == '\\') {
        if (prevLine[0] == '+') {
          removeEOFNL = true;
        } else if (prevLine[0] == '-') {
          addEOFNL = true;
        }
      }
      prevLine = line;
    }
    if (removeEOFNL) {
      if (addEOFNL) {
        // This means the final line gets changed but doesn't have a trailing newline in either the
        // original or patched version. In that case, we do nothing if fuzzFactor > 0, and if
        // fuzzFactor is 0, we simply validate that the source file has no trailing newline.
        if (!fuzzFactor && lines[lines.length - 1] == '') {
          return false;
        }
      } else if (lines[lines.length - 1] == '') {
        lines.pop();
      } else if (!fuzzFactor) {
        return false;
      }
    } else if (addEOFNL) {
      if (lines[lines.length - 1] != '') {
        lines.push('');
      } else if (!fuzzFactor) {
        return false;
      }
    }

    /**
     * Checks if the hunk can be made to fit at the provided location with at most `maxErrors`
     * insertions, substitutions, or deletions, while ensuring also that:
     * - lines deleted in the hunk match exactly, and
     * - wherever an insertion operation or block of insertion operations appears in the hunk, the
     *   immediately preceding and following lines of context match exactly
     *
     * `toPos` should be set such that lines[toPos] is meant to match hunkLines[0].
     *
     * If the hunk can be applied, returns an object with properties `oldLineLastI` and
     * `replacementLines`. Otherwise, returns null.
     */
    function applyHunk(hunkLines, toPos, maxErrors) {
      var hunkLinesI = arguments.length > 3 && arguments[3] !== undefined ? arguments[3] : 0;
      var lastContextLineMatched = arguments.length > 4 && arguments[4] !== undefined ? arguments[4] : true;
      var patchedLines = arguments.length > 5 && arguments[5] !== undefined ? arguments[5] : [];
      var patchedLinesLength = arguments.length > 6 && arguments[6] !== undefined ? arguments[6] : 0;
      var nConsecutiveOldContextLines = 0;
      var nextContextLineMustMatch = false;
      for (; hunkLinesI < hunkLines.length; hunkLinesI++) {
        var hunkLine = hunkLines[hunkLinesI],
          operation = hunkLine.length > 0 ? hunkLine[0] : ' ',
          content = hunkLine.length > 0 ? hunkLine.substr(1) : hunkLine;
        if (operation === '-') {
          if (compareLine(toPos + 1, lines[toPos], operation, content)) {
            toPos++;
            nConsecutiveOldContextLines = 0;
          } else {
            if (!maxErrors || lines[toPos] == null) {
              return null;
            }
            patchedLines[patchedLinesLength] = lines[toPos];
            return applyHunk(hunkLines, toPos + 1, maxErrors - 1, hunkLinesI, false, patchedLines, patchedLinesLength + 1);
          }
        }
        if (operation === '+') {
          if (!lastContextLineMatched) {
            return null;
          }
          patchedLines[patchedLinesLength] = content;
          patchedLinesLength++;
          nConsecutiveOldContextLines = 0;
          nextContextLineMustMatch = true;
        }
        if (operation === ' ') {
          nConsecutiveOldContextLines++;
          patchedLines[patchedLinesLength] = lines[toPos];
          if (compareLine(toPos + 1, lines[toPos], operation, content)) {
            patchedLinesLength++;
            lastContextLineMatched = true;
            nextContextLineMustMatch = false;
            toPos++;
          } else {
            if (nextContextLineMustMatch || !maxErrors) {
              return null;
            }

            // Consider 3 possibilities in sequence:
            // 1. lines contains a *substitution* not included in the patch context, or
            // 2. lines contains an *insertion* not included in the patch context, or
            // 3. lines contains a *deletion* not included in the patch context
            // The first two options are of course only possible if the line from lines is non-null -
            // i.e. only option 3 is possible if we've overrun the end of the old file.
            return lines[toPos] && (applyHunk(hunkLines, toPos + 1, maxErrors - 1, hunkLinesI + 1, false, patchedLines, patchedLinesLength + 1) || applyHunk(hunkLines, toPos + 1, maxErrors - 1, hunkLinesI, false, patchedLines, patchedLinesLength + 1)) || applyHunk(hunkLines, toPos, maxErrors - 1, hunkLinesI + 1, false, patchedLines, patchedLinesLength);
          }
        }
      }

      // Before returning, trim any unmodified context lines off the end of patchedLines and reduce
      // toPos (and thus oldLineLastI) accordingly. This allows later hunks to be applied to a region
      // that starts in this hunk's trailing context.
      patchedLinesLength -= nConsecutiveOldContextLines;
      toPos -= nConsecutiveOldContextLines;
      patchedLines.length = patchedLinesLength;
      return {
        patchedLines: patchedLines,
        oldLineLastI: toPos - 1
      };
    }
    var resultLines = [];

    // Search best fit offsets for each hunk based on the previous ones
    var prevHunkOffset = 0;
    for (var _i = 0; _i < hunks.length; _i++) {
      var hunk = hunks[_i];
      var hunkResult = void 0;
      var maxLine = lines.length - hunk.oldLines + fuzzFactor;
      var toPos = void 0;
      for (var maxErrors = 0; maxErrors <= fuzzFactor; maxErrors++) {
        toPos = hunk.oldStart + prevHunkOffset - 1;
        var iterator = distanceIterator(toPos, minLine, maxLine);
        for (; toPos !== undefined; toPos = iterator()) {
          hunkResult = applyHunk(hunk.lines, toPos, maxErrors);
          if (hunkResult) {
            break;
          }
        }
        if (hunkResult) {
          break;
        }
      }
      if (!hunkResult) {
        return false;
      }

      // Copy everything from the end of where we applied the last hunk to the start of this hunk
      for (var _i2 = minLine; _i2 < toPos; _i2++) {
        resultLines.push(lines[_i2]);
      }

      // Add the lines produced by applying the hunk:
      for (var _i3 = 0; _i3 < hunkResult.patchedLines.length; _i3++) {
        var _line = hunkResult.patchedLines[_i3];
        resultLines.push(_line);
      }

      // Set lower text limit to end of the current hunk, so next ones don't try
      // to fit over already patched text
      minLine = hunkResult.oldLineLastI + 1;

      // Note the offset between where the patch said the hunk should've applied and where we
      // applied it, so we can adjust future hunks accordingly:
      prevHunkOffset = toPos + 1 - hunk.oldStart;
    }

    // Copy over the rest of the lines from the old text
    for (var _i4 = minLine; _i4 < lines.length; _i4++) {
      resultLines.push(lines[_i4]);
    }
    return resultLines.join('\n');
  }

  // Wrapper that supports multiple file patches via callbacks.
  function applyPatches(uniDiff, options) {
    if (typeof uniDiff === 'string') {
      uniDiff = parsePatch(uniDiff);
    }
    var currentIndex = 0;
    function processIndex() {
      var index = uniDiff[currentIndex++];
      if (!index) {
        return options.complete();
      }
      options.loadFile(index, function (err, data) {
        if (err) {
          return options.complete(err);
        }
        var updatedContent = applyPatch(data, index, options);
        options.patched(index, updatedContent, function (err) {
          if (err) {
            return options.complete(err);
          }
          processIndex();
        });
      });
    }
    processIndex();
  }

  function structuredPatch(oldFileName, newFileName, oldStr, newStr, oldHeader, newHeader, options) {
    if (!options) {
      options = {};
    }
    if (typeof options === 'function') {
      options = {
        callback: options
      };
    }
    if (typeof options.context === 'undefined') {
      options.context = 4;
    }
    if (options.newlineIsToken) {
      throw new Error('newlineIsToken may not be used with patch-generation functions, only with diffing functions');
    }
    if (!options.callback) {
      return diffLinesResultToPatch(diffLines(oldStr, newStr, options));
    } else {
      var _options = options,
        _callback = _options.callback;
      diffLines(oldStr, newStr, _objectSpread2(_objectSpread2({}, options), {}, {
        callback: function callback(diff) {
          var patch = diffLinesResultToPatch(diff);
          _callback(patch);
        }
      }));
    }
    function diffLinesResultToPatch(diff) {
      // STEP 1: Build up the patch with no "\ No newline at end of file" lines and with the arrays
      //         of lines containing trailing newline characters. We'll tidy up later...

      if (!diff) {
        return;
      }
      diff.push({
        value: '',
        lines: []
      }); // Append an empty value to make cleanup easier

      function contextLines(lines) {
        return lines.map(function (entry) {
          return ' ' + entry;
        });
      }
      var hunks = [];
      var oldRangeStart = 0,
        newRangeStart = 0,
        curRange = [],
        oldLine = 1,
        newLine = 1;
      var _loop = function _loop() {
        var current = diff[i],
          lines = current.lines || splitLines(current.value);
        current.lines = lines;
        if (current.added || current.removed) {
          var _curRange;
          // If we have previous context, start with that
          if (!oldRangeStart) {
            var prev = diff[i - 1];
            oldRangeStart = oldLine;
            newRangeStart = newLine;
            if (prev) {
              curRange = options.context > 0 ? contextLines(prev.lines.slice(-options.context)) : [];
              oldRangeStart -= curRange.length;
              newRangeStart -= curRange.length;
            }
          }

          // Output our changes
          (_curRange = curRange).push.apply(_curRange, _toConsumableArray(lines.map(function (entry) {
            return (current.added ? '+' : '-') + entry;
          })));

          // Track the updated file position
          if (current.added) {
            newLine += lines.length;
          } else {
            oldLine += lines.length;
          }
        } else {
          // Identical context lines. Track line changes
          if (oldRangeStart) {
            // Close out any changes that have been output (or join overlapping)
            if (lines.length <= options.context * 2 && i < diff.length - 2) {
              var _curRange2;
              // Overlapping
              (_curRange2 = curRange).push.apply(_curRange2, _toConsumableArray(contextLines(lines)));
            } else {
              var _curRange3;
              // end the range and output
              var contextSize = Math.min(lines.length, options.context);
              (_curRange3 = curRange).push.apply(_curRange3, _toConsumableArray(contextLines(lines.slice(0, contextSize))));
              var _hunk = {
                oldStart: oldRangeStart,
                oldLines: oldLine - oldRangeStart + contextSize,
                newStart: newRangeStart,
                newLines: newLine - newRangeStart + contextSize,
                lines: curRange
              };
              hunks.push(_hunk);
              oldRangeStart = 0;
              newRangeStart = 0;
              curRange = [];
            }
          }
          oldLine += lines.length;
          newLine += lines.length;
        }
      };
      for (var i = 0; i < diff.length; i++) {
        _loop();
      }

      // Step 2: eliminate the trailing `\n` from each line of each hunk, and, where needed, add
      //         "\ No newline at end of file".
      for (var _i = 0, _hunks = hunks; _i < _hunks.length; _i++) {
        var hunk = _hunks[_i];
        for (var _i2 = 0; _i2 < hunk.lines.length; _i2++) {
          if (hunk.lines[_i2].endsWith('\n')) {
            hunk.lines[_i2] = hunk.lines[_i2].slice(0, -1);
          } else {
            hunk.lines.splice(_i2 + 1, 0, '\\ No newline at end of file');
            _i2++; // Skip the line we just added, then continue iterating
          }
        }
      }
      return {
        oldFileName: oldFileName,
        newFileName: newFileName,
        oldHeader: oldHeader,
        newHeader: newHeader,
        hunks: hunks
      };
    }
  }
  function formatPatch(diff) {
    if (Array.isArray(diff)) {
      return diff.map(formatPatch).join('\n');
    }
    var ret = [];
    if (diff.oldFileName == diff.newFileName) {
      ret.push('Index: ' + diff.oldFileName);
    }
    ret.push('===================================================================');
    ret.push('--- ' + diff.oldFileName + (typeof diff.oldHeader === 'undefined' ? '' : '\t' + diff.oldHeader));
    ret.push('+++ ' + diff.newFileName + (typeof diff.newHeader === 'undefined' ? '' : '\t' + diff.newHeader));
    for (var i = 0; i < diff.hunks.length; i++) {
      var hunk = diff.hunks[i];
      // Unified Diff Format quirk: If the chunk size is 0,
      // the first number is one lower than one would expect.
      // https://www.artima.com/weblogs/viewpost.jsp?thread=164293
      if (hunk.oldLines === 0) {
        hunk.oldStart -= 1;
      }
      if (hunk.newLines === 0) {
        hunk.newStart -= 1;
      }
      ret.push('@@ -' + hunk.oldStart + ',' + hunk.oldLines + ' +' + hunk.newStart + ',' + hunk.newLines + ' @@');
      ret.push.apply(ret, hunk.lines);
    }
    return ret.join('\n') + '\n';
  }
  function createTwoFilesPatch(oldFileName, newFileName, oldStr, newStr, oldHeader, newHeader, options) {
    var _options2;
    if (typeof options === 'function') {
      options = {
        callback: options
      };
    }
    if (!((_options2 = options) !== null && _options2 !== void 0 && _options2.callback)) {
      var patchObj = structuredPatch(oldFileName, newFileName, oldStr, newStr, oldHeader, newHeader, options);
      if (!patchObj) {
        return;
      }
      return formatPatch(patchObj);
    } else {
      var _options3 = options,
        _callback2 = _options3.callback;
      structuredPatch(oldFileName, newFileName, oldStr, newStr, oldHeader, newHeader, _objectSpread2(_objectSpread2({}, options), {}, {
        callback: function callback(patchObj) {
          if (!patchObj) {
            _callback2();
          } else {
            _callback2(formatPatch(patchObj));
          }
        }
      }));
    }
  }
  function createPatch(fileName, oldStr, newStr, oldHeader, newHeader, options) {
    return createTwoFilesPatch(fileName, fileName, oldStr, newStr, oldHeader, newHeader, options);
  }

  /**
   * Split `text` into an array of lines, including the trailing newline character (where present)
   */
  function splitLines(text) {
    var hasTrailingNl = text.endsWith('\n');
    var result = text.split('\n').map(function (line) {
      return line + '\n';
    });
    if (hasTrailingNl) {
      result.pop();
    } else {
      result.push(result.pop().slice(0, -1));
    }
    return result;
  }

  function arrayEqual(a, b) {
    if (a.length !== b.length) {
      return false;
    }
    return arrayStartsWith(a, b);
  }
  function arrayStartsWith(array, start) {
    if (start.length > array.length) {
      return false;
    }
    for (var i = 0; i < start.length; i++) {
      if (start[i] !== array[i]) {
        return false;
      }
    }
    return true;
  }

  function calcLineCount(hunk) {
    var _calcOldNewLineCount = calcOldNewLineCount(hunk.lines),
      oldLines = _calcOldNewLineCount.oldLines,
      newLines = _calcOldNewLineCount.newLines;
    if (oldLines !== undefined) {
      hunk.oldLines = oldLines;
    } else {
      delete hunk.oldLines;
    }
    if (newLines !== undefined) {
      hunk.newLines = newLines;
    } else {
      delete hunk.newLines;
    }
  }
  function merge(mine, theirs, base) {
    mine = loadPatch(mine, base);
    theirs = loadPatch(theirs, base);
    var ret = {};

    // For index we just let it pass through as it doesn't have any necessary meaning.
    // Leaving sanity checks on this to the API consumer that may know more about the
    // meaning in their own context.
    if (mine.index || theirs.index) {
      ret.index = mine.index || theirs.index;
    }
    if (mine.newFileName || theirs.newFileName) {
      if (!fileNameChanged(mine)) {
        // No header or no change in ours, use theirs (and ours if theirs does not exist)
        ret.oldFileName = theirs.oldFileName || mine.oldFileName;
        ret.newFileName = theirs.newFileName || mine.newFileName;
        ret.oldHeader = theirs.oldHeader || mine.oldHeader;
        ret.newHeader = theirs.newHeader || mine.newHeader;
      } else if (!fileNameChanged(theirs)) {
        // No header or no change in theirs, use ours
        ret.oldFileName = mine.oldFileName;
        ret.newFileName = mine.newFileName;
        ret.oldHeader = mine.oldHeader;
        ret.newHeader = mine.newHeader;
      } else {
        // Both changed... figure it out
        ret.oldFileName = selectField(ret, mine.oldFileName, theirs.oldFileName);
        ret.newFileName = selectField(ret, mine.newFileName, theirs.newFileName);
        ret.oldHeader = selectField(ret, mine.oldHeader, theirs.oldHeader);
        ret.newHeader = selectField(ret, mine.newHeader, theirs.newHeader);
      }
    }
    ret.hunks = [];
    var mineIndex = 0,
      theirsIndex = 0,
      mineOffset = 0,
      theirsOffset = 0;
    while (mineIndex < mine.hunks.length || theirsIndex < theirs.hunks.length) {
      var mineCurrent = mine.hunks[mineIndex] || {
          oldStart: Infinity
        },
        theirsCurrent = theirs.hunks[theirsIndex] || {
          oldStart: Infinity
        };
      if (hunkBefore(mineCurrent, theirsCurrent)) {
        // This patch does not overlap with any of the others, yay.
        ret.hunks.push(cloneHunk(mineCurrent, mineOffset));
        mineIndex++;
        theirsOffset += mineCurrent.newLines - mineCurrent.oldLines;
      } else if (hunkBefore(theirsCurrent, mineCurrent)) {
        // This patch does not overlap with any of the others, yay.
        ret.hunks.push(cloneHunk(theirsCurrent, theirsOffset));
        theirsIndex++;
        mineOffset += theirsCurrent.newLines - theirsCurrent.oldLines;
      } else {
        // Overlap, merge as best we can
        var mergedHunk = {
          oldStart: Math.min(mineCurrent.oldStart, theirsCurrent.oldStart),
          oldLines: 0,
          newStart: Math.min(mineCurrent.newStart + mineOffset, theirsCurrent.oldStart + theirsOffset),
          newLines: 0,
          lines: []
        };
        mergeLines(mergedHunk, mineCurrent.oldStart, mineCurrent.lines, theirsCurrent.oldStart, theirsCurrent.lines);
        theirsIndex++;
        mineIndex++;
        ret.hunks.push(mergedHunk);
      }
    }
    return ret;
  }
  function loadPatch(param, base) {
    if (typeof param === 'string') {
      if (/^@@/m.test(param) || /^Index:/m.test(param)) {
        return parsePatch(param)[0];
      }
      if (!base) {
        throw new Error('Must provide a base reference or pass in a patch');
      }
      return structuredPatch(undefined, undefined, base, param);
    }
    return param;
  }
  function fileNameChanged(patch) {
    return patch.newFileName && patch.newFileName !== patch.oldFileName;
  }
  function selectField(index, mine, theirs) {
    if (mine === theirs) {
      return mine;
    } else {
      index.conflict = true;
      return {
        mine: mine,
        theirs: theirs
      };
    }
  }
  function hunkBefore(test, check) {
    return test.oldStart < check.oldStart && test.oldStart + test.oldLines < check.oldStart;
  }
  function cloneHunk(hunk, offset) {
    return {
      oldStart: hunk.oldStart,
      oldLines: hunk.oldLines,
      newStart: hunk.newStart + offset,
      newLines: hunk.newLines,
      lines: hunk.lines
    };
  }
  function mergeLines(hunk, mineOffset, mineLines, theirOffset, theirLines) {
    // This will generally result in a conflicted hunk, but there are cases where the context
    // is the only overlap where we can successfully merge the content here.
    var mine = {
        offset: mineOffset,
        lines: mineLines,
        index: 0
      },
      their = {
        offset: theirOffset,
        lines: theirLines,
        index: 0
      };

    // Handle any leading content
    insertLeading(hunk, mine, their);
    insertLeading(hunk, their, mine);

    // Now in the overlap content. Scan through and select the best changes from each.
    while (mine.index < mine.lines.length && their.index < their.lines.length) {
      var mineCurrent = mine.lines[mine.index],
        theirCurrent = their.lines[their.index];
      if ((mineCurrent[0] === '-' || mineCurrent[0] === '+') && (theirCurrent[0] === '-' || theirCurrent[0] === '+')) {
        // Both modified ...
        mutualChange(hunk, mine, their);
      } else if (mineCurrent[0] === '+' && theirCurrent[0] === ' ') {
        var _hunk$lines;
        // Mine inserted
        (_hunk$lines = hunk.lines).push.apply(_hunk$lines, _toConsumableArray(collectChange(mine)));
      } else if (theirCurrent[0] === '+' && mineCurrent[0] === ' ') {
        var _hunk$lines2;
        // Theirs inserted
        (_hunk$lines2 = hunk.lines).push.apply(_hunk$lines2, _toConsumableArray(collectChange(their)));
      } else if (mineCurrent[0] === '-' && theirCurrent[0] === ' ') {
        // Mine removed or edited
        removal(hunk, mine, their);
      } else if (theirCurrent[0] === '-' && mineCurrent[0] === ' ') {
        // Their removed or edited
        removal(hunk, their, mine, true);
      } else if (mineCurrent === theirCurrent) {
        // Context identity
        hunk.lines.push(mineCurrent);
        mine.index++;
        their.index++;
      } else {
        // Context mismatch
        conflict(hunk, collectChange(mine), collectChange(their));
      }
    }

    // Now push anything that may be remaining
    insertTrailing(hunk, mine);
    insertTrailing(hunk, their);
    calcLineCount(hunk);
  }
  function mutualChange(hunk, mine, their) {
    var myChanges = collectChange(mine),
      theirChanges = collectChange(their);
    if (allRemoves(myChanges) && allRemoves(theirChanges)) {
      // Special case for remove changes that are supersets of one another
      if (arrayStartsWith(myChanges, theirChanges) && skipRemoveSuperset(their, myChanges, myChanges.length - theirChanges.length)) {
        var _hunk$lines3;
        (_hunk$lines3 = hunk.lines).push.apply(_hunk$lines3, _toConsumableArray(myChanges));
        return;
      } else if (arrayStartsWith(theirChanges, myChanges) && skipRemoveSuperset(mine, theirChanges, theirChanges.length - myChanges.length)) {
        var _hunk$lines4;
        (_hunk$lines4 = hunk.lines).push.apply(_hunk$lines4, _toConsumableArray(theirChanges));
        return;
      }
    } else if (arrayEqual(myChanges, theirChanges)) {
      var _hunk$lines5;
      (_hunk$lines5 = hunk.lines).push.apply(_hunk$lines5, _toConsumableArray(myChanges));
      return;
    }
    conflict(hunk, myChanges, theirChanges);
  }
  function removal(hunk, mine, their, swap) {
    var myChanges = collectChange(mine),
      theirChanges = collectContext(their, myChanges);
    if (theirChanges.merged) {
      var _hunk$lines6;
      (_hunk$lines6 = hunk.lines).push.apply(_hunk$lines6, _toConsumableArray(theirChanges.merged));
    } else {
      conflict(hunk, swap ? theirChanges : myChanges, swap ? myChanges : theirChanges);
    }
  }
  function conflict(hunk, mine, their) {
    hunk.conflict = true;
    hunk.lines.push({
      conflict: true,
      mine: mine,
      theirs: their
    });
  }
  function insertLeading(hunk, insert, their) {
    while (insert.offset < their.offset && insert.index < insert.lines.length) {
      var line = insert.lines[insert.index++];
      hunk.lines.push(line);
      insert.offset++;
    }
  }
  function insertTrailing(hunk, insert) {
    while (insert.index < insert.lines.length) {
      var line = insert.lines[insert.index++];
      hunk.lines.push(line);
    }
  }
  function collectChange(state) {
    var ret = [],
      operation = state.lines[state.index][0];
    while (state.index < state.lines.length) {
      var line = state.lines[state.index];

      // Group additions that are immediately after subtractions and treat them as one "atomic" modify change.
      if (operation === '-' && line[0] === '+') {
        operation = '+';
      }
      if (operation === line[0]) {
        ret.push(line);
        state.index++;
      } else {
        break;
      }
    }
    return ret;
  }
  function collectContext(state, matchChanges) {
    var changes = [],
      merged = [],
      matchIndex = 0,
      contextChanges = false,
      conflicted = false;
    while (matchIndex < matchChanges.length && state.index < state.lines.length) {
      var change = state.lines[state.index],
        match = matchChanges[matchIndex];

      // Once we've hit our add, then we are done
      if (match[0] === '+') {
        break;
      }
      contextChanges = contextChanges || change[0] !== ' ';
      merged.push(match);
      matchIndex++;

      // Consume any additions in the other block as a conflict to attempt
      // to pull in the remaining context after this
      if (change[0] === '+') {
        conflicted = true;
        while (change[0] === '+') {
          changes.push(change);
          change = state.lines[++state.index];
        }
      }
      if (match.substr(1) === change.substr(1)) {
        changes.push(change);
        state.index++;
      } else {
        conflicted = true;
      }
    }
    if ((matchChanges[matchIndex] || '')[0] === '+' && contextChanges) {
      conflicted = true;
    }
    if (conflicted) {
      return changes;
    }
    while (matchIndex < matchChanges.length) {
      merged.push(matchChanges[matchIndex++]);
    }
    return {
      merged: merged,
      changes: changes
    };
  }
  function allRemoves(changes) {
    return changes.reduce(function (prev, change) {
      return prev && change[0] === '-';
    }, true);
  }
  function skipRemoveSuperset(state, removeChanges, delta) {
    for (var i = 0; i < delta; i++) {
      var changeContent = removeChanges[removeChanges.length - delta + i].substr(1);
      if (state.lines[state.index + i] !== ' ' + changeContent) {
        return false;
      }
    }
    state.index += delta;
    return true;
  }
  function calcOldNewLineCount(lines) {
    var oldLines = 0;
    var newLines = 0;
    lines.forEach(function (line) {
      if (typeof line !== 'string') {
        var myCount = calcOldNewLineCount(line.mine);
        var theirCount = calcOldNewLineCount(line.theirs);
        if (oldLines !== undefined) {
          if (myCount.oldLines === theirCount.oldLines) {
            oldLines += myCount.oldLines;
          } else {
            oldLines = undefined;
          }
        }
        if (newLines !== undefined) {
          if (myCount.newLines === theirCount.newLines) {
            newLines += myCount.newLines;
          } else {
            newLines = undefined;
          }
        }
      } else {
        if (newLines !== undefined && (line[0] === '+' || line[0] === ' ')) {
          newLines++;
        }
        if (oldLines !== undefined && (line[0] === '-' || line[0] === ' ')) {
          oldLines++;
        }
      }
    });
    return {
      oldLines: oldLines,
      newLines: newLines
    };
  }

  function reversePatch(structuredPatch) {
    if (Array.isArray(structuredPatch)) {
      return structuredPatch.map(reversePatch).reverse();
    }
    return _objectSpread2(_objectSpread2({}, structuredPatch), {}, {
      oldFileName: structuredPatch.newFileName,
      oldHeader: structuredPatch.newHeader,
      newFileName: structuredPatch.oldFileName,
      newHeader: structuredPatch.oldHeader,
      hunks: structuredPatch.hunks.map(function (hunk) {
        return {
          oldLines: hunk.newLines,
          oldStart: hunk.newStart,
          newLines: hunk.oldLines,
          newStart: hunk.oldStart,
          lines: hunk.lines.map(function (l) {
            if (l.startsWith('-')) {
              return "+".concat(l.slice(1));
            }
            if (l.startsWith('+')) {
              return "-".concat(l.slice(1));
            }
            return l;
          })
        };
      })
    });
  }

  // See: http://code.google.com/p/google-diff-match-patch/wiki/API
  function convertChangesToDMP(changes) {
    var ret = [],
      change,
      operation;
    for (var i = 0; i < changes.length; i++) {
      change = changes[i];
      if (change.added) {
        operation = 1;
      } else if (change.removed) {
        operation = -1;
      } else {
        operation = 0;
      }
      ret.push([operation, change.value]);
    }
    return ret;
  }

  function convertChangesToXML(changes) {
    var ret = [];
    for (var i = 0; i < changes.length; i++) {
      var change = changes[i];
      if (change.added) {
        ret.push('<ins>');
      } else if (change.removed) {
        ret.push('<del>');
      }
      ret.push(escapeHTML(change.value));
      if (change.added) {
        ret.push('</ins>');
      } else if (change.removed) {
        ret.push('</del>');
      }
    }
    return ret.join('');
  }
  function escapeHTML(s) {
    var n = s;
    n = n.replace(/&/g, '&amp;');
    n = n.replace(/</g, '&lt;');
    n = n.replace(/>/g, '&gt;');
    n = n.replace(/"/g, '&quot;');
    return n;
  }

  exports.Diff = Diff;
  exports.applyPatch = applyPatch;
  exports.applyPatches = applyPatches;
  exports.canonicalize = canonicalize;
  exports.convertChangesToDMP = convertChangesToDMP;
  exports.convertChangesToXML = convertChangesToXML;
  exports.createPatch = createPatch;
  exports.createTwoFilesPatch = createTwoFilesPatch;
  exports.diffArrays = diffArrays;
  exports.diffChars = diffChars;
  exports.diffCss = diffCss;
  exports.diffJson = diffJson;
  exports.diffLines = diffLines;
  exports.diffSentences = diffSentences;
  exports.diffTrimmedLines = diffTrimmedLines;
  exports.diffWords = diffWords;
  exports.diffWordsWithSpace = diffWordsWithSpace;
  exports.formatPatch = formatPatch;
  exports.merge = merge;
  exports.parsePatch = parsePatch;
  exports.reversePatch = reversePatch;
  exports.structuredPatch = structuredPatch;

}));
