/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.generateOptions = generateOptions;
/*istanbul ignore end*/
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
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJnZW5lcmF0ZU9wdGlvbnMiLCJvcHRpb25zIiwiZGVmYXVsdHMiLCJjYWxsYmFjayIsIm5hbWUiLCJoYXNPd25Qcm9wZXJ0eSJdLCJzb3VyY2VzIjpbIi4uLy4uL3NyYy91dGlsL3BhcmFtcy5qcyJdLCJzb3VyY2VzQ29udGVudCI6WyJleHBvcnQgZnVuY3Rpb24gZ2VuZXJhdGVPcHRpb25zKG9wdGlvbnMsIGRlZmF1bHRzKSB7XG4gIGlmICh0eXBlb2Ygb3B0aW9ucyA9PT0gJ2Z1bmN0aW9uJykge1xuICAgIGRlZmF1bHRzLmNhbGxiYWNrID0gb3B0aW9ucztcbiAgfSBlbHNlIGlmIChvcHRpb25zKSB7XG4gICAgZm9yIChsZXQgbmFtZSBpbiBvcHRpb25zKSB7XG4gICAgICAvKiBpc3RhbmJ1bCBpZ25vcmUgZWxzZSAqL1xuICAgICAgaWYgKG9wdGlvbnMuaGFzT3duUHJvcGVydHkobmFtZSkpIHtcbiAgICAgICAgZGVmYXVsdHNbbmFtZV0gPSBvcHRpb25zW25hbWVdO1xuICAgICAgfVxuICAgIH1cbiAgfVxuICByZXR1cm4gZGVmYXVsdHM7XG59XG4iXSwibWFwcGluZ3MiOiI7Ozs7Ozs7O0FBQU8sU0FBU0EsZUFBZUEsQ0FBQ0MsT0FBTyxFQUFFQyxRQUFRLEVBQUU7RUFDakQsSUFBSSxPQUFPRCxPQUFPLEtBQUssVUFBVSxFQUFFO0lBQ2pDQyxRQUFRLENBQUNDLFFBQVEsR0FBR0YsT0FBTztFQUM3QixDQUFDLE1BQU0sSUFBSUEsT0FBTyxFQUFFO0lBQ2xCLEtBQUssSUFBSUcsSUFBSSxJQUFJSCxPQUFPLEVBQUU7TUFDeEI7TUFDQSxJQUFJQSxPQUFPLENBQUNJLGNBQWMsQ0FBQ0QsSUFBSSxDQUFDLEVBQUU7UUFDaENGLFFBQVEsQ0FBQ0UsSUFBSSxDQUFDLEdBQUdILE9BQU8sQ0FBQ0csSUFBSSxDQUFDO01BQ2hDO0lBQ0Y7RUFDRjtFQUNBLE9BQU9GLFFBQVE7QUFDakIiLCJpZ25vcmVMaXN0IjpbXX0=
