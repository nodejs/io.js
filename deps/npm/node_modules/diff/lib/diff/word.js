/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.diffWords = diffWords;
exports.diffWordsWithSpace = diffWordsWithSpace;
exports.wordWithSpaceDiff = exports.wordDiff = void 0;
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_base = _interopRequireDefault(require("./base"))
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_string = require("../util/string")
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { "default": obj }; }
/*istanbul ignore end*/
// Based on https://en.wikipedia.org/wiki/Latin_script_in_Unicode
//
// Ranges and exceptions:
// Latin-1 Supplement, 0080–00FF
//  - U+00D7  × Multiplication sign
//  - U+00F7  ÷ Division sign
// Latin Extended-A, 0100–017F
// Latin Extended-B, 0180–024F
// IPA Extensions, 0250–02AF
// Spacing Modifier Letters, 02B0–02FF
//  - U+02C7  ˇ &#711;  Caron
//  - U+02D8  ˘ &#728;  Breve
//  - U+02D9  ˙ &#729;  Dot Above
//  - U+02DA  ˚ &#730;  Ring Above
//  - U+02DB  ˛ &#731;  Ogonek
//  - U+02DC  ˜ &#732;  Small Tilde
//  - U+02DD  ˝ &#733;  Double Acute Accent
// Latin Extended Additional, 1E00–1EFF
var extendedWordChars = "a-zA-Z0-9_\\u{C0}-\\u{FF}\\u{D8}-\\u{F6}\\u{F8}-\\u{2C6}\\u{2C8}-\\u{2D7}\\u{2DE}-\\u{2FF}\\u{1E00}-\\u{1EFF}";

// Each token is one of the following:
// - A punctuation mark plus the surrounding whitespace
// - A word plus the surrounding whitespace
// - Pure whitespace (but only in the special case where this the entire text
//   is just whitespace)
//
// We have to include surrounding whitespace in the tokens because the two
// alternative approaches produce horribly broken results:
// * If we just discard the whitespace, we can't fully reproduce the original
//   text from the sequence of tokens and any attempt to render the diff will
//   get the whitespace wrong.
// * If we have separate tokens for whitespace, then in a typical text every
//   second token will be a single space character. But this often results in
//   the optimal diff between two texts being a perverse one that preserves
//   the spaces between words but deletes and reinserts actual common words.
//   See https://github.com/kpdecker/jsdiff/issues/160#issuecomment-1866099640
//   for an example.
//
// Keeping the surrounding whitespace of course has implications for .equals
// and .join, not just .tokenize.

