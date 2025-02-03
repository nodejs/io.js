/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.reversePatch = reversePatch;
function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
function ownKeys(e, r) { var t = Object.keys(e); if (Object.getOwnPropertySymbols) { var o = Object.getOwnPropertySymbols(e); r && (o = o.filter(function (r) { return Object.getOwnPropertyDescriptor(e, r).enumerable; })), t.push.apply(t, o); } return t; }
function _objectSpread(e) { for (var r = 1; r < arguments.length; r++) { var t = null != arguments[r] ? arguments[r] : {}; r % 2 ? ownKeys(Object(t), !0).forEach(function (r) { _defineProperty(e, r, t[r]); }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) { Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r)); }); } return e; }
function _defineProperty(obj, key, value) { key = _toPropertyKey(key); if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == _typeof(i) ? i : i + ""; }
function _toPrimitive(t, r) { if ("object" != _typeof(t) || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r || "default"); if ("object" != _typeof(i)) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
/*istanbul ignore end*/
function reversePatch(structuredPatch) {
  if (Array.isArray(structuredPatch)) {
    return structuredPatch.map(reversePatch).reverse();
  }
  return (
    /*istanbul ignore start*/
    _objectSpread(_objectSpread({},
    /*istanbul ignore end*/
    structuredPatch), {}, {
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
              return (
                /*istanbul ignore start*/
                "+".concat(
                /*istanbul ignore end*/
                l.slice(1))
              );
            }
            if (l.startsWith('+')) {
              return (
                /*istanbul ignore start*/
                "-".concat(
                /*istanbul ignore end*/
                l.slice(1))
              );
            }
            return l;
          })
        };
      })
    })
  );
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJyZXZlcnNlUGF0Y2giLCJzdHJ1Y3R1cmVkUGF0Y2giLCJBcnJheSIsImlzQXJyYXkiLCJtYXAiLCJyZXZlcnNlIiwiX29iamVjdFNwcmVhZCIsIm9sZEZpbGVOYW1lIiwibmV3RmlsZU5hbWUiLCJvbGRIZWFkZXIiLCJuZXdIZWFkZXIiLCJodW5rcyIsImh1bmsiLCJvbGRMaW5lcyIsIm5ld0xpbmVzIiwib2xkU3RhcnQiLCJuZXdTdGFydCIsImxpbmVzIiwibCIsInN0YXJ0c1dpdGgiLCJjb25jYXQiLCJzbGljZSJdLCJzb3VyY2VzIjpbIi4uLy4uL3NyYy9wYXRjaC9yZXZlcnNlLmpzIl0sInNvdXJjZXNDb250ZW50IjpbImV4cG9ydCBmdW5jdGlvbiByZXZlcnNlUGF0Y2goc3RydWN0dXJlZFBhdGNoKSB7XG4gIGlmIChBcnJheS5pc0FycmF5KHN0cnVjdHVyZWRQYXRjaCkpIHtcbiAgICByZXR1cm4gc3RydWN0dXJlZFBhdGNoLm1hcChyZXZlcnNlUGF0Y2gpLnJldmVyc2UoKTtcbiAgfVxuXG4gIHJldHVybiB7XG4gICAgLi4uc3RydWN0dXJlZFBhdGNoLFxuICAgIG9sZEZpbGVOYW1lOiBzdHJ1Y3R1cmVkUGF0Y2gubmV3RmlsZU5hbWUsXG4gICAgb2xkSGVhZGVyOiBzdHJ1Y3R1cmVkUGF0Y2gubmV3SGVhZGVyLFxuICAgIG5ld0ZpbGVOYW1lOiBzdHJ1Y3R1cmVkUGF0Y2gub2xkRmlsZU5hbWUsXG4gICAgbmV3SGVhZGVyOiBzdHJ1Y3R1cmVkUGF0Y2gub2xkSGVhZGVyLFxuICAgIGh1bmtzOiBzdHJ1Y3R1cmVkUGF0Y2guaHVua3MubWFwKGh1bmsgPT4ge1xuICAgICAgcmV0dXJuIHtcbiAgICAgICAgb2xkTGluZXM6IGh1bmsubmV3TGluZXMsXG4gICAgICAgIG9sZFN0YXJ0OiBodW5rLm5ld1N0YXJ0LFxuICAgICAgICBuZXdMaW5lczogaHVuay5vbGRMaW5lcyxcbiAgICAgICAgbmV3U3RhcnQ6IGh1bmsub2xkU3RhcnQsXG4gICAgICAgIGxpbmVzOiBodW5rLmxpbmVzLm1hcChsID0+IHtcbiAgICAgICAgICBpZiAobC5zdGFydHNXaXRoKCctJykpIHsgcmV0dXJuIGArJHtsLnNsaWNlKDEpfWA7IH1cbiAgICAgICAgICBpZiAobC5zdGFydHNXaXRoKCcrJykpIHsgcmV0dXJuIGAtJHtsLnNsaWNlKDEpfWA7IH1cbiAgICAgICAgICByZXR1cm4gbDtcbiAgICAgICAgfSlcbiAgICAgIH07XG4gICAgfSlcbiAgfTtcbn1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7Ozs7Ozs7QUFBTyxTQUFTQSxZQUFZQSxDQUFDQyxlQUFlLEVBQUU7RUFDNUMsSUFBSUMsS0FBSyxDQUFDQyxPQUFPLENBQUNGLGVBQWUsQ0FBQyxFQUFFO0lBQ2xDLE9BQU9BLGVBQWUsQ0FBQ0csR0FBRyxDQUFDSixZQUFZLENBQUMsQ0FBQ0ssT0FBTyxDQUFDLENBQUM7RUFDcEQ7RUFFQTtJQUFBO0lBQUFDLGFBQUEsQ0FBQUEsYUFBQTtJQUFBO0lBQ0tMLGVBQWU7TUFDbEJNLFdBQVcsRUFBRU4sZUFBZSxDQUFDTyxXQUFXO01BQ3hDQyxTQUFTLEVBQUVSLGVBQWUsQ0FBQ1MsU0FBUztNQUNwQ0YsV0FBVyxFQUFFUCxlQUFlLENBQUNNLFdBQVc7TUFDeENHLFNBQVMsRUFBRVQsZUFBZSxDQUFDUSxTQUFTO01BQ3BDRSxLQUFLLEVBQUVWLGVBQWUsQ0FBQ1UsS0FBSyxDQUFDUCxHQUFHLENBQUMsVUFBQVEsSUFBSSxFQUFJO1FBQ3ZDLE9BQU87VUFDTEMsUUFBUSxFQUFFRCxJQUFJLENBQUNFLFFBQVE7VUFDdkJDLFFBQVEsRUFBRUgsSUFBSSxDQUFDSSxRQUFRO1VBQ3ZCRixRQUFRLEVBQUVGLElBQUksQ0FBQ0MsUUFBUTtVQUN2QkcsUUFBUSxFQUFFSixJQUFJLENBQUNHLFFBQVE7VUFDdkJFLEtBQUssRUFBRUwsSUFBSSxDQUFDSyxLQUFLLENBQUNiLEdBQUcsQ0FBQyxVQUFBYyxDQUFDLEVBQUk7WUFDekIsSUFBSUEsQ0FBQyxDQUFDQyxVQUFVLENBQUMsR0FBRyxDQUFDLEVBQUU7Y0FBRTtnQkFBQTtnQkFBQSxJQUFBQyxNQUFBO2dCQUFBO2dCQUFXRixDQUFDLENBQUNHLEtBQUssQ0FBQyxDQUFDLENBQUM7Y0FBQTtZQUFJO1lBQ2xELElBQUlILENBQUMsQ0FBQ0MsVUFBVSxDQUFDLEdBQUcsQ0FBQyxFQUFFO2NBQUU7Z0JBQUE7Z0JBQUEsSUFBQUMsTUFBQTtnQkFBQTtnQkFBV0YsQ0FBQyxDQUFDRyxLQUFLLENBQUMsQ0FBQyxDQUFDO2NBQUE7WUFBSTtZQUNsRCxPQUFPSCxDQUFDO1VBQ1YsQ0FBQztRQUNILENBQUM7TUFDSCxDQUFDO0lBQUM7RUFBQTtBQUVOIiwiaWdub3JlTGlzdCI6W119
