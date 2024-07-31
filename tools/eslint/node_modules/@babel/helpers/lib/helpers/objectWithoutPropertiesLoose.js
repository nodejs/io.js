"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.default = _objectWithoutPropertiesLoose;
function _objectWithoutPropertiesLoose(source, excluded) {
  if (source == null) return {};
  var target = {};
  for (var key in source) {
    if (Object.prototype.hasOwnProperty.call(source, key)) {
      if (excluded.includes(key)) continue;
      target[key] = source[key];
    }
  }
  return target;
}

//# sourceMappingURL=objectWithoutPropertiesLoose.js.map
