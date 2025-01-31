/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.arrayDiff = void 0;
exports.diffArrays = diffArrays;
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_base = _interopRequireDefault(require("./base"))
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { "default": obj }; }
/*istanbul ignore end*/
var arrayDiff =
/*istanbul ignore start*/
exports.arrayDiff =
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
arrayDiff.tokenize = function (value) {
  return value.slice();
};
arrayDiff.join = arrayDiff.removeEmpty = function (value) {
  return value;
};
function diffArrays(oldArr, newArr, callback) {
  return arrayDiff.diff(oldArr, newArr, callback);
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfYmFzZSIsIl9pbnRlcm9wUmVxdWlyZURlZmF1bHQiLCJyZXF1aXJlIiwib2JqIiwiX19lc01vZHVsZSIsImFycmF5RGlmZiIsImV4cG9ydHMiLCJEaWZmIiwidG9rZW5pemUiLCJ2YWx1ZSIsInNsaWNlIiwiam9pbiIsInJlbW92ZUVtcHR5IiwiZGlmZkFycmF5cyIsIm9sZEFyciIsIm5ld0FyciIsImNhbGxiYWNrIiwiZGlmZiJdLCJzb3VyY2VzIjpbIi4uLy4uL3NyYy9kaWZmL2FycmF5LmpzIl0sInNvdXJjZXNDb250ZW50IjpbImltcG9ydCBEaWZmIGZyb20gJy4vYmFzZSc7XG5cbmV4cG9ydCBjb25zdCBhcnJheURpZmYgPSBuZXcgRGlmZigpO1xuYXJyYXlEaWZmLnRva2VuaXplID0gZnVuY3Rpb24odmFsdWUpIHtcbiAgcmV0dXJuIHZhbHVlLnNsaWNlKCk7XG59O1xuYXJyYXlEaWZmLmpvaW4gPSBhcnJheURpZmYucmVtb3ZlRW1wdHkgPSBmdW5jdGlvbih2YWx1ZSkge1xuICByZXR1cm4gdmFsdWU7XG59O1xuXG5leHBvcnQgZnVuY3Rpb24gZGlmZkFycmF5cyhvbGRBcnIsIG5ld0FyciwgY2FsbGJhY2spIHsgcmV0dXJuIGFycmF5RGlmZi5kaWZmKG9sZEFyciwgbmV3QXJyLCBjYWxsYmFjayk7IH1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7O0FBQUE7QUFBQTtBQUFBQSxLQUFBLEdBQUFDLHNCQUFBLENBQUFDLE9BQUE7QUFBQTtBQUFBO0FBQTBCLG1DQUFBRCx1QkFBQUUsR0FBQSxXQUFBQSxHQUFBLElBQUFBLEdBQUEsQ0FBQUMsVUFBQSxHQUFBRCxHQUFBLGdCQUFBQSxHQUFBO0FBQUE7QUFFbkIsSUFBTUUsU0FBUztBQUFBO0FBQUFDLE9BQUEsQ0FBQUQsU0FBQTtBQUFBO0FBQUc7QUFBSUU7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUEsQ0FBSSxDQUFDLENBQUM7QUFDbkNGLFNBQVMsQ0FBQ0csUUFBUSxHQUFHLFVBQVNDLEtBQUssRUFBRTtFQUNuQyxPQUFPQSxLQUFLLENBQUNDLEtBQUssQ0FBQyxDQUFDO0FBQ3RCLENBQUM7QUFDREwsU0FBUyxDQUFDTSxJQUFJLEdBQUdOLFNBQVMsQ0FBQ08sV0FBVyxHQUFHLFVBQVNILEtBQUssRUFBRTtFQUN2RCxPQUFPQSxLQUFLO0FBQ2QsQ0FBQztBQUVNLFNBQVNJLFVBQVVBLENBQUNDLE1BQU0sRUFBRUMsTUFBTSxFQUFFQyxRQUFRLEVBQUU7RUFBRSxPQUFPWCxTQUFTLENBQUNZLElBQUksQ0FBQ0gsTUFBTSxFQUFFQyxNQUFNLEVBQUVDLFFBQVEsQ0FBQztBQUFFIiwiaWdub3JlTGlzdCI6W119
