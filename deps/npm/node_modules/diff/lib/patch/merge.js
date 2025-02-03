/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.calcLineCount = calcLineCount;
exports.merge = merge;
/*istanbul ignore end*/
var
/*istanbul ignore start*/
_create = require("./create")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_parse = require("./parse")
/*istanbul ignore end*/
;
var
/*istanbul ignore start*/
_array = require("../util/array")
/*istanbul ignore end*/
;
/*istanbul ignore start*/ function _toConsumableArray(arr) { return _arrayWithoutHoles(arr) || _iterableToArray(arr) || _unsupportedIterableToArray(arr) || _nonIterableSpread(); }
function _nonIterableSpread() { throw new TypeError("Invalid attempt to spread non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method."); }
function _unsupportedIterableToArray(o, minLen) { if (!o) return; if (typeof o === "string") return _arrayLikeToArray(o, minLen); var n = Object.prototype.toString.call(o).slice(8, -1); if (n === "Object" && o.constructor) n = o.constructor.name; if (n === "Map" || n === "Set") return Array.from(o); if (n === "Arguments" || /^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(n)) return _arrayLikeToArray(o, minLen); }
function _iterableToArray(iter) { if (typeof Symbol !== "undefined" && iter[Symbol.iterator] != null || iter["@@iterator"] != null) return Array.from(iter); }
function _arrayWithoutHoles(arr) { if (Array.isArray(arr)) return _arrayLikeToArray(arr); }
function _arrayLikeToArray(arr, len) { if (len == null || len > arr.length) len = arr.length; for (var i = 0, arr2 = new Array(len); i < len; i++) arr2[i] = arr[i]; return arr2; }
/*istanbul ignore end*/
function calcLineCount(hunk) {
  var
    /*istanbul ignore start*/
    _calcOldNewLineCount =
    /*istanbul ignore end*/
    calcOldNewLineCount(hunk.lines),
    /*istanbul ignore start*/
    /*istanbul ignore end*/
    oldLines = _calcOldNewLineCount.oldLines,
    /*istanbul ignore start*/
    /*istanbul ignore end*/
    newLines = _calcOldNewLineCount.newLines;
  if (oldLines !== undefined) {
    hunk.oldLines = oldLines;
  } else {
    delete hunk.oldLines;
  }
  if (newLines !== undefined) {
    hunk.newLines = newLines;
  } else {
    delete hunk.newLines;
  }
}
function merge(mine, theirs, base) {
  mine = loadPatch(mine, base);
  theirs = loadPatch(theirs, base);
  var ret = {};

  // For index we just let it pass through as it doesn't have any necessary meaning.
  // Leaving sanity checks on this to the API consumer that may know more about the
  // meaning in their own context.
  if (mine.index || theirs.index) {
    ret.index = mine.index || theirs.index;
  }
  if (mine.newFileName || theirs.newFileName) {
    if (!fileNameChanged(mine)) {
      // No header or no change in ours, use theirs (and ours if theirs does not exist)
      ret.oldFileName = theirs.oldFileName || mine.oldFileName;
      ret.newFileName = theirs.newFileName || mine.newFileName;
      ret.oldHeader = theirs.oldHeader || mine.oldHeader;
      ret.newHeader = theirs.newHeader || mine.newHeader;
    } else if (!fileNameChanged(theirs)) {
      // No header or no change in theirs, use ours
      ret.oldFileName = mine.oldFileName;
      ret.newFileName = mine.newFileName;
      ret.oldHeader = mine.oldHeader;
      ret.newHeader = mine.newHeader;
    } else {
      // Both changed... figure it out
      ret.oldFileName = selectField(ret, mine.oldFileName, theirs.oldFileName);
      ret.newFileName = selectField(ret, mine.newFileName, theirs.newFileName);
      ret.oldHeader = selectField(ret, mine.oldHeader, theirs.oldHeader);
      ret.newHeader = selectField(ret, mine.newHeader, theirs.newHeader);
    }
  }
  ret.hunks = [];
  var mineIndex = 0,
    theirsIndex = 0,
    mineOffset = 0,
    theirsOffset = 0;
  while (mineIndex < mine.hunks.length || theirsIndex < theirs.hunks.length) {
    var mineCurrent = mine.hunks[mineIndex] || {
        oldStart: Infinity
      },
      theirsCurrent = theirs.hunks[theirsIndex] || {
        oldStart: Infinity
      };
    if (hunkBefore(mineCurrent, theirsCurrent)) {
      // This patch does not overlap with any of the others, yay.
      ret.hunks.push(cloneHunk(mineCurrent, mineOffset));
      mineIndex++;
      theirsOffset += mineCurrent.newLines - mineCurrent.oldLines;
    } else if (hunkBefore(theirsCurrent, mineCurrent)) {
      // This patch does not overlap with any of the others, yay.
      ret.hunks.push(cloneHunk(theirsCurrent, theirsOffset));
      theirsIndex++;
      mineOffset += theirsCurrent.newLines - theirsCurrent.oldLines;
    } else {
      // Overlap, merge as best we can
      var mergedHunk = {
        oldStart: Math.min(mineCurrent.oldStart, theirsCurrent.oldStart),
        oldLines: 0,
        newStart: Math.min(mineCurrent.newStart + mineOffset, theirsCurrent.oldStart + theirsOffset),
        newLines: 0,
        lines: []
      };
      mergeLines(mergedHunk, mineCurrent.oldStart, mineCurrent.lines, theirsCurrent.oldStart, theirsCurrent.lines);
      theirsIndex++;
      mineIndex++;
      ret.hunks.push(mergedHunk);
    }
  }
  return ret;
}
function loadPatch(param, base) {
  if (typeof param === 'string') {
    if (/^@@/m.test(param) || /^Index:/m.test(param)) {
      return (
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
        (param)[0]
      );
    }
    if (!base) {
      throw new Error('Must provide a base reference or pass in a patch');
    }
    return (
      /*istanbul ignore start*/
      (0,
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      _create
      /*istanbul ignore end*/
      .
      /*istanbul ignore start*/
      structuredPatch)
      /*istanbul ignore end*/
      (undefined, undefined, base, param)
    );
  }
  return param;
}
function fileNameChanged(patch) {
  return patch.newFileName && patch.newFileName !== patch.oldFileName;
}
function selectField(index, mine, theirs) {
  if (mine === theirs) {
    return mine;
  } else {
    index.conflict = true;
    return {
      mine: mine,
      theirs: theirs
    };
  }
}
function hunkBefore(test, check) {
  return test.oldStart < check.oldStart && test.oldStart + test.oldLines < check.oldStart;
}
function cloneHunk(hunk, offset) {
  return {
    oldStart: hunk.oldStart,
    oldLines: hunk.oldLines,
    newStart: hunk.newStart + offset,
    newLines: hunk.newLines,
    lines: hunk.lines
  };
}
function mergeLines(hunk, mineOffset, mineLines, theirOffset, theirLines) {
  // This will generally result in a conflicted hunk, but there are cases where the context
  // is the only overlap where we can successfully merge the content here.
  var mine = {
      offset: mineOffset,
      lines: mineLines,
      index: 0
    },
    their = {
      offset: theirOffset,
      lines: theirLines,
      index: 0
    };

  // Handle any leading content
  insertLeading(hunk, mine, their);
  insertLeading(hunk, their, mine);

  // Now in the overlap content. Scan through and select the best changes from each.
  while (mine.index < mine.lines.length && their.index < their.lines.length) {
    var mineCurrent = mine.lines[mine.index],
      theirCurrent = their.lines[their.index];
    if ((mineCurrent[0] === '-' || mineCurrent[0] === '+') && (theirCurrent[0] === '-' || theirCurrent[0] === '+')) {
      // Both modified ...
      mutualChange(hunk, mine, their);
    } else if (mineCurrent[0] === '+' && theirCurrent[0] === ' ') {
      /*istanbul ignore start*/
      var _hunk$lines;
      /*istanbul ignore end*/
      // Mine inserted
      /*istanbul ignore start*/
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      (_hunk$lines =
      /*istanbul ignore end*/
      hunk.lines).push.apply(
      /*istanbul ignore start*/
      _hunk$lines
      /*istanbul ignore end*/
      ,
      /*istanbul ignore start*/
      _toConsumableArray(
      /*istanbul ignore end*/
      collectChange(mine)));
    } else if (theirCurrent[0] === '+' && mineCurrent[0] === ' ') {
      /*istanbul ignore start*/
      var _hunk$lines2;
      /*istanbul ignore end*/
      // Theirs inserted
      /*istanbul ignore start*/
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      (_hunk$lines2 =
      /*istanbul ignore end*/
      hunk.lines).push.apply(
      /*istanbul ignore start*/
      _hunk$lines2
      /*istanbul ignore end*/
      ,
      /*istanbul ignore start*/
      _toConsumableArray(
      /*istanbul ignore end*/
      collectChange(their)));
    } else if (mineCurrent[0] === '-' && theirCurrent[0] === ' ') {
      // Mine removed or edited
      removal(hunk, mine, their);
    } else if (theirCurrent[0] === '-' && mineCurrent[0] === ' ') {
      // Their removed or edited
      removal(hunk, their, mine, true);
    } else if (mineCurrent === theirCurrent) {
      // Context identity
      hunk.lines.push(mineCurrent);
      mine.index++;
      their.index++;
    } else {
      // Context mismatch
      conflict(hunk, collectChange(mine), collectChange(their));
    }
  }

  // Now push anything that may be remaining
  insertTrailing(hunk, mine);
  insertTrailing(hunk, their);
  calcLineCount(hunk);
}
function mutualChange(hunk, mine, their) {
  var myChanges = collectChange(mine),
    theirChanges = collectChange(their);
  if (allRemoves(myChanges) && allRemoves(theirChanges)) {
    // Special case for remove changes that are supersets of one another
    if (
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _array
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    arrayStartsWith)
    /*istanbul ignore end*/
    (myChanges, theirChanges) && skipRemoveSuperset(their, myChanges, myChanges.length - theirChanges.length)) {
      /*istanbul ignore start*/
      var _hunk$lines3;
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      (_hunk$lines3 =
      /*istanbul ignore end*/
      hunk.lines).push.apply(
      /*istanbul ignore start*/
      _hunk$lines3
      /*istanbul ignore end*/
      ,
      /*istanbul ignore start*/
      _toConsumableArray(
      /*istanbul ignore end*/
      myChanges));
      return;
    } else if (
    /*istanbul ignore start*/
    (0,
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    _array
    /*istanbul ignore end*/
    .
    /*istanbul ignore start*/
    arrayStartsWith)
    /*istanbul ignore end*/
    (theirChanges, myChanges) && skipRemoveSuperset(mine, theirChanges, theirChanges.length - myChanges.length)) {
      /*istanbul ignore start*/
      var _hunk$lines4;
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      /*istanbul ignore end*/
      /*istanbul ignore start*/
      (_hunk$lines4 =
      /*istanbul ignore end*/
      hunk.lines).push.apply(
      /*istanbul ignore start*/
      _hunk$lines4
      /*istanbul ignore end*/
      ,
      /*istanbul ignore start*/
      _toConsumableArray(
      /*istanbul ignore end*/
      theirChanges));
      return;
    }
  } else if (
  /*istanbul ignore start*/
  (0,
  /*istanbul ignore end*/
  /*istanbul ignore start*/
  _array
  /*istanbul ignore end*/
  .
  /*istanbul ignore start*/
  arrayEqual)
  /*istanbul ignore end*/
  (myChanges, theirChanges)) {
    /*istanbul ignore start*/
    var _hunk$lines5;
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    (_hunk$lines5 =
    /*istanbul ignore end*/
    hunk.lines).push.apply(
    /*istanbul ignore start*/
    _hunk$lines5
    /*istanbul ignore end*/
    ,
    /*istanbul ignore start*/
    _toConsumableArray(
    /*istanbul ignore end*/
    myChanges));
    return;
  }
  conflict(hunk, myChanges, theirChanges);
}
function removal(hunk, mine, their, swap) {
  var myChanges = collectChange(mine),
    theirChanges = collectContext(their, myChanges);
  if (theirChanges.merged) {
    /*istanbul ignore start*/
    var _hunk$lines6;
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    /*istanbul ignore end*/
    /*istanbul ignore start*/
    (_hunk$lines6 =
    /*istanbul ignore end*/
    hunk.lines).push.apply(
    /*istanbul ignore start*/
    _hunk$lines6
    /*istanbul ignore end*/
    ,
    /*istanbul ignore start*/
    _toConsumableArray(
    /*istanbul ignore end*/
    theirChanges.merged));
  } else {
    conflict(hunk, swap ? theirChanges : myChanges, swap ? myChanges : theirChanges);
  }
}
function conflict(hunk, mine, their) {
  hunk.conflict = true;
  hunk.lines.push({
    conflict: true,
    mine: mine,
    theirs: their
  });
}
function insertLeading(hunk, insert, their) {
  while (insert.offset < their.offset && insert.index < insert.lines.length) {
    var line = insert.lines[insert.index++];
    hunk.lines.push(line);
    insert.offset++;
  }
}
function insertTrailing(hunk, insert) {
  while (insert.index < insert.lines.length) {
    var line = insert.lines[insert.index++];
    hunk.lines.push(line);
  }
}
function collectChange(state) {
  var ret = [],
    operation = state.lines[state.index][0];
  while (state.index < state.lines.length) {
    var line = state.lines[state.index];

    // Group additions that are immediately after subtractions and treat them as one "atomic" modify change.
    if (operation === '-' && line[0] === '+') {
      operation = '+';
    }
    if (operation === line[0]) {
      ret.push(line);
      state.index++;
    } else {
      break;
    }
  }
  return ret;
}
function collectContext(state, matchChanges) {
  var changes = [],
    merged = [],
    matchIndex = 0,
    contextChanges = false,
    conflicted = false;
  while (matchIndex < matchChanges.length && state.index < state.lines.length) {
    var change = state.lines[state.index],
      match = matchChanges[matchIndex];

    // Once we've hit our add, then we are done
    if (match[0] === '+') {
      break;
    }
    contextChanges = contextChanges || change[0] !== ' ';
    merged.push(match);
    matchIndex++;

    // Consume any additions in the other block as a conflict to attempt
    // to pull in the remaining context after this
    if (change[0] === '+') {
      conflicted = true;
      while (change[0] === '+') {
        changes.push(change);
        change = state.lines[++state.index];
      }
    }
    if (match.substr(1) === change.substr(1)) {
      changes.push(change);
      state.index++;
    } else {
      conflicted = true;
    }
  }
  if ((matchChanges[matchIndex] || '')[0] === '+' && contextChanges) {
    conflicted = true;
  }
  if (conflicted) {
    return changes;
  }
  while (matchIndex < matchChanges.length) {
    merged.push(matchChanges[matchIndex++]);
  }
  return {
    merged: merged,
    changes: changes
  };
}
function allRemoves(changes) {
  return changes.reduce(function (prev, change) {
    return prev && change[0] === '-';
  }, true);
}
function skipRemoveSuperset(state, removeChanges, delta) {
  for (var i = 0; i < delta; i++) {
    var changeContent = removeChanges[removeChanges.length - delta + i].substr(1);
    if (state.lines[state.index + i] !== ' ' + changeContent) {
      return false;
    }
  }
  state.index += delta;
  return true;
}
function calcOldNewLineCount(lines) {
  var oldLines = 0;
  var newLines = 0;
  lines.forEach(function (line) {
    if (typeof line !== 'string') {
      var myCount = calcOldNewLineCount(line.mine);
      var theirCount = calcOldNewLineCount(line.theirs);
      if (oldLines !== undefined) {
        if (myCount.oldLines === theirCount.oldLines) {
          oldLines += myCount.oldLines;
        } else {
          oldLines = undefined;
        }
      }
      if (newLines !== undefined) {
        if (myCount.newLines === theirCount.newLines) {
          newLines += myCount.newLines;
        } else {
          newLines = undefined;
        }
      }
    } else {
      if (newLines !== undefined && (line[0] === '+' || line[0] === ' ')) {
        newLines++;
      }
      if (oldLines !== undefined && (line[0] === '-' || line[0] === ' ')) {
        oldLines++;
      }
    }
  });
  return {
    oldLines: oldLines,
    newLines: newLines
  };
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJfY3JlYXRlIiwicmVxdWlyZSIsIl9wYXJzZSIsIl9hcnJheSIsIl90b0NvbnN1bWFibGVBcnJheSIsImFyciIsIl9hcnJheVdpdGhvdXRIb2xlcyIsIl9pdGVyYWJsZVRvQXJyYXkiLCJfdW5zdXBwb3J0ZWRJdGVyYWJsZVRvQXJyYXkiLCJfbm9uSXRlcmFibGVTcHJlYWQiLCJUeXBlRXJyb3IiLCJvIiwibWluTGVuIiwiX2FycmF5TGlrZVRvQXJyYXkiLCJuIiwiT2JqZWN0IiwicHJvdG90eXBlIiwidG9TdHJpbmciLCJjYWxsIiwic2xpY2UiLCJjb25zdHJ1Y3RvciIsIm5hbWUiLCJBcnJheSIsImZyb20iLCJ0ZXN0IiwiaXRlciIsIlN5bWJvbCIsIml0ZXJhdG9yIiwiaXNBcnJheSIsImxlbiIsImxlbmd0aCIsImkiLCJhcnIyIiwiY2FsY0xpbmVDb3VudCIsImh1bmsiLCJfY2FsY09sZE5ld0xpbmVDb3VudCIsImNhbGNPbGROZXdMaW5lQ291bnQiLCJsaW5lcyIsIm9sZExpbmVzIiwibmV3TGluZXMiLCJ1bmRlZmluZWQiLCJtZXJnZSIsIm1pbmUiLCJ0aGVpcnMiLCJiYXNlIiwibG9hZFBhdGNoIiwicmV0IiwiaW5kZXgiLCJuZXdGaWxlTmFtZSIsImZpbGVOYW1lQ2hhbmdlZCIsIm9sZEZpbGVOYW1lIiwib2xkSGVhZGVyIiwibmV3SGVhZGVyIiwic2VsZWN0RmllbGQiLCJodW5rcyIsIm1pbmVJbmRleCIsInRoZWlyc0luZGV4IiwibWluZU9mZnNldCIsInRoZWlyc09mZnNldCIsIm1pbmVDdXJyZW50Iiwib2xkU3RhcnQiLCJJbmZpbml0eSIsInRoZWlyc0N1cnJlbnQiLCJodW5rQmVmb3JlIiwicHVzaCIsImNsb25lSHVuayIsIm1lcmdlZEh1bmsiLCJNYXRoIiwibWluIiwibmV3U3RhcnQiLCJtZXJnZUxpbmVzIiwicGFyYW0iLCJwYXJzZVBhdGNoIiwiRXJyb3IiLCJzdHJ1Y3R1cmVkUGF0Y2giLCJwYXRjaCIsImNvbmZsaWN0IiwiY2hlY2siLCJvZmZzZXQiLCJtaW5lTGluZXMiLCJ0aGVpck9mZnNldCIsInRoZWlyTGluZXMiLCJ0aGVpciIsImluc2VydExlYWRpbmciLCJ0aGVpckN1cnJlbnQiLCJtdXR1YWxDaGFuZ2UiLCJfaHVuayRsaW5lcyIsImFwcGx5IiwiY29sbGVjdENoYW5nZSIsIl9odW5rJGxpbmVzMiIsInJlbW92YWwiLCJpbnNlcnRUcmFpbGluZyIsIm15Q2hhbmdlcyIsInRoZWlyQ2hhbmdlcyIsImFsbFJlbW92ZXMiLCJhcnJheVN0YXJ0c1dpdGgiLCJza2lwUmVtb3ZlU3VwZXJzZXQiLCJfaHVuayRsaW5lczMiLCJfaHVuayRsaW5lczQiLCJhcnJheUVxdWFsIiwiX2h1bmskbGluZXM1Iiwic3dhcCIsImNvbGxlY3RDb250ZXh0IiwibWVyZ2VkIiwiX2h1bmskbGluZXM2IiwiaW5zZXJ0IiwibGluZSIsInN0YXRlIiwib3BlcmF0aW9uIiwibWF0Y2hDaGFuZ2VzIiwiY2hhbmdlcyIsIm1hdGNoSW5kZXgiLCJjb250ZXh0Q2hhbmdlcyIsImNvbmZsaWN0ZWQiLCJjaGFuZ2UiLCJtYXRjaCIsInN1YnN0ciIsInJlZHVjZSIsInByZXYiLCJyZW1vdmVDaGFuZ2VzIiwiZGVsdGEiLCJjaGFuZ2VDb250ZW50IiwiZm9yRWFjaCIsIm15Q291bnQiLCJ0aGVpckNvdW50Il0sInNvdXJjZXMiOlsiLi4vLi4vc3JjL3BhdGNoL21lcmdlLmpzIl0sInNvdXJjZXNDb250ZW50IjpbImltcG9ydCB7c3RydWN0dXJlZFBhdGNofSBmcm9tICcuL2NyZWF0ZSc7XG5pbXBvcnQge3BhcnNlUGF0Y2h9IGZyb20gJy4vcGFyc2UnO1xuXG5pbXBvcnQge2FycmF5RXF1YWwsIGFycmF5U3RhcnRzV2l0aH0gZnJvbSAnLi4vdXRpbC9hcnJheSc7XG5cbmV4cG9ydCBmdW5jdGlvbiBjYWxjTGluZUNvdW50KGh1bmspIHtcbiAgY29uc3Qge29sZExpbmVzLCBuZXdMaW5lc30gPSBjYWxjT2xkTmV3TGluZUNvdW50KGh1bmsubGluZXMpO1xuXG4gIGlmIChvbGRMaW5lcyAhPT0gdW5kZWZpbmVkKSB7XG4gICAgaHVuay5vbGRMaW5lcyA9IG9sZExpbmVzO1xuICB9IGVsc2Uge1xuICAgIGRlbGV0ZSBodW5rLm9sZExpbmVzO1xuICB9XG5cbiAgaWYgKG5ld0xpbmVzICE9PSB1bmRlZmluZWQpIHtcbiAgICBodW5rLm5ld0xpbmVzID0gbmV3TGluZXM7XG4gIH0gZWxzZSB7XG4gICAgZGVsZXRlIGh1bmsubmV3TGluZXM7XG4gIH1cbn1cblxuZXhwb3J0IGZ1bmN0aW9uIG1lcmdlKG1pbmUsIHRoZWlycywgYmFzZSkge1xuICBtaW5lID0gbG9hZFBhdGNoKG1pbmUsIGJhc2UpO1xuICB0aGVpcnMgPSBsb2FkUGF0Y2godGhlaXJzLCBiYXNlKTtcblxuICBsZXQgcmV0ID0ge307XG5cbiAgLy8gRm9yIGluZGV4IHdlIGp1c3QgbGV0IGl0IHBhc3MgdGhyb3VnaCBhcyBpdCBkb2Vzbid0IGhhdmUgYW55IG5lY2Vzc2FyeSBtZWFuaW5nLlxuICAvLyBMZWF2aW5nIHNhbml0eSBjaGVja3Mgb24gdGhpcyB0byB0aGUgQVBJIGNvbnN1bWVyIHRoYXQgbWF5IGtub3cgbW9yZSBhYm91dCB0aGVcbiAgLy8gbWVhbmluZyBpbiB0aGVpciBvd24gY29udGV4dC5cbiAgaWYgKG1pbmUuaW5kZXggfHwgdGhlaXJzLmluZGV4KSB7XG4gICAgcmV0LmluZGV4ID0gbWluZS5pbmRleCB8fCB0aGVpcnMuaW5kZXg7XG4gIH1cblxuICBpZiAobWluZS5uZXdGaWxlTmFtZSB8fCB0aGVpcnMubmV3RmlsZU5hbWUpIHtcbiAgICBpZiAoIWZpbGVOYW1lQ2hhbmdlZChtaW5lKSkge1xuICAgICAgLy8gTm8gaGVhZGVyIG9yIG5vIGNoYW5nZSBpbiBvdXJzLCB1c2UgdGhlaXJzIChhbmQgb3VycyBpZiB0aGVpcnMgZG9lcyBub3QgZXhpc3QpXG4gICAgICByZXQub2xkRmlsZU5hbWUgPSB0aGVpcnMub2xkRmlsZU5hbWUgfHwgbWluZS5vbGRGaWxlTmFtZTtcbiAgICAgIHJldC5uZXdGaWxlTmFtZSA9IHRoZWlycy5uZXdGaWxlTmFtZSB8fCBtaW5lLm5ld0ZpbGVOYW1lO1xuICAgICAgcmV0Lm9sZEhlYWRlciA9IHRoZWlycy5vbGRIZWFkZXIgfHwgbWluZS5vbGRIZWFkZXI7XG4gICAgICByZXQubmV3SGVhZGVyID0gdGhlaXJzLm5ld0hlYWRlciB8fCBtaW5lLm5ld0hlYWRlcjtcbiAgICB9IGVsc2UgaWYgKCFmaWxlTmFtZUNoYW5nZWQodGhlaXJzKSkge1xuICAgICAgLy8gTm8gaGVhZGVyIG9yIG5vIGNoYW5nZSBpbiB0aGVpcnMsIHVzZSBvdXJzXG4gICAgICByZXQub2xkRmlsZU5hbWUgPSBtaW5lLm9sZEZpbGVOYW1lO1xuICAgICAgcmV0Lm5ld0ZpbGVOYW1lID0gbWluZS5uZXdGaWxlTmFtZTtcbiAgICAgIHJldC5vbGRIZWFkZXIgPSBtaW5lLm9sZEhlYWRlcjtcbiAgICAgIHJldC5uZXdIZWFkZXIgPSBtaW5lLm5ld0hlYWRlcjtcbiAgICB9IGVsc2Uge1xuICAgICAgLy8gQm90aCBjaGFuZ2VkLi4uIGZpZ3VyZSBpdCBvdXRcbiAgICAgIHJldC5vbGRGaWxlTmFtZSA9IHNlbGVjdEZpZWxkKHJldCwgbWluZS5vbGRGaWxlTmFtZSwgdGhlaXJzLm9sZEZpbGVOYW1lKTtcbiAgICAgIHJldC5uZXdGaWxlTmFtZSA9IHNlbGVjdEZpZWxkKHJldCwgbWluZS5uZXdGaWxlTmFtZSwgdGhlaXJzLm5ld0ZpbGVOYW1lKTtcbiAgICAgIHJldC5vbGRIZWFkZXIgPSBzZWxlY3RGaWVsZChyZXQsIG1pbmUub2xkSGVhZGVyLCB0aGVpcnMub2xkSGVhZGVyKTtcbiAgICAgIHJldC5uZXdIZWFkZXIgPSBzZWxlY3RGaWVsZChyZXQsIG1pbmUubmV3SGVhZGVyLCB0aGVpcnMubmV3SGVhZGVyKTtcbiAgICB9XG4gIH1cblxuICByZXQuaHVua3MgPSBbXTtcblxuICBsZXQgbWluZUluZGV4ID0gMCxcbiAgICAgIHRoZWlyc0luZGV4ID0gMCxcbiAgICAgIG1pbmVPZmZzZXQgPSAwLFxuICAgICAgdGhlaXJzT2Zmc2V0ID0gMDtcblxuICB3aGlsZSAobWluZUluZGV4IDwgbWluZS5odW5rcy5sZW5ndGggfHwgdGhlaXJzSW5kZXggPCB0aGVpcnMuaHVua3MubGVuZ3RoKSB7XG4gICAgbGV0IG1pbmVDdXJyZW50ID0gbWluZS5odW5rc1ttaW5lSW5kZXhdIHx8IHtvbGRTdGFydDogSW5maW5pdHl9LFxuICAgICAgICB0aGVpcnNDdXJyZW50ID0gdGhlaXJzLmh1bmtzW3RoZWlyc0luZGV4XSB8fCB7b2xkU3RhcnQ6IEluZmluaXR5fTtcblxuICAgIGlmIChodW5rQmVmb3JlKG1pbmVDdXJyZW50LCB0aGVpcnNDdXJyZW50KSkge1xuICAgICAgLy8gVGhpcyBwYXRjaCBkb2VzIG5vdCBvdmVybGFwIHdpdGggYW55IG9mIHRoZSBvdGhlcnMsIHlheS5cbiAgICAgIHJldC5odW5rcy5wdXNoKGNsb25lSHVuayhtaW5lQ3VycmVudCwgbWluZU9mZnNldCkpO1xuICAgICAgbWluZUluZGV4Kys7XG4gICAgICB0aGVpcnNPZmZzZXQgKz0gbWluZUN1cnJlbnQubmV3TGluZXMgLSBtaW5lQ3VycmVudC5vbGRMaW5lcztcbiAgICB9IGVsc2UgaWYgKGh1bmtCZWZvcmUodGhlaXJzQ3VycmVudCwgbWluZUN1cnJlbnQpKSB7XG4gICAgICAvLyBUaGlzIHBhdGNoIGRvZXMgbm90IG92ZXJsYXAgd2l0aCBhbnkgb2YgdGhlIG90aGVycywgeWF5LlxuICAgICAgcmV0Lmh1bmtzLnB1c2goY2xvbmVIdW5rKHRoZWlyc0N1cnJlbnQsIHRoZWlyc09mZnNldCkpO1xuICAgICAgdGhlaXJzSW5kZXgrKztcbiAgICAgIG1pbmVPZmZzZXQgKz0gdGhlaXJzQ3VycmVudC5uZXdMaW5lcyAtIHRoZWlyc0N1cnJlbnQub2xkTGluZXM7XG4gICAgfSBlbHNlIHtcbiAgICAgIC8vIE92ZXJsYXAsIG1lcmdlIGFzIGJlc3Qgd2UgY2FuXG4gICAgICBsZXQgbWVyZ2VkSHVuayA9IHtcbiAgICAgICAgb2xkU3RhcnQ6IE1hdGgubWluKG1pbmVDdXJyZW50Lm9sZFN0YXJ0LCB0aGVpcnNDdXJyZW50Lm9sZFN0YXJ0KSxcbiAgICAgICAgb2xkTGluZXM6IDAsXG4gICAgICAgIG5ld1N0YXJ0OiBNYXRoLm1pbihtaW5lQ3VycmVudC5uZXdTdGFydCArIG1pbmVPZmZzZXQsIHRoZWlyc0N1cnJlbnQub2xkU3RhcnQgKyB0aGVpcnNPZmZzZXQpLFxuICAgICAgICBuZXdMaW5lczogMCxcbiAgICAgICAgbGluZXM6IFtdXG4gICAgICB9O1xuICAgICAgbWVyZ2VMaW5lcyhtZXJnZWRIdW5rLCBtaW5lQ3VycmVudC5vbGRTdGFydCwgbWluZUN1cnJlbnQubGluZXMsIHRoZWlyc0N1cnJlbnQub2xkU3RhcnQsIHRoZWlyc0N1cnJlbnQubGluZXMpO1xuICAgICAgdGhlaXJzSW5kZXgrKztcbiAgICAgIG1pbmVJbmRleCsrO1xuXG4gICAgICByZXQuaHVua3MucHVzaChtZXJnZWRIdW5rKTtcbiAgICB9XG4gIH1cblxuICByZXR1cm4gcmV0O1xufVxuXG5mdW5jdGlvbiBsb2FkUGF0Y2gocGFyYW0sIGJhc2UpIHtcbiAgaWYgKHR5cGVvZiBwYXJhbSA9PT0gJ3N0cmluZycpIHtcbiAgICBpZiAoKC9eQEAvbSkudGVzdChwYXJhbSkgfHwgKCgvXkluZGV4Oi9tKS50ZXN0KHBhcmFtKSkpIHtcbiAgICAgIHJldHVybiBwYXJzZVBhdGNoKHBhcmFtKVswXTtcbiAgICB9XG5cbiAgICBpZiAoIWJhc2UpIHtcbiAgICAgIHRocm93IG5ldyBFcnJvcignTXVzdCBwcm92aWRlIGEgYmFzZSByZWZlcmVuY2Ugb3IgcGFzcyBpbiBhIHBhdGNoJyk7XG4gICAgfVxuICAgIHJldHVybiBzdHJ1Y3R1cmVkUGF0Y2godW5kZWZpbmVkLCB1bmRlZmluZWQsIGJhc2UsIHBhcmFtKTtcbiAgfVxuXG4gIHJldHVybiBwYXJhbTtcbn1cblxuZnVuY3Rpb24gZmlsZU5hbWVDaGFuZ2VkKHBhdGNoKSB7XG4gIHJldHVybiBwYXRjaC5uZXdGaWxlTmFtZSAmJiBwYXRjaC5uZXdGaWxlTmFtZSAhPT0gcGF0Y2gub2xkRmlsZU5hbWU7XG59XG5cbmZ1bmN0aW9uIHNlbGVjdEZpZWxkKGluZGV4LCBtaW5lLCB0aGVpcnMpIHtcbiAgaWYgKG1pbmUgPT09IHRoZWlycykge1xuICAgIHJldHVybiBtaW5lO1xuICB9IGVsc2Uge1xuICAgIGluZGV4LmNvbmZsaWN0ID0gdHJ1ZTtcbiAgICByZXR1cm4ge21pbmUsIHRoZWlyc307XG4gIH1cbn1cblxuZnVuY3Rpb24gaHVua0JlZm9yZSh0ZXN0LCBjaGVjaykge1xuICByZXR1cm4gdGVzdC5vbGRTdGFydCA8IGNoZWNrLm9sZFN0YXJ0XG4gICAgJiYgKHRlc3Qub2xkU3RhcnQgKyB0ZXN0Lm9sZExpbmVzKSA8IGNoZWNrLm9sZFN0YXJ0O1xufVxuXG5mdW5jdGlvbiBjbG9uZUh1bmsoaHVuaywgb2Zmc2V0KSB7XG4gIHJldHVybiB7XG4gICAgb2xkU3RhcnQ6IGh1bmsub2xkU3RhcnQsIG9sZExpbmVzOiBodW5rLm9sZExpbmVzLFxuICAgIG5ld1N0YXJ0OiBodW5rLm5ld1N0YXJ0ICsgb2Zmc2V0LCBuZXdMaW5lczogaHVuay5uZXdMaW5lcyxcbiAgICBsaW5lczogaHVuay5saW5lc1xuICB9O1xufVxuXG5mdW5jdGlvbiBtZXJnZUxpbmVzKGh1bmssIG1pbmVPZmZzZXQsIG1pbmVMaW5lcywgdGhlaXJPZmZzZXQsIHRoZWlyTGluZXMpIHtcbiAgLy8gVGhpcyB3aWxsIGdlbmVyYWxseSByZXN1bHQgaW4gYSBjb25mbGljdGVkIGh1bmssIGJ1dCB0aGVyZSBhcmUgY2FzZXMgd2hlcmUgdGhlIGNvbnRleHRcbiAgLy8gaXMgdGhlIG9ubHkgb3ZlcmxhcCB3aGVyZSB3ZSBjYW4gc3VjY2Vzc2Z1bGx5IG1lcmdlIHRoZSBjb250ZW50IGhlcmUuXG4gIGxldCBtaW5lID0ge29mZnNldDogbWluZU9mZnNldCwgbGluZXM6IG1pbmVMaW5lcywgaW5kZXg6IDB9LFxuICAgICAgdGhlaXIgPSB7b2Zmc2V0OiB0aGVpck9mZnNldCwgbGluZXM6IHRoZWlyTGluZXMsIGluZGV4OiAwfTtcblxuICAvLyBIYW5kbGUgYW55IGxlYWRpbmcgY29udGVudFxuICBpbnNlcnRMZWFkaW5nKGh1bmssIG1pbmUsIHRoZWlyKTtcbiAgaW5zZXJ0TGVhZGluZyhodW5rLCB0aGVpciwgbWluZSk7XG5cbiAgLy8gTm93IGluIHRoZSBvdmVybGFwIGNvbnRlbnQuIFNjYW4gdGhyb3VnaCBhbmQgc2VsZWN0IHRoZSBiZXN0IGNoYW5nZXMgZnJvbSBlYWNoLlxuICB3aGlsZSAobWluZS5pbmRleCA8IG1pbmUubGluZXMubGVuZ3RoICYmIHRoZWlyLmluZGV4IDwgdGhlaXIubGluZXMubGVuZ3RoKSB7XG4gICAgbGV0IG1pbmVDdXJyZW50ID0gbWluZS5saW5lc1ttaW5lLmluZGV4XSxcbiAgICAgICAgdGhlaXJDdXJyZW50ID0gdGhlaXIubGluZXNbdGhlaXIuaW5kZXhdO1xuXG4gICAgaWYgKChtaW5lQ3VycmVudFswXSA9PT0gJy0nIHx8IG1pbmVDdXJyZW50WzBdID09PSAnKycpXG4gICAgICAgICYmICh0aGVpckN1cnJlbnRbMF0gPT09ICctJyB8fCB0aGVpckN1cnJlbnRbMF0gPT09ICcrJykpIHtcbiAgICAgIC8vIEJvdGggbW9kaWZpZWQgLi4uXG4gICAgICBtdXR1YWxDaGFuZ2UoaHVuaywgbWluZSwgdGhlaXIpO1xuICAgIH0gZWxzZSBpZiAobWluZUN1cnJlbnRbMF0gPT09ICcrJyAmJiB0aGVpckN1cnJlbnRbMF0gPT09ICcgJykge1xuICAgICAgLy8gTWluZSBpbnNlcnRlZFxuICAgICAgaHVuay5saW5lcy5wdXNoKC4uLiBjb2xsZWN0Q2hhbmdlKG1pbmUpKTtcbiAgICB9IGVsc2UgaWYgKHRoZWlyQ3VycmVudFswXSA9PT0gJysnICYmIG1pbmVDdXJyZW50WzBdID09PSAnICcpIHtcbiAgICAgIC8vIFRoZWlycyBpbnNlcnRlZFxuICAgICAgaHVuay5saW5lcy5wdXNoKC4uLiBjb2xsZWN0Q2hhbmdlKHRoZWlyKSk7XG4gICAgfSBlbHNlIGlmIChtaW5lQ3VycmVudFswXSA9PT0gJy0nICYmIHRoZWlyQ3VycmVudFswXSA9PT0gJyAnKSB7XG4gICAgICAvLyBNaW5lIHJlbW92ZWQgb3IgZWRpdGVkXG4gICAgICByZW1vdmFsKGh1bmssIG1pbmUsIHRoZWlyKTtcbiAgICB9IGVsc2UgaWYgKHRoZWlyQ3VycmVudFswXSA9PT0gJy0nICYmIG1pbmVDdXJyZW50WzBdID09PSAnICcpIHtcbiAgICAgIC8vIFRoZWlyIHJlbW92ZWQgb3IgZWRpdGVkXG4gICAgICByZW1vdmFsKGh1bmssIHRoZWlyLCBtaW5lLCB0cnVlKTtcbiAgICB9IGVsc2UgaWYgKG1pbmVDdXJyZW50ID09PSB0aGVpckN1cnJlbnQpIHtcbiAgICAgIC8vIENvbnRleHQgaWRlbnRpdHlcbiAgICAgIGh1bmsubGluZXMucHVzaChtaW5lQ3VycmVudCk7XG4gICAgICBtaW5lLmluZGV4Kys7XG4gICAgICB0aGVpci5pbmRleCsrO1xuICAgIH0gZWxzZSB7XG4gICAgICAvLyBDb250ZXh0IG1pc21hdGNoXG4gICAgICBjb25mbGljdChodW5rLCBjb2xsZWN0Q2hhbmdlKG1pbmUpLCBjb2xsZWN0Q2hhbmdlKHRoZWlyKSk7XG4gICAgfVxuICB9XG5cbiAgLy8gTm93IHB1c2ggYW55dGhpbmcgdGhhdCBtYXkgYmUgcmVtYWluaW5nXG4gIGluc2VydFRyYWlsaW5nKGh1bmssIG1pbmUpO1xuICBpbnNlcnRUcmFpbGluZyhodW5rLCB0aGVpcik7XG5cbiAgY2FsY0xpbmVDb3VudChodW5rKTtcbn1cblxuZnVuY3Rpb24gbXV0dWFsQ2hhbmdlKGh1bmssIG1pbmUsIHRoZWlyKSB7XG4gIGxldCBteUNoYW5nZXMgPSBjb2xsZWN0Q2hhbmdlKG1pbmUpLFxuICAgICAgdGhlaXJDaGFuZ2VzID0gY29sbGVjdENoYW5nZSh0aGVpcik7XG5cbiAgaWYgKGFsbFJlbW92ZXMobXlDaGFuZ2VzKSAmJiBhbGxSZW1vdmVzKHRoZWlyQ2hhbmdlcykpIHtcbiAgICAvLyBTcGVjaWFsIGNhc2UgZm9yIHJlbW92ZSBjaGFuZ2VzIHRoYXQgYXJlIHN1cGVyc2V0cyBvZiBvbmUgYW5vdGhlclxuICAgIGlmIChhcnJheVN0YXJ0c1dpdGgobXlDaGFuZ2VzLCB0aGVpckNoYW5nZXMpXG4gICAgICAgICYmIHNraXBSZW1vdmVTdXBlcnNldCh0aGVpciwgbXlDaGFuZ2VzLCBteUNoYW5nZXMubGVuZ3RoIC0gdGhlaXJDaGFuZ2VzLmxlbmd0aCkpIHtcbiAgICAgIGh1bmsubGluZXMucHVzaCguLi4gbXlDaGFuZ2VzKTtcbiAgICAgIHJldHVybjtcbiAgICB9IGVsc2UgaWYgKGFycmF5U3RhcnRzV2l0aCh0aGVpckNoYW5nZXMsIG15Q2hhbmdlcylcbiAgICAgICAgJiYgc2tpcFJlbW92ZVN1cGVyc2V0KG1pbmUsIHRoZWlyQ2hhbmdlcywgdGhlaXJDaGFuZ2VzLmxlbmd0aCAtIG15Q2hhbmdlcy5sZW5ndGgpKSB7XG4gICAgICBodW5rLmxpbmVzLnB1c2goLi4uIHRoZWlyQ2hhbmdlcyk7XG4gICAgICByZXR1cm47XG4gICAgfVxuICB9IGVsc2UgaWYgKGFycmF5RXF1YWwobXlDaGFuZ2VzLCB0aGVpckNoYW5nZXMpKSB7XG4gICAgaHVuay5saW5lcy5wdXNoKC4uLiBteUNoYW5nZXMpO1xuICAgIHJldHVybjtcbiAgfVxuXG4gIGNvbmZsaWN0KGh1bmssIG15Q2hhbmdlcywgdGhlaXJDaGFuZ2VzKTtcbn1cblxuZnVuY3Rpb24gcmVtb3ZhbChodW5rLCBtaW5lLCB0aGVpciwgc3dhcCkge1xuICBsZXQgbXlDaGFuZ2VzID0gY29sbGVjdENoYW5nZShtaW5lKSxcbiAgICAgIHRoZWlyQ2hhbmdlcyA9IGNvbGxlY3RDb250ZXh0KHRoZWlyLCBteUNoYW5nZXMpO1xuICBpZiAodGhlaXJDaGFuZ2VzLm1lcmdlZCkge1xuICAgIGh1bmsubGluZXMucHVzaCguLi4gdGhlaXJDaGFuZ2VzLm1lcmdlZCk7XG4gIH0gZWxzZSB7XG4gICAgY29uZmxpY3QoaHVuaywgc3dhcCA/IHRoZWlyQ2hhbmdlcyA6IG15Q2hhbmdlcywgc3dhcCA/IG15Q2hhbmdlcyA6IHRoZWlyQ2hhbmdlcyk7XG4gIH1cbn1cblxuZnVuY3Rpb24gY29uZmxpY3QoaHVuaywgbWluZSwgdGhlaXIpIHtcbiAgaHVuay5jb25mbGljdCA9IHRydWU7XG4gIGh1bmsubGluZXMucHVzaCh7XG4gICAgY29uZmxpY3Q6IHRydWUsXG4gICAgbWluZTogbWluZSxcbiAgICB0aGVpcnM6IHRoZWlyXG4gIH0pO1xufVxuXG5mdW5jdGlvbiBpbnNlcnRMZWFkaW5nKGh1bmssIGluc2VydCwgdGhlaXIpIHtcbiAgd2hpbGUgKGluc2VydC5vZmZzZXQgPCB0aGVpci5vZmZzZXQgJiYgaW5zZXJ0LmluZGV4IDwgaW5zZXJ0LmxpbmVzLmxlbmd0aCkge1xuICAgIGxldCBsaW5lID0gaW5zZXJ0LmxpbmVzW2luc2VydC5pbmRleCsrXTtcbiAgICBodW5rLmxpbmVzLnB1c2gobGluZSk7XG4gICAgaW5zZXJ0Lm9mZnNldCsrO1xuICB9XG59XG5mdW5jdGlvbiBpbnNlcnRUcmFpbGluZyhodW5rLCBpbnNlcnQpIHtcbiAgd2hpbGUgKGluc2VydC5pbmRleCA8IGluc2VydC5saW5lcy5sZW5ndGgpIHtcbiAgICBsZXQgbGluZSA9IGluc2VydC5saW5lc1tpbnNlcnQuaW5kZXgrK107XG4gICAgaHVuay5saW5lcy5wdXNoKGxpbmUpO1xuICB9XG59XG5cbmZ1bmN0aW9uIGNvbGxlY3RDaGFuZ2Uoc3RhdGUpIHtcbiAgbGV0IHJldCA9IFtdLFxuICAgICAgb3BlcmF0aW9uID0gc3RhdGUubGluZXNbc3RhdGUuaW5kZXhdWzBdO1xuICB3aGlsZSAoc3RhdGUuaW5kZXggPCBzdGF0ZS5saW5lcy5sZW5ndGgpIHtcbiAgICBsZXQgbGluZSA9IHN0YXRlLmxpbmVzW3N0YXRlLmluZGV4XTtcblxuICAgIC8vIEdyb3VwIGFkZGl0aW9ucyB0aGF0IGFyZSBpbW1lZGlhdGVseSBhZnRlciBzdWJ0cmFjdGlvbnMgYW5kIHRyZWF0IHRoZW0gYXMgb25lIFwiYXRvbWljXCIgbW9kaWZ5IGNoYW5nZS5cbiAgICBpZiAob3BlcmF0aW9uID09PSAnLScgJiYgbGluZVswXSA9PT0gJysnKSB7XG4gICAgICBvcGVyYXRpb24gPSAnKyc7XG4gICAgfVxuXG4gICAgaWYgKG9wZXJhdGlvbiA9PT0gbGluZVswXSkge1xuICAgICAgcmV0LnB1c2gobGluZSk7XG4gICAgICBzdGF0ZS5pbmRleCsrO1xuICAgIH0gZWxzZSB7XG4gICAgICBicmVhaztcbiAgICB9XG4gIH1cblxuICByZXR1cm4gcmV0O1xufVxuZnVuY3Rpb24gY29sbGVjdENvbnRleHQoc3RhdGUsIG1hdGNoQ2hhbmdlcykge1xuICBsZXQgY2hhbmdlcyA9IFtdLFxuICAgICAgbWVyZ2VkID0gW10sXG4gICAgICBtYXRjaEluZGV4ID0gMCxcbiAgICAgIGNvbnRleHRDaGFuZ2VzID0gZmFsc2UsXG4gICAgICBjb25mbGljdGVkID0gZmFsc2U7XG4gIHdoaWxlIChtYXRjaEluZGV4IDwgbWF0Y2hDaGFuZ2VzLmxlbmd0aFxuICAgICAgICAmJiBzdGF0ZS5pbmRleCA8IHN0YXRlLmxpbmVzLmxlbmd0aCkge1xuICAgIGxldCBjaGFuZ2UgPSBzdGF0ZS5saW5lc1tzdGF0ZS5pbmRleF0sXG4gICAgICAgIG1hdGNoID0gbWF0Y2hDaGFuZ2VzW21hdGNoSW5kZXhdO1xuXG4gICAgLy8gT25jZSB3ZSd2ZSBoaXQgb3VyIGFkZCwgdGhlbiB3ZSBhcmUgZG9uZVxuICAgIGlmIChtYXRjaFswXSA9PT0gJysnKSB7XG4gICAgICBicmVhaztcbiAgICB9XG5cbiAgICBjb250ZXh0Q2hhbmdlcyA9IGNvbnRleHRDaGFuZ2VzIHx8IGNoYW5nZVswXSAhPT0gJyAnO1xuXG4gICAgbWVyZ2VkLnB1c2gobWF0Y2gpO1xuICAgIG1hdGNoSW5kZXgrKztcblxuICAgIC8vIENvbnN1bWUgYW55IGFkZGl0aW9ucyBpbiB0aGUgb3RoZXIgYmxvY2sgYXMgYSBjb25mbGljdCB0byBhdHRlbXB0XG4gICAgLy8gdG8gcHVsbCBpbiB0aGUgcmVtYWluaW5nIGNvbnRleHQgYWZ0ZXIgdGhpc1xuICAgIGlmIChjaGFuZ2VbMF0gPT09ICcrJykge1xuICAgICAgY29uZmxpY3RlZCA9IHRydWU7XG5cbiAgICAgIHdoaWxlIChjaGFuZ2VbMF0gPT09ICcrJykge1xuICAgICAgICBjaGFuZ2VzLnB1c2goY2hhbmdlKTtcbiAgICAgICAgY2hhbmdlID0gc3RhdGUubGluZXNbKytzdGF0ZS5pbmRleF07XG4gICAgICB9XG4gICAgfVxuXG4gICAgaWYgKG1hdGNoLnN1YnN0cigxKSA9PT0gY2hhbmdlLnN1YnN0cigxKSkge1xuICAgICAgY2hhbmdlcy5wdXNoKGNoYW5nZSk7XG4gICAgICBzdGF0ZS5pbmRleCsrO1xuICAgIH0gZWxzZSB7XG4gICAgICBjb25mbGljdGVkID0gdHJ1ZTtcbiAgICB9XG4gIH1cblxuICBpZiAoKG1hdGNoQ2hhbmdlc1ttYXRjaEluZGV4XSB8fCAnJylbMF0gPT09ICcrJ1xuICAgICAgJiYgY29udGV4dENoYW5nZXMpIHtcbiAgICBjb25mbGljdGVkID0gdHJ1ZTtcbiAgfVxuXG4gIGlmIChjb25mbGljdGVkKSB7XG4gICAgcmV0dXJuIGNoYW5nZXM7XG4gIH1cblxuICB3aGlsZSAobWF0Y2hJbmRleCA8IG1hdGNoQ2hhbmdlcy5sZW5ndGgpIHtcbiAgICBtZXJnZWQucHVzaChtYXRjaENoYW5nZXNbbWF0Y2hJbmRleCsrXSk7XG4gIH1cblxuICByZXR1cm4ge1xuICAgIG1lcmdlZCxcbiAgICBjaGFuZ2VzXG4gIH07XG59XG5cbmZ1bmN0aW9uIGFsbFJlbW92ZXMoY2hhbmdlcykge1xuICByZXR1cm4gY2hhbmdlcy5yZWR1Y2UoZnVuY3Rpb24ocHJldiwgY2hhbmdlKSB7XG4gICAgcmV0dXJuIHByZXYgJiYgY2hhbmdlWzBdID09PSAnLSc7XG4gIH0sIHRydWUpO1xufVxuZnVuY3Rpb24gc2tpcFJlbW92ZVN1cGVyc2V0KHN0YXRlLCByZW1vdmVDaGFuZ2VzLCBkZWx0YSkge1xuICBmb3IgKGxldCBpID0gMDsgaSA8IGRlbHRhOyBpKyspIHtcbiAgICBsZXQgY2hhbmdlQ29udGVudCA9IHJlbW92ZUNoYW5nZXNbcmVtb3ZlQ2hhbmdlcy5sZW5ndGggLSBkZWx0YSArIGldLnN1YnN0cigxKTtcbiAgICBpZiAoc3RhdGUubGluZXNbc3RhdGUuaW5kZXggKyBpXSAhPT0gJyAnICsgY2hhbmdlQ29udGVudCkge1xuICAgICAgcmV0dXJuIGZhbHNlO1xuICAgIH1cbiAgfVxuXG4gIHN0YXRlLmluZGV4ICs9IGRlbHRhO1xuICByZXR1cm4gdHJ1ZTtcbn1cblxuZnVuY3Rpb24gY2FsY09sZE5ld0xpbmVDb3VudChsaW5lcykge1xuICBsZXQgb2xkTGluZXMgPSAwO1xuICBsZXQgbmV3TGluZXMgPSAwO1xuXG4gIGxpbmVzLmZvckVhY2goZnVuY3Rpb24obGluZSkge1xuICAgIGlmICh0eXBlb2YgbGluZSAhPT0gJ3N0cmluZycpIHtcbiAgICAgIGxldCBteUNvdW50ID0gY2FsY09sZE5ld0xpbmVDb3VudChsaW5lLm1pbmUpO1xuICAgICAgbGV0IHRoZWlyQ291bnQgPSBjYWxjT2xkTmV3TGluZUNvdW50KGxpbmUudGhlaXJzKTtcblxuICAgICAgaWYgKG9sZExpbmVzICE9PSB1bmRlZmluZWQpIHtcbiAgICAgICAgaWYgKG15Q291bnQub2xkTGluZXMgPT09IHRoZWlyQ291bnQub2xkTGluZXMpIHtcbiAgICAgICAgICBvbGRMaW5lcyArPSBteUNvdW50Lm9sZExpbmVzO1xuICAgICAgICB9IGVsc2Uge1xuICAgICAgICAgIG9sZExpbmVzID0gdW5kZWZpbmVkO1xuICAgICAgICB9XG4gICAgICB9XG5cbiAgICAgIGlmIChuZXdMaW5lcyAhPT0gdW5kZWZpbmVkKSB7XG4gICAgICAgIGlmIChteUNvdW50Lm5ld0xpbmVzID09PSB0aGVpckNvdW50Lm5ld0xpbmVzKSB7XG4gICAgICAgICAgbmV3TGluZXMgKz0gbXlDb3VudC5uZXdMaW5lcztcbiAgICAgICAgfSBlbHNlIHtcbiAgICAgICAgICBuZXdMaW5lcyA9IHVuZGVmaW5lZDtcbiAgICAgICAgfVxuICAgICAgfVxuICAgIH0gZWxzZSB7XG4gICAgICBpZiAobmV3TGluZXMgIT09IHVuZGVmaW5lZCAmJiAobGluZVswXSA9PT0gJysnIHx8IGxpbmVbMF0gPT09ICcgJykpIHtcbiAgICAgICAgbmV3TGluZXMrKztcbiAgICAgIH1cbiAgICAgIGlmIChvbGRMaW5lcyAhPT0gdW5kZWZpbmVkICYmIChsaW5lWzBdID09PSAnLScgfHwgbGluZVswXSA9PT0gJyAnKSkge1xuICAgICAgICBvbGRMaW5lcysrO1xuICAgICAgfVxuICAgIH1cbiAgfSk7XG5cbiAgcmV0dXJuIHtvbGRMaW5lcywgbmV3TGluZXN9O1xufVxuIl0sIm1hcHBpbmdzIjoiOzs7Ozs7Ozs7QUFBQTtBQUFBO0FBQUFBLE9BQUEsR0FBQUMsT0FBQTtBQUFBO0FBQUE7QUFDQTtBQUFBO0FBQUFDLE1BQUEsR0FBQUQsT0FBQTtBQUFBO0FBQUE7QUFFQTtBQUFBO0FBQUFFLE1BQUEsR0FBQUYsT0FBQTtBQUFBO0FBQUE7QUFBMEQsbUNBQUFHLG1CQUFBQyxHQUFBLFdBQUFDLGtCQUFBLENBQUFELEdBQUEsS0FBQUUsZ0JBQUEsQ0FBQUYsR0FBQSxLQUFBRywyQkFBQSxDQUFBSCxHQUFBLEtBQUFJLGtCQUFBO0FBQUEsU0FBQUEsbUJBQUEsY0FBQUMsU0FBQTtBQUFBLFNBQUFGLDRCQUFBRyxDQUFBLEVBQUFDLE1BQUEsU0FBQUQsQ0FBQSxxQkFBQUEsQ0FBQSxzQkFBQUUsaUJBQUEsQ0FBQUYsQ0FBQSxFQUFBQyxNQUFBLE9BQUFFLENBQUEsR0FBQUMsTUFBQSxDQUFBQyxTQUFBLENBQUFDLFFBQUEsQ0FBQUMsSUFBQSxDQUFBUCxDQUFBLEVBQUFRLEtBQUEsYUFBQUwsQ0FBQSxpQkFBQUgsQ0FBQSxDQUFBUyxXQUFBLEVBQUFOLENBQUEsR0FBQUgsQ0FBQSxDQUFBUyxXQUFBLENBQUFDLElBQUEsTUFBQVAsQ0FBQSxjQUFBQSxDQUFBLG1CQUFBUSxLQUFBLENBQUFDLElBQUEsQ0FBQVosQ0FBQSxPQUFBRyxDQUFBLCtEQUFBVSxJQUFBLENB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
