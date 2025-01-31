/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.arrayEqual = arrayEqual;
exports.arrayStartsWith = arrayStartsWith;
/*istanbul ignore end*/
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
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJhcnJheUVxdWFsIiwiYSIsImIiLCJsZW5ndGgiLCJhcnJheVN0YXJ0c1dpdGgiLCJhcnJheSIsInN0YXJ0IiwiaSJdLCJzb3VyY2VzIjpbIi4uLy4uL3NyYy91dGlsL2FycmF5LmpzIl0sInNvdXJjZXNDb250ZW50IjpbImV4cG9ydCBmdW5jdGlvbiBhcnJheUVxdWFsKGEsIGIpIHtcbiAgaWYgKGEubGVuZ3RoICE9PSBiLmxlbmd0aCkge1xuICAgIHJldHVybiBmYWxzZTtcbiAgfVxuXG4gIHJldHVybiBhcnJheVN0YXJ0c1dpdGgoYSwgYik7XG59XG5cbmV4cG9ydCBmdW5jdGlvbiBhcnJheVN0YXJ0c1dpdGgoYXJyYXksIHN0YXJ0KSB7XG4gIGlmIChzdGFydC5sZW5ndGggPiBhcnJheS5sZW5ndGgpIHtcbiAgICByZXR1cm4gZmFsc2U7XG4gIH1cblxuICBmb3IgKGxldCBpID0gMDsgaSA8IHN0YXJ0Lmxlbmd0aDsgaSsrKSB7XG4gICAgaWYgKHN0YXJ0W2ldICE9PSBhcnJheVtpXSkge1xuICAgICAgcmV0dXJuIGZhbHNlO1xuICAgIH1cbiAgfVxuXG4gIHJldHVybiB0cnVlO1xufVxuIl0sIm1hcHBpbmdzIjoiOzs7Ozs7Ozs7QUFBTyxTQUFTQSxVQUFVQSxDQUFDQyxDQUFDLEVBQUVDLENBQUMsRUFBRTtFQUMvQixJQUFJRCxDQUFDLENBQUNFLE1BQU0sS0FBS0QsQ0FBQyxDQUFDQyxNQUFNLEVBQUU7SUFDekIsT0FBTyxLQUFLO0VBQ2Q7RUFFQSxPQUFPQyxlQUFlLENBQUNILENBQUMsRUFBRUMsQ0FBQyxDQUFDO0FBQzlCO0FBRU8sU0FBU0UsZUFBZUEsQ0FBQ0MsS0FBSyxFQUFFQyxLQUFLLEVBQUU7RUFDNUMsSUFBSUEsS0FBSyxDQUFDSCxNQUFNLEdBQUdFLEtBQUssQ0FBQ0YsTUFBTSxFQUFFO0lBQy9CLE9BQU8sS0FBSztFQUNkO0VBRUEsS0FBSyxJQUFJSSxDQUFDLEdBQUcsQ0FBQyxFQUFFQSxDQUFDLEdBQUdELEtBQUssQ0FBQ0gsTUFBTSxFQUFFSSxDQUFDLEVBQUUsRUFBRTtJQUNyQyxJQUFJRCxLQUFLLENBQUNDLENBQUMsQ0FBQyxLQUFLRixLQUFLLENBQUNFLENBQUMsQ0FBQyxFQUFFO01BQ3pCLE9BQU8sS0FBSztJQUNkO0VBQ0Y7RUFFQSxPQUFPLElBQUk7QUFDYiIsImlnbm9yZUxpc3QiOltdfQ==
