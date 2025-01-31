/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.canonicalize = canonicalize;
exports.diffJson = diffJson;
exports.jsonDiff = void 0;
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_base = _interopRequireDefault(require("./base"))
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_line = require("./line")
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { "default": obj }; }
function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
/*istanbul ignore end*/
var jsonDiff =
/*istanbul ignore start*/
exports.jsonDiff =
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
// Discriminate between two lines of pretty-printed, serialized JSON where one of them has a
// dangling comma and the other doesn't. Turns out including the dangling comma yields the nicest output:
jsonDiff.useLongestToken = true;
jsonDiff.tokenize =
/*istanbul ignore start*/
_line
/*istanbul ignore end*/
.
/*istanbul ignore start*/
lineDiff
/*istanbul ignore end*/
.tokenize;
jsonDiff.castInput = function (value, options) {
  var
    /*istanbul ignore start*/
    /*istanbul ignore end*/
    undefinedReplacement = options.undefinedReplacement,
    /*istanbul ignore start*/
    _options$stringifyRep =
    /*istanbul ignore end*/
    options.stringifyReplacer,
    /*istanbul ignore start*/
    /*istanbul ignore end*/
    stringifyReplacer = _options$stringifyRep === void 0 ? function (k, v)
    /*istanbul ignore start*/
    {
      return (
        /*istanbul ignore end*/
        typeof v === 'undefined' ? undefinedReplacement : v
      );
    } : _options$stringifyRep;
  return typeof value === 'string' ? value : JSON.stringify(canonicalize(value, null, null, stringifyReplacer), stringifyReplacer, '  ');
};
jsonDiff.equals = function (left, right, options) {
  return (
    /*istanbul ignore start*/
    _base
    /*istanbul ignore end*/
    [
    /*istanbul ignore start*/
    "default"
    /*istanbul ignore end*/
    ].prototype.equals.call(jsonDiff, left.replace(/,([\r\n])/g, '$1'), right.replace(/,([\r\n])/g, '$1'), options)
  );
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
  if (
  /*istanbul ignore start*/
  _typeof(
  /*istanbul ignore end*/
  obj) === 'object' && obj !== null) {
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
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfYmFzZSIsIl9pbnRlcm9wUmVxdWlyZURlZmF1bHQiLCJyZXF1aXJlIiwiX2xpbmUiLCJvYmoiLCJfX2VzTW9kdWxlIiwiX3R5cGVvZiIsIm8iLCJTeW1ib2wiLCJpdGVyYXRvciIsImNvbnN0cnVjdG9yIiwicHJvdG90eXBlIiwianNvbkRpZmYiLCJleHBvcnRzIiwiRGlmZiIsInVzZUxvbmdlc3RUb2tlbiIsInRva2VuaXplIiwibGluZURpZmYiLCJjYXN0SW5wdXQiLCJ2YWx1ZSIsIm9wdGlvbnMiLCJ1bmRlZmluZWRSZXBsYWNlbWVudCIsIl9vcHRpb25zJHN0cmluZ2lmeVJlcCIsInN0cmluZ2lmeVJlcGxhY2VyIiwiayIsInYiLCJKU09OIiwic3RyaW5naWZ5IiwiY2Fub25pY2FsaXplIiwiZXF1YWxzIiwibGVmdCIsInJpZ2h0IiwiY2FsbCIsInJlcGxhY2UiLCJkaWZmSnNvbiIsIm9sZE9iaiIsIm5ld09iaiIsImRpZmYiLCJzdGFjayIsInJlcGxhY2VtZW50U3RhY2siLCJyZXBsYWNlciIsImtleSIsImkiLCJsZW5ndGgiLCJjYW5vbmljYWxpemVkT2JqIiwiT2JqZWN0IiwidG9TdHJpbmciLCJwdXNoIiwiQXJyYXkiLCJwb3AiLCJ0b0pTT04iLCJzb3J0ZWRLZXlzIiwiaGFzT3duUHJvcGVydHkiLCJzb3J0Il0sInNvdXJjZXMiOlsiLi4vLi4vc3JjL2RpZmYvanNvbi5qcyJdLCJzb3VyY2VzQ29udGVudCI6WyJpbXBvcnQgRGlmZiBmcm9tICcuL2Jhc2UnO1xuaW1wb3J0IHtsaW5lRGlmZn0gZnJvbSAnLi9saW5lJztcblxuZXhwb3J0IGNvbnN0IGpzb25EaWZmID0gbmV3IERpZmYoKTtcbi8vIERpc2NyaW1pbmF0ZSBiZXR3ZWVuIHR3byBsaW5lcyBvZiBwcmV0dHktcHJpbnRlZCwgc2VyaWFsaXplZCBKU09OIHdoZXJlIG9uZSBvZiB0aGVtIGhhcyBhXG4vLyBkYW5nbGluZyBjb21tYSBhbmQgdGhlIG90aGVyIGRvZXNuJ3QuIFR1cm5zIG91dCBpbmNsdWRpbmcgdGhlIGRhbmdsaW5nIGNvbW1hIHlpZWxkcyB0aGUgbmljZXN0IG91dHB1dDpcbmpzb25EaWZmLnVzZUxvbmdlc3RUb2tlbiA9IHRydWU7XG5cbmpzb25EaWZmLnRva2VuaXplID0gbGluZURpZmYudG9rZW5pemU7XG5qc29uRGlmZi5jYXN0SW5wdXQgPSBmdW5jdGlvbih2YWx1ZSwgb3B0aW9ucykge1xuICBjb25zdCB7dW5kZWZpbmVkUmVwbGFjZW1lbnQsIHN0cmluZ2lmeVJlcGxhY2VyID0gKGssIHYpID0+IHR5cGVvZiB2ID09PSAndW5kZWZpbmVkJyA/IHVuZGVmaW5lZFJlcGxhY2VtZW50IDogdn0gPSBvcHRpb25zO1xuXG4gIHJldHVybiB0eXBlb2YgdmFsdWUgPT09ICdzdHJpbmcnID8gdmFsdWUgOiBKU09OLnN0cmluZ2lmeShjYW5vbmljYWxpemUodmFsdWUsIG51bGwsIG51bGwsIHN0cmluZ2lmeVJlcGxhY2VyKSwgc3RyaW5naWZ5UmVwbGFjZXIsICcgICcpO1xufTtcbmpzb25EaWZmLmVxdWFscyA9IGZ1bmN0aW9uKGxlZnQsIHJpZ2h0LCBvcHRpb25zKSB7XG4gIHJldHVybiBEaWZmLnByb3RvdHlwZS5lcXVhbHMuY2FsbChqc29uRGlmZiwgbGVmdC5yZXBsYWNlKC8sKFtcXHJcXG5dKS9nLCAnJDEnKSwgcmlnaHQucmVwbGFjZSgvLChbXFxyXFxuXSkvZywgJyQxJyksIG9wdGlvbnMpO1xufTtcblxuZXhwb3J0IGZ1bmN0aW9uIGRpZmZKc29uKG9sZE9iaiwgbmV3T2JqLCBvcHRpb25zKSB7IHJldHVybiBqc29uRGlmZi5kaWZmKG9sZE9iaiwgbmV3T2JqLCBvcHRpb25zKTsgfVxuXG4vLyBUaGlzIGZ1bmN0aW9uIGhhbmRsZXMgdGhlIHByZXNlbmNlIG9mIGNpcmN1bGFyIHJlZmVyZW5jZXMgYnkgYmFpbGluZyBvdXQgd2hlbiBlbmNvdW50ZXJpbmcgYW5cbi8vIG9iamVjdCB0aGF0IGlzIGFscmVhZHkgb24gdGhlIFwic3RhY2tcIiBvZiBpdGVtcyBiZWluZyBwcm9jZXNzZWQuIEFjY2VwdHMgYW4gb3B0aW9uYWwgcmVwbGFjZXJcbmV4cG9ydCBmdW5jdGlvbiBjYW5vbmljYWxpemUob2JqLCBzdGFjaywgcmVwbGFjZW1lbnRTdGFjaywgcmVwbGFjZXIsIGtleSkge1xuICBzdGFjayA9IHN0YWNrIHx8IFtdO1xuICByZXBsYWNlbWVudFN0YWNrID0gcmVwbGFjZW1lbnRTdGFjayB8fCBbXTtcblxuICBpZiAocmVwbGFjZXIpIHtcbiAgICBvYmogPSByZXBsYWNlcihrZXksIG9iaik7XG4gIH1cblxuICBsZXQgaTtcblxuICBmb3IgKGkgPSAwOyBpIDwgc3RhY2subGVuZ3RoOyBpICs9IDEpIHtcbiAgICBpZiAoc3RhY2tbaV0gPT09IG9iaikge1xuICAgICAgcmV0dXJuIHJlcGxhY2VtZW50U3RhY2tbaV07XG4gICAgfVxuICB9XG5cbiAgbGV0IGNhbm9uaWNhbGl6ZWRPYmo7XG5cbiAgaWYgKCdbb2JqZWN0IEFycmF5XScgPT09IE9iamVjdC5wcm90b3R5cGUudG9TdHJpbmcuY2FsbChvYmopKSB7XG4gICAgc3RhY2sucHVzaChvYmopO1xuICAgIGNhbm9uaWNhbGl6ZWRPYmogPSBuZXcgQXJyYXkob2JqLmxlbmd0aCk7XG4gICAgcmVwbGFjZW1lbnRTdGFjay5wdXNoKGNhbm9uaWNhbGl6ZWRPYmopO1xuICAgIGZvciAoaSA9IDA7IGkgPCBvYmoubGVuZ3RoOyBpICs9IDEpIHtcbiAgICAgIGNhbm9uaWNhbGl6ZWRPYmpbaV0gPSBjYW5vbmljYWxpemUob2JqW2ldLCBzdGFjaywgcmVwbGFjZW1lbnRTdGFjaywgcmVwbGFjZXIsIGtleSk7XG4gICAgfVxuICAgIHN0YWNrLnBvcCgpO1xuICAgIHJlcGxhY2VtZW50U3RhY2sucG9wKCk7XG4gICAgcmV0dXJuIGNhbm9uaWNhbGl6ZWRPYmo7XG4gIH1cblxuICBpZiAob2JqICYmIG9iai50b0pTT04pIHtcbiAgICBvYmogPSBvYmoudG9KU09OKCk7XG4gIH1cblxuICBpZiAodHlwZW9mIG9iaiA9PT0gJ29iamVjdCcgJiYgb2JqICE9PSBudWxsKSB7XG4gICAgc3RhY2sucHVzaChvYmopO1xuICAgIGNhbm9uaWNhbGl6ZWRPYmogPSB7fTtcbiAgICByZXBsYWNlbWVudFN0YWNrLnB1c2goY2Fub25pY2FsaXplZE9iaik7XG4gICAgbGV0IHNvcnRlZEtleXMgPSBbXSxcbiAgICAgICAga2V5O1xuICAgIGZvciAoa2V5IGluIG9iaikge1xuICAgICAgLyogaXN0YW5idWwgaWdub3JlIGVsc2UgKi9cbiAgICAgIGlmIChPYmplY3QucHJvdG90eXBlLmhhc093blByb3BlcnR5LmNhbGwob2JqLCBrZXkpKSB7XG4gICAgICAgIHNvcnRlZEtleXMucHVzaChrZXkpO1xuICAgICAgfVxuICAgIH1cbiAgICBzb3J0ZWRLZXlzLnNvcnQoKTtcbiAgICBmb3IgKGkgPSAwOyBpIDwgc29ydGVkS2V5cy5sZW5ndGg7IGkgKz0gMSkge1xuICAgICAga2V5ID0gc29ydGVkS2V5c1tpXTtcbiAgICAgIGNhbm9uaWNhbGl6ZWRPYmpba2V5XSA9IGNhbm9uaWNhbGl6ZShvYmpba2V5XSwgc3RhY2ssIHJlcGxhY2VtZW50U3RhY2ssIHJlcGxhY2VyLCBrZXkpO1xuICAgIH1cbiAgICBzdGFjay5wb3AoKTtcbiAgICByZXBsYWNlbWVudFN0YWNrLnBvcCgpO1xuICB9IGVsc2Uge1xuICAgIGNhbm9uaWNhbGl6ZWRPYmogPSBvYmo7XG4gIH1cbiAgcmV0dXJuIGNhbm9uaWNhbGl6ZWRPYmo7XG59XG4iXSwibWFwcGluZ3MiOiI7Ozs7Ozs7Ozs7QUFBQTtBQUFBO0FBQUFBLEtBQUEsR0FBQUMsc0JBQUEsQ0FBQUMsT0FBQTtBQUFBO0FBQUE7QUFDQTtBQUFBO0FBQUFDLEtBQUEsR0FBQUQsT0FBQTtBQUFBO0FBQUE7QUFBZ0MsbUNBQUFELHVCQUFBRyxHQUFBLFdBQUFBLEdBQUEsSUFBQUEsR0FBQSxDQUFBQyxVQUFBLEdBQUFELEdBQUEsZ0JBQUFBLEdBQUE7QUFBQSxTQUFBRSxRQUFBQyxDQUFBLHNDQUFBRCxPQUFBLHdCQUFBRSxN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
