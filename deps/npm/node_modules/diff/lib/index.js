/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
Object.defineProperty(exports, "Diff", {
  enumerable: true,
  get: function get() {
    return _base["default"];
  }
});
Object.defineProperty(exports, "applyPatch", {
  enumerable: true,
  get: function get() {
    return _apply.applyPatch;
  }
});
Object.defineProperty(exports, "applyPatches", {
  enumerable: true,
  get: function get() {
    return _apply.applyPatches;
  }
});
Object.defineProperty(exports, "canonicalize", {
  enumerable: true,
  get: function get() {
    return _json.canonicalize;
  }
});
Object.defineProperty(exports, "convertChangesToDMP", {
  enumerable: true,
  get: function get() {
    return _dmp.convertChangesToDMP;
  }
});
Object.defineProperty(exports, "convertChangesToXML", {
  enumerable: true,
  get: function get() {
    return _xml.convertChangesToXML;
  }
});
Object.defineProperty(exports, "createPatch", {
  enumerable: true,
  get: function get() {
    return _create.createPatch;
  }
});
Object.defineProperty(exports, "createTwoFilesPatch", {
  enumerable: true,
  get: function get() {
    return _create.createTwoFilesPatch;
  }
});
Object.defineProperty(exports, "diffArrays", {
  enumerable: true,
  get: function get() {
    return _array.diffArrays;
  }
});
Object.defineProperty(exports, "diffChars", {
  enumerable: true,
  get: function get() {
    return _character.diffChars;
  }
});
Object.defineProperty(exports, "diffCss", {
  enumerable: true,
  get: function get() {
    return _css.diffCss;
  }
});
Object.defineProperty(exports, "diffJson", {
  enumerable: true,
  get: function get() {
    return _json.diffJson;
  }
});
Object.defineProperty(exports, "diffLines", {
  enumerable: true,
  get: function get() {
    return _line.diffLines;
  }
});
Object.defineProperty(exports, "diffSentences", {
  enumerable: true,
  get: function get() {
    return _sentence.diffSentences;
  }
});
Object.defineProperty(exports, "diffTrimmedLines", {
  enumerable: true,
  get: function get() {
    return _line.diffTrimmedLines;
  }
});
Object.defineProperty(exports, "diffWords", {
  enumerable: true,
  get: function get() {
    return _word.diffWords;
  }
});
Object.defineProperty(exports, "diffWordsWithSpace", {
  enumerable: true,
  get: function get() {
    return _word.diffWordsWithSpace;
  }
});
Object.defineProperty(exports, "formatPatch", {
  enumerable: true,
  get: function get() {
    return _create.formatPatch;
  }
});
Object.defineProperty(exports, "merge", {
  enumerable: true,
  get: function get() {
    return _merge.merge;
  }
});
Object.defineProperty(exports, "parsePatch", {
  enumerable: true,
  get: function get() {
    return _parse.parsePatch;
  }
});
Object.defineProperty(exports, "reversePatch", {
  enumerable: true,
  get: function get() {
    return _reverse.reversePatch;
  }
});
Object.defineProperty(exports, "structuredPatch", {
  enumerable: true,
  get: function get() {
    return _create.structuredPatch;
  }
});
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_base = _interopRequireDefault(require("./diff/base"))
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_character = require("./diff/character")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_word = require("./diff/word")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_line = require("./diff/line")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_sentence = require("./diff/sentence")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_css = require("./diff/css")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_json = require("./diff/json")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_array = require("./diff/array")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_apply = require("./patch/apply")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_parse = require("./patch/parse")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_merge = require("./patch/merge")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_reverse = require("./patch/reverse")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_create = require("./patch/create")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_dmp = require("./convert/dmp")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_xml = require("./convert/xml")
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { "default": obj }; }
/*istanbul ignore end*/
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfYmFzZSIsIl9pbnRlcm9wUmVxdWlyZURlZmF1bHQiLCJyZXF1aXJlIiwiX2NoYXJhY3RlciIsIl93b3JkIiwiX2xpbmUiLCJfc2VudGVuY2UiLCJfY3NzIiwiX2pzb24iLCJfYXJyYXkiLCJfYXBwbHkiLCJfcGFyc2UiLCJfbWVyZ2UiLCJfcmV2ZXJzZSIsIl9jcmVhdGUiLCJfZG1wIiwiX3htbCIsIm9iaiIsIl9fZXNNb2R1bGUiXSwic291cmNlcyI6WyIuLi9zcmMvaW5kZXguanMiXSwic291cmNlc0NvbnRlbnQiOlsiLyogU2VlIExJQ0VOU0UgZmlsZSBmb3IgdGVybXMgb2YgdXNlICovXG5cbi8qXG4gKiBUZXh0IGRpZmYgaW1wbGVtZW50YXRpb24uXG4gKlxuICogVGhpcyBsaWJyYXJ5IHN1cHBvcnRzIHRoZSBmb2xsb3dpbmcgQVBJczpcbiAqIERpZmYuZGlmZkNoYXJzOiBDaGFyYWN0ZXIgYnkgY2hhcmFjdGVyIGRpZmZcbiAqIERpZmYuZGlmZldvcmRzOiBXb3JkIChhcyBkZWZpbmVkIGJ5IFxcYiByZWdleCkgZGlmZiB3aGljaCBpZ25vcmVzIHdoaXRlc3BhY2VcbiAqIERpZmYuZGlmZkxpbmVzOiBMaW5lIGJhc2VkIGRpZmZcbiAqXG4gKiBEaWZmLmRpZmZDc3M6IERpZmYgdGFyZ2V0ZWQgYXQgQ1NTIGNvbnRlbnRcbiAqXG4gKiBUaGVzZSBtZXRob2RzIGFyZSBiYXNlZCBvbiB0aGUgaW1wbGVtZW50YXRpb24gcHJvcG9zZWQgaW5cbiAqIFwiQW4gTyhORCkgRGlmZmVyZW5jZSBBbGdvcml0aG0gYW5kIGl0cyBWYXJpYXRpb25zXCIgKE15ZXJzLCAxOTg2KS5cbiAqIGh0dHA6Ly9jaXRlc2VlcnguaXN0LnBzdS5lZHUvdmlld2RvYy9zdW1tYXJ5P2RvaT0xMC4xLjEuNC42OTI3XG4gKi9cbmltcG9ydCBEaWZmIGZyb20gJy4vZGlmZi9iYXNlJztcbmltcG9ydCB7ZGlmZkNoYXJzfSBmcm9tICcuL2RpZmYvY2hhcmFjdGVyJztcbmltcG9ydCB7ZGlmZldvcmRzLCBkaWZmV29yZHNXaXRoU3BhY2V9IGZyb20gJy4vZGlmZi93b3JkJztcbmltcG9ydCB7ZGlmZkxpbmVzLCBkaWZmVHJpbW1lZExpbmVzfSBmcm9tICcuL2RpZmYvbGluZSc7XG5pbXBvcnQge2RpZmZTZW50ZW5jZXN9IGZyb20gJy4vZGlmZi9zZW50ZW5jZSc7XG5cbmltcG9ydCB7ZGlmZkNzc30gZnJvbSAnLi9kaWZmL2Nzcyc7XG5pbXBvcnQge2RpZmZKc29uLCBjYW5vbmljYWxpemV9IGZyb20gJy4vZGlmZi9qc29uJztcblxuaW1wb3J0IHtkaWZmQXJyYXlzfSBmcm9tICcuL2RpZmYvYXJyYXknO1xuXG5pbXBvcnQge2FwcGx5UGF0Y2gsIGFwcGx5UGF0Y2hlc30gZnJvbSAnLi9wYXRjaC9hcHBseSc7XG5pbXBvcnQge3BhcnNlUGF0Y2h9IGZyb20gJy4vcGF0Y2gvcGFyc2UnO1xuaW1wb3J0IHttZXJnZX0gZnJvbSAnLi9wYXRjaC9tZXJnZSc7XG5pbXBvcnQge3JldmVyc2VQYXRjaH0gZnJvbSAnLi9wYXRjaC9yZXZlcnNlJztcbmltcG9ydCB7c3RydWN0dXJlZFBhdGNoLCBjcmVhdGVUd29GaWxlc1BhdGNoLCBjcmVhdGVQYXRjaCwgZm9ybWF0UGF0Y2h9IGZyb20gJy4vcGF0Y2gvY3JlYXRlJztcblxuaW1wb3J0IHtjb252ZXJ0Q2hhbmdlc1RvRE1QfSBmcm9tICcuL2NvbnZlcnQvZG1wJztcbmltcG9ydCB7Y29udmVydENoYW5nZXNUb1hNTH0gZnJvbSAnLi9jb252ZXJ0L3htbCc7XG5cbmV4cG9ydCB7XG4gIERpZmYsXG5cbiAgZGlmZkNoYXJzLFxuICBkaWZmV29yZHMsXG4gIGRpZmZXb3Jkc1dpdGhTcGFjZSxcbiAgZGlmZkxpbmVzLFxuICBkaWZmVHJpbW1lZExpbmVzLFxuICBkaWZmU2VudGVuY2VzLFxuXG4gIGRpZmZDc3MsXG4gIGRpZmZKc29uLFxuXG4gIGRpZmZBcnJheXMsXG5cbiAgc3RydWN0dXJlZFBhdGNoLFxuICBjcmVhdGVUd29GaWxlc1BhdGNoLFxuICBjcmVhdGVQYXRjaCxcbiAgZm9ybWF0UGF0Y2gsXG4gIGFwcGx5UGF0Y2gsXG4gIGFwcGx5UGF0Y2hlcyxcbiAgcGFyc2VQYXRjaCxcbiAgbWVyZ2UsXG4gIHJldmVyc2VQYXRjaCxcbiAgY29udmVydENoYW5nZXNUb0RNUCxcbiAgY29udmVydENoYW5nZXNUb1hNTCxcbiAgY2Fub25pY2FsaXplXG59O1xuIl0sIm1hcHBpbmdzIjoiOzs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7O0FBZ0JBO0FBQUE7QUFBQUEsS0FBQSxHQUFBQyxzQkFBQSxDQUFBQyxPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQUMsVUFBQSxHQUFBRCxPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQUUsS0FBQSxHQUFBRixPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQUcsS0FBQSxHQUFBSCxPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQUksU0FBQSxHQUFBSixPQUFBO0FBQUE7QUFBQTtBQUVBO0FBQUE7QUFBQUssSUFBQSxHQUFBTCxPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQU0sS0FBQSxHQUFBTixPQUFBO0FBQUE7QUFBQTtBQUVBO0FBQUE7QUFBQU8sTUFBQSxHQUFBUCxPQUFBO0FBQUE7QUFBQTtBQUVBO0FBQUE7QUFBQVEsTUFBQSxHQUFBUixPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQVMsTUFBQSxHQUFBVCxPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQVUsTUFBQSxHQUFBVixPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQVcsUUFBQSxHQUFBWCxPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQVksT0FBQSxHQUFBWixPQUFBO0FBQUE7QUFBQTtBQUVBO0FBQUE7QUFBQWEsSUFBQSxHQUFBYixPQUFBO0FBQUE7QUFBQTtBQUNBO0FBQUE7QUFBQWMsSUFBQSxHQUFBZCxPQUFBO0FBQUE7QUFBQTtBQUFrRCxtQ0FBQUQsdUJBQUFnQixHQUFBLFdBQUFBLEdBQUEsSUFBQUEsR0FBQSxDQUFBQyxVQUFBLEdBQUFELEdBQUEsZ0JBQUFBLEdBQUE7QUFBQSIsImlnbm9yZUxpc3QiOltdfQ==
