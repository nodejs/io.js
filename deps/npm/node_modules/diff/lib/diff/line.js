/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.diffLines = diffLines;
exports.diffTrimmedLines = diffTrimmedLines;
exports.lineDiff = void 0;
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_base = _interopRequireDefault(require("./base"))
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_params = require("../util/params")
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { "default": obj }; }
/*istanbul ignore end*/
var lineDiff =
/*istanbul ignore start*/
exports.lineDiff =
/*istanbul ignore end*/
new
/*istanbul ignore start*/
_base
/*istanbul ignore end*/
[
/*istanbul ignore start*/
"default"
/*istanbul ignore end*/
]();
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
  return (
    /*istanbul ignore start*/
    _base
    /*istanbul ignore end*/
    [
    /*istanbul ignore start*/
    "default"
    /*istanbul ignore end*/
    ].prototype.equals.call(this, left, right, options)
  );
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
  var options =
  /*istanbul ignore start*/
  (0,
  /*istanbul ignore end*/
  /*istanbul ignore start*/
  _params
  /*istanbul ignore end*/
  .
  /*istanbul ignore start*/
  generateOptions)
  /*istanbul ignore end*/
  (callback, {
    ignoreWhitespace: true
  });
  return lineDiff.diff(oldStr, newStr, options);
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfYmFzZSIsIl9pbnRlcm9wUmVxdWlyZURlZmF1bHQiLCJyZXF1aXJlIiwiX3BhcmFtcyIsIm9iaiIsIl9fZXNNb2R1bGUiLCJsaW5lRGlmZiIsImV4cG9ydHMiLCJEaWZmIiwidG9rZW5pemUiLCJ2YWx1ZSIsIm9wdGlvbnMiLCJzdHJpcFRyYWlsaW5nQ3IiLCJyZXBsYWNlIiwicmV0TGluZXMiLCJsaW5lc0FuZE5ld2xpbmVzIiwic3BsaXQiLCJsZW5ndGgiLCJwb3AiLCJpIiwibGluZSIsIm5ld2xpbmVJc1Rva2VuIiwicHVzaCIsImVxdWFscyIsImxlZnQiLCJyaWdodCIsImlnbm9yZVdoaXRlc3BhY2UiLCJpbmNsdWRlcyIsInRyaW0iLCJpZ25vcmVOZXdsaW5lQXRFb2YiLCJlbmRzV2l0aCIsInNsaWNlIiwicHJvdG90eXBlIiwiY2FsbCIsImRpZmZMaW5lcyIsIm9sZFN0ciIsIm5ld1N0ciIsImNhbGxiYWNrIiwiZGlmZiIsImRpZmZUcmltbWVkTGluZXMiLCJnZW5lcmF0ZU9wdGlvbnMiXSwic291cmNlcyI6WyIuLi8uLi9zcmMvZGlmZi9saW5lLmpzIl0sInNvdXJjZXNDb250ZW50IjpbImltcG9ydCBEaWZmIGZyb20gJy4vYmFzZSc7XG5pbXBvcnQge2dlbmVyYXRlT3B0aW9uc30gZnJvbSAnLi4vdXRpbC9wYXJhbXMnO1xuXG5leHBvcnQgY29uc3QgbGluZURpZmYgPSBuZXcgRGlmZigpO1xubGluZURpZmYudG9rZW5pemUgPSBmdW5jdGlvbih2YWx1ZSwgb3B0aW9ucykge1xuICBpZihvcHRpb25zLnN0cmlwVHJhaWxpbmdDcikge1xuICAgIC8vIHJlbW92ZSBvbmUgXFxyIGJlZm9yZSBcXG4gdG8gbWF0Y2ggR05VIGRpZmYncyAtLXN0cmlwLXRyYWlsaW5nLWNyIGJlaGF2aW9yXG4gICAgdmFsdWUgPSB2YWx1ZS5yZXBsYWNlKC9cXHJcXG4vZywgJ1xcbicpO1xuICB9XG5cbiAgbGV0IHJldExpbmVzID0gW10sXG4gICAgICBsaW5lc0FuZE5ld2xpbmVzID0gdmFsdWUuc3BsaXQoLyhcXG58XFxyXFxuKS8pO1xuXG4gIC8vIElnbm9yZSB0aGUgZmluYWwgZW1wdHkgdG9rZW4gdGhhdCBvY2N1cnMgaWYgdGhlIHN0cmluZyBlbmRzIHdpdGggYSBuZXcgbGluZVxuICBpZiAoIWxpbmVzQW5kTmV3bGluZXNbbGluZXNBbmROZXdsaW5lcy5sZW5ndGggLSAxXSkge1xuICAgIGxpbmVzQW5kTmV3bGluZXMucG9wKCk7XG4gIH1cblxuICAvLyBNZXJnZSB0aGUgY29udGVudCBhbmQgbGluZSBzZXBhcmF0b3JzIGludG8gc2luZ2xlIHRva2Vuc1xuICBmb3IgKGxldCBpID0gMDsgaSA8IGxpbmVzQW5kTmV3bGluZXMubGVuZ3RoOyBpKyspIHtcbiAgICBsZXQgbGluZSA9IGxpbmVzQW5kTmV3bGluZXNbaV07XG5cbiAgICBpZiAoaSAlIDIgJiYgIW9wdGlvbnMubmV3bGluZUlzVG9rZW4pIHtcbiAgICAgIHJldExpbmVzW3JldExpbmVzLmxlbmd0aCAtIDFdICs9IGxpbmU7XG4gICAgfSBlbHNlIHtcbiAgICAgIHJldExpbmVzLnB1c2gobGluZSk7XG4gICAgfVxuICB9XG5cbiAgcmV0dXJuIHJldExpbmVzO1xufTtcblxubGluZURpZmYuZXF1YWxzID0gZnVuY3Rpb24obGVmdCwgcmlnaHQsIG9wdGlvbnMpIHtcbiAgLy8gSWYgd2UncmUgaWdub3Jpbmcgd2hpdGVzcGFjZSwgd2UgbmVlZCB0byBub3JtYWxpc2UgbGluZXMgYnkgc3RyaXBwaW5nXG4gIC8vIHdoaXRlc3BhY2UgYmVmb3JlIGNoZWNraW5nIGVxdWFsaXR5LiAoVGhpcyBoYXMgYW4gYW5ub3lpbmcgaW50ZXJhY3Rpb25cbiAgLy8gd2l0aCBuZXdsaW5lSXNUb2tlbiB0aGF0IHJlcXVpcmVzIHNwZWNpYWwgaGFuZGxpbmc6IGlmIG5ld2xpbmVzIGdldCB0aGVpclxuICAvLyBvd24gdG9rZW4sIHRoZW4gd2UgRE9OJ1Qgd2FudCB0byB0cmltIHRoZSAqbmV3bGluZSogdG9rZW5zIGRvd24gdG8gZW1wdHlcbiAgLy8gc3RyaW5ncywgc2luY2UgdGhpcyB3b3VsZCBjYXVzZSB1cyB0byB0cmVhdCB3aGl0ZXNwYWNlLW9ubHkgbGluZSBjb250ZW50XG4gIC8vIGFzIGVxdWFsIHRvIGEgc2VwYXJhdG9yIGJldHdlZW4gbGluZXMsIHdoaWNoIHdvdWxkIGJlIHdlaXJkIGFuZFxuICAvLyBpbmNvbnNpc3RlbnQgd2l0aCB0aGUgZG9jdW1lbnRlZCBiZWhhdmlvciBvZiB0aGUgb3B0aW9ucy4pXG4gIGlmIChvcHRpb25zLmlnbm9yZVdoaXRlc3BhY2UpIHtcbiAgICBpZiAoIW9wdGlvbnMubmV3bGluZUlzVG9rZW4gfHwgIWxlZnQuaW5jbHVkZXMoJ1xcbicpKSB7XG4gICAgICBsZWZ0ID0gbGVmdC50cmltKCk7XG4gICAgfVxuICAgIGlmICghb3B0aW9ucy5uZXdsaW5lSXNUb2tlbiB8fCAhcmlnaHQuaW5jbHVkZXMoJ1xcbicpKSB7XG4gICAgICByaWdodCA9IHJpZ2h0LnRyaW0oKTtcbiAgICB9XG4gIH0gZWxzZSBpZiAob3B0aW9ucy5pZ25vcmVOZXdsaW5lQXRFb2YgJiYgIW9wdGlvbnMubmV3bGluZUlzVG9rZW4pIHtcbiAgICBpZiAobGVmdC5lbmRzV2l0aCgnXFxuJykpIHtcbiAgICAgIGxlZnQgPSBsZWZ0LnNsaWNlKDAsIC0xKTtcbiAgICB9XG4gICAgaWYgKHJpZ2h0LmVuZHNXaXRoKCdcXG4nKSkge1xuICAgICAgcmlnaHQgPSByaWdodC5zbGljZSgwLCAtMSk7XG4gICAgfVxuICB9XG4gIHJldHVybiBEaWZmLnByb3RvdHlwZS5lcXVhbHMuY2FsbCh0aGlzLCBsZWZ0LCByaWdodCwgb3B0aW9ucyk7XG59O1xuXG5leHBvcnQgZnVuY3Rpb24gZGlmZkxpbmVzKG9sZFN0ciwgbmV3U3RyLCBjYWxsYmFjaykgeyByZXR1cm4gbGluZURpZmYuZGlmZihvbGRTdHIsIG5ld1N0ciwgY2FsbGJhY2spOyB9XG5cbi8vIEtlcHQgZm9yIGJhY2t3YXJkcyBjb21wYXRpYmlsaXR5LiBUaGlzIGlzIGEgcmF0aGVyIGFyYml0cmFyeSB3cmFwcGVyIG1ldGhvZFxuLy8gdGhhdCBqdXN0IGNhbGxzIGBkaWZmTGluZXNgIHdpdGggYGlnbm9yZVdoaXRlc3BhY2U6IHRydWVgLiBJdCdzIGNvbmZ1c2luZyB0b1xuLy8gaGF2ZSB0d28gd2F5cyB0byBkbyBleGFjdGx5IHRoZSBzYW1lIHRoaW5nIGluIHRoZSBBUEksIHNvIHdlIG5vIGxvbmdlclxuLy8gZG9jdW1lbnQgdGhpcyBvbmUgKGxpYnJhcnkgdXNlcnMgc2hvdWxkIGV4cGxpY2l0bHkgdXNlIGBkaWZmTGluZXNgIHdpdGhcbi8vIGBpZ25vcmVXaGl0ZXNwYWNlOiB0cnVlYCBpbnN0ZWFkKSBidXQgd2Uga2VlcCBpdCBhcm91bmQgdG8gbWFpbnRhaW5cbi8vIGNvbXBhdGliaWxpdHkgd2l0aCBjb2RlIHRoYXQgdXNlZCBvbGQgdmVyc2lvbnMuXG5leHBvcnQgZnVuY3Rpb24gZGlmZlRyaW1tZWRMaW5lcyhvbGRTdHIsIG5ld1N0ciwgY2FsbGJhY2spIHtcbiAgbGV0IG9wdGlvbnMgPSBnZW5lcmF0ZU9wdGlvbnMoY2FsbGJhY2ssIHtpZ25vcmVXaGl0ZXNwYWNlOiB0cnVlfSk7XG4gIHJldHVybiBsaW5lRGlmZi5kaWZmKG9sZFN0ciwgbmV3U3RyLCBvcHRpb25zKTtcbn1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7OztBQUFBO0FBQUE7QUFBQUEsS0FBQSxHQUFBQyxzQkFBQSxDQUFBQyxPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQUMsT0FBQSxHQUFBRCxPQUFBO0FBQUE7QUFBQTtBQUErQyxtQ0FBQUQsdUJBQUFHLEdBQUEsV0FBQUEsR0FBQSxJQUFBQSxHQUFBLENBQUFDLFVBQUEsR0FBQUQsR0FBQSxnQkFBQUEsR0FBQTtBQUFBO0FBRXhDLElBQU1FLFFBQVE7QUFBQTtBQUFBQyxPQUFBLENBQUFELFFBQUE7QUFBQTtBQUFHO0FBQUlFO0FBQUFBO0FBQUFBO0FBQUFBO0FBQUFBO0FBQUFBO0FBQUFBO0FBQUFBLENBQUksQ0FBQyxDQUFDO0FBQ2xDRixRQUFRLENBQUNHLFFBQVEsR0FBRyxVQUFTQyxLQUFLLEVBQUVDLE9BQU8sRUFBRTtFQUMzQyxJQUFHQSxPQUFPLENBQUNDLGVBQWUsRUFBRTtJQUMxQjtJQUNBRixLQUFLLEdBQUdBLEtBQUssQ0FBQ0csT0FBTyxDQUFDLE9BQU8sRUFBRSxJQUFJLENBQUM7RUFDdEM7RUFFQSxJQUFJQyxRQUFRLEdBQUcsRUFBRTtJQUNiQyxnQkFBZ0IsR0FBR0wsS0FBSyxDQUFDTSxLQUFLLENBQUMsV0FBVyxDQUFDOztFQUUvQztFQUNBLElBQUksQ0FBQ0QsZ0JBQWdCLENBQUNBLGdCQUFnQixDQUFDRSxNQUFNLEdBQUcsQ0FBQyxDQUFDLEVBQUU7SUFDbERGLGdCQUFnQixDQUFDRyxHQUFHLENBQUMsQ0FBQztFQUN4Qjs7RUFFQTtFQUNBLEtBQUssSUFBSUMsQ0FBQyxHQUFHLENBQUMsRUFBRUEsQ0FBQyxHQUFHSixnQkFBZ0IsQ0FBQ0UsTUFBTSxFQUFFRSxDQUFDLEVBQUUsRUFBRTtJQUNoRCxJQUFJQyxJQUFJLEdBQUdMLGdCQUFnQixDQUFDSSxDQUFDLENBQUM7SUFFOUIsSUFBSUEsQ0FBQyxHQUFHLENBQUMsSUFBSSxDQUFDUixPQUFPLENBQUNVLGNBQWMsRUFBRTtNQUNwQ1AsUUFBUSxDQUFDQSxRQUFRLENBQUNHLE1BQU0sR0FBRyxDQUFDLENBQUMsSUFBSUcsSUFBSTtJQUN2QyxDQUFDLE1BQU07TUFDTE4sUUFBUSxDQUFDUSxJQUFJLENBQUNGLElBQUksQ0FBQztJQUNyQjtFQUNGO0VBRUEsT0FBT04sUUFBUTtBQUNqQixDQUFDO0FBRURSLFFBQVEsQ0FBQ2lCLE1BQU0sR0FBRyxVQUFTQyxJQUFJLEVBQUVDLEtBQUssRUFBRWQsT0FBTyxFQUFFO0VBQy9DO0VBQ0E7RUFDQTtFQUNBO0VBQ0E7RUFDQTtFQUNBO0VBQ0EsSUFBSUEsT0FBTyxDQUFDZSxnQkFBZ0IsRUFBRTtJQUM1QixJQUFJLENBQUNmLE9BQU8sQ0FBQ1UsY0FBYyxJQUFJLENBQUNHLElBQUksQ0FBQ0csUUFBUSxDQUFDLElBQUksQ0FBQyxFQUFFO01BQ25ESCxJQUFJLEdBQUdBLElBQUksQ0FBQ0ksSUFBSSxDQUFDLENBQUM7SUFDcEI7SUFDQSxJQUFJLENBQUNqQixPQUFPLENBQUNVLGNBQWMsSUFBSSxDQUFDSSxLQUFLLENBQUNFLFFBQVEsQ0FBQyxJQUFJLENBQUMsRUFBRTtNQUNwREYsS0FBSyxHQUFHQSxLQUFLLENBQUNHLElBQUksQ0FBQyxDQUFDO0lBQ3RCO0VBQ0YsQ0FBQyxNQUFNLElBQUlqQixPQUFPLENBQUNrQixrQkFBa0IsSUFBSSxDQUFDbEIsT0FBTyxDQUFDVSxjQUFjLEVBQUU7SUFDaEUsSUFBSUcsSUFBSSxDQUFDTSxRQUFRLENBQUMsSUFBSSxDQUFDLEVBQUU7TUFDdkJOLElBQUksR0FBR0EsSUFBSSxDQUFDTyxLQUFLLENBQUMsQ0FBQyxFQUFFLENBQUMsQ0FBQyxDQUFDO0lBQzFCO0lBQ0EsSUFBSU4sS0FBSyxDQUFDSyxRQUFRLENBQUMsSUFBSSxDQUFDLEVBQUU7TUFDeEJMLEtBQUssR0FBR0EsS0FBSyxDQUFDTSxLQUFLLENBQUMsQ0FBQyxFQUFFLENBQUMsQ0FBQyxDQUFDO0lBQzVCO0VBQ0Y7RUFDQSxPQUFPdkI7SUFBQUE7SUFBQUE7SUFBQUE7SUFBQUE7SUFBQUE7SUFBQUE7SUFBQUE7SUFBQUEsQ0FBSSxDQUFDd0IsU0FBUyxDQUFDVCxNQUFNLENBQUNVLElBQUksQ0FBQyxJQUFJLEVBQUVULElBQUksRUFBRUMsS0FBSyxFQUFFZCxPQUFPO0VBQUM7QUFDL0QsQ0FBQztBQUVNLFNBQVN1QixTQUFTQSxDQUFDQyxNQUFNLEVBQUVDLE1BQU0sRUFBRUMsUUFBUSxFQUFFO0VBQUUsT0FBTy9CLFFBQVEsQ0FBQ2dDLElBQUksQ0FBQ0gsTUFBTSxFQUFFQyxNQUFNLEVBQUVDLFFBQVEsQ0FBQztBQUFFOztBQUV0RztBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDTyxTQUFTRSxnQkFBZ0JBLENBQUNKLE1BQU0sRUFBRUMsTUFBTSxFQUFFQyxRQUFRLEVBQUU7RUFDekQsSUFBSTFCLE9BQU87RUFBRztFQUFBO0VBQUE7RUFBQTZCO0VBQUFBO0VBQUFBO0VBQUFBO0VBQUFBO0VBQUFBLGVBQWU7RUFBQTtFQUFBLENBQUNILFFBQVEsRUFBRTtJQUFDWCxnQkFBZ0IsRUFBRTtFQUFJLENBQUMsQ0FBQztFQUNqRSxPQUFPcEIsUUFBUSxDQUFDZ0MsSUFBSSxDQUFDSCxNQUFNLEVBQUVDLE1BQU0sRUFBRXpCLE9BQU8sQ0FBQztBQUMvQyIsImlnbm9yZUxpc3QiOltdfQ==
