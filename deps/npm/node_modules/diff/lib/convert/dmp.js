/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.convertChangesToDMP = convertChangesToDMP;
/*istanbul ignore end*/
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
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJjb252ZXJ0Q2hhbmdlc1RvRE1QIiwiY2hhbmdlcyIsInJldCIsImNoYW5nZSIsIm9wZXJhdGlvbiIsImkiLCJsZW5ndGgiLCJhZGRlZCIsInJlbW92ZWQiLCJwdXNoIiwidmFsdWUiXSwic291cmNlcyI6WyIuLi8uLi9zcmMvY29udmVydC9kbXAuanMiXSwic291cmNlc0NvbnRlbnQiOlsiLy8gU2VlOiBodHRwOi8vY29kZS5nb29nbGUuY29tL3AvZ29vZ2xlLWRpZmYtbWF0Y2gtcGF0Y2gvd2lraS9BUElcbmV4cG9ydCBmdW5jdGlvbiBjb252ZXJ0Q2hhbmdlc1RvRE1QKGNoYW5nZXMpIHtcbiAgbGV0IHJldCA9IFtdLFxuICAgICAgY2hhbmdlLFxuICAgICAgb3BlcmF0aW9uO1xuICBmb3IgKGxldCBpID0gMDsgaSA8IGNoYW5nZXMubGVuZ3RoOyBpKyspIHtcbiAgICBjaGFuZ2UgPSBjaGFuZ2VzW2ldO1xuICAgIGlmIChjaGFuZ2UuYWRkZWQpIHtcbiAgICAgIG9wZXJhdGlvbiA9IDE7XG4gICAgfSBlbHNlIGlmIChjaGFuZ2UucmVtb3ZlZCkge1xuICAgICAgb3BlcmF0aW9uID0gLTE7XG4gICAgfSBlbHNlIHtcbiAgICAgIG9wZXJhdGlvbiA9IDA7XG4gICAgfVxuXG4gICAgcmV0LnB1c2goW29wZXJhdGlvbiwgY2hhbmdlLnZhbHVlXSk7XG4gIH1cbiAgcmV0dXJuIHJldDtcbn1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7QUFBQTtBQUNPLFNBQVNBLG1CQUFtQkEsQ0FBQ0MsT0FBTyxFQUFFO0VBQzNDLElBQUlDLEdBQUcsR0FBRyxFQUFFO0lBQ1JDLE1BQU07SUFDTkMsU0FBUztFQUNiLEtBQUssSUFBSUMsQ0FBQyxHQUFHLENBQUMsRUFBRUEsQ0FBQyxHQUFHSixPQUFPLENBQUNLLE1BQU0sRUFBRUQsQ0FBQyxFQUFFLEVBQUU7SUFDdkNGLE1BQU0sR0FBR0YsT0FBTyxDQUFDSSxDQUFDLENBQUM7SUFDbkIsSUFBSUYsTUFBTSxDQUFDSSxLQUFLLEVBQUU7TUFDaEJILFNBQVMsR0FBRyxDQUFDO0lBQ2YsQ0FBQyxNQUFNLElBQUlELE1BQU0sQ0FBQ0ssT0FBTyxFQUFFO01BQ3pCSixTQUFTLEdBQUcsQ0FBQyxDQUFDO0lBQ2hCLENBQUMsTUFBTTtNQUNMQSxTQUFTLEdBQUcsQ0FBQztJQUNmO0lBRUFGLEdBQUcsQ0FBQ08sSUFBSSxDQUFDLENBQUNMLFNBQVMsRUFBRUQsTUFBTSxDQUFDTyxLQUFLLENBQUMsQ0FBQztFQUNyQztFQUNBLE9BQU9SLEdBQUc7QUFDWiIsImlnbm9yZUxpc3QiOltdfQ==