// This regex does NOT fully implement the tokenization rules described above.
// Instead, it gives runs of whitespace their own "token". The tokenize method
// then handles stitching whitespace tokens onto adjacent word or punctuation
// tokens.
var tokenizeIncludingWhitespace = new RegExp(
/*istanbul ignore start*/
"[".concat(
/*istanbul ignore end*/
extendedWordChars, "]+|\\s+|[^").concat(extendedWordChars, "]"), 'ug');
var wordDiff =
/*istanbul ignore start*/
exports.wordDiff =
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
wordDiff.equals = function (left, right, options) {
  if (options.ignoreCase) {
    left = left.toLowerCase();
    right = right.toLowerCase();
  }
  return left.trim() === right.trim();
};
wordDiff.tokenize = function (value) {
  /*istanbul ignore start*/
  var
  /*istanbul ignore end*/
  options = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : {};
  var parts;
  if (options.intlSegmenter) {
    if (options.intlSegmenter.resolvedOptions().granularity != 'word') {
      throw new Error('The segmenter passed must have a granularity of "word"');
    }
    parts = Array.from(options.intlSegmenter.segment(value), function (segment)
    /*istanbul ignore start*/
    {
      return (
        /*istanbul ignore end*/
        segment.segment
      );
    });
  } else {
    parts = value.match(tokenizeIncludingWhitespace) || [];
  }
  var tokens = [];
  var prevPart = null;
  parts.forEach(function (part) {
    if (/\s/.test(part)) {
      if (prevPart == null) {
        tokens.push(part);
      } else {
        tokens.push(tokens.pop() + part);
      }
    } else if (/\s/.test(prevPart)) {
      if (tokens[tokens.length - 1] == prevPart) {
        tokens.push(tokens.pop() + part);
      } else {
        tokens.push(prevPart + part);
      }
    } else {
      tokens.push(part);
    }
    prevPart = part;
  });
  return tokens;
};
wordDiff.join = function (tokens) {
  // Tokens being joined here will always have appeared consecutively in the
  // same text, so we can simply strip off the leading whitespace from all the
  // tokens except the first (and except any whitespace-only tokens - but such
  // a token will always be the first and only token anyway) and then join them
  // and the whitespace around words and punctuation will end up correct.
  return tokens.map(function (token, i) {
    if (i == 0) {
      return token;
    } else {
      return token.replace(/^\s+/, '');
    }
  }).join('');
};
wordDiff.postProcess = function (changes, options) {
  if (!changes || options.oneChangePerToken) {
    return changes;
  }
  var lastKeep = null;
  // Change objects representing any insertion or deletion since the last
  // "keep" change object. There can be at most one of each.
  var insertion = null;
  var deletion = null;
  changes.forEach(function (change) {
    if (change.added) {
      insertion = change;
    } else if (change.removed) {
      deletion = change;
    } else {
      if (insertion || deletion) {
        // May be false at start of text
        dedupeWhitespaceInChangeObjects(lastKeep, deletion, insertion, change);
      }
      lastKeep = change;
      insertion = null;
      deletion = null;
    }
  });
  if (insertion || deletion) {
    dedupeWhitespaceInChangeObjects(lastKeep, deletion, insertion, null);
  }
  return changes;
};
function diffWords(oldStr, newStr, options) {
  // This option has never been documented and never will be (it's clearer to
  // just call `diffWordsWithSpace` directly if you need that behavior), but
  // has existed in jsdiff for a long time, so we retain support for it here
  // for the sake of backwards compatibility.
  if (
  /*istanbul ignore start*/
  (
  /*istanbul ignore end*/
  options === null || options === void 0 ? void 0 : options.ignoreWhitespace) != null && !options.ignoreWhitespace) {
    return diffWordsWithSpace(oldStr, newStr, options);
  }
  return wordDiff.diff(oldStr, newStr, options);
}
function dedupeWhitespaceInChangeObjects(startKeep, deletion, insertion, endKeep) {
  // Before returning, we tidy up the leading and trailing whitespace of the
  // change objects to eliminate cases where trailing whitespace in one object
  // is repeated as leading whitespace in the next.
  // Below are examples of the outcomes we want here to explain the code.
  // I=insert, K=keep, D=delete
  // 1. diffing 'foo bar baz' vs 'foo baz'
  //    Prior to cleanup, we have K:'foo ' D:' bar ' K:' baz'
  //    After cleanup, we want:   K:'foo ' D:'bar ' K:'baz'
  //
  // 2. Diffing 'foo bar baz' vs 'foo qux baz'
  //    Prior to cleanup, we have K:'foo ' D:' bar ' I:' qux ' K:' baz'
  //    After cleanup, we want K:'foo ' D:'bar' I:'qux' K:' baz'
  //
  // 3. Diffing 'foo\nbar baz' vs 'foo baz'
  //    Prior to cleanup, we have K:'foo ' D:'\nbar ' K:' baz'
  //    After cleanup, we want K'foo' D:'\nbar' K:' baz'
  //
  // 4. Diffing 'foo baz' vs 'foo\nbar baz'
  //    Prior to cleanup, we have K:'foo\n' I:'\nbar ' K:' baz'
  //    After cleanup, we ideally want K'foo' I:'\nbar' K:' baz'
  //    but don't actually manage this currently (the pre-cleanup change
  //    objects don't contain enough information to make it possible).
  //
  // 5. Diffing 'foo   bar baz' vs 'foo  baz'
  //    Prior to cleanup, we have K:'foo  ' D:'   bar ' K:'  baz'
  //    After cleanup, we want K:'foo  ' D:' bar ' K:'baz'
  //
  // Our handling is unavoidably imperfect in the case where there's a single
  // indel between keeps and the whitespace has changed. For instance, consider
  // diffing 'foo\tbar\nbaz' vs 'foo baz'. Unless we create an extra change
  // object to represent the insertion of the space character (which isn't even
  // a token), we have no way to avoid losing information about the texts'
  // original whitespace in the result we return. Still, we do our best to
  // output something that will look sensible if we e.g. print it with
  // insertions in green and deletions in red.

  // Between two "keep" change objects (or before the first or after the last
  // change object), we can have either:
  // * A "delete" followed by an "insert"
  // * Just an "insert"
  // * Just a "delete"
  // We handle the three cases separately.
  if (deletion && insertion) {
    var oldWsPrefix = deletion.value.match(/^\s*/)[0];
    var oldWsSuffix = deletion.value.match(/\s*$/)[0];
    var newWsPrefix = insertion.value.match(/^\s*/)[0];
    var newWsSuffix = insertion.value.match(/\s*$/)[0];
    if (startKeep) {
      var commonWsPrefix =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      longestCommonPrefix)
      /*istanbul ignore end*/
      (oldWsPrefix, newWsPrefix);
      startKeep.value =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      replaceSuffix)
      /*istanbul ignore end*/
      (startKeep.value, newWsPrefix, commonWsPrefix);
      deletion.value =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      removePrefix)
      /*istanbul ignore end*/
      (deletion.value, commonWsPrefix);
      insertion.value =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      removePrefix)
      /*istanbul ignore end*/
      (insertion.value, commonWsPrefix);
    }
    if (endKeep) {
      var commonWsSuffix =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      longestCommonSuffix)
      /*istanbul ignore end*/
      (oldWsSuffix, newWsSuffix);
      endKeep.value =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      replacePrefix)
      /*istanbul ignore end*/
      (endKeep.value, newWsSuffix, commonWsSuffix);
      deletion.value =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      removeSuffix)
      /*istanbul ignore end*/
      (deletion.value, commonWsSuffix);
      insertion.value =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _string
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      removeSuffix)
      /*istanbul ignore end*/
      (insertion.value, commonWsSuffix);
    }
  } else if (insertion) {
    // The whitespaces all reflect what was in the new text rather than
    // the old, so we essentially have no information about whitespace
    // insertion or deletion. We just want to dedupe the whitespace.
    // We do that by having each change object keep its trailing
    // whitespace and deleting duplicate leading whitespace where
    // present.
    if (startKeep) {
      insertion.value = insertion.value.replace(/^\s*/, '');
    }
    if (endKeep) {
      endKeep.value = endKeep.value.replace(/^\s*/, '');
    }
    // otherwise we've got a deletion and no insertion
  } else if (startKeep && endKeep) {
    var newWsFull = endKeep.value.match(/^\s*/)[0],
      delWsStart = deletion.value.match(/^\s*/)[0],
      delWsEnd = deletion.value.match(/\s*$/)[0];

    // Any whitespace that comes straight after startKeep in both the old and
    // new texts, assign to startKeep and remove from the deletion.
    var newWsStart =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    longestCommonPrefix)
    /*istanbul ignore end*/
    (newWsFull, delWsStart);
    deletion.value =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    removePrefix)
    /*istanbul ignore end*/
    (deletion.value, newWsStart);

    // Any whitespace that comes straight before endKeep in both the old and
    // new texts, and hasn't already been assigned to startKeep, assign to
    // endKeep and remove from the deletion.
    var newWsEnd =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    longestCommonSuffix)
    /*istanbul ignore end*/
    (
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    removePrefix)
    /*istanbul ignore end*/
    (newWsFull, newWsStart), delWsEnd);
    deletion.value =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    removeSuffix)
    /*istanbul ignore end*/
    (deletion.value, newWsEnd);
    endKeep.value =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    replacePrefix)
    /*istanbul ignore end*/
    (endKeep.value, newWsFull, newWsEnd);

    // If there's any whitespace from the new text that HASN'T already been
    // assigned, assign it to the start:
    startKeep.value =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    replaceSuffix)
    /*istanbul ignore end*/
    (startKeep.value, newWsFull, newWsFull.slice(0, newWsFull.length - newWsEnd.length));
  } else if (endKeep) {
    // We are at the start of the text. Preserve all the whitespace on
    // endKeep, and just remove whitespace from the end of deletion to the
    // extent that it overlaps with the start of endKeep.
    var endKeepWsPrefix = endKeep.value.match(/^\s*/)[0];
    var deletionWsSuffix = deletion.value.match(/\s*$/)[0];
    var overlap =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    maximumOverlap)
    /*istanbul ignore end*/
    (deletionWsSuffix, endKeepWsPrefix);
    deletion.value =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    removeSuffix)
    /*istanbul ignore end*/
    (deletion.value, overlap);
  } else if (startKeep) {
    // We are at the END of the text. Preserve all the whitespace on
    // startKeep, and just remove whitespace from the start of deletion to
    // the extent that it overlaps with the end of startKeep.
    var startKeepWsSuffix = startKeep.value.match(/\s*$/)[0];
    var deletionWsPrefix = deletion.value.match(/^\s*/)[0];
    var _overlap =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    maximumOverlap)
    /*istanbul ignore end*/
    (startKeepWsSuffix, deletionWsPrefix);
    deletion.value =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    removePrefix)
    /*istanbul ignore end*/
    (deletion.value, _overlap);
  }
}
var wordWithSpaceDiff =
/*istanbul ignore start*/
exports.wordWithSpaceDiff =
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
wordWithSpaceDiff.tokenize = function (value) {
  // Slightly different to the tokenizeIncludingWhitespace regex used above in
  // that this one treats each individual newline as a distinct tokens, rather
  // than merging them into other surrounding whitespace. This was requested
  // in https://github.com/kpdecker/jsdiff/issues/180 &
  //    https://github.com/kpdecker/jsdiff/issues/211
  var regex = new RegExp(
  /*istanbul ignore start*/
  "(\\r?\\n)|[".concat(
  /*istanbul ignore end*/
  extendedWordChars, "]+|[^\\S\\n\\r]+|[^").concat(extendedWordChars, "]"), 'ug');
  return value.match(regex) || [];
};
function diffWordsWithSpace(oldStr, newStr, options) {
  return wordWithSpaceDiff.diff(oldStr, newStr, options);
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfYmFzZSIsIl9pbnRlcm9wUmVxdWlyZURlZmF1bHQiLCJyZXF1aXJlIiwiX3N0cmluZyIsIm9iaiIsIl9fZXNNb2R1bGUiLCJleHRlbmRlZFdvcmRDaGFycyIsInRva2VuaXplSW5jbHVkaW5nV2hpdGVzcGFjZSIsIlJlZ0V4cCIsImNvbmNhdCIsIndvcmREaWZmIiwiZXhwb3J0cyIsIkRpZmYiLCJlcXVhbHMiLCJsZWZ0IiwicmlnaHQiLCJvcHRpb25zIiwiaWdub3JlQ2FzZSIsInRvTG93ZXJDYXNlIiwidHJpbSIsInRva2VuaXplIiwidmFsdWUiLCJhcmd1bWVudHMiLCJsZW5ndGgiLCJ1bmRlZmluZWQiLCJwYXJ0cyIsImludGxTZWdtZW50ZXIiLCJyZXNvbHZlZE9wdGlvbnMiLCJncmFudWxhcml0eSIsIkVycm9yIiwiQXJyYXkiLCJmcm9tIiwic2VnbWVudCIsIm1hdGNoIiwidG9rZW5zIiwicHJldlBhcnQiLCJmb3JFYWNoIiwicGFydCIsInRlc3QiLCJwdXNoIiwicG9wIiwiam9pbiIsIm1hcCIsInRva2VuIiwiaSIsInJlcGxhY2UiLCJwb3N0UHJvY2VzcyIsImNoYW5nZXMiLCJvbmVDaGFuZ2VQZXJUb2tlbiIsImxhc3RLZWVwIiwiaW5zZXJ0aW9uIiwiZGVsZXRpb24iLCJjaGFuZ2UiLCJhZGRlZCIsInJlbW92ZWQiLCJkZWR1cGVXaGl0ZXNwYWNlSW5DaGFuZ2VPYmplY3RzIiwiZGlmZldvcmRzIiwib2xkU3RyIiwibmV3U3RyIiwiaWdub3JlV2hpdGVzcGFjZSIsImRpZmZXb3Jkc1dpdGhTcGFjZSIsImRpZmYiLCJzdGFydEtlZXAiLCJlbmRLZWVwIiwib2xkV3NQcmVmaXgiLCJvbGRXc1N1ZmZpeCIsIm5ld1dzUHJlZml4IiwibmV3V3NTdWZmaXgiLCJjb21tb25Xc1ByZWZpeCIsImxvbmdlc3RDb21tb25QcmVmaXgiLCJyZXBsYWNlU3VmZml4IiwicmVtb3ZlUHJlZml4IiwiY29tbW9uV3NTdWZmaXgiLCJsb25nZXN0Q29tbW9uU3VmZml4IiwicmVwbGFjZVByZWZpeCIsInJlbW92ZVN1ZmZpeCIsIm5ld1dzRnVsbCIsImRlbFdzU3RhcnQiLCJkZWxXc0VuZCIsIm5ld1dzU3RhcnQiLCJuZXdXc0VuZCIsInNsaWNlIiwiZW5kS2VlcFdzUHJlZml4IiwiZGVsZXRpb25Xc1N1ZmZpeCIsIm92ZXJsYXAiLCJtYXhpbXVtT3ZlcmxhcCIsInN0YXJ0S2VlcFdzU3VmZml4IiwiZGVsZXRpb25Xc1ByZWZpeCIsIndvcmRXaXRoU3BhY2VEaWZmIiwicmVnZXgiXSwic291cmNlcyI6WyIuLi8uLi9zcmMvZGlmZi93b3JkLmpzIl0sInNvdXJjZXNDb250ZW50IjpbImltcG9ydCBEaWZmIGZyb20gJy4vYmFzZSc7XG5pbXBvcnQgeyBsb25nZXN0Q29tbW9uUHJlZml4LCBsb25nZXN0Q29tbW9uU3VmZml4LCByZXBsYWNlUHJlZml4LCByZXBsYWNlU3VmZml4LCByZW1vdmVQcmVmaXgsIHJlbW92ZVN1ZmZpeCwgbWF4aW11bU92ZXJsYXAgfSBmcm9tICcuLi91dGlsL3N0cmluZyc7XG5cbi8vIEJhc2VkIG9uIGh0dHBzOi8vZW4ud2lraXBlZGlhLm9yZy93aWtpL0xhdGluX3NjcmlwdF9pbl9Vbmljb2RlXG4vL1xuLy8gUmFuZ2VzIGFuZCBleGNlcHRpb25zOlxuLy8gTGF0aW4tMSBTdXBwbGVtZW50LCAwMDgw4oCTMDBGRlxuLy8gIC0gVSswMEQ3ICDDlyBNdWx0aXBsaWNhdGlvbiBzaWduXG4vLyAgLSBVKzAwRjcgIMO3IERpdmlzaW9uIHNpZ25cbi8vIExhdGluIEV4dGVuZGVkLUEsIDAxMDDigJMwMTdGXG4vLyBMYXRpbiBFeHRlbmRlZC1CLCAwMTgw4oCTMDI0RlxuLy8gSVBBIEV4dGVuc2lvbnMsIDAyNTDigJMwMkFGXG4vLyBTcGFjaW5nIE1vZGlmaWVyIExldHRlcnMsIDAyQjDigJMwMkZGXG4vLyAgLSBVKzAyQzcgIMuHICYjNzExOyAgQ2Fyb25cbi8vICAtIFUrMDJEOCAgy5ggJiM3Mjg7ICBCcmV2ZVxuLy8gIC0gVSswMkQ5ICDLmSAmIzcyOTsgIERvdCBBYm92ZVxuLy8gIC0gVSswMkRBICDLmiAmIzczMDsgIFJpbmcgQWJvdmVcbi8vICAtIFUrMDJEQiAgy5sgJiM3MzE7ICBPZ29uZWtcbi8vICAtIFUrMDJEQyAgy5wgJiM3MzI7ICBTbWFsbCBUaWxkZVxuLy8gIC0gVSswMkREICDLnSAmIzczMzsgIERvdWJsZSBBY3V0ZSBBY2NlbnRcbi8vIExhdGluIEV4dGVuZGVkIEFkZGl0aW9uYWwsIDFFMDDigJMxRUZGXG5jb25zdCBleHRlbmRlZFdvcmRDaGFycyA9ICdhLXpBLVowLTlfXFxcXHV7QzB9LVxcXFx1e0ZGfVxcXFx1e0Q4fS1cXFxcdXtGNn1cXFxcdXtGOH0tXFxcXHV7MkM2fVxcXFx1ezJDOH0tXFxcXHV7MkQ3fVxcXFx1ezJERX0tXFxcXHV7MkZGfVxcXFx1ezFFMDB9LVxcXFx1ezFFRkZ9JztcblxuLy8gRWFjaCB0b2tlbiBpcyBvbmUgb2YgdGhlIGZvbGxvd2luZzpcbi8vIC0gQSBwdW5jdHVhdGlvbiBtYXJrIHBsdXMgdGhlIHN1cnJvdW5kaW5nIHdoaXRlc3BhY2Vcbi8vIC0gQSB3b3JkIHBsdXMgdGhlIHN1cnJvdW5kaW5nIHdoaXRlc3BhY2Vcbi8vIC0gUHVyZSB3aGl0ZXNwYWNlIChidXQgb25seSBpbiB0aGUgc3BlY2lhbCBjYXNlIHdoZXJlIHRoaXMgdGhlIGVudGlyZSB0ZXh0XG4vLyAgIGlzIGp1c3Qgd2hpdGVzcGFjZSlcbi8vXG4vLyBXZSBoYXZlIHRvIGluY2x1ZGUgc3Vycm91bmRpbmcgd2hpdGVzcGFjZSBpbiB0aGUgdG9rZW5zIGJlY2F1c2UgdGhlIHR3b1xuLy8gYWx0ZXJuYXRpdmUgYXBwcm9hY2hlcyBwcm9kdWNlIGhvcnJpYmx5IGJyb2tlbiByZXN1bHRzOlxuLy8gKiBJZiB3ZSBqdXN0IGRpc2NhcmQgdGhlIHdoaXRlc3BhY2UsIHdlIGNhbid0IGZ1bGx5IHJlcHJvZHVjZSB0aGUgb3JpZ2luYWxcbi8vICAgdGV4dCBmcm9tIHRoZSBzZXF1ZW5jZSBvZiB0b2tlbnMgYW5kIGFueSBhdHRlbXB0IHRvIHJlbmRlciB0aGUgZGlmZiB3aWxsXG4vLyAgIGdldCB0aGUgd2hpdGVzcGFjZSB3cm9uZy5cbi8vICogSWYgd2UgaGF2ZSBzZXBhcmF0ZSB0b2tlbnMgZm9yIHdoaXRlc3BhY2UsIHRoZW4gaW4gYSB0eXBpY2FsIHRleHQgZXZlcnlcbi8vICAgc2Vjb25kIHRva2VuIHdpbGwgYmUgYSBzaW5nbGUgc3BhY2UgY2hhcmFjdGVyLiBCdXQgdGhpcyBvZnRlbiByZXN1bHRzIGluXG4vLyAgIHRoZSBvcHRpbWFsIGRpZmYgYmV0d2VlbiB0d28gdGV4dHMgYmVpbmcgYSBwZXJ2ZXJzZSBvbmUgdGhhdCBwcmVzZXJ2ZXNcbi8vICAgdGhlIHNwYWNlcyBiZXR3ZWVuIHdvcmRzIGJ1dCBkZWxldGVzIGFuZCByZWluc2VydHMgYWN0dWFsIGNvbW1vbiB3b3Jkcy5cbi8vICAgU2VlIGh0dHBzOi8vZ2l0aHViLmNvbS9rcGRlY2tlci9qc2RpZmYvaXNzdWVzLzE2MCNpc3N1ZWNvbW1lbnQtMTg2NjA5OTY0MFxuLy8gICBmb3IgYW4gZXhhbXBsZS5cbi8vXG4vLyBLZWVwaW5nIHRoZSBzdXJyb3VuZGluZyB3aGl0ZXNwYWNlIG9mIGNvdXJzZSBoYXMgaW1wbGljYXRpb25zIGZvciAuZXF1YWxzXG4vLyBhbmQgLmpvaW4sIG5vdCBqdXN0IC50b2tlbml6ZS5cblxuLy8gVGhpcyByZWdleCBkb2VzIE5PVCBmdWxseSBpbXBsZW1lbnQgdGhlIHRva2VuaXphdGlvbiBydWxlcyBkZXNjcmliZWQgYWJvdmUuXG4vLyBJbnN0ZWFkLCBpdCBnaXZlcyBydW5zIG9mIHdoaXRlc3BhY2UgdGhlaXIgb3duIFwidG9rZW5cIi4gVGhlIHRva2VuaXplIG1ldGhvZFxuLy8gdGhlbiBoYW5kbGVzIHN0aXRjaGluZyB3aGl0ZXNwYWNlIHRva2VucyBvbnRvIGFkamFjZW50IHdvcmQgb3IgcHVuY3R1YXRpb25cbi8vIHRva2Vucy5cbmNvbnN0IHRva2VuaXplSW5jbHVkaW5nV2hpdGVzcGFjZSA9IG5ldyBSZWdFeHAoYFske2V4dGVuZGVkV29yZENoYXJzfV0rfFxcXFxzK3xbXiR7ZXh0ZW5kZWRXb3JkQ2hhcnN9XWAsICd1ZycpO1xuXG5leHBvcnQgY29uc3Qgd29yZERpZmYgPSBuZXcgRGlmZigpO1xud29yZERpZmYuZXF1YWxzID0gZnVuY3Rpb24obGVmdCwgcmlnaHQsIG9wdGlvbnMpIHtcbiAgaWYgKG9wdGlvbnMuaWdub3JlQ2FzZSkge1xuICAgIGxlZnQgPSBsZWZ0LnRvTG93ZXJDYXNlKCk7XG4gICAgcmlnaHQgPSByaWdodC50b0xvd2VyQ2FzZSgpO1xuICB9XG5cbiAgcmV0dXJuIGxlZnQudHJpbSgpID09PSByaWdodC50cmltKCk7XG59O1xuXG53b3JkRGlmZi50b2tlbml6ZSA9IGZ1bmN0aW9uKHZhbHVlLCBvcHRpb25zID0ge30pIHtcbiAgbGV0IHBhcnRzO1xuICBpZiAob3B0aW9ucy5pbnRsU2VnbWVudGVyKSB7XG4gICAgaWYgKG9wdGlvbnMuaW50bFNlZ21lbnRlci5yZXNvbHZlZE9wdGlvbnMoKS5ncmFudWxhcml0eSAhPSAnd29yZCcpIHtcbiAgICAgIHRocm93IG5ldyBFcnJvcignVGhlIHNlZ21lbnRlciBwYXNzZWQgbXVzdCBoYXZlIGEgZ3JhbnVsYXJpdHkgb2YgXCJ3b3JkXCInKTtcbiAgICB9XG4gICAgcGFydHMgPSBBcnJheS5mcm9tKG9wdGlvbnMuaW50bFNlZ21lbnRlci5zZWdtZW50KHZhbHVlKSwgc2VnbWVudCA9PiBzZWdtZW50LnNlZ21lbnQpO1xuICB9IGVsc2Uge1xuICAgIHBhcnRzID0gdmFsdWUubWF0Y2godG9rZW5pemVJbmNsdWRpbmdXaGl0ZXNwYWNlKSB8fCBbXTtcbiAgfVxuICBjb25zdCB0b2tlbnMgPSBbXTtcbiAgbGV0IHByZXZQYXJ0ID0gbnVsbDtcbiAgcGFydHMuZm9yRWFjaChwYXJ0ID0+IHtcbiAgICBpZiAoKC9cXHMvKS50ZXN0KHBhcnQpKSB7XG4gICAgICBpZiAocHJldlBhcnQgPT0gbnVsbCkge1xuICAgICAgICB0b2tlbnMucHVzaChwYXJ0KTtcbiAgICAgIH0gZWxzZSB7XG4gICAgICAgIHRva2Vucy5wdXNoKHRva2Vucy5wb3AoKSArIHBhcnQpO1xuICAgICAgfVxuICAgIH0gZWxzZSBpZiAoKC9cXHMvKS50ZXN0KHByZXZQYXJ0KSkge1xuICAgICAgaWYgKHRva2Vuc1t0b2tlbnMubGVuZ3RoIC0gMV0gPT0gcHJldlBhcnQpIHtcbiAgICAgICAgdG9rZW5zLnB1c2godG9rZW5zLnBvcCgpICsgcGFydCk7XG4gICAgICB9IGVsc2Uge1xuICAgICAgICB0b2tlbnMucHVzaChwcmV2UGFydCArIHBhcnQpO1xuICAgICAgfVxuICAgIH0gZWxzZSB7XG4gICAgICB0b2tlbnMucHVzaChwYXJ0KTtcbiAgICB9XG5cbiAgICBwcmV2UGFydCA9IHBhcnQ7XG4gIH0pO1xuICByZXR1cm4gdG9rZW5zO1xufTtcblxud29yZERpZmYuam9pbiA9IGZ1bmN0aW9uKHRva2Vucykge1xuICAvLyBUb2tlbnMgYmVpbmcgam9pbmVkIGhlcmUgd2lsbCBhbHdheXMgaGF2ZSBhcHBlYXJlZCBjb25zZWN1dGl2ZWx5IGluIHRoZVxuICAvLyBzYW1lIHRleHQsIHNvIHdlIGNhbiBzaW1wbHkgc3RyaXAgb2ZmIHRoZSBsZWFkaW5nIHdoaXRlc3BhY2UgZnJvbSBhbGwgdGhlXG4gIC8vIHRva2VucyBleGNlcHQgdGhlIGZpcnN0IChhbmQgZXhjZXB0IGFueSB3aGl0ZXNwYWNlLW9ubHkgdG9rZW5zIC0gYnV0IHN1Y2hcbiAgLy8gYSB0b2tlbiB3aWxsIGFsd2F5cyBiZSB0aGUgZmlyc3QgYW5kIG9ubHkgdG9rZW4gYW55d2F5KSBhbmQgdGhlbiBqb2luIHRoZW1cbiAgLy8gYW5kIHRoZSB3aGl0ZXNwYWNlIGFyb3VuZCB3b3JkcyBhbmQgcHVuY3R1YXRpb24gd2lsbCBlbmQgdXAgY29ycmVjdC5cbiAgcmV0dXJuIHRva2Vucy5tYXAoKHRva2VuLCBpKSA9PiB7XG4gICAgaWYgKGkgPT0gMCkge1xuICAgICAgcmV0dXJuIHRva2VuO1xuICAgIH0gZWxzZSB7XG4gICAgICByZXR1cm4gdG9rZW4ucmVwbGFjZSgoL15cXHMrLyksICcnKTtcbiAgICB9XG4gIH0pLmpvaW4oJycpO1xufTtcblxud29yZERpZmYucG9zdFByb2Nlc3MgPSBmdW5jdGlvbihjaGFuZ2VzLCBvcHRpb25zKSB7XG4gIGlmICghY2hhbmdlcyB8fCBvcHRpb25zLm9uZUNoYW5nZVBlclRva2VuKSB7XG4gICAgcmV0dXJuIGNoYW5nZXM7XG4gIH1cblxuICBsZXQgbGFzdEtlZXAgPSBudWxsO1xuICAvLyBDaGFuZ2Ugb2JqZWN0cyByZXByZXNlbnRpbmcgYW55IGluc2VydGlvbiBvciBkZWxldGlvbiBzaW5jZSB0aGUgbGFzdFxuICAvLyBcImtlZXBcIiBjaGFuZ2Ugb2JqZWN0LiBUaGVyZSBjYW4gYmUgYXQgbW9zdCBvbmUgb2YgZWFjaC5cbiAgbGV0IGluc2VydGlvbiA9IG51bGw7XG4gIGxldCBkZWxldGlvbiA9IG51bGw7XG4gIGNoYW5nZXMuZm9yRWFjaChjaGFuZ2UgPT4ge1xuICAgIGlmIChjaGFuZ2UuYWRkZWQpIHtcbiAgICAgIGluc2VydGlvbiA9IGNoYW5nZTtcbiAgICB9IGVsc2UgaWYgKGNoYW5nZS5yZW1vdmVkKSB7XG4gICAgICBkZWxldGlvbiA9IGNoYW5nZTtcbiAgICB9IGVsc2Uge1xuICAgICAgaWYgKGluc2VydGlvbiB8fCBkZWxldGlvbikgeyAvLyBNYXkgYmUgZmFsc2UgYXQgc3RhcnQgb2YgdGV4dFxuICAgICAgICBkZWR1cGVXaGl0ZXNwYWNlSW5DaGFuZ2VPYmplY3RzKGxhc3RLZWVwLCBkZWxldGlvbiwgaW5zZXJ0aW9uLCBjaGFuZ2UpO1xuICAgICAgfVxuICAgICAgbGFzdEtlZXAgPSBjaGFuZ2U7XG4gICAgICBpbnNlcnRpb24gPSBudWxsO1xuICAgICAgZGVsZXRpb24gPSBudWxsO1xuICAgIH1cbiAgfSk7XG4gIGlmIChpbnNlcnRpb24gfHwgZGVsZXRpb24pIHtcbiAgICBkZWR1cGVXaGl0ZXNwYWNlSW5DaGFuZ2VPYmplY3RzKGxhc3RLZWVwLCBkZWxldGlvbiwgaW5zZXJ0aW9uLCBudWxsKTtcbiAgfVxuICByZXR1cm4gY2hhbmdlcztcbn07XG5cbmV4cG9ydCBmdW5jdGlvbiBkaWZmV29yZHMob2xkU3RyLCBuZXdTdHIsIG9wdGlvbnMpIHtcbiAgLy8gVGhpcyBvcHRpb24gaGFzIG5ldmVyIGJlZW4gZG9jdW1lbnRlZCBhbmQgbmV2ZXIgd2lsbCBiZSAoaXQncyBjbGVhcmVyIHRvXG4gIC8vIGp1c3QgY2FsbCBgZGlmZldvcmRzV2l0aFNwYWNlYCBkaXJlY3RseSBpZiB5b3UgbmVlZCB0aGF0IGJlaGF2aW9yKSwgYnV0XG4gIC8vIGhhcyBleGlzdGVkIGluIGpzZGlmZiBmb3IgYSBsb25nIHRpbWUsIHNvIHdlIHJldGFpbiBzdXBwb3J0IGZvciBpdCBoZXJlXG4gIC8vIGZvciB0aGUgc2FrZSBvZiBiYWNrd2FyZHMgY29tcGF0aWJpbGl0eS5cbiAgaWYgKG9wdGlvbnM/Lmlnbm9yZVdoaXRlc3BhY2UgIT0gbnVsbCAmJiAhb3B0aW9ucy5pZ25vcmVXaGl0ZXNwYWNlKSB7XG4gICAgcmV0dXJuIGRpZmZXb3Jkc1dpdGhTcGFjZShvbGRTdHIsIG5ld1N0ciwgb3B0aW9ucyk7XG4gIH1cblxuICByZXR1cm4gd29yZERpZmYuZGlmZihvbGRTdHIsIG5ld1N0ciwgb3B0aW9ucyk7XG59XG5cbmZ1bmN0aW9uIGRlZHVwZVdoaXRlc3BhY2VJbkNoYW5nZU9iamVjdHMoc3RhcnRLZWVwLCBkZWxldGlvbiwgaW5zZXJ0aW9uLCBlbmRLZWVwKSB7XG4gIC8vIEJlZm9yZSByZXR1cm5pbmcsIHdlIHRpZHkgdXAgdGhlIGxlYWRpbmcgYW5kIHRyYWlsaW5nIHdoaXRlc3BhY2Ugb2YgdGhlXG4gIC8vIGNoYW5nZSBvYmplY3RzIHRvIGVsaW1pbmF0ZSBjYXNlcyB3aGVyZSB0cmFpbGluZyB3aGl0ZXNwYWNlIGluIG9uZSBvYmplY3RcbiAgLy8gaXMgcmVwZWF0ZWQgYXMgbGVhZGluZyB3aGl0ZXNwYWNlIGluIHRoZSBuZXh0LlxuICAvLyBCZWxvdyBhcmUgZXhhbXBsZXMgb2YgdGhlIG91dGNvbWVzIHdlIHdhbnQgaGVyZSB0byBleHBsYWluIHRoZSBjb2RlLlxuICAvLyBJPWluc2VydCwgSz1rZWVwLCBEPWRlbGV0ZVxuICAvLyAxLiBkaWZmaW5nICdmb28gYmFyIGJheicgdnMgJ2ZvbyBiYXonXG4gIC8vICAgIFByaW9yIHRvIGNsZWFudXAsIHdlIGhhdmUgSzonZm9vICcgRDonIGJhciAnIEs6JyBiYXonXG4gIC8vICAgIEFmdGVyIGNsZWFudXAsIHdlIHdhbnQ6ICAgSzonZm9vICcgRDonYmFyICcgSzonYmF6J1xuICAvL1xuICAvLyAyLiBEaWZmaW5nICdmb28gYmFyIGJheicgdnMgJ2ZvbyBxdXggYmF6J1xuICAvLyAgICBQcmlvciB0byBjbGVhbnVwLCB3ZSBoYXZlIEs6J2ZvbyAnIEQ6JyBiYXIgJyBJOicgcXV4ICcgSzonIGJheidcbiAgLy8gICAgQWZ0ZXIgY2xlYW51cCwgd2Ugd2FudCBLOidmb28gJyBEOidiYXInIEk6J3F1eCcgSzonIGJheidcbiAgLy9cbiAgLy8gMy4gRGlmZmluZyAnZm9vXFxuYmFyIGJheicgdnMgJ2ZvbyBiYXonXG4gIC8vICAgIFByaW9yIHRvIGNsZWFudXAsIHdlIGhhdmUgSzonZm9vICcgRDonXFxuYmFyICcgSzonIGJheidcbiAgLy8gICAgQWZ0ZXIgY2xlYW51cCwgd2Ugd2FudCBLJ2ZvbycgRDonXFxuYmFyJyBLOicgYmF6J1xuICAvL1xuICAvLyA0LiBEaWZmaW5nICdmb28gYmF6JyB2cyAnZm9vXFxuYmFyIGJheidcbiAgLy8gICAgUHJpb3IgdG8gY2xlYW51cCwgd2UgaGF2ZSBLOidmb29cXG4nIEk6J1xcbmJhciAnIEs6JyBiYXonXG4gIC8vICAgIEFmdGVyIGNsZWFudXAsIHdlIGlkZWFsbHkgd2FudCBLJ2ZvbycgSTonXFxuYmFyJyBLOicgYmF6J1xuICAvLyAgICBidXQgZG9uJ3QgYWN0dWFsbHkgbWFuYWdlIHRoaXMgY3VycmVudGx5ICh0aGUgcHJlLWNsZWFudXAgY2hhbmdlXG4gIC8vICAgIG9iamVjdHMgZG9uJ3QgY29udGFpbiBlbm91Z2ggaW5mb3JtYXRpb24gdG8gbWFrZSBpdCBwb3NzaWJsZSkuXG4gIC8vXG4gIC8vIDUuIERpZmZpbmcgJ2ZvbyAgIGJhciBiYXonIHZzICdmb28gIGJheidcbiAgLy8gICAgUHJpb3IgdG8gY2xlYW51cCwgd2UgaGF2ZSBLOidmb28gICcgRDonICAgYmFyICcgSzonICBiYXonXG4gIC8vICAgIEFmdGVyIGNsZWFudXAsIHdlIHdhbnQgSzonZm9vICAnIEQ6JyBiYXIgJyBLOidiYXonXG4gIC8vXG4gIC8vIE91ciBoYW5kbGluZyBpcyB1bmF2b2lkYWJseSBpbXBlcmZlY3QgaW4gdGhlIGNhc2Ugd2hlcmUgdGhlcmUncyBhIHNpbmdsZVxuICAvLyBpbmRlbCBiZXR3ZWVuIGtlZXBzIGFuZCB0aGUgd2hpdGVzcGFjZSBoYXMgY2hhbmdlZC4gRm9yIGluc3RhbmNlLCBjb25zaWRlclxuICAvLyBkaWZmaW5nICdmb29cXHRiYXJcXG5iYXonIHZzICdmb28gYmF6Jy4gVW5sZXNzIHdlIGNyZWF0ZSBhbiBleHRyYSBjaGFuZ2VcbiAgLy8gb2JqZWN0IHRvIHJlcHJlc2VudCB0aGUgaW5zZXJ0aW9uIG9mIHRoZSBzcGFjZSBjaGFyYWN0ZXIgKHdoaWNoIGlzbid0IGV2ZW5cbiAgLy8gYSB0b2tlbiksIHdlIGhhdmUgbm8gd2F5IHRvIGF2b2lkIGxvc2luZyBpbmZvcm1hdGlvbiBhYm91dCB0aGUgdGV4dHMnXG4gIC8vIG9yaWdpbmFsIHdoaXRlc3BhY2UgaW4gdGhlIHJlc3VsdCB3ZSByZXR1cm4uIFN0aWxsLCB3ZSBkbyBvdXIgYmVzdCB0b1xuICAvLyBvdXRwdXQgc29tZXRoaW5nIHRoYXQgd2lsbCBsb29rIHNlbnNpYmxlIGlmIHdlIGUuZy4gcHJpbnQgaXQgd2l0aFxuICAvLyBpbnNlcnRpb25zIGluIGdyZWVuIGFuZCBkZWxldGlvbnMgaW4gcmVkLlxuXG4gIC8vIEJldHdlZW4gdHdvIFwia2VlcFwiIGNoYW5nZSBvYmplY3RzIChvciBiZWZvcmUgdGhlIGZpcnN0IG9yIGFmdGVyIHRoZSBsYXN0XG4gIC8vIGNoYW5nZSBvYmplY3QpLCB3ZSBjYW4gaGF2ZSBlaXRoZXI6XG4gIC8vICogQSBcImRlbGV0ZVwiIGZvbGxvd2VkIGJ5IGFuIFwiaW5zZXJ0XCJcbiAgLy8gKiBKdXN0IGFuIFwiaW5zZXJ0XCJcbiAgLy8gKiBKdXN0IGEgXCJkZWxldGVcIlxuICAvLyBXZSBoYW5kbGUgdGhlIHRocmVlIGNhc2VzIHNlcGFyYXRlbHkuXG4gIGlmIChkZWxldGlvbiAmJiBpbnNlcnRpb24pIHtcbiAgICBjb25zdCBvbGRXc1ByZWZpeCA9IGRlbGV0aW9uLnZhbHVlLm1hdGNoKC9eXFxzKi8pWzBdO1xuICAgIGNvbnN0IG9sZFdzU3VmZml4ID0gZGVsZXRpb24udmFsdWUubWF0Y2goL1xccyokLylbMF07XG4gICAgY29uc3QgbmV3V3NQcmVmaXggPSBpbnNlcnRpb24udmFsdWUubWF0Y2goL15cXHMqLylbMF07XG4gICAgY29uc3QgbmV3V3NTdWZmaXggPSBpbnNlcnRpb24udmFsdWUubWF0Y2goL1xccyokLylbMF07XG5cbiAgICBpZiAoc3RhcnRLZWVwKSB7XG4gICAgICBjb25zdCBjb21tb25Xc1ByZWZpeCA9IGxvbmdlc3RDb21tb25QcmVmaXgob2xkV3NQcmVmaXgsIG5ld1dzUHJlZml4KTtcbiAgICAgIHN0YXJ0S2VlcC52YWx1ZSA9IHJlcGxhY2VTdWZmaXgoc3RhcnRLZWVwLnZhbHVlLCBuZXdXc1ByZWZpeCwgY29tbW9uV3NQcmVmaXgpO1xuICAgICAgZGVsZXRpb24udmFsdWUgPSByZW1vdmVQcmVmaXgoZGVsZXRpb24udmFsdWUsIGNvbW1vbldzUHJlZml4KTtcbiAgICAgIGluc2VydGlvbi52YWx1ZSA9IHJlbW92ZVByZWZpeChpbnNlcnRpb24udmFsdWUsIGNvbW1vbldzUHJlZml4KTtcbiAgICB9XG4gICAgaWYgKGVuZEtlZXApIHtcbiAgICAgIGNvbnN0IGNvbW1vbldzU3VmZml4ID0gbG9uZ2VzdENvbW1vblN1ZmZpeChvbGRXc1N1ZmZpeCwgbmV3V3NTdWZmaXgpO1xuICAgICAgZW5kS2VlcC52YWx1ZSA9IHJlcGxhY2VQcmVmaXgoZW5kS2VlcC52YWx1ZSwgbmV3V3NTdWZmaXgsIGNvbW1vbldzU3VmZml4KTtcbiAgICAgIGRlbGV0aW9uLnZhbHVlID0gcmVtb3ZlU3VmZml4KGRlbGV0aW9uLnZhbHVlLCBjb21tb25Xc1N1ZmZpeCk7XG4gICAgICBpbnNlcnRpb24udmFsdWUgPSByZW1vdmVTdWZmaXgoaW5zZXJ0aW9uLnZhbHVlLCBjb21tb25Xc1N1ZmZpeCk7XG4gICAgfVxuICB9IGVsc2UgaWYgKGluc2VydGlvbikge1xuICAgIC8vIFRoZSB3aGl0ZXNwYWNlcyBhbGwgcmVmbGVjdCB3aGF0IHdhcyBpbiB0aGUgbmV3IHRleHQgcmF0aGVyIHRoYW5cbiAgICAvLyB0aGUgb2xkLCBzbyB3ZSBlc3NlbnRpYWxseSBoYXZlIG5vIGluZm9ybWF0aW9uIGFib3V0IHdoaXRlc3BhY2VcbiAgICAvLyBpbnNlcnRpb24gb3IgZGVsZXRpb24uIFdlIGp1c3Qgd2FudCB0byBkZWR1cGUgdGhlIHdoaXRlc3BhY2UuXG4gICAgLy8gV2UgZG8gdGhhdCBieSBoYXZpbmcgZWFjaCBjaGFuZ2Ugb2JqZWN0IGtlZXAgaXRzIHRyYWlsaW5nXG4gICAgLy8gd2hpdGVzcGFjZSBhbmQgZGVsZXRpbmcgZHVwbGljYXRlIGxlYWRpbmcgd2hpdGVzcGFjZSB3aGVyZVxuICAgIC8vIHByZXNlbnQuXG4gICAgaWYgKHN0YXJ0S2VlcCkge1xuICAgICAgaW5zZXJ0aW9uLnZhbHVlID0gaW5zZXJ0aW9uLnZhbHVlLnJlcGxhY2UoL15cXHMqLywgJycpO1xuICAgIH1cbiAgICBpZiAoZW5kS2VlcCkge1xuICAgICAgZW5kS2VlcC52YWx1ZSA9IGVuZEtlZXAudmFsdWUucmVwbGFjZSgvXlxccyovLCAnJyk7XG4gICAgfVxuICAvLyBvdGhlcndpc2Ugd2UndmUgZ290IGEgZGVsZXRpb24gYW5kIG5vIGluc2VydGlvblxuICB9IGVsc2UgaWYgKHN0YXJ0S2VlcCAmJiBlbmRLZWVwKSB7XG4gICAgY29uc3QgbmV3V3NGdWxsID0gZW5kS2VlcC52YWx1ZS5tYXRjaCgvXlxccyovKVswXSxcbiAgICAgICAgZGVsV3NTdGFydCA9IGRlbGV0aW9uLnZhbHVlLm1hdGNoKC9eXFxzKi8pWzBdLFxuICAgICAgICBkZWxXc0VuZCA9IGRlbGV0aW9uLnZhbHVlLm1hdGNoKC9cXHMqJC8pWzBdO1xuXG4gICAgLy8gQW55IHdoaXRlc3BhY2UgdGhhdCBjb21lcyBzdHJhaWdodCBhZnRlciBzdGFydEtlZXAgaW4gYm90aCB0aGUgb2xkIGFuZFxuICAgIC8vIG5ldyB0ZXh0cywgYXNzaWduIHRvIHN0YXJ0S2VlcCBhbmQgcmVtb3ZlIGZyb20gdGhlIGRlbGV0aW9uLlxuICAgIGNvbnN0IG5ld1dzU3RhcnQgPSBsb25nZXN0Q29tbW9uUHJlZml4KG5ld1dzRnVsbCwgZGVsV3NTdGFydCk7XG4gICAgZGVsZXRpb24udmFsdWUgPSByZW1vdmVQcmVmaXgoZGVsZXRpb24udmFsdWUsIG5ld1dzU3RhcnQpO1xuXG4gICAgLy8gQW55IHdoaXRlc3BhY2UgdGhhdCBjb21lcyBzdHJhaWdodCBiZWZvcmUgZW5kS2VlcCBpbiBib3RoIHRoZSBvbGQgYW5kXG4gICAgLy8gbmV3IHRleHRzLCBhbmQgaGFzbid0IGFscmVhZHkgYmVlbiBhc3NpZ25lZCB0byBzdGFydEtlZXAsIGFzc2lnbiB0b1xuICAgIC8vIGVuZEtlZXAgYW5kIHJlbW92ZSBmcm9tIHRoZSBkZWxldGlvbi5cbiAgICBjb25zdCBuZXdXc0VuZCA9IGxvbmdlc3RDb21tb25TdWZmaXgoXG4gICAgICByZW1vdmVQcmVmaXgobmV3V3NGdWxsLCBuZXdXc1N0YXJ0KSxcbiAgICAgIGRlbFdzRW5kXG4gICAgKTtcbiAgICBkZWxldGlvbi52YWx1ZSA9IHJlbW92ZVN1ZmZpeChkZWxldGlvbi52YWx1ZSwgbmV3V3NFbmQpO1xuICAgIGVuZEtlZXAudmFsdWUgPSByZXBsYWNlUHJlZml4KGVuZEtlZXAudmFsdWUsIG5ld1dzRnVsbCwgbmV3V3NFbmQpO1xuXG4gICAgLy8gSWYgdGhlcmUncyBhbnkgd2hpdGVzcGFjZSBmcm9tIHRoZSBuZXcgdGV4dCB0aGF0IEhBU04nVCBhbHJlYWR5IGJlZW5cbiAgICAvLyBhc3NpZ25lZCwgYXNzaWduIGl0IHRvIHRoZSBzdGFydDpcbiAgICBzdGFydEtlZXAudmFsdWUgPSByZXBsYWNlU3VmZml4KFxuICAgICAgc3RhcnRLZWVwLnZhbHVlLFxuICAgICAgbmV3V3NGdWxsLFxuICAgICAgbmV3V3NGdWxsLnNsaWNlKDAsIG5ld1dzRnVsbC5sZW5ndGggLSBuZXdXc0VuZC5sZW5ndGgpXG4gICAgKTtcbiAgfSBlbHNlIGlmIChlbmRLZWVwKSB7XG4gICAgLy8gV2UgYXJlIGF0IHRoZSBzdGFydCBvZiB0aGUgdGV4dC4gUHJlc2VydmUgYWxsIHRoZSB3aGl0ZXNwYWNlIG9uXG4gICAgLy8gZW5kS2VlcCwgYW5kIGp1c3QgcmVtb3ZlIHdoaXRlc3BhY2UgZnJvbSB0aGUgZW5kIG9mIGRlbGV0aW9uIHRvIHRoZVxuICAgIC8vIGV4dGVudCB0aGF0IGl0IG92ZXJsYXBzIHdpdGggdGhlIHN0YXJ0IG9mIGVuZEtlZXAuXG4gICAgY29uc3QgZW5kS2VlcFdzUHJlZml4ID0gZW5kS2VlcC52YWx1ZS5tYXRjaCgvXlxccyovKVswXTtcbiAgICBjb25zdCBkZWxldGlvbldzU3VmZml4ID0gZGVsZXRpb24udmFsdWUubWF0Y2goL1xccyokLylbMF07XG4gICAgY29uc3Qgb3ZlcmxhcCA9IG1heGltdW1PdmVybGFwKGRlbGV0aW9uV3NTdWZmaXgsIGVuZEtlZXBXc1ByZWZpeCk7XG4gICAgZGVsZXRpb24udmFsdWUgPSByZW1vdmVTdWZmaXgoZGVsZXRpb24udmFsdWUsIG92ZXJsYXApO1xuICB9IGVsc2UgaWYgKHN0YXJ0S2VlcCkge1xuICAgIC8vIFdlIGFyZSBhdCB0aGUgRU5EIG9mIHRoZSB0ZXh0LiBQcmVzZXJ2ZSBhbGwgdGhlIHdoaXRlc3BhY2Ugb25cbiAgICAvLyBzdGFydEtlZXAsIGFuZCBqdXN0IHJlbW92ZSB3aGl0ZXNwYWNlIGZyb20gdGhlIHN0YXJ0IG9mIGRlbGV0aW9uIHRvXG4gICAgLy8gdGhlIGV4dGVudCB0aGF0IGl0IG92ZXJsYXBzIHdpdGggdGhlIGVuZCBvZiBzdGFydEtlZXAuXG4gICAgY29uc3Qgc3RhcnRLZWVwV3NTdWZmaXggPSBzdGFydEtlZXAudmFsdWUubWF0Y2goL1xccyokLylbMF07XG4gICAgY29uc3QgZGVsZXRpb25Xc1ByZWZpeCA9IGRlbGV0aW9uLnZhbHVlLm1hdGNoKC9eXFxzKi8pWzBdO1xuICAgIGNvbnN0IG92ZXJsYXAgPSBtYXhpbXVtT3ZlcmxhcChzdGFydEtlZXBXc1N1ZmZpeCwgZGVsZXRpb25Xc1ByZWZpeCk7XG4gICAgZGVsZXRpb24udmFsdWUgPSByZW1vdmVQcmVmaXgoZGVsZXRpb24udmFsdWUsIG92ZXJsYXApO1xuICB9XG59XG5cblxuZXhwb3J0IGNvbnN0IHdvcmRXaXRoU3BhY2VEaWZmID0gbmV3IERpZmYoKTtcbndvcmRXaXRoU3BhY2VEaWZmLnRva2VuaXplID0gZnVuY3Rpb24odmFsdWUpIHtcbiAgLy8gU2xpZ2h0bHkgZGlmZmVyZW50IHRvIHRoZSB0b2tlbml6ZUluY2x1ZGluZ1doaXRlc3BhY2UgcmVnZXggdXNlZCBhYm92ZSBpblxuICAvLyB0aGF0IHRoaXMgb25lIHRyZWF0cyBlYWNoIGluZGl2aWR1YWwgbmV3bGluZSBhcyBhIGRpc3RpbmN0IHRva2VucywgcmF0aGVyXG4gIC8vIHRoYW4gbWVyZ2luZyB0aGVtIGludG8gb3RoZXIgc3Vycm91bmRpbmcgd2hpdGVzcGFjZS4gVGhpcyB3YXMgcmVxdWVzdGVkXG4gIC8vIGluIGh0dHBzOi8vZ2l0aHViLmNvbS9rcGRlY2tlci9qc2RpZmYvaXNzdWVzLzE4MCAmXG4gIC8vICAgIGh0dHBzOi8vZ2l0aHViLmNvbS9rcGRlY2tlci9qc2RpZmYvaXNzdWVzLzIxMVxuICBjb25zdCByZWdleCA9IG5ldyBSZWdFeHAoYChcXFxccj9cXFxcbil8WyR7ZXh0ZW5kZWRXb3JkQ2hhcnN9XSt8W15cXFxcU1xcXFxuXFxcXHJdK3xbXiR7ZXh0ZW5kZWRXb3JkQ2hhcnN9XWAsICd1ZycpO1xuICByZXR1cm4gdmFsdWUubWF0Y2gocmVnZXgpIHx8IFtdO1xufTtcbmV4cG9ydCBmdW5jdGlvbiBkaWZmV29yZHNXaXRoU3BhY2Uob2xkU3RyLCBuZXdTdHIsIG9wdGlvbnMpIHtcbiAgcmV0dXJuIHdvcmRXaXRoU3BhY2VEaWZmLmRpZmYob2xkU3RyLCBuZXdTdHIsIG9wdGlvbnMpO1xufVxuIl0sIm1hcHBpbmdzIjoiOzs7Ozs7Ozs7O0FBQUE7QUFBQTtBQUFBQSxLQUFBLEdBQUFDLHNCQUFBLENBQUFDLE9BQUE7QUFBQTtBQUFBO0FBQ0E7QUFBQTtBQUFBQyxPQUFBLEdBQUFELE9BQUE7QUFBQTtBQUFBO0FBQW9KLG1DQUFBRCx1QkFBQUcsR0FBQSxXQUFBQSxHQUFBLElBQUFBLEdBQUEsQ0FBQUMsVUFBQSxHQUFBRCxHQUFBLGdCQUFBQSxHQUFBO0FBQUE7QUFFcEo7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0EsSUFBTUUsaUJBQWlCLEdBQUcsK0dBQStHOztBQUV6STtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBO0FBQ0E7QUFDQTtBQUNBOztBQUVBO0FBQ0E7QUFDQTtBQUNBO0FBQ0EsSUFBTUMsMkJBQTJCLEdBQUcsSUFBSUMsTUFBTTtBQUFBO0FBQUEsSUFBQUMsTUFBQTtBQUFBO0FBQUtILGlCQUFpQixnQkFBQUcsTUFBQSxDQUFhSCxpQkFBaUIsUUFBSyxJQUFJLENBQUM7QUFFckcsSUFBTUksUUFBUTtBQUFBO0FBQUFDLE9BQUEsQ0FBQUQsUUFBQTtBQUFBO0FBQUc7QUFBSUU7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUE7QUFBQUEsQ0FBSSxDQUFDLENBQUM7QUFDbENGLFFBQVEsQ0FBQ0csTUFBTSxHQUFHLFVBQVNDLElBQUks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
