/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.isUnix = isUnix;
exports.isWin = isWin;
exports.unixToWin = unixToWin;
exports.winToUnix = winToUnix;
function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
function ownKeys(e, r) { var t = Object.keys(e); if (Object.getOwnPropertySymbols) { var o = Object.getOwnPropertySymbols(e); r && (o = o.filter(function (r) { return Object.getOwnPropertyDescriptor(e, r).enumerable; })), t.push.apply(t, o); } return t; }
function _objectSpread(e) { for (var r = 1; r < arguments.length; r++) { var t = null != arguments[r] ? arguments[r] : {}; r % 2 ? ownKeys(Object(t), !0).forEach(function (r) { _defineProperty(e, r, t[r]); }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) { Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r)); }); } return e; }
function _defineProperty(obj, key, value) { key = _toPropertyKey(key); if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == _typeof(i) ? i : i + ""; }
function _toPrimitive(t, r) { if ("object" != _typeof(t) || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r || "default"); if ("object" != _typeof(i)) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
/*istanbul ignore end*/
function unixToWin(patch) {
  if (Array.isArray(patch)) {
    return patch.map(unixToWin);
  }
  return (
    /*istanbul ignore start*/
    _objectSpread(_objectSpread({},
    /*istanbul ignore end*/
    patch), {}, {
      hunks: patch.hunks.map(function (hunk)
      /*istanbul ignore start*/
      {
        return _objectSpread(_objectSpread({},
        /*istanbul ignore end*/
        hunk), {}, {
          lines: hunk.lines.map(function (line, i)
          /*istanbul ignore start*/
          {
            var _hunk$lines;
            return (
              /*istanbul ignore end*/
              line.startsWith('\\') || line.endsWith('\r') ||
              /*istanbul ignore start*/
              (_hunk$lines =
              /*istanbul ignore end*/
              hunk.lines[i + 1]) !== null && _hunk$lines !== void 0 &&
              /*istanbul ignore start*/
              _hunk$lines
              /*istanbul ignore end*/
              .startsWith('\\') ? line : line + '\r'
            );
          })
        });
      })
    })
  );
}
function winToUnix(patch) {
  if (Array.isArray(patch)) {
    return patch.map(winToUnix);
  }
  return (
    /*istanbul ignore start*/
    _objectSpread(_objectSpread({},
    /*istanbul ignore end*/
    patch), {}, {
      hunks: patch.hunks.map(function (hunk)
      /*istanbul ignore start*/
      {
        return _objectSpread(_objectSpread({},
        /*istanbul ignore end*/
        hunk), {}, {
          lines: hunk.lines.map(function (line)
          /*istanbul ignore start*/
          {
            return (
              /*istanbul ignore end*/
              line.endsWith('\r') ? line.substring(0, line.length - 1) : line
            );
          })
        });
      })
    })
  );
}

/**
 * Returns true if the patch consistently uses Unix line endings (or only involves one line and has
 * no line endings).
 */
function isUnix(patch) {
  if (!Array.isArray(patch)) {
    patch = [patch];
  }
  return !patch.some(function (index)
  /*istanbul ignore start*/
  {
    return (
      /*istanbul ignore end*/
      index.hunks.some(function (hunk)
      /*istanbul ignore start*/
      {
        return (
          /*istanbul ignore end*/
          hunk.lines.some(function (line)
          /*istanbul ignore start*/
          {
            return (
              /*istanbul ignore end*/
              !line.startsWith('\\') && line.endsWith('\r')
            );
          })
        );
      })
    );
  });
}

/**
 * Returns true if the patch uses Windows line endings and only Windows line endings.
 */
function isWin(patch) {
  if (!Array.isArray(patch)) {
    patch = [patch];
  }
  return patch.some(function (index)
  /*istanbul ignore start*/
  {
    return (
      /*istanbul ignore end*/
      index.hunks.some(function (hunk)
      /*istanbul ignore start*/
      {
        return (
          /*istanbul ignore end*/
          hunk.lines.some(function (line)
          /*istanbul ignore start*/
          {
            return (
              /*istanbul ignore end*/
              line.endsWith('\r')
            );
          })
        );
      })
    );
  }) && patch.every(function (index)
  /*istanbul ignore start*/
  {
    return (
      /*istanbul ignore end*/
      index.hunks.every(function (hunk)
      /*istanbul ignore start*/
      {
        return (
          /*istanbul ignore end*/
          hunk.lines.every(function (line, i)
          /*istanbul ignore start*/
          {
            var _hunk$lines2;
            return (
              /*istanbul ignore end*/
              line.startsWith('\\') || line.endsWith('\r') ||
              /*istanbul ignore start*/
              ((_hunk$lines2 =
              /*istanbul ignore end*/
              hunk.lines[i + 1]) === null || _hunk$lines2 === void 0 ? void 0 :
              /*istanbul ignore start*/
              _hunk$lines2
              /*istanbul ignore end*/
              .startsWith('\\'))
            );
          })
        );
      })
    );
  });
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJ1bml4VG9XaW4iLCJwYXRjaCIsIkFycmF5IiwiaXNBcnJheSIsIm1hcCIsIl9vYmplY3RTcHJlYWQiLCJodW5rcyIsImh1bmsiLCJsaW5lcyIsImxpbmUiLCJpIiwiX2h1bmskbGluZXMiLCJzdGFydHNXaXRoIiwiZW5kc1dpdGgiLCJ3aW5Ub1VuaXgiLCJzdWJzdHJpbmciLCJsZW5ndGgiLCJpc1VuaXgiLCJzb21lIiwiaW5kZXgiLCJpc1dpbiIsImV2ZXJ5IiwiX2h1bmskbGluZXMyIl0sInNvdXJjZXMiOlsiLi4vLi4vc3JjL3BhdGNoL2xpbmUtZW5kaW5ncy5qcyJdLCJzb3VyY2VzQ29udGVudCI6WyJleHBvcnQgZnVuY3Rpb24gdW5peFRvV2luKHBhdGNoKSB7XG4gIGlmIChBcnJheS5pc0FycmF5KHBhdGNoKSkge1xuICAgIHJldHVybiBwYXRjaC5tYXAodW5peFRvV2luKTtcbiAgfVxuXG4gIHJldHVybiB7XG4gICAgLi4ucGF0Y2gsXG4gICAgaHVua3M6IHBhdGNoLmh1bmtzLm1hcChodW5rID0+ICh7XG4gICAgICAuLi5odW5rLFxuICAgICAgbGluZXM6IGh1bmsubGluZXMubWFwKFxuICAgICAgICAobGluZSwgaSkgPT5cbiAgICAgICAgICAobGluZS5zdGFydHNXaXRoKCdcXFxcJykgfHwgbGluZS5lbmRzV2l0aCgnXFxyJykgfHwgaHVuay5saW5lc1tpICsgMV0/LnN0YXJ0c1dpdGgoJ1xcXFwnKSlcbiAgICAgICAgICAgID8gbGluZVxuICAgICAgICAgICAgOiBsaW5lICsgJ1xccidcbiAgICAgIClcbiAgICB9KSlcbiAgfTtcbn1cblxuZXhwb3J0IGZ1bmN0aW9uIHdpblRvVW5peChwYXRjaCkge1xuICBpZiAoQXJyYXkuaXNBcnJheShwYXRjaCkpIHtcbiAgICByZXR1cm4gcGF0Y2gubWFwKHdpblRvVW5peCk7XG4gIH1cblxuICByZXR1cm4ge1xuICAgIC4uLnBhdGNoLFxuICAgIGh1bmtzOiBwYXRjaC5odW5rcy5tYXAoaHVuayA9PiAoe1xuICAgICAgLi4uaHVuayxcbiAgICAgIGxpbmVzOiBodW5rLmxpbmVzLm1hcChsaW5lID0+IGxpbmUuZW5kc1dpdGgoJ1xccicpID8gbGluZS5zdWJzdHJpbmcoMCwgbGluZS5sZW5ndGggLSAxKSA6IGxpbmUpXG4gICAgfSkpXG4gIH07XG59XG5cbi8qKlxuICogUmV0dXJucyB0cnVlIGlmIHRoZSBwYXRjaCBjb25zaXN0ZW50bHkgdXNlcyBVbml4IGxpbmUgZW5kaW5ncyAob3Igb25seSBpbnZvbHZlcyBvbmUgbGluZSBhbmQgaGFzXG4gKiBubyBsaW5lIGVuZGluZ3MpLlxuICovXG5leHBvcnQgZnVuY3Rpb24gaXNVbml4KHBhdGNoKSB7XG4gIGlmICghQXJyYXkuaXNBcnJheShwYXRjaCkpIHsgcGF0Y2ggPSBbcGF0Y2hdOyB9XG4gIHJldHVybiAhcGF0Y2guc29tZShcbiAgICBpbmRleCA9PiBpbmRleC5odW5rcy5zb21lKFxuICAgICAgaHVuayA9PiBodW5rLmxpbmVzLnNvbWUoXG4gICAgICAgIGxpbmUgPT4gIWxpbmUuc3RhcnRzV2l0aCgnXFxcXCcpICYmIGxpbmUuZW5kc1dpdGgoJ1xccicpXG4gICAgICApXG4gICAgKVxuICApO1xufVxuXG4vKipcbiAqIFJldHVybnMgdHJ1ZSBpZiB0aGUgcGF0Y2ggdXNlcyBXaW5kb3dzIGxpbmUgZW5kaW5ncyBhbmQgb25seSBXaW5kb3dzIGxpbmUgZW5kaW5ncy5cbiAqL1xuZXhwb3J0IGZ1bmN0aW9uIGlzV2luKHBhdGNoKSB7XG4gIGlmICghQXJyYXkuaXNBcnJheShwYXRjaCkpIHsgcGF0Y2ggPSBbcGF0Y2hdOyB9XG4gIHJldHVybiBwYXRjaC5zb21lKGluZGV4ID0+IGluZGV4Lmh1bmtzLnNvbWUoaHVuayA9PiBodW5rLmxpbmVzLnNvbWUobGluZSA9PiBsaW5lLmVuZHNXaXRoKCdcXHInKSkpKVxuICAgICYmIHBhdGNoLmV2ZXJ5KFxuICAgICAgaW5kZXggPT4gaW5kZXguaHVua3MuZXZlcnkoXG4gICAgICAgIGh1bmsgPT4gaHVuay5saW5lcy5ldmVyeShcbiAgICAgICAgICAobGluZSwgaSkgPT4gbGluZS5zdGFydHNXaXRoKCdcXFxcJykgfHwgbGluZS5lbmRzV2l0aCgnXFxyJykgfHwgaHVuay5saW5lc1tpICsgMV0/LnN0YXJ0c1dpdGgoJ1xcXFwnKVxuICAgICAgICApXG4gICAgICApXG4gICAgKTtcbn1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7Ozs7Ozs7Ozs7QUFBTyxTQUFTQSxTQUFTQSxDQUFDQyxLQUFLLEVBQUU7RUFDL0IsSUFBSUMsS0FBSyxDQUFDQyxPQUFPLENBQUNGLEtBQUssQ0FBQyxFQUFFO0lBQ3hCLE9BQU9BLEtBQUssQ0FBQ0csR0FBRyxDQUFDSixTQUFTLENBQUM7RUFDN0I7RUFFQTtJQUFBO0lBQUFLLGFBQUEsQ0FBQUEsYUFBQTtJQUFBO0lBQ0tKLEtBQUs7TUFDUkssS0FBSyxFQUFFTCxLQUFLLENBQUNLLEtBQUssQ0FBQ0YsR0FBRyxDQUFDLFVBQUFHLElBQUk7TUFBQTtNQUFBO1FBQUEsT0FBQUYsYUFBQSxDQUFBQSxhQUFBO1FBQUE7UUFDdEJFLElBQUk7VUFDUEMsS0FBSyxFQUFFRCxJQUFJLENBQUNDLEtBQUssQ0FBQ0osR0FBRyxDQUNuQixVQUFDSyxJQUFJLEVBQUVDLENBQUM7VUFBQTtVQUFBO1lBQUEsSUFBQUMsV0FBQTtZQUFBO2NBQUE7Y0FDTEYsSUFBSSxDQUFDRyxVQUFVLENBQUMsSUFBSSxDQUFDLElBQUlILElBQUksQ0FBQ0ksUUFBUSxDQUFDLElBQUksQ0FBQztjQUFBO2NBQUEsQ0FBQUYsV0FBQTtjQUFBO2NBQUlKLElBQUksQ0FBQ0MsS0FBSyxDQUFDRSxDQUFDLEdBQUcsQ0FBQyxDQUFDLGNBQUFDLFdBQUE7Y0FBakI7Y0FBQUE7Y0FBQTtjQUFBLENBQW1CQyxVQUFVLENBQUMsSUFBSSxDQUFDLEdBQ2hGSCxJQUFJLEdBQ0pBLElBQUksR0FBRztZQUFJO1VBQUEsQ0FDbkI7UUFBQztNQUFBLENBQ0Q7SUFBQztFQUFBO0FBRVA7QUFFTyxTQUFTSyxTQUFTQSxDQUFDYixLQUFL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
