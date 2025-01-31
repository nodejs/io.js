/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.applyPatch = applyPatch;
exports.applyPatches = applyPatches;
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_string = require("../util/string")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_lineEndings = require("./line-endings")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_parse = require("./parse")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_distanceIterator = _interopRequireDefault(require("../util/distance-iterator"))
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { "default": obj }; }
/*istanbul ignore end*/
function applyPatch(source, uniDiff) {
  /*istanbul ignore start*/
  var
  /*istanbul ignore end*/
  options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
  if (typeof uniDiff === 'string') {
    uniDiff =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _parse
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    parsePatch)
    /*istanbul ignore end*/
    (uniDiff);
  }
  if (Array.isArray(uniDiff)) {
    if (uniDiff.length > 1) {
      throw new Error('applyPatch only works with a single input.');
    }
    uniDiff = uniDiff[0];
  }
  if (options.autoConvertLineEndings || options.autoConvertLineEndings == null) {
    if (
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    hasOnlyWinLineEndings)
    /*istanbul ignore end*/
    (source) &&
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _lineEndings
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    isUnix)
    /*istanbul ignore end*/
    (uniDiff)) {
      uniDiff =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _lineEndings
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      unixToWin)
      /*istanbul ignore end*/
      (uniDiff);
    } else if (
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _string
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    hasOnlyUnixLineEndings)
    /*istanbul ignore end*/
    (source) &&
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _lineEndings
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    isWin)
    /*istanbul ignore end*/
    (uniDiff)) {
      uniDiff =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _lineEndings
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      winToUnix)
      /*istanbul ignore end*/
      (uniDiff);
    }
  }

  // Apply the diff to the input
  var lines = source.split('\n'),
    hunks = uniDiff.hunks,
    compareLine = options.compareLine || function (lineNumber, line, operation, patchContent)
    /*istanbul ignore start*/
    {
      return (
        /*istanbul ignore end*/
        line === patchContent
      );
    },
    fuzzFactor = options.fuzzFactor || 0,
    minLine = 0;
  if (fuzzFactor < 0 || !Number.isInteger(fuzzFactor)) {
    throw new Error('fuzzFactor must be a non-negative integer');
  }

  // Special case for empty patch.
  if (!hunks.length) {
    return source;
  }

  // Before anything else, handle EOFNL insertion/removal. If the patch tells us to make a change
  // to the EOFNL that is redundant/impossible - i.e. to remove a newline that's not there, or add a
  // newline that already exists - then we either return false and fail to apply the patch (if
  // fuzzFactor is 0) or simply ignore the problem and do nothing (if fuzzFactor is >0).
  // If we do need to remove/add a newline at EOF, this will always be in the final hunk:
  var prevLine = '',
    removeEOFNL = false,
    addEOFNL = false;
  for (var i = 0; i < hunks[hunks.length - 1].lines.length; i++) {
    var line = hunks[hunks.length - 1].lines[i];
    if (line[0] == '\\') {
      if (prevLine[0] == '+') {
        removeEOFNL = true;
      } else if (prevLine[0] == '-') {
        addEOFNL = true;
      }
    }
    prevLine = line;
  }
  if (removeEOFNL) {
    if (addEOFNL) {
      // This means the final line gets changed but doesn't have a trailing newline in either the
      // original or patched version. In that case, we do nothing if fuzzFactor > 0, and if
      // fuzzFactor is 0, we simply validate that the source file has no trailing newline.
      if (!fuzzFactor && lines[lines.length - 1] == '') {
        return false;
      }
    } else if (lines[lines.length - 1] == '') {
      lines.pop();
    } else if (!fuzzFactor) {
      return false;
    }
  } else if (addEOFNL) {
    if (lines[lines.length - 1] != '') {
      lines.push('');
    } else if (!fuzzFactor) {
      return false;
    }
  }

  /**
   * Checks if the hunk can be made to fit at the provided location with at most `maxErrors`
   * insertions, substitutions, or deletions, while ensuring also that:
   * - lines deleted in the hunk match exactly, and
   * - wherever an insertion operation or block of insertion operations appears in the hunk, the
   *   immediately preceding and following lines of context match exactly
   *
   * `toPos` should be set such that lines[toPos] is meant to match hunkLines[0].
   *
   * If the hunk can be applied, returns an object with properties `oldLineLastI` and
   * `replacementLines`. Otherwise, returns null.
   */
  function applyHunk(hunkLines, toPos, maxErrors) {
    /*istanbul ignore start*/
    var
    /*istanbul ignore end*/
    hunkLinesI = arguments.length > 3 && arguments[3] !== undefined ? arguments[3] : 0;
    /*istanbul ignore start*/
    var
    /*istanbul ignore end*/
    lastContextLineMatched = arguments.length > 4 && arguments[4] !== undefined ? arguments[4] : true;
    /*istanbul ignore start*/
    var
    /*istanbul ignore end*/
    patchedLines = arguments.length > 5 && arguments[5] !== undefined ? arguments[5] : [];
    /*istanbul ignore start*/
    var
    /*istanbul ignore end*/
    patchedLinesLength = arguments.length > 6 && arguments[6] !== undefined ? arguments[6] : 0;
    var nConsecutiveOldContextLines = 0;
    var nextContextLineMustMatch = false;
    for (; hunkLinesI < hunkLines.length; hunkLinesI++) {
      var hunkLine = hunkLines[hunkLinesI],
        operation = hunkLine.length > 0 ? hunkLine[0] : ' ',
        content = hunkLine.length > 0 ? hunkLine.substr(1) : hunkLine;
      if (operation === '-') {
        if (compareLine(toPos + 1, lines[toPos], operation, content)) {
          toPos++;
          nConsecutiveOldContextLines = 0;
        } else {
          if (!maxErrors || lines[toPos] == null) {
            return null;
          }
          patchedLines[patchedLinesLength] = lines[toPos];
          return applyHunk(hunkLines, toPos + 1, maxErrors - 1, hunkLinesI, false, patchedLines, patchedLinesLength + 1);
        }
      }
      if (operation === '+') {
        if (!lastContextLineMatched) {
          return null;
        }
        patchedLines[patchedLinesLength] = content;
        patchedLinesLength++;
        nConsecutiveOldContextLines = 0;
        nextContextLineMustMatch = true;
      }
      if (operation === ' ') {
        nConsecutiveOldContextLines++;
        patchedLines[patchedLinesLength] = lines[toPos];
        if (compareLine(toPos + 1, lines[toPos], operation, content)) {
          patchedLinesLength++;
          lastContextLineMatched = true;
          nextContextLineMustMatch = false;
          toPos++;
        } else {
          if (nextContextLineMustMatch || !maxErrors) {
            return null;
          }

          // Consider 3 possibilities in sequence:
          // 1. lines contains a *substitution* not included in the patch context, or
          // 2. lines contains an *insertion* not included in the patch context, or
          // 3. lines contains a *deletion* not included in the patch context
          // The first two options are of course only possible if the line from lines is non-null -
          // i.e. only option 3 is possible if we've overrun the end of the old file.
          return lines[toPos] && (applyHunk(hunkLines, toPos + 1, maxErrors - 1, hunkLinesI + 1, false, patchedLines, patchedLinesLength + 1) || applyHunk(hunkLines, toPos + 1, maxErrors - 1, hunkLinesI, false, patchedLines, patchedLinesLength + 1)) || applyHunk(hunkLines, toPos, maxErrors - 1, hunkLinesI + 1, false, patchedLines, patchedLinesLength);
        }
      }
    }

    // Before returning, trim any unmodified context lines off the end of patchedLines and reduce
    // toPos (and thus oldLineLastI) accordingly. This allows later hunks to be applied to a region
    // that starts in this hunk's trailing context.
    patchedLinesLength -= nConsecutiveOldContextLines;
    toPos -= nConsecutiveOldContextLines;
    patchedLines.length = patchedLinesLength;
    return {
      patchedLines: patchedLines,
      oldLineLastI: toPos - 1
    };
  }
  var resultLines = [];

  // Search best fit offsets for each hunk based on the previous ones
  var prevHunkOffset = 0;
  for (var _i = 0; _i < hunks.length; _i++) {
    var hunk = hunks[_i];
    var hunkResult =
    /*istanbul ignore start*/
    void 0
    /*istanbul ignore end*/
    ;
    var maxLine = lines.length - hunk.oldLines + fuzzFactor;
    var toPos =
    /*istanbul ignore start*/
    void 0
    /*istanbul ignore end*/
    ;
    for (var maxErrors = 0; maxErrors <= fuzzFactor; maxErrors++) {
      toPos = hunk.oldStart + prevHunkOffset - 1;
      var iterator =
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _distanceIterator
      /*istanbul ignore end*/
      [
      /*istanbul ignore start*/
      "default"
      /*istanbul ignore end*/
      ])(toPos, minLine, maxLine);
      for (; toPos !== undefined; toPos = iterator()) {
        hunkResult = applyHunk(hunk.lines, toPos, maxErrors);
        if (hunkResult) {
          break;
        }
      }
      if (hunkResult) {
        break;
      }
    }
    if (!hunkResult) {
      return false;
    }

    // Copy everything from the end of where we applied the last hunk to the start of this hunk
    for (var _i2 = minLine; _i2 < toPos; _i2++) {
      resultLines.push(lines[_i2]);
    }

    // Add the lines produced by applying the hunk:
    for (var _i3 = 0; _i3 < hunkResult.patchedLines.length; _i3++) {
      var _line = hunkResult.patchedLines[_i3];
      resultLines.push(_line);
    }

    // Set lower text limit to end of the current hunk, so next ones don't try
    // to fit over already patched text
    minLine = hunkResult.oldLineLastI + 1;

    // Note the offset between where the patch said the hunk should've applied and where we
    // applied it, so we can adjust future hunks accordingly:
    prevHunkOffset = toPos + 1 - hunk.oldStart;
  }

  // Copy over the rest of the lines from the old text
  for (var _i4 = minLine; _i4 < lines.length; _i4++) {
    resultLines.push(lines[_i4]);
  }
  return resultLines.join('\n');
}

