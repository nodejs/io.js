/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.createPatch = createPatch;
exports.createTwoFilesPatch = createTwoFilesPatch;
exports.formatPatch = formatPatch;
exports.structuredPatch = structuredPatch;
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_line = require("../diff/line")
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
function _toConsumableArray(arr) { return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread(); }
function _nonIterableSpread() { throw new TypeError("Invalid attempt to spread non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); }
function _unsupportedIterableToArray(o, minLen) { if (!o) return; if (typeof o === "string") return _arrayLikeToArray(o, minLen); var n = Object.prototype.toString.call(o).slice(8, -1); if (n === "Object" && o.constructor) n = o.constructor.name; if (n === "Map" || n === "Set") return Array.from(o); if (n === "Arguments" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n)) return _arrayLikeToArray(o, minLen); }
function _iterableToArray(iter) { if (typeof Symbol !== "undefined" && iter[Symbol.iterator] != null || iter["@@iterator"] != null) return Array.from(iter); }
function _arrayWithoutHoles(arr) { if (Array.isArray(arr)) return _arrayLikeToArray(arr); }
function _arrayLikeToArray(arr, len) { if (len == null || len > arr.length) len = arr.length; for (var i = 0, arr2 = new Array(len); i < len; i++) arr2[i] = arr[i]; return arr2; }
function ownKeys(e, r) { var t = Object.keys(e); if (Object.getOwnPropertySymbols) { var o = Object.getOwnPropertySymbols(e); r && (o = o.filter(function (r) { return Object.getOwnPropertyDescriptor(e, r).enumerable; })), t.push.apply(t, o); } return t; }
function _objectSpread(e) { for (var r = 1; r < arguments.length; r++) { var t = null != arguments[r] ? arguments[r] : {}; r % 2 ? ownKeys(Object(t), !0).forEach(function (r) { _defineProperty(e, r, t[r]); }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) { Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r)); }); } return e; }
function _defineProperty(obj, key, value) { key = _toPropertyKey(key); if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == _typeof(i) ? i : i + ""; }
function _toPrimitive(t, r) { if ("object" != _typeof(t) || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r || "default"); if ("object" != _typeof(i)) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
/*istanbul ignore end*/
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
    return diffLinesResultToPatch(
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _line
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    diffLines)
    /*istanbul ignore end*/
    (oldStr, newStr, options));
  } else {
    var
      /*istanbul ignore start*/
      _options =
      /*istanbul ignore end*/
      options,
      /*istanbul ignore start*/
      /*istanbul ignore end*/
      _callback = _options.callback;
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _line
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    diffLines)
    /*istanbul ignore end*/
    (oldStr, newStr,
    /*istanbul ignore start*/
    _objectSpread(_objectSpread({},
    /*istanbul ignore end*/
    options), {}, {
      callback: function
      /*istanbul ignore start*/
      callback
      /*istanbul ignore end*/
      (diff) {
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
    /*istanbul ignore start*/
    var _loop = function _loop()
    /*istanbul ignore end*/
    {
      var current = diff[i],
        lines = current.lines || splitLines(current.value);
      current.lines = lines;
      if (current.added || current.removed) {
        /*istanbul ignore start*/
        var _curRange;
        /*istanbul ignore end*/
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
        /*istanbul ignore start*/
        /*istanbul ignore end*/
        /*istanbul ignore start*/
        (_curRange =
        /*istanbul ignore end*/
        curRange).push.apply(
        /*istanbul ignore start*/
        _curRange
        /*istanbul ignore end*/
        ,
        /*istanbul ignore start*/
        _toConsumableArray(
        /*istanbul ignore end*/
        lines.map(function (entry) {
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
            /*istanbul ignore start*/
            var _curRange2;
            /*istanbul ignore end*/
            // Overlapping
            /*istanbul ignore start*/
            /*istanbul ignore end*/
            /*istanbul ignore start*/
            (_curRange2 =
            /*istanbul ignore end*/
            curRange).push.apply(
            /*istanbul ignore start*/
            _curRange2
            /*istanbul ignore end*/
            ,
            /*istanbul ignore start*/
            _toConsumableArray(
            /*istanbul ignore end*/
            contextLines(lines)));
          } else {
            /*istanbul ignore start*/
            var _curRange3;
            /*istanbul ignore end*/
            // end the range and output
            var contextSize = Math.min(lines.length, options.context);
            /*istanbul ignore start*/
            /*istanbul ignore end*/
            /*istanbul ignore start*/
            (_curRange3 =
            /*istanbul ignore end*/
            curRange).push.apply(
            /*istanbul ignore start*/
            _curRange3
            /*istanbul ignore end*/
            ,
            /*istanbul ignore start*/
            _toConsumableArray(
            /*istanbul ignore end*/
            contextLines(lines.slice(0, contextSize))));
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
    for (var i = 0; i < diff.length; i++)
    /*istanbul ignore start*/
    {
      _loop();
    }

    // Step 2: eliminate the trailing `\n` from each line of each hunk, and, where needed, add
    //         "\ No newline at end of file".
    /*istanbul ignore end*/
    for (
    /*istanbul ignore start*/
    var _i = 0, _hunks =
      /*istanbul ignore end*/
      hunks;
    /*istanbul ignore start*/
    _i < _hunks.length
    /*istanbul ignore end*/
    ;
    /*istanbul ignore start*/
    _i++
    /*istanbul ignore end*/
    ) {
      var hunk =
      /*istanbul ignore start*/
      _hunks[_i]
      /*istanbul ignore end*/
      ;
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
  /*istanbul ignore start*/
  var _options2;
  /*istanbul ignore end*/
  if (typeof options === 'function') {
    options = {
      callback: options
    };
  }
  if (!
  /*istanbul ignore start*/
  ((_options2 =
  /*istanbul ignore end*/
  options) !== null && _options2 !== void 0 &&
  /*istanbul ignore start*/
  _options2
  /*istanbul ignore end*/
  .callback)) {
    var patchObj = structuredPatch(oldFileName, newFileName, oldStr, newStr, oldHeader, newHeader, options);
    if (!patchObj) {
      return;
    }
    return formatPatch(patchObj);
  } else {
    var
      /*istanbul ignore start*/
      _options3 =
      /*istanbul ignore end*/
      options,
      /*istanbul ignore start*/
      /*istanbul ignore end*/
      _callback2 = _options3.callback;
    structuredPatch(oldFileName, newFileName, oldStr, newStr, oldHeader, newHeader,
    /*istanbul ignore start*/
    _objectSpread(_objectSpread({},
    /*istanbul ignore end*/
    options), {}, {
      callback: function
      /*istanbul ignore start*/
      callback
      /*istanbul ignore end*/
      (patchObj) {
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
  var result = text.split('\n').map(function (line)
  /*istanbul ignore start*/
  {
    return (
      /*istanbul ignore end*/
      line + '\n'
    );
  });
  if (hasTrailingNl) {
    result.pop();
  } else {
    result.push(result.pop().slice(0, -1));
  }
  return result;
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfbGluZSIsInJlcXVpcmUiLCJfdHlwZW9mIiwibyIsIlN5bWJvbCIsIml0ZXJhdG9yIiwiY29uc3RydWN0b3IiLCJwcm90b3R5cGUiLCJfdG9Db25zdW1hYmxlQXJyYXkiLCJhcnIiLCJfYXJyYXlXaXRob3V0SG9sZXMiLCJfaXRlcmFibGVUb0FycmF5IiwiX3Vuc3VwcG9ydGVkSXRlcmFibGVUb0FycmF5IiwiX25vbkl0ZXJhYmxlU3ByZWFkIiwiVHlwZUVycm9yIiwibWluTGVuIiwiX2FycmF5TGlrZVRvQXJyYXkiLCJuIiwiT2JqZWN0IiwidG9TdHJpbmciLCJjYWxsIiwic2xpY2UiLCJuYW1lIiwiQXJyYXkiLCJmcm9tIiwidGVzdCIsIml0ZXIiLCJpc0FycmF5IiwibGVuIiwibGVuZ3RoIiwiaSIsImFycjIiLCJvd25LZXlzIiwiZSIsInIiLCJ0Iiwia2V5cyIsImdldE93blByb3BlcnR5U3ltYm9scyIsImZpbHRlciIsImdldE93blByb3BlcnR5RGVzY3JpcHRvciIsImVudW1lcmFibGUiLCJwdXNoIiwiYXBwbHkiLCJfb2JqZWN0U3ByZWFkIiwiYXJndW1lbnRzIiwiZm9yRWFjaCIsIl9kZWZpbmVQcm9wZXJ0eSIsImdldE93blByb3BlcnR5RGVzY3JpcHRvcnMiLCJkZWZpbmVQcm9wZXJ0aWVzIiwiZGVmaW5lUHJvcGVydHkiLCJvYmoiLCJrZXkiLCJ2YWx1ZSIsIl90b1Byb3BlcnR5S2V5IiwiY29uZmlndXJhYmxlIiwid3JpdGFibGUiLCJfdG9QcmltaXRpdmUiLCJ0b1ByaW1pdGl2ZSIsIlN0cmluZyIsIk51bWJlciIsInN0cnVjdHVyZWRQYXRjaCIsIm9sZEZpbGVOYW1lIiwibmV3RmlsZU5hbWUiLCJvbGRTdHIiLCJuZXdTdHIiLCJvbGRIZWFkZXIiLCJuZXdIZWFkZXIiLCJvcHRpb25zIiwiY2FsbGJhY2siLCJjb250ZXh0IiwibmV3bGluZUlzVG9rZW4iLCJFcnJvciIsImRpZmZMaW5lc1Jlc3VsdFRvUGF0Y2giLCJkaWZmTGluZXMiLCJfb3B0aW9ucyIsImRpZmYiLCJwYXRjaCIsImxpbmVzIiwiY29udGV4dExpbmVzIiwibWFwIiwiZW50cnkiLCJodW5rcyIsIm9sZFJhbmdlU3RhcnQiLCJuZXdSYW5nZVN0YXJ0IiwiY3VyUmFuZ2UiLCJvbGRMaW5lIiwibmV3TGluZSIsIl9sb29wIiwiY3VycmVudCIsInNwbGl0TGluZXMiLCJhZGRlZCIsInJlbW92ZWQiLCJfY3VyUmFuZ2UiLCJwcmV2IiwiX2N1clJhbmdlMiIsIl9jdXJSYW5nZTMiLCJjb250ZXh0U2l6ZSIsIk1hdGgiLCJtaW4iLCJodW5rIiwib2xkU3RhcnQiLCJvbGRMaW5lcyIsIm5ld1N0YXJ0IiwibmV3TGluZXMiLCJfaSIsIl9odW5rcyIsImVuZHNXaXRoIiwic3BsaWNlIiwiZm9ybWF0UGF0Y2giLCJqb2luIiwicmV0IiwiY3JlYXRlVHdvRmlsZXNQYXRjaCIsIl9vcHRpb25zMiIsInBhdGNoT2JqIiwiX29wdGlvbnMzIiwiY3JlYXRlUGF0Y2giLCJmaWxlTmFtZSIsInRleHQiLCJoYXNUcmFpbGluZ05sIiwicmVzdWx0Iiwic3BsaXQiLCJsaW5lIiwicG9wIl0sInNvdXJjZXMiOlsiLi4vLi4vc3JjL3BhdGNoL2NyZWF0ZS5qcyJdLCJzb3VyY2VzQ29udGVudCI6WyJpbXBvcnQge2RpZmZMaW5lc30gZnJvbSAnLi4vZGlmZi9saW5lJztcblxuZXhwb3J0IGZ1bmN0aW9uIHN0cnVjdHVyZWRQYXRjaChvbGRGaWxlTmFtZSwgbmV3RmlsZU5hbWUsIG9sZFN0ciwgbmV3U3RyLCBvbGRIZWFkZXIsIG5ld0hlYWRlciwgb3B0aW9ucykge1xuICBpZiAoIW9wdGlvbnMpIHtcbiAgICBvcHRpb25zID0ge307XG4gIH1cbiAgaWYgKHR5cGVvZiBvcHRpb25zID09PSAnZnVuY3Rpb24nKSB7XG4gICAgb3B0aW9ucyA9IHtjYWxsYmFjazogb3B0aW9uc307XG4gIH1cbiAgaWYgKHR5cGVvZiBvcHRpb25zLmNvbnRleHQgPT09ICd1bmRlZmluZWQnKSB7XG4gICAgb3B0aW9ucy5jb250ZXh0ID0gNDtcbiAgfVxuICBpZiAob3B0aW9ucy5uZXdsaW5lSXNUb2tlbikge1xuICAgIHRocm93IG5ldyBFcnJvcignbmV3bGluZUlzVG9rZW4gbWF5IG5vdCBiZSB1c2VkIHdpdGggcGF0Y2gtZ2VuZXJhdGlvbiBmdW5jdGlvbnMsIG9ubHkgd2l0aCBkaWZmaW5nIGZ1bmN0aW9ucycpO1xuICB9XG5cbiAgaWYgKCFvcHRpb25zLmNhbGxiYWNrKSB7XG4gICAgcmV0dXJuIGRpZmZMaW5lc1Jlc3VsdFRvUGF0Y2goZGlmZkxpbmVzKG9sZFN0ciwgbmV3U3RyLCBvcHRpb25zKSk7XG4gIH0gZWxzZSB7XG4gICAgY29uc3Qge2NhbGxiYWNrfSA9IG9wdGlvbnM7XG4gICAgZGlmZkxpbmVzKFxuICAgICAgb2xkU3RyLFxuICAgICAgbmV3U3RyLFxuICAgICAge1xuICAgICAgICAuLi5vcHRpb25zLFxuICAgICAgICBjYWxsYmFjazogKGRpZmYpID0+IHtcbiAgICAgICAgICBjb25zdCBwYXRjaCA9IGRpZmZMaW5lc1Jlc3VsdFRvUGF0Y2goZGlmZik7XG4gICAgICAgICAgY2FsbGJhY2socGF0Y2gpO1xuICAgICAgICB9XG4gICAgICB9XG4gICAgKTtcbiAgfVxuXG4gIGZ1bmN0aW9uIGRpZmZMaW5lc1Jlc3VsdFRvUGF0Y2goZGlmZikge1xuICAgIC8vIFNURVAgMTogQnVpbGQgdXAgdGhlIHBhdGNoIHdpdGggbm8gXCJcXCBObyBuZXdsaW5lIGF0IGVuZCBvZiBmaWxlXCIgbGluZXMgYW5kIHdpdGggdGhlIGFycmF5c1xuICAgIC8vICAgICAgICAgb2YgbGluZXMgY29udGFpbmluZyB0cmFpbGluZyBuZXdsaW5lIGNoYXJhY3RlcnMuIFdlJ2xsIHRpZHkgdXAgbGF0ZXIuLi5cblxuICAgIGlmKCFkaWZmKSB7XG4gICAgICByZXR1cm47XG4gICAgfVxuXG4gICAgZGlmZi5wdXNoKHt2YWx1ZTogJycsIGxpbmVzOiBbXX0pOyAvLyBBcHBlbmQgYW4gZW1wdHkgdmFsdWUgdG8gbWFrZSBjbGVhbnVwIGVhc2llclxuXG4gICAgZnVuY3Rpb24gY29udGV4dExpbmVzKGxpbmVzKSB7XG4gICAgICByZXR1cm4gbGluZXMubWFwKGZ1bmN0aW9uKGVudHJ5KSB7IHJldHVybiAnICcgKyBlbnRyeTsgfSk7XG4gICAgfVxuXG4gICAgbGV0IGh1bmtzID0gW107XG4gICAgbGV0IG9sZFJhbmdlU3RhcnQgPSAwLCBuZXdSYW5nZVN0YXJ0ID0gMCwgY3VyUmFuZ2UgPSBbXSxcbiAgICAgICAgb2xkTGluZSA9IDEsIG5ld0xpbmUgPSAxO1xuICAgIGZvciAobGV0IGkgPSAwOyBpIDwgZGlmZi5sZW5ndGg7IGkrKykge1xuICAgICAgY29uc3QgY3VycmVudCA9IGRpZmZbaV0sXG4gICAgICAgICAgICBsaW5lcyA9IGN1cnJlbnQubGluZXMgfHwgc3BsaXRMaW5lcyhjdXJyZW50LnZhbHVlKTtcbiAgICAgIGN1cnJlbnQubGluZXMgPSBsaW5lcztcblxuICAgICAgaWYgKGN1cnJlbnQuYWRkZWQgfHwgY3VycmVudC5yZW1vdmVkKSB7XG4gICAgICAgIC8vIElmIHdlIGhhdmUgcHJldmlvdXMgY29udGV4dCwgc3RhcnQgd2l0aCB0aGF0XG4gICAgICAgIGlmICghb2xkUmFuZ2VTdGFydCkge1xuICAgICAgICAgIGNvbnN0IHByZXYgPSBkaWZmW2kgLSAxXTtcbiAgICAgICAgICBvbGRSYW5nZVN0YXJ0ID0gb2xkTGluZTtcbiAgICAgICAgICBuZXdSYW5nZVN0YXJ0ID0gbmV3TGluZTtcblxuICAgICAgICAgIGlmIChwcmV2KSB7XG4gICAgICAgICAgICBjdXJSYW5nZSA9IG9wdGlvbnMuY29udGV4dCA+IDAgPyBjb250ZXh0TGluZXMocHJldi5saW5lcy5zbGljZSgtb3B0aW9ucy5jb250ZXh0KSkgOiBbXTtcbiAgICAgICAgICAgIG9sZFJhbmdlU3RhcnQgLT0gY3VyUmFuZ2UubGVuZ3RoO1xuICAgICAgICAgICAgbmV3UmFuZ2VTdGFydCAtPSBjdXJSYW5nZS5sZW5ndGg7XG4gICAgICAgICAgfVxuICAgICAgICB9XG5cbiAgICAgICAgLy8gT3V0cHV0IG91ciBjaGFuZ2VzXG4gICAgICAgIGN1clJhbmdlLnB1c2goLi4uIGxpbmVzLm1hcChmdW5jdGlvbihlbnRyeSkge1xuICAgICAgICAgIHJldHVybiAoY3VycmVudC5hZGRlZCA/ICcrJyA6ICctJykgKyBlbnRyeTtcbiAgICAgICAgfSkpO1xuXG4gICAgICAgIC8vIFRyYWNrIHRoZSB1cGRhdGVkIGZpbGUgcG9zaXRpb25cbiAgICAgICAgaWYgKGN1cnJlbnQuYWRkZWQpIHtcbiAgICAgICAgICBuZXdMaW5lICs9IGxpbmVzLmxlbmd0aDtcbiAgICAgICAgfSBlbHNlIHtcbiAgICAgICAgICBvbGRMaW5lICs9IGxpbmVzLmxlbmd0aDtcbiAgICAgICAgfVxuICAgICAgfSBlbHNlIHtcbiAgICAgICAgLy8gSWRlbnRpY2FsIGNvbnRleHQgbGluZXMuIFRyYWNrIGxpbmUgY2hhbmdlc1xuICAgICAgICBpZiAob2xkUmFuZ2VTdGFydCkge1xuICAgICAgICAgIC8vIENsb3NlIG91dCBhbnkgY2hhbmdlcyB0aGF0IGhhdmUgYmVlbiBvdXRwdXQgKG9yIGpvaW4gb3ZlcmxhcHBpbmcpXG4gICAgICAgICAgaWYgKGxpbmVzLmxlbmd0aCA8PSBvcHRpb25zLmNvbnRleHQgKiAyICYmIGkgPCBkaWZmLmxlbmd0aCAtIDIpIHtcbiAgICAgICAgICAgIC8vIE92ZXJsYXBwaW5nXG4gICAgICAgICAgICBjdXJSYW5nZS5wdXNoKC4uLiBjb250ZXh0TGluZXMobGluZXMpKTtcbiAgICAgICAgICB9IGVsc2Uge1xuICAgICAgICAgICAgLy8gZW5kIHRoZSByYW5nZSBhbmQgb3V0cHV0XG4gICAgICAgICAgICBsZXQgY29udGV4dFNpemUgPSBNYXRoLm1pbihsaW5lcy5sZW5ndGgsIG9wdGlvbnMuY29udGV4dCk7XG4gICAgICAgICAgICBjdXJSYW5nZS5wdXNoKC4uLiBjb250ZXh0TGluZXMobGluZXMuc2xpY2UoMCwgY29udGV4dFNpemUpKSk7XG5cbiAgICAgICAgICAgIGxldCBodW5rID0ge1xuICAgICAgICAgICAgICBvbGRTdGFydDogb2xkUmFuZ2VTdGFydCxcbiAgICAgICAgICAgICAgb2xkTGluZXM6IChvbGRMaW5lIC0gb2xkUmFuZ2VTdGFydCArIGNvbnRleHRTaXplKSxcbiAgICAgICAgICAgICAgbmV3U3RhcnQ6IG5ld1JhbmdlU3RhcnQsXG4gICAgICAgICAgICAgIG5ld0xpbmVzOiAobmV3TGluZSAtIG5ld1JhbmdlU3RhcnQgKyBjb250ZXh0U2l6ZSksXG4gICAgICAgICAgICAgIGxpbmVzOiBjdXJSYW5nZVxuICAgICAgICAgICAgfTtcbiAgICAgICAgICAgIGh1bmtzLnB1c2goaHVuayk7XG5cbiAgICAgICAgICAgIG9sZFJhbmdlU3RhcnQgPSAwO1xuICAgICAgICAgICAgbmV3UmFuZ2VTdGFydCA9IDA7XG4gICAgICAgICAgICBjdXJSYW5nZSA9IFtdO1xuICAgICAgICAgIH1cbiAgICAgICAgfVxuICAgICAgICBvbGRMaW5lICs9IGxpbmVzLmxlbmd0aDtcbiAgICAgICAgbmV3TGluZSArPSBsaW5lcy5sZW5ndGg7XG4gICAgICB9XG4gICAgfVxuXG4gICAgLy8gU3RlcCAyOiBlbGltaW5hdGUgdGhlIHRyYWlsaW5nIGBcXG5gIGZyb20gZWFjaCBsaW5lIG9mIGVhY2ggaHVuaywgYW5kLCB3aGVyZSBuZWVkZWQsIGFkZFxuICAgIC8vICAgICAgICAgXCJcXCBObyBuZXdsaW5lIGF0IGVuZCBvZiBmaWxlXCIuXG4gICAgZm9yIChjb25zdCBodW5rIG9mIGh1bmtzKSB7XG4gICAgICBmb3IgKGxldCBpID0gMDsgaSA8IGh1bmsubGluZXMubGVuZ3RoOyBpKyspIHtcbiAgICAgICAgaWYgKGh1bmsubGluZXNbaV0uZW5kc1dpdGgoJ1xcbicpKSB7XG4gICAgICAgICAgaHVuay5saW5lc1tpXSA9IGh1bmsubGluZXNbaV0uc2xpY2UoMCwgLTEpO1xuICAgICAgICB9IGVsc2Uge1xuICAgICAgICAgIGh1bmsubGluZXMuc3BsaWNlKGkgKyAxLCAwLCAnXFxcXCBObyBuZXdsaW5lIGF0IGVuZCBvZiBmaWxlJyk7XG4gICAgICAgICAgaSsrOyAvLyBTa2lwIHRoZSBsaW5lIHdlIGp1c3QgYWRkZWQsIHRoZW4gY29udGludWUgaXRlcmF0aW5nXG4gICAgICAgIH1cbiAgICAgIH1cbiAgICB9XG5cbiAgICByZXR1cm4ge1xuICAgICAgb2xkRmlsZU5hbWU6IG9sZEZpbGVOYW1lLCBuZXdGaWxlTmFtZTogbmV3RmlsZU5hbWUsXG4gICAgICBvbGRIZWFkZXI6IG9sZEhlYWRlciwgbmV3SGVhZGVyOiBuZXdIZWFkZXIsXG4gICAgICBodW5rczogaHVua3NcbiAgICB9O1xuICB9XG59XG5cbmV4cG9ydCBmdW5jdGlvbiBmb3JtYXRQYXRjaChkaWZmKSB7XG4gIGlmIChBcnJheS5pc0FycmF5KGRpZmYpKSB7XG4gICAgcmV0dXJuIGRpZmYubWFwKGZvcm1hdFBhdGNoKS5qb2luKCdcXG4nKTtcbiAgfVxuXG4gIGNvbnN0IHJldCA9IFtdO1xuICBpZiAoZGlmZi5vbGRGaWxlTmFtZSA9PSBkaWZmLm5ld0ZpbGVOYW1lKSB7XG4gICAgcmV0LnB1c2goJ0luZGV4OiAnICsgZGlmZi5vbGRGaWxlTmFtZSk7XG4gIH1cbiAgcmV0LnB1c2goJz09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0nKTtcbiAgcmV0LnB1c2goJy0tLSAnICsgZGlmZi5vbGRGaWxlTmFtZSArICh0eXBlb2YgZGlmZi5vbGRIZWFkZXIgPT09ICd1bmRlZmluZWQnID8gJycgOiAnXFx0JyArIGRpZmYub2xkSGVhZGVyKSk7XG4gIHJldC5wdXNoKCcrKysgJyArIGRpZmYubmV3RmlsZU5hbWUgKyAodHlwZW9mIGRpZmYubmV3SGVhZGVyID09PSAndW5kZWZpbmVkJyA/ICcnIDogJ1xcdCcgKyBkaWZmLm5ld0hlYWRlcikpO1xuXG4gIGZvciAobGV0IGkgPSAwOyBpIDwgZGlmZi5odW5rcy5sZW5ndGg7IGkrKykge1xuICAgIGNvbnN0IGh1bmsgPSBkaWZmLmh1bmtzW2ldO1xuICAgIC8vIFVuaWZpZWQgRGlmZiBGb3JtYXQgcXVpcms6IElmIHRoZSBjaHVuayBzaXplIGlzIDAsXG4gICAgLy8gdGhlIGZpcnN0IG51bWJlciBpcyBvbmUgbG93ZXIgdGhhbiBvbmUgd291bGQgZXhwZWN0LlxuICAgIC8vIGh0dHBzOi8vd3d3LmFydGltYS5jb20vd2VibG9ncy92aWV3cG9zdC5qc3A/dGhyZWFkPTE2NDI5M1xuICAgIGlmIChodW5rLm9sZExpbmVzID09PSAwKSB7XG4gICAgICBodW5rLm9sZFN0YXJ0IC09IDE7XG4gICAgfVxuICAgIGlmIChodW5rLm5ld0xpbmVzID09PSAwKSB7XG4gICAgICBodW5rLm5ld1N0YXJ0IC09IDE7XG4gICAgfVxuICAgIHJldC5wdXNoKFxuICAgICAgJ0BAIC0nICsgaHVuay5vbGRTdGFydCArICcsJyArIGh1bmsub2xkTGluZXNcbiAgICAgICsgJyArJyArIGh1bmsubmV3U3RhcnQgKyAnLCcgKyBodW5rLm5ld0xpbmVzXG4gICAgICArICcgQEAnXG4gICAgKTtcbiAgICByZXQucHVzaC5hcHBseShyZXQsIGh1bmsubGluZXMpO1xuICB9XG5cbiAgcmV0dXJuIHJldC5qb2luKCdcXG4nKSArICdcXG4nO1xufVxuXG5leHBvcnQgZnVuY3Rpb24gY3JlYXRlVHdvRmlsZXNQYXRjaChvbGRGaWxlTmFtZSwgbmV3RmlsZU5hbWUsIG9sZFN0ciwgbmV3U3RyLCBvbGRIZWFkZXIsIG5ld0hlYWRlciwgb3B0aW9ucykge1xuICBpZiAodHlwZW9mIG9wdGlvbnMgPT09ICdmdW5jdGlvbicpIHtcbiAgICBvcHRpb25zID0ge2NhbGxiYWNrOiBvcHRpb25zfTtcbiAgfVxuXG4gIGlmICghb3B0aW9ucz8uY2FsbGJhY2spIHtcbiAgICBjb25zdCBwYXRjaE9iaiA9IHN0cnVjdHVyZWRQYXRjaChvbGRGaWxlTmFtZSwgbmV3RmlsZU5hbWUsIG9sZFN0ciwgbmV3U3RyLCBvbGRIZWFkZXIsIG5ld0hlYWRlciwgb3B0aW9ucyk7XG4gICAgaWYgKCFwYXRjaE9iaikge1xuICAgICAgcmV0dXJuO1xuICAgIH1cbiAgICByZXR1cm4gZm9ybWF0UGF0Y2gocGF0Y2hPYmopO1xuICB9IGVsc2Uge1xuICAgIGNvbnN0IHtjYWxsYmFja30gPSBvcHRpb25zO1xuICAgIHN0cnVjdHVyZWRQYXRjaChcbiAgICAgIG9sZEZpbGVOYW1lLFxuICAgICAgbmV3RmlsZU5hbWUsXG4gICAgICBvbGRTdHIsXG4gICAgICBuZXdTdHIsXG4gICAgICBvbGRIZWFkZXIsXG4gICAgICBuZXdIZWFkZXIsXG4gICAgICB7XG4gICAgICAgIC4uLm9wdGlvbnMsXG4gICAgICAgIGNhbGxiYWNrOiBwYXRjaE9iaiA9PiB7XG4gICAgICAgICAgaWYgKCFwYXRjaE9iaikge1xuICAgICAgICAgICAgY2FsbGJhY2soKTtcbiAgICAgICAgICB9IGVsc2Uge1xuICAgICAgICAgICAgY2FsbGJhY2soZm9ybWF0UGF0Y2gocGF0Y2hPYmopKTtcbiAgICAgICAgICB9XG4gICAgICAgIH1cbiAgICAgIH1cbiAgICApO1xuICB9XG59XG5cbmV4cG9ydCBmdW5jdGlvbiBjcmVhdGVQYXRjaChmaWxlTmFtZSwgb2xkU3RyLCBuZXdTdHIsIG9sZEhlYWRlciwgbmV3SGVhZGVyLCBvcHRpb25zKSB7XG4gIHJldHVybiBjcmVhdGVUd29GaWxlc1BhdGNoKGZpbGVOYW1lLCBmaWxlTmFtZSwgb2xkU3RyLCBuZXdTdHIsIG9sZEhlYWRlciwgbmV3SGVhZGVyLCBvcHRpb25zKTtcbn1cblxuLyoqXG4gKiBTcGxpdCBgdGV4dGAgaW50byBhbiBhcnJheSBvZiBsaW5lcywgaW5jbHVkaW5nIHRoZSB0cmFpbGluZyBuZXdsaW5lIGNoYXJhY3RlciAod2hlcmUgcHJlc2VudClcbiAqL1xuZnVuY3Rpb24gc3BsaXRMaW5lcyh0ZXh0KSB7XG4gIGNvbnN0IGhhc1RyYWlsaW5nTmwgPSB0ZXh0LmVuZHNXaXRoKCdcXG4nKTtcbiAgY29uc3QgcmVzdWx0ID0gdGV4dC5zcGxpdCgnXFxuJykubWFwKGxpbmUgPT4gbGluZSArICdcXG4nKTtcbiAgaWYgKGhhc1RyYWlsaW5nTmwpIHtcbiAgICByZXN1bHQucG9wKCk7XG4gIH0gZWxzZSB7XG4gICAgcmVzdWx0LnB1c2gocmVzdWx0LnBvcCgpLnNsaWNlKDAsIC0xKSk7XG4gIH1cbiAgcmV0dXJuIHJlc3VsdDtcbn1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7Ozs7QUFBQTtBQUFBO0FBQUFBLEtBQUEsR0FBQUMsT0FBQTtBQUFBO0FBQUE7QUFBdUMsbUNBQUFDLFFBQUFDLENBQUEsc0NBQUFELE9BQUEsd0JBQUFFLE1BQUEsdUJBQUFBLE1BQUEsQ0FBQUMsUUFBQSxhQUFBRixDQUFBLGtCQUFBQSxDQUFBLGdCQUFBQSxDQUFBLFdBQUFBLENBQUEseUJBQUFDLE1BQUEsSUFBQUQsQ0FBQSxDQUFBRyxXQUFBLEtBQUFGLE1BQUEsSUFBQUQsQ0FBQSxLQUFBQyxNQUFBLENBQUFHLFNBQUEscUJBQUFKLENBQUEsS0FBQUQsT0FBQSxDQUFBQyxDQUFBO0FBQUEsU0FBQUssbUJBQUFDLEdBQUEsV0FBQUMsa0JBQUEsQ0FBQUQsR0FBQSxLQUFBRSxnQkFBQSxDQUFBRixHQUFBLEtBQUFHLDJCQUFBLENBQUFILEdBQUEsS0FBQUksa0JBQUE7QUFBQSxTQUFBQSxtQkFBQSxjQUFBQyxTQUFBO0FBQUEsU0FBQUYsNEJBQUFULENBQUEsRUFBQVksTUFBQSxTQUFBWixDQUFBLHFCQUFBQSxDQUFBLHNCQUFBYSxpQkFBQSxDQUFBYixDQUFBLEVBQUFZLE1BQUEsT0FBQUUsQ0FBQSxHQUFBQyxNQUFBLENBQUFYLFNBQUEsQ0FBQVksUUFBQSxDQUFBQyxJQUFBLENBQUFqQixDQUFBLEVBQUFrQixLQUFBLGFBQUFKLENBQUEsaUJBQUFkLENBQUEsQ0FBQUcsV0FBQSxFQUFBVyxDQUFBLEdBQUFkLENBQUEsQ0FBQUcsV0FBQSxDQUFBZ0IsSUFBQSxNQUFBTCxDQUFBLGNBQUFBLENBQUEsbUJBQUFNLEtBQUEsQ0FBQUMsSUFBQSxDQUFBckIsQ0FBQSxPQUFBYyxDQUFBLCtE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