// Wrapper that supports multiple file patches via callbacks.
function applyPatches(uniDiff, options) {
  if (typeof uniDiff === 'string') {
    uniDiff =
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _parse
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    parsePatch)
    /*istanbul ignore end*/
    (uniDiff);
  }
  var currentIndex = 0;
  function processIndex() {
    var index = uniDiff[currentIndex++];
    if (!index) {
      return options.complete();
    }
    options.loadFile(index, function (err, data) {
      if (err) {
        return options.complete(err);
      }
      var updatedContent = applyPatch(data, index, options);
      options.patched(index, updatedContent, function (err) {
        if (err) {
          return options.complete(err);
        }
        processIndex();
      });
    });
  }
  processIndex();
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfc3RyaW5nIiwicmVxdWlyZSIsIl9saW5lRW5kaW5ncyIsIl9wYXJzZSIsIl9kaXN0YW5jZUl0ZXJhdG9yIiwiX2ludGVyb3BSZXF1aXJlRGVmYXVsdCIsIm9iaiIsIl9fZXNNb2R1bGUiLCJhcHBseVBhdGNoIiwic291cmNlIiwidW5pRGlmZiIsIm9wdGlvbnMiLCJhcmd1bWVudHMiLCJsZW5ndGgiLCJ1bmRlZmluZWQiLCJwYXJzZVBhdGNoIiwiQXJyYXkiLCJpc0FycmF5IiwiRXJyb3IiLCJhdXRvQ29udmVydExpbmVFbmRpbmdzIiwiaGFzT25seVdpbkxpbmVFbmRpbmdzIiwiaXNVbml4IiwidW5peFRvV2luIiwiaGFzT25seVVuaXhMaW5lRW5kaW5ncyIsImlzV2luIiwid2luVG9Vbml4IiwibGluZXMiLCJzcGxpdCIsImh1bmtzIiwiY29tcGFyZUxpbmUiLCJsaW5lTnVtYmVyIiwibGluZSIsIm9wZXJhdGlvbiIsInBhdGNoQ29udGVudCIsImZ1enpGYWN0b3IiLCJtaW5MaW5lIiwiTnVtYmVyIiwiaXNJbnRlZ2VyIiwicHJldkxpbmUiLCJyZW1vdmVFT0ZOTCIsImFkZEVPRk5MIiwiaSIsInBvcCIsInB1c2giLCJhcHBseUh1bmsiLCJodW5rTGluZXMiLCJ0b1BvcyIsIm1heEVycm9ycyIsImh1bmtMaW5lc0kiLCJsYXN0Q29udGV4dExpbmVNYXRjaGVkIiwicGF0Y2hlZExpbmVzIiwicGF0Y2hlZExpbmVzTGVuZ3RoIiwibkNvbnNlY3V0aXZlT2xkQ29udGV4dExpbmVzIiwibmV4dENvbnRleHRMaW5lTXVzdE1hdGNoIiwiaHVua0xpbmUiLCJjb250ZW50Iiwic3Vic3RyIiwib2xkTGluZUxhc3RJIiwicmVzdWx0TGluZXMiLCJwcmV2SHVua09mZnNldCIsImh1bmsiLCJodW5rUmVzdWx0IiwibWF4TGluZSIsIm9sZExpbmVzIiwib2xkU3RhcnQiLCJpdGVyYXRvciIsImRpc3RhbmNlSXRlcmF0b3IiLCJqb2luIiwiYXBwbHlQYXRjaGVzIiwiY3VycmVudEluZGV4IiwicHJvY2Vzc0luZGV4IiwiaW5kZXgiLCJjb21wbGV0ZSIsImxvYWRGaWxlIiwiZXJyIiwiZGF0YSIsInVwZGF0ZWRDb250ZW50IiwicGF0Y2hlZCJdLCJzb3VyY2VzIjpbIi4uLy4uL3NyYy9wYXRjaC9hcHBseS5qcyJdLCJzb3VyY2VzQ29udGVudCI6WyJpbXBvcnQge2hhc09ubHlXaW5MaW5lRW5kaW5ncywgaGFzT25seVVuaXhMaW5lRW5kaW5nc30gZnJvbSAnLi4vdXRpbC9zdHJpbmcnO1xuaW1wb3J0IHtpc1dpbiwgaXNVbml4LCB1bml4VG9XaW4sIHdpblRvVW5peH0gZnJvbSAnLi9saW5lLWVuZGluZ3MnO1xuaW1wb3J0IHtwYXJzZVBhdGNofSBmcm9tICcuL3BhcnNlJztcbmltcG9ydCBkaXN0YW5jZUl0ZXJhdG9yIGZyb20gJy4uL3V0aWwvZGlzdGFuY2UtaXRlcmF0b3InO1xuXG5leHBvcnQgZnVuY3Rpb24gYXBwbHlQYXRjaChzb3VyY2UsIHVuaURpZmYsIG9wdGlvbnMgPSB7fSkge1xuICBpZiAodHlwZW9mIHVuaURpZmYgPT09ICdzdHJpbmcnKSB7XG4gICAgdW5pRGlmZiA9IHBhcnNlUGF0Y2godW5pRGlmZik7XG4gIH1cblxuICBpZiAoQXJyYXkuaXNBcnJheSh1bmlEaWZmKSkge1xuICAgIGlmICh1bmlEaWZmLmxlbmd0aCA+IDEpIHtcbiAgICAgIHRocm93IG5ldyBFcnJvcignYXBwbHlQYXRjaCBvbmx5IHdvcmtzIHdpdGggYSBzaW5nbGUgaW5wdXQuJyk7XG4gICAgfVxuXG4gICAgdW5pRGlmZiA9IHVuaURpZmZbMF07XG4gIH1cblxuICBpZiAob3B0aW9ucy5hdXRvQ29udmVydExpbmVFbmRpbmdzIHx8IG9wdGlvbnMuYXV0b0NvbnZlcnRMaW5lRW5kaW5ncyA9PSBudWxsKSB7XG4gICAgaWYgKGhhc09ubHlXaW5MaW5lRW5kaW5ncyhzb3VyY2UpICYmIGlzVW5peCh1bmlEaWZmKSkge1xuICAgICAgdW5pRGlmZiA9IHVuaXhUb1dpbih1bmlEaWZmKTtcbiAgICB9IGVsc2UgaWYgKGhhc09ubHlVbml4TGluZUVuZGluZ3Moc291cmNlKSAmJiBpc1dpbih1bmlEaWZmKSkge1xuICAgICAgdW5pRGlmZiA9IHdpblRvVW5peCh1bmlEaWZmKTtcbiAgICB9XG4gIH1cblxuICAvLyBBcHBseSB0aGUgZGlmZiB0byB0aGUgaW5wdXRcbiAgbGV0IGxpbmVzID0gc291cmNlLnNwbGl0KCdcXG4nKSxcbiAgICAgIGh1bmtzID0gdW5pRGlmZi5odW5rcyxcblxuICAgICAgY29tcGFyZUxpbmUgPSBvcHRpb25zLmNvbXBhcmVMaW5lIHx8ICgobGluZU51bWJlciwgbGluZSwgb3BlcmF0aW9uLCBwYXRjaENvbnRlbnQpID0+IGxpbmUgPT09IHBhdGNoQ29udGVudCksXG4gICAgICBmdXp6RmFjdG9yID0gb3B0aW9ucy5mdXp6RmFjdG9yIHx8IDAsXG4gICAgICBtaW5MaW5lID0gMDtcblxuICBpZiAoZnV6ekZhY3RvciA8IDAgfHwgIU51bWJlci5pc0ludGVnZXIoZnV6ekZhY3RvcikpIHtcbiAgICB0aHJvdyBuZXcgRXJyb3IoJ2Z1enpGYWN0b3IgbXVzdCBiZSBhIG5vbi1uZWdhdGl2ZSBpbnRlZ2VyJyk7XG4gIH1cblxuICAvLyBTcGVjaWFsIGNhc2UgZm9yIGVtcHR5IHBhdGNoLlxuICBpZiAoIWh1bmtzLmxlbmd0aCkge1xuICAgIHJldHVybiBzb3VyY2U7XG4gIH1cblxuICAvLyBCZWZvcmUgYW55dGhpbmcgZWxzZSwgaGFuZGxlIEVPRk5MIGluc2VydGlvbi9yZW1vdmFsLiBJZiB0aGUgcGF0Y2ggdGVsbHMgdXMgdG8gbWFrZSBhIGNoYW5nZVxuICAvLyB0byB0aGUgRU9GTkwgdGhhdCBpcyByZWR1bmRhbnQvaW1wb3NzaWJsZSAtIGkuZS4gdG8gcmVtb3ZlIGEgbmV3bGluZSB0aGF0J3Mgbm90IHRoZXJlLCBvciBhZGQgYVxuICAvLyBuZXdsaW5lIHRoYXQgYWxyZWFkeSBleGlzdHMgLSB0aGVuIHdlIGVpdGhlciByZXR1cm4gZmFsc2UgYW5kIGZhaWwgdG8gYXBwbHkgdGhlIHBhdGNoIChpZlxuICAvLyBmdXp6RmFjdG9yIGlzIDApIG9yIHNpbXBseSBpZ25vcmUgdGhlIHByb2JsZW0gYW5kIGRvIG5vdGhpbmcgKGlmIGZ1enpGYWN0b3IgaXMgPjApLlxuICAvLyBJZiB3ZSBkbyBuZWVkIHRvIHJlbW92ZS9hZGQgYSBuZXdsaW5lIGF0IEVPRiwgdGhpcyB3aWxsIGFsd2F5cyBiZSBpbiB0aGUgZmluYWwgaHVuazpcbiAgbGV0IHByZXZMaW5lID0gJycsXG4gICAgICByZW1vdmVFT0ZOTCA9IGZhbHNlLFxuICAgICAgYWRkRU9GTkwgPSBmYWxzZTtcbiAgZm9yIChsZXQgaSA9IDA7IGkgPCBodW5rc1todW5rcy5sZW5ndGggLSAxXS5saW5lcy5sZW5ndGg7IGkrKykge1xuICAgIGNvbnN0IGxpbmUgPSBodW5rc1todW5rcy5sZW5ndGggLSAxXS5saW5lc1tpXTtcbiAgICBpZiAobGluZVswXSA9PSAnXFxcXCcpIHtcbiAgICAgIGlmIChwcmV2TGluZVswXSA9PSAnKycpIHtcbiAgICAgICAgcmVtb3ZlRU9GTkwgPSB0cnVlO1xuICAgICAgfSBlbHNlIGlmIChwcmV2TGluZVswXSA9PSAnLScpIHtcbiAgICAgICAgYWRkRU9GTkwgPSB0cnVlO1xuICAgICAgfVxuICAgIH1cbiAgICBwcmV2TGluZSA9IGxpbmU7XG4gIH1cbiAgaWYgKHJlbW92ZUVPRk5MKSB7XG4gICAgaWYgKGFkZEVPRk5MKSB7XG4gICAgICAvLyBUaGlzIG1lYW5zIHRoZSBmaW5hbCBsaW5lIGdldHMgY2hhbmdlZCBidXQgZG9lc24ndCBoYXZlIGEgdHJhaWxpbmcgbmV3bGluZSBpbiBlaXRoZXIgdGhlXG4gICAgICAvLyBvcmlnaW5hbCBvciBwYXRjaGVkIHZlcnNpb24uIEluIHRoYXQgY2FzZSwgd2UgZG8gbm90aGluZyBpZiBmdXp6RmFjdG9yID4gMCwgYW5kIGlmXG4gICAgICAvLyBmdXp6RmFjdG9yIGlzIDAsIHdlIHNpbXBseSB2YWxpZGF0ZSB0aGF0IHRoZSBzb3VyY2UgZmlsZSBoYXMgbm8gdHJhaWxpbmcgbmV3bGluZS5cbiAgICAgIGlmICghZnV6ekZhY3RvciAmJiBsaW5lc1tsaW5lcy5sZW5ndGggLSAxXSA9PSAnJykge1xuICAgICAgICByZXR1cm4gZmFsc2U7XG4gICAgICB9XG4gICAgfSBlbHNlIGlmIChsaW5lc1tsaW5lcy5sZW5ndGggLSAxXSA9PSAnJykge1xuICAgICAgbGluZXMucG9wKCk7XG4gICAgfSBlbHNlIGlmICghZnV6ekZhY3Rvcikge1xuICAgICAgcmV0dXJuIGZhbHNlO1xuICAgIH1cbiAgfSBlbHNlIGlmIChhZGRFT0ZOTCkge1xuICAgIGlmIChsaW5lc1tsaW5lcy5sZW5ndGggLSAxXSAhPSAnJykge1xuICAgICAgbGluZXMucHVzaCgnJyk7XG4gICAgfSBlbHNlIGlmICghZnV6ekZhY3Rvcikge1xuICAgICAgcmV0dXJuIGZhbHNlO1xuICAgIH1cbiAgfVxuXG4gIC8qKlxuICAgKiBDaGVja3MgaWYgdGhlIGh1bmsgY2FuIGJlIG1hZGUgdG8gZml0IGF0IHRoZSBwcm92aWRlZCBsb2NhdGlvbiB3aXRoIGF0IG1vc3QgYG1heEVycm9yc2BcbiAgICogaW5zZXJ0aW9ucywgc3Vic3RpdHV0aW9ucywgb3IgZGVsZXRpb25zLCB3aGlsZSBlbnN1cmluZyBhbHNvIHRoYXQ6XG4gICAqIC0gbGluZXMgZGVsZXRlZCBpbiB0aGUgaHVuayBtYXRjaCBleGFjdGx5LCBhbmRcbiAgICogLSB3aGVyZXZlciBhbiBpbnNlcnRpb24gb3BlcmF0aW9uIG9yIGJsb2NrIG9mIGluc2VydGlvbiBvcGVyYXRpb25zIGFwcGVhcnMgaW4gdGhlIGh1bmssIHRoZVxuICAgKiAgIGltbWVkaWF0ZWx5IHByZWNlZGluZyBhbmQgZm9sbG93aW5nIGxpbmVzIG9mIGNvbnRleHQgbWF0Y2ggZXhhY3RseVxuICAgKlxuICAgKiBgdG9Qb3NgIHNob3VsZCBiZSBzZXQgc3VjaCB0aGF0IGxpbmVzW3RvUG9zXSBpcyBtZWFudCB0byBtYXRjaCBodW5rTGluZXNbMF0uXG4gICAqXG4gICAqIElmIHRoZSBodW5rIGNhbiBiZSBhcHBsaWVkLCByZXR1cm5zIGFuIG9iamVjdCB3aXRoIHByb3BlcnRpZXMgYG9sZExpbmVMYXN0SWAgYW5kXG4gICAqIGByZXBsYWNlbWVudExpbmVzYC4gT3RoZXJ3aXNlLCByZXR1cm5zIG51bGwuXG4gICAqL1xuICBmdW5jdGlvbiBhcHBseUh1bmsoXG4gICAgaHVua0xpbmVzLFxuICAgIHRvUG9zLFxuICAgIG1heEVycm9ycyxcbiAgICBodW5rTGluZXNJID0gMCxcbiAgICBsYXN0Q29udGV4dExpbmVNYXRjaGVkID0gdHJ1ZSxcbiAgICBwYXRjaGVkTGluZXMgPSBbXSxcbiAgICBwYXRjaGVkTGluZXNMZW5ndGggPSAwLFxuICApIHtcbiAgICBsZXQgbkNvbnNlY3V0aXZlT2xkQ29udGV4dExpbmVzID0gMDtcbiAgICBsZXQgbmV4dENvbnRleHRMaW5lTXVzdE1hdGNoID0gZmFsc2U7XG4gICAgZm9yICg7IGh1bmtMaW5lc0kgPCBodW5rTGluZXMubGVuZ3RoOyBodW5rTGluZXNJKyspIHtcbiAgICAgIGxldCBodW5rTGluZSA9IGh1bmtMaW5lc1todW5rTGluZXNJXSxcbiAgICAgICAgICBvcGVyYXRpb24gPSAoaHVua0xpbmUubGVuZ3RoID4gMCA/IGh1bmtMaW5lWzBdIDogJyAnKSxcbiAgICAgICAgICBjb250ZW50ID0gKGh1bmtMaW5lLmxlbmd0aCA+IDAgPyBodW5rTGluZS5zdWJzdHIoMSkgOiBodW5rTGluZSk7XG5cbiAgICAgIGlmIChvcGVyYXRpb24gPT09ICctJykge1xuICAgICAgICBpZiAoY29tcGFyZUxpbmUodG9Qb3MgKyAxLCBsaW5lc1t0b1Bvc10sIG9wZXJhdGlvbiwgY29udGVudCkpIHtcbiAgICAgICAgICB0b1BvcysrO1xuICAgICAgICAgIG5Db25zZWN1dGl2ZU9sZENvbnRleHRMaW5lcyA9IDA7XG4gICAgICAgIH0gZWxzZSB7XG4gICAgICAgICAgaWYgKCFtYXhFcnJvcnMgfHwgbGluZXNbdG9Qb3NdID09IG51bGwpIHtcbiAgICAgICAgICAgIHJldHVybiBudWxsO1xuICAgICAgICAgIH1cbiAgICAgICAgICBwYXRjaGVkTGluZXNbcGF0Y2hlZExpbmVzTGVuZ3RoXSA9IGxpbmVzW3RvUG9zXTtcbiAgICAgICAgICByZXR1cm4gYXBwbHlIdW5rKFxuICAgICAgICAgICAgaHVua0xpbmVzLFxuICAgICAgICAgICAgdG9Qb3MgKyAxLFxuICAgICAgICAgICAgbWF4RXJyb3JzIC0gMSxcbiAgICAgICAgICAgIGh1bmtMaW5lc0ksXG4gICAgICAgICAgICBmYWxzZSxcbiAgICAgICAgICAgIHBhdGNoZWRMaW5lcyxcbiAgICAgICAgICAgIHBhdGNoZWRMaW5lc0xlbmd0aCArIDEsXG4gICAgICAgICAgKTtcbiAgICAgICAgfVxuICAgICAgfVxuXG4gICAgICBpZiAob3BlcmF0aW9uID09PSAnKycpIHtcbiAgICAgICAgaWYgKCFsYXN0Q29udGV4dExpbmVNYXRjaGVkKSB7XG4gICAgICAgICAgcmV0dXJuIG51bGw7XG4gICAgICAgIH1cbiAgICAgICAgcGF0Y2hlZExpbmVzW3BhdGNoZWRMaW5lc0xlbmd0aF0gPSBjb250ZW50O1xuICAgICAgICBwYXRjaGVkTGluZXNMZW5ndGgrKztcbiAgICAgICAgbkNvbnNlY3V0aXZlT2xkQ29udGV4dExpbmVzID0gMDtcbiAgICAgICAgbmV4dENvbnRleHRMaW5lTXVzdE1hdGNoID0gdHJ1ZTtcbiAgICAgIH1cblxuICAgICAgaWYgKG9wZXJhdGlvbiA9PT0gJyAnKSB7XG4gICAgICAgIG5Db25zZWN1dGl2ZU9sZENvbnRleHRMaW5lcysrO1xuICAgICAgICBwYXRjaGVkTGluZXNbcGF0Y2hlZExpbmVzTGVuZ3RoXSA9IGxpbmVzW3RvUG9zXTtcbiAgICAgICAgaWYgKGNvbXBhcmVMaW5lKHRvUG9zICsgMSwgbGluZXNbdG9Qb3NdLCBvcGVyYXRpb24sIGNvbnRlbnQpKSB7XG4gICAgICAgICAgcGF0Y2hlZExpbmVzTGVuZ3RoKys7XG4gICAgICAgICAgbGFzdENvbnRleHRMaW5lTWF0Y2hlZCA9IHRydWU7XG4gICAgICAgICAgbmV4dENvbnRleHRMaW5lTXVzdE1hdGNoID0gZmFsc2U7XG4gICAgICAgICAgdG9Qb3MrKztcbiAgICAgICAgfSBlbHNlIHtcbiAgICAgICAgICBpZiAobmV4dENvbnRleHRMaW5lTXVzdE1hdGNoIHx8ICFtYXhFcnJvcnMpIHtcbiAgICAgICAgICAgIHJldHVybiBudWxsO1xuICAgICAgICAgIH1cblxuICAgICAgICAgIC8vIENvbnNpZGVyIDMgcG9zc2liaWxpdGllcyBpbiBzZXF1ZW5jZTpcbiAgICAgICAgICAvLyAxLiBsaW5lcyBjb250YWlucyBhICpzdWJzdGl0dXRpb24qIG5vdCBpbmNsdWRlZCBpbiB0aGUgcGF0Y2ggY29udGV4dCwgb3JcbiAgICAgICAgICAvLyAyLiBsaW5lcyBjb250YWlucyBhbiAqaW5zZXJ0aW9uKiBub3QgaW5jbHVkZWQgaW4gdGhlIHBhdGNoIGNvbnRleHQsIG9yXG4gICAgICAgICAgLy8gMy4gbGluZXMgY29udGFpbnMgYSAqZGVsZXRpb24qIG5vdCBpbmNsdWRlZCBpbiB0aGUgcGF0Y2ggY29udGV4dFxuICAgICAgICAgIC8vIFRoZSBmaXJzdCB0d28gb3B0aW9ucyBhcmUgb2YgY291cnNlIG9ubHkgcG9zc2libGUgaWYgdGhlIGxpbmUgZnJvbSBsaW5lcyBpcyBub24tbnVsbCAtXG4gICAgICAgICAgLy8gaS5lLiBvbmx5IG9wdGlvbiAzIGlzIHBvc3NpYmxlIGlmIHdlJ3ZlIG92ZXJydW4gdGhlIGVuZCBvZiB0aGUgb2xkIGZpbGUuXG4gICAgICAgICAgcmV0dXJuIChcbiAgICAgICAgICAgIGxpbmVzW3RvUG9zXSAmJiAoXG4gICAgICAgICAgICAgIGFwcGx5SHVuayhcbiAgICAgICAgICAgICAgICBodW5rTGluZXMsXG4gICAgICAgICAgICAgICAgdG9Qb3MgKyAxLFxuICAgICAgICAgICAgICAgIG1heEVycm9ycyAtIDEsXG4gICAgICAgICAgICAgICAgaHVua0xpbmVzSSArIDEsXG4gICAgICAgICAgICAgICAgZmFsc2UsXG4gICAgICAgICAgICAgICAgcGF0Y2hlZExpbmVzLFxuICAgICAgICAgICAgICAgIHBhdGNoZWRMaW5lc0xlbmd0aCArIDFcbiAgICAgICAgICAgICAgKSB8fCBhcHBseUh1bmsoXG4gICAgICAgICAgICAgICAgaHVua0xpbmVzLFxuICAgICAgICAgICAgICAgIHRvUG9zICsgMSxcbiAgICAgICAgICAgICAgICBtYXhFcnJvcnMgLSAxLFxuICAgICAgICAgICAgICAgIGh1bmtMaW5lc0ksXG4gICAgICAgICAgICAgICAgZmFsc2UsXG4gICAgICAgICAgICAgICAgcGF0Y2hlZExpbmVzLFxuICAgICAgICAgICAgICAgIHBhdGNoZWRMaW5lc0xlbmd0aCArIDFcbiAgICAgICAgICAgICAgKVxuICAgICAgICAgICAgKSB8fCBhcHBseUh1bmsoXG4gICAgICAgICAgICAgIGh1bmtMaW5lcyxcbiAgICAgICAgICAgICAgdG9Qb3MsXG4gICAgICAgICAgICAgIG1heEVycm9ycyAtIDEsXG4gICAgICAgICAgICAgIGh1bmtMaW5lc0kgKyAxLFxuICAgICAgICAgICAgICBmYWxzZSxcbiAgICAgICAgICAgICAgcGF0Y2hlZExpbmVzLFxuICAgICAgICAgICAgICBwYXRjaGVkTGluZXNMZW5ndGhcbiAgICAgICAgICAgIClcbiAgICAgICAgICApO1xuICAgICAgICB9XG4gICAgICB9XG4gICAgfVxuXG4gICAgLy8gQmVmb3JlIHJldHVybmluZywgdHJpbSBhbnkgdW5tb2RpZmllZCBjb250ZXh0IGxpbmVzIG9mZiB0aGUgZW5kIG9mIHBhdGNoZWRMaW5lcyBhbmQgcmVkdWNlXG4gICAgLy8gdG9Qb3MgKGFuZCB0aHVzIG9sZExpbmVMYXN0SSkgYWNjb3JkaW5nbHkuIFRoaXMgYWxsb3dzIGxhdGVyIGh1bmtzIHRvIGJlIGFwcGxpZWQgdG8gYSByZWdpb25cbiAgICAvLyB0aGF0IHN0YXJ0cyBpbiB0aGlzIGh1bmsncyB0cmFpbGluZyBjb250ZXh0LlxuICAgIHBhdGNoZWRMaW5lc0xlbmd0aCAtPSBuQ29uc2VjdXRpdmVPbGRDb250ZXh0TGluZXM7XG4gICAgdG9Qb3MgLT0gbkNvbnNlY3V0aXZlT2xkQ29udGV4dExpbmVzO1xuICAgIHBhdGNoZWRMaW5lcy5sZW5ndGggPSBwYXRjaGVkTGluZXNMZW5ndGg7XG4gICAgcmV0dXJuIHtcbiAgICAgIHBhdGNoZWRMaW5lcyxcbiAgICAgIG9sZExpbmVMYXN0STogdG9Qb3MgLSAxXG4gICAgfTtcbiAgfVxuXG4gIGNvbnN0IHJlc3VsdExpbmVzID0gW107XG5cbiAgLy8gU2VhcmNoIGJlc3QgZml0IG9mZnNldHMgZm9yIGVhY2ggaHVuayBiYXNlZCBvbiB0aGUgcHJldmlvdXMgb25lc1xuICBsZXQgcHJldkh1bmtPZmZzZXQgPSAwO1xuICBmb3IgKGxldCBpID0gMDsgaSA8IGh1bmtzLmxlbmd0aDsgaSsrKSB7XG4gICAgY29uc3QgaHVuayA9IGh1bmtzW2ldO1xuICAgIGxldCBodW5rUmVzdWx0O1xuICAgIGxldCBtYXhMaW5lID0gbGluZXMubGVuZ3RoIC0gaHVuay5vbGRMaW5lcyArIGZ1enpGYWN0b3I7XG4gICAgbGV0IHRvUG9zO1xuICAgIGZvciAobGV0IG1heEVycm9ycyA9IDA7IG1heEVycm9ycyA8PSBmdXp6RmFjdG9yOyBtYXhFcnJvcnMrKykge1xuICAgICAgdG9Qb3MgPSBodW5rLm9sZFN0YXJ0ICsgcHJldkh1bmtPZmZzZXQgLSAxO1xuICAgICAgbGV0IGl0ZXJhdG9yID0gZGlzdGFuY2VJdGVyYXRvcih0b1BvcywgbWluTGluZSwgbWF4TGluZSk7XG4gICAgICBmb3IgKDsgdG9Qb3MgIT09IHVuZGVmaW5lZDsgdG9Qb3MgPSBpdGVyYXRvcigpKSB7XG4gICAgICAgIGh1bmtSZXN1bHQgPSBhcHBseUh1bmsoaHVuay5saW5lcywgdG9Qb3MsIG1heEVycm9ycyk7XG4gICAgICAgIGlmIChodW5rUmVzdWx0KSB7XG4gICAgICAgICAgYnJlYWs7XG4gICAgICAgIH1cbiAgICAgIH1cbiAgICAgIGlmIChodW5rUmVzdWx0KSB7XG4gICAgICAgIGJyZWFrO1xuICAgICAgfVxuICAgIH1cblxuICAgIGlmICghaHVua1Jlc3VsdCkge1xuICAgICAgcmV0dXJuIGZhbHNlO1xuICAgIH1cblxuICAgIC8vIENvcHkgZXZlcnl0aGluZyBmcm9tIHRoZSBlbmQgb2Ygd2hlcmUgd2UgYXBwbGllZCB0aGUgbGFzdCBodW5rIHRvIHRoZSBzdGFydCBvZiB0aGlzIGh1bmtcbiAgICBmb3IgKGxldCBpID0gbWluTGluZTsgaSA8IHRvUG9zOyBpKyspIHtcbiAgICAgIHJlc3VsdExpbmVzLnB1c2gobGluZXNbaV0pO1xuICAgIH1cblxuICAgIC8vIEFkZCB0aGUgbGluZXMgcHJvZHVjZWQgYnkgYXBwbHlpbmcgdGhlIGh1bms6XG4gICAgZm9yIChsZXQgaSA9IDA7IGkgPCBodW5rUmVzdWx0LnBhdGNoZWRMaW5lcy5sZW5ndGg7IGkrKykge1xuICAgICAgY29uc3QgbGluZSA9IGh1bmtSZXN1bHQucGF0Y2hlZExpbmVzW2ldO1xuICAgICAgcmVzdWx0TGluZXMucHVzaChsaW5lKTtcbiAgICB9XG5cbiAgICAvLyBTZXQgbG93ZXIgdGV4dCBsaW1pdCB0byBlbmQgb2YgdGhlIGN1cnJlbnQgaHVuaywgc28gbmV4dCBvbmVzIGRvbid0IHRyeVxuICAgIC8vIHRvIGZpdCBvdmVyIGFscmVhZHkgcGF0Y2hlZCB0ZXh0XG4gICAgbWluTGluZSA9IGh1bmtSZXN1bHQub2xkTGluZUxhc3RJICsgMTtcblxuICAgIC8vIE5vdGUgdGhlIG9mZnNldCBiZXR3ZWVuIHdoZXJlIHRoZSBwYXRjaCBzYWlkIHRoZSBodW5rIHNob3VsZCd2ZSBhcHBsaWVkIGFuZCB3aGVyZSB3ZVxuICAgIC8vIGFwcGxpZWQgaXQsIHNvIHdlIGNhbiBhZGp1c3QgZnV0dXJlIGh1bmtzIGFjY29yZGluZ2x5OlxuICAgIHByZXZIdW5rT2Zmc2V0ID0gdG9Qb3MgKyAxIC0gaHVuay5vbGRTdGFydDtcbiAgfVxuXG4gIC8vIENvcHkgb3ZlciB0aGUgcmVzdCBvZiB0aGUgbGluZXMgZnJvbSB0aGUgb2xkIHRleHRcbiAgZm9yIChsZXQgaSA9IG1pbkxpbmU7IGkgPCBsaW5lcy5sZW5ndGg7IGkrKykge1xuICAgIHJlc3VsdExpbmVzLnB1c2gobGluZXNbaV0pO1xuICB9XG5cbiAgcmV0dXJuIHJlc3VsdExpbmVzLmpvaW4oJ1xcbicpO1xufVxuXG4vLyBXcmFwcGVyIHRoYXQgc3VwcG9ydHMgbXVsdGlwbGUgZmlsZSBwYXRjaGVzIHZpYSBjYWxsYmFja3MuXG5leHBvcnQgZnVuY3Rpb24gYXBwbHlQYXRjaGVzKHVuaURpZmYsIG9wdGlvbnMpIHtcbiAgaWYgKHR5cGVvZiB1bmlEaWZmID09PSAnc3RyaW5nJykge1xuICAgIHVuaURpZmYgPSBwYXJzZVBhdGNoKHVuaURpZmYpO1xuICB9XG5cbiAgbGV0IGN1cnJlbnRJbmRleCA9IDA7XG4gIGZ1bmN0aW9uIHByb2Nlc3NJbmRleCgpIHtcbiAgICBsZXQgaW5kZXggPSB1bmlEaWZmW2N1cnJlbnRJbmRleCsrXTtcbiAgICBpZiAoIWluZGV4KSB7XG4gICAgICByZXR1cm4gb3B0aW9ucy5jb21wbGV0ZSgpO1xuICAgIH1cblxuICAgIG9wdGlvbnMubG9hZEZpbGUoaW5kZXgsIGZ1bmN0aW9uKGVyciwgZGF0YSkge1xuICAgICAgaWYgKGVycikge1xuICAgICAgICByZXR1cm4gb3B0aW9ucy5jb21wbGV0ZShlcnIpO1xuICAgICAgfVxuXG4gICAgICBsZXQgdXBkYXRlZENvbnRlbnQgPSBhcHBseVBhdGNoKGRhdGEsIGluZGV4LCBvcHRpb25zKTtcbiAgICAgIG9wdGlvbnMucGF0Y2hlZChpbmRleCwgdXBkYXRlZENvbnRlbnQsIGZ1bmN0aW9uKGVycikge1xuICAgICAgICBpZiAoZXJyKSB7XG4gICAgICAgICAgcmV0dXJuIG9wdGlvbnMuY29tcGxldGUoZXJyKTtcbiAgICAgICAgfVxuXG4gICAgICAgIHByb2Nlc3NJbmRleCgpO1xuICAgICAgfSk7XG4gICAgfSk7XG4gIH1cbiAgcHJvY2Vzc0luZGV4KCk7XG59XG4iXSwibWFwcGluZ3MiOiI7Ozs7Ozs7OztB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
