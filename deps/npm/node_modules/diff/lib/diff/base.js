/*istanbul ignore start*/
"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports["default"] = Diff;
/*istanbul ignore end*/
function Diff() {}
Diff.prototype = {
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  diff: function diff(oldString, newString) {
    /*istanbul ignore start*/
    var _options$timeout;
    var
    /*istanbul ignore end*/
    options = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : {};
    var callback = options.callback;
    if (typeof options === 'function') {
      callback = options;
      options = {};
    }
    var self = this;
    function done(value) {
      value = self.postProcess(value, options);
      if (callback) {
        setTimeout(function () {
          callback(value);
        }, 0);
        return true;
      } else {
        return value;
      }
    }

    // Allow subclasses to massage the input prior to running
    oldString = this.castInput(oldString, options);
    newString = this.castInput(newString, options);
    oldString = this.removeEmpty(this.tokenize(oldString, options));
    newString = this.removeEmpty(this.tokenize(newString, options));
    var newLen = newString.length,
      oldLen = oldString.length;
    var editLength = 1;
    var maxEditLength = newLen + oldLen;
    if (options.maxEditLength != null) {
      maxEditLength = Math.min(maxEditLength, options.maxEditLength);
    }
    var maxExecutionTime =
    /*istanbul ignore start*/
    (_options$timeout =
    /*istanbul ignore end*/
    options.timeout) !== null && _options$timeout !== void 0 ? _options$timeout : Infinity;
    var abortAfterTimestamp = Date.now() + maxExecutionTime;
    var bestPath = [{
      oldPos: -1,
      lastComponent: undefined
    }];

    // Seed editLength = 0, i.e. the content starts with the same values
    var newPos = this.extractCommon(bestPath[0], newString, oldString, 0, options);
    if (bestPath[0].oldPos + 1 >= oldLen && newPos + 1 >= newLen) {
      // Identity per the equality and tokenizer
      return done(buildValues(self, bestPath[0].lastComponent, newString, oldString, self.useLongestToken));
    }

    // Once we hit the right edge of the edit graph on some diagonal k, we can
    // definitely reach the end of the edit graph in no more than k edits, so
    // there's no point in considering any moves to diagonal k+1 any more (from
    // which we're guaranteed to need at least k+1 more edits).
    // Similarly, once we've reached the bottom of the edit graph, there's no
    // point considering moves to lower diagonals.
    // We record this fact by setting minDiagonalToConsider and
    // maxDiagonalToConsider to some finite value once we've hit the edge of
    // the edit graph.
    // This optimization is not faithful to the original algorithm presented in
    // Myers's paper, which instead pointlessly extends D-paths off the end of
    // the edit graph - see page 7 of Myers's paper which notes this point
    // explicitly and illustrates it with a diagram. This has major performance
    // implications for some common scenarios. For instance, to compute a diff
    // where the new text simply appends d characters on the end of the
    // original text of length n, the true Myers algorithm will take O(n+d^2)
    // time while this optimization needs only O(n+d) time.
    var minDiagonalToConsider = -Infinity,
      maxDiagonalToConsider = Infinity;

    // Main worker method. checks all permutations of a given edit length for acceptance.
    function execEditLength() {
      for (var diagonalPath = Math.max(minDiagonalToConsider, -editLength); diagonalPath <= Math.min(maxDiagonalToConsider, editLength); diagonalPath += 2) {
        var basePath =
        /*istanbul ignore start*/
        void 0
        /*istanbul ignore end*/
        ;
        var removePath = bestPath[diagonalPath - 1],
          addPath = bestPath[diagonalPath + 1];
        if (removePath) {
          // No one else is going to attempt to use this value, clear it
          bestPath[diagonalPath - 1] = undefined;
        }
        var canAdd = false;
        if (addPath) {
          // what newPos will be after we do an insertion:
          var addPathNewPos = addPath.oldPos - diagonalPath;
          canAdd = addPath && 0 <= addPathNewPos && addPathNewPos < newLen;
        }
        var canRemove = removePath && removePath.oldPos + 1 < oldLen;
        if (!canAdd && !canRemove) {
          // If this path is a terminal then prune
          bestPath[diagonalPath] = undefined;
          continue;
        }

        // Select the diagonal that we want to branch from. We select the prior
        // path whose position in the old string is the farthest from the origin
        // and does not pass the bounds of the diff graph
        if (!canRemove || canAdd && removePath.oldPos < addPath.oldPos) {
          basePath = self.addToPath(addPath, true, false, 0, options);
        } else {
          basePath = self.addToPath(removePath, false, true, 1, options);
        }
        newPos = self.extractCommon(basePath, newString, oldString, diagonalPath, options);
        if (basePath.oldPos + 1 >= oldLen && newPos + 1 >= newLen) {
          // If we have hit the end of both strings, then we are done
          return done(buildValues(self, basePath.lastComponent, newString, oldString, self.useLongestToken));
        } else {
          bestPath[diagonalPath] = basePath;
          if (basePath.oldPos + 1 >= oldLen) {
            maxDiagonalToConsider = Math.min(maxDiagonalToConsider, diagonalPath - 1);
          }
          if (newPos + 1 >= newLen) {
            minDiagonalToConsider = Math.max(minDiagonalToConsider, diagonalPath + 1);
          }
        }
      }
      editLength++;
    }

    // Performs the length of edit iteration. Is a bit fugly as this has to support the
    // sync and async mode which is never fun. Loops over execEditLength until a value
    // is produced, or until the edit length exceeds options.maxEditLength (if given),
    // in which case it will return undefined.
    if (callback) {
      (function exec() {
        setTimeout(function () {
          if (editLength > maxEditLength || Date.now() > abortAfterTimestamp) {
            return callback();
          }
          if (!execEditLength()) {
            exec();
          }
        }, 0);
      })();
    } else {
      while (editLength <= maxEditLength && Date.now() <= abortAfterTimestamp) {
        var ret = execEditLength();
        if (ret) {
          return ret;
        }
      }
    }
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  addToPath: function addToPath(path, added, removed, oldPosInc, options) {
    var last = path.lastComponent;
    if (last && !options.oneChangePerToken && last.added === added && last.removed === removed) {
      return {
        oldPos: path.oldPos + oldPosInc,
        lastComponent: {
          count: last.count + 1,
          added: added,
          removed: removed,
          previousComponent: last.previousComponent
        }
      };
    } else {
      return {
        oldPos: path.oldPos + oldPosInc,
        lastComponent: {
          count: 1,
          added: added,
          removed: removed,
          previousComponent: last
        }
      };
    }
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  extractCommon: function extractCommon(basePath, newString, oldString, diagonalPath, options) {
    var newLen = newString.length,
      oldLen = oldString.length,
      oldPos = basePath.oldPos,
      newPos = oldPos - diagonalPath,
      commonCount = 0;
    while (newPos + 1 < newLen && oldPos + 1 < oldLen && this.equals(oldString[oldPos + 1], newString[newPos + 1], options)) {
      newPos++;
      oldPos++;
      commonCount++;
      if (options.oneChangePerToken) {
        basePath.lastComponent = {
          count: 1,
          previousComponent: basePath.lastComponent,
          added: false,
          removed: false
        };
      }
    }
    if (commonCount && !options.oneChangePerToken) {
      basePath.lastComponent = {
        count: commonCount,
        previousComponent: basePath.lastComponent,
        added: false,
        removed: false
      };
    }
    basePath.oldPos = oldPos;
    return newPos;
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  equals: function equals(left, right, options) {
    if (options.comparator) {
      return options.comparator(left, right);
    } else {
      return left === right || options.ignoreCase && left.toLowerCase() === right.toLowerCase();
    }
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  removeEmpty: function removeEmpty(array) {
    var ret = [];
    for (var i = 0; i < array.length; i++) {
      if (array[i]) {
        ret.push(array[i]);
      }
    }
    return ret;
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  castInput: function castInput(value) {
    return value;
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  tokenize: function tokenize(value) {
    return Array.from(value);
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  join: function join(chars) {
    return chars.join('');
  },
  /*istanbul ignore start*/
  /*istanbul ignore end*/
  postProcess: function postProcess(changeObjects) {
    return changeObjects;
  }
};
function buildValues(diff, lastComponent, newString, oldString, useLongestToken) {
  // First we convert our linked list of components in reverse order to an
  // array in the right order:
  var components = [];
  var nextComponent;
  while (lastComponent) {
    components.push(lastComponent);
    nextComponent = lastComponent.previousComponent;
    delete lastComponent.previousComponent;
    lastComponent = nextComponent;
  }
  components.reverse();
  var componentPos = 0,
    componentLen = components.length,
    newPos = 0,
    oldPos = 0;
  for (; componentPos < componentLen; componentPos++) {
    var component = components[componentPos];
    if (!component.removed) {
      if (!component.added && useLongestToken) {
        var value = newString.slice(newPos, newPos + component.count);
        value = value.map(function (value, i) {
          var oldValue = oldString[oldPos + i];
          return oldValue.length > value.length ? oldValue : value;
        });
        component.value = diff.join(value);
      } else {
        component.value = diff.join(newString.slice(newPos, newPos + component.count));
      }
      newPos += component.count;

      // Common case
      if (!component.added) {
        oldPos += component.count;
      }
    } else {
      component.value = diff.join(oldString.slice(oldPos, oldPos + component.count));
      oldPos += component.count;
    }
  }
  return components;
}
//# sourceMappingURL=data:application/json;charset=utf-8;base64,eyJ2ZXJzaW9uIjozLCJuYW1lcyI6WyJEaWZmIiwicHJvdG90eXBlIiwiZGlmZiIsIm9sZFN0cmluZyIsIm5ld1N0cmluZyIsIl9vcHRpb25zJHRpbWVvdXQiLCJvcHRpb25zIiwiYXJndW1lbnRzIiwibGVuZ3RoIiwidW5kZWZpbmVkIiwiY2FsbGJhY2siLCJzZWxmIiwiZG9uZSIsInZhbHVlIiwicG9zdFByb2Nlc3MiLCJzZXRUaW1lb3V0IiwiY2FzdElucHV0IiwicmVtb3ZlRW1wdHkiLCJ0b2tlbml6ZSIsIm5ld0xlbiIsIm9sZExlbiIsImVkaXRMZW5ndGgiLCJtYXhFZGl0TGVuZ3RoIiwiTWF0aCIsIm1pbiIsIm1heEV4ZWN1dGlvblRpbWUiLCJ0aW1lb3V0IiwiSW5maW5pdHkiLCJhYm9ydEFmdGVyVGltZXN0YW1wIiwiRGF0ZSIsIm5vdyIsImJlc3RQYXRoIiwib2xkUG9zIiwibGFzdENvbXBvbmVudCIsIm5ld1BvcyIsImV4dHJhY3RDb21tb24iLCJidWlsZFZhbHVlcyIsInVzZUxvbmdlc3RUb2tlbiIsIm1pbkRpYWdvbmFsVG9Db25zaWRlciIsIm1heERpYWdvbmFsVG9Db25zaWRlciIsImV4ZWNFZGl0TGVuZ3RoIiwiZGlhZ29uYWxQYXRoIiwibWF4IiwiYmFzZVBhdGgiLCJyZW1vdmVQYXRoIiwiYWRkUGF0aCIsImNhbkFkZCIsImFkZFBhdGhOZXdQb3MiLCJjYW5SZW1vdmUiLCJhZGRUb1BhdGgiLCJleGVjIiwicmV0IiwicGF0aCIsImFkZGVkIiwicmVtb3ZlZCIsIm9sZFBvc0luYyIsImxhc3QiLCJvbmVDaGFuZ2VQZXJUb2tlbiIsImNvdW50IiwicHJldmlvdXNDb21wb25lbnQiLCJjb21tb25Db3VudCIsImVxdWFscyIsImxlZnQiLCJyaWdodCIsImNvbXBhcmF0b3IiLCJpZ25vcmVDYXNlIiwidG9Mb3dlckNhc2UiLCJhcnJheSIsImkiLCJwdXNoIiwiQXJyYXkiLCJmcm9tIiwiam9pbiIsImNoYXJzIiwiY2hhbmdlT2JqZWN0cyIsImNvbXBvbmVudHMiLCJuZXh0Q29tcG9uZW50IiwicmV2ZXJzZSIsImNvbXBvbmVudFBvcyIsImNvbXBvbmVudExlbiIsImNvbXBvbmVudCIsInNsaWNlIiwibWFwIiwib2xkVmFsdWUiXSwic291cmNlcyI6WyIuLi8uLi9zcmMvZGlmZi9iYXNlLmpzIl0sInNvdXJjZXNDb250ZW50IjpbImV4cG9ydCBkZWZhdWx0IGZ1bmN0aW9uIERpZmYoKSB7fVxuXG5EaWZmLnByb3RvdHlwZSA9IHtcbiAgZGlmZihvbGRTdHJpbmcsIG5ld1N0cmluZywgb3B0aW9ucyA9IHt9KSB7XG4gICAgbGV0IGNhbGxiYWNrID0gb3B0aW9ucy5jYWxsYmFjaztcbiAgICBpZiAodHlwZW9mIG9wdGlvbnMgPT09ICdmdW5jdGlvbicpIHtcbiAgICAgIGNhbGxiYWNrID0gb3B0aW9ucztcbiAgICAgIG9wdGlvbnMgPSB7fTtcbiAgICB9XG5cbiAgICBsZXQgc2VsZiA9IHRoaXM7XG5cbiAgICBmdW5jdGlvbiBkb25lKHZhbHVlKSB7XG4gICAgICB2YWx1ZSA9IHNlbGYucG9zdFByb2Nlc3ModmFsdWUsIG9wdGlvbnMpO1xuICAgICAgaWYgKGNhbGxiYWNrKSB7XG4gICAgICAgIHNldFRpbWVvdXQoZnVuY3Rpb24oKSB7IGNhbGxiYWNrKHZhbHVlKTsgfSwgMCk7XG4gICAgICAgIHJldHVybiB0cnVlO1xuICAgICAgfSBlbHNlIHtcbiAgICAgICAgcmV0dXJuIHZhbHVlO1xuICAgICAgfVxuICAgIH1cblxuICAgIC8vIEFsbG93IHN1YmNsYXNzZXMgdG8gbWFzc2FnZSB0aGUgaW5wdXQgcHJpb3IgdG8gcnVubmluZ1xuICAgIG9sZFN0cmluZyA9IHRoaXMuY2FzdElucHV0KG9sZFN0cmluZywgb3B0aW9ucyk7XG4gICAgbmV3U3RyaW5nID0gdGhpcy5jYXN0SW5wdXQobmV3U3RyaW5nLCBvcHRpb25zKTtcblxuICAgIG9sZFN0cmluZyA9IHRoaXMucmVtb3ZlRW1wdHkodGhpcy50b2tlbml6ZShvbGRTdHJpbmcsIG9wdGlvbnMpKTtcbiAgICBuZXdTdHJpbmcgPSB0aGlzLnJlbW92ZUVtcHR5KHRoaXMudG9rZW5pemUobmV3U3RyaW5nLCBvcHRpb25zKSk7XG5cbiAgICBsZXQgbmV3TGVuID0gbmV3U3RyaW5nLmxlbmd0aCwgb2xkTGVuID0gb2xkU3RyaW5nLmxlbmd0aDtcbiAgICBsZXQgZWRpdExlbmd0aCA9IDE7XG4gICAgbGV0IG1heEVkaXRMZW5ndGggPSBuZXdMZW4gKyBvbGRMZW47XG4gICAgaWYob3B0aW9ucy5tYXhFZGl0TGVuZ3RoICE9IG51bGwpIHtcbiAgICAgIG1heEVkaXRMZW5ndGggPSBNYXRoLm1pbihtYXhFZGl0TGVuZ3RoLCBvcHRpb25zLm1heEVkaXRMZW5ndGgpO1xuICAgIH1cbiAgICBjb25zdCBtYXhFeGVjdXRpb25UaW1lID0gb3B0aW9ucy50aW1lb3V0ID8/IEluZmluaXR5O1xuICAgIGNvbnN0IGFib3J0QWZ0ZXJUaW1lc3RhbXAgPSBEYXRlLm5vdygpICsgbWF4RXhlY3V0aW9uVGltZTtcblxuICAgIGxldCBiZXN0UGF0aCA9IFt7IG9sZFBvczogLTEsIGxhc3RDb21wb25lbnQ6IHVuZGVmaW5lZCB9XTtcblxuICAgIC8vIFNlZWQgZWRpdExlbmd0aCA9IDAsIGkuZS4gdGhlIGNvbnRlbnQgc3RhcnRzIHdpdGggdGhlIHNhbWUgdmFsdWVzXG4gICAgbGV0IG5ld1BvcyA9IHRoaXMuZXh0cmFjdENvbW1vbihiZXN0UGF0aFswXSwgbmV3U3RyaW5nLCBvbGRTdHJpbmcsIDAsIG9wdGlvbnMpO1xuICAgIGlmIChiZXN0UGF0aFswXS5vbGRQb3MgKyAxID49IG9sZExlbiAmJiBuZXdQb3MgKyAxID49IG5ld0xlbikge1xuICAgICAgLy8gSWRlbnRpdHkgcGVyIHRoZSBlcXVhbGl0eSBhbmQgdG9rZW5pemVyXG4gICAgICByZXR1cm4gZG9uZShidWlsZFZhbHVlcyhzZWxmLCBiZXN0UGF0aFswXS5sYXN0Q29tcG9uZW50LCBuZXdTdHJpbmcsIG9sZFN0cmluZywgc2VsZi51c2VMb25nZXN0VG9rZW4pKTtcbiAgICB9XG5cbiAgICAvLyBPbmNlIHdlIGhpdCB0aGUgcmlnaHQgZWRnZSBvZiB0aGUgZWRpdCBncmFwaCBvbiBzb21lIGRpYWdvbmFsIGssIHdlIGNhblxuICAgIC8vIGRlZmluaXRlbHkgcmVhY2ggdGhlIGVuZCBvZiB0aGUgZWRpdCBncmFwaCBpbiBubyBtb3JlIHRoYW4gayBlZGl0cywgc29cbiAgICAvLyB0aGVyZSdzIG5vIHBvaW50IGluIGNvbnNpZGVyaW5nIGFueSBtb3ZlcyB0byBkaWFnb25hbCBrKzEgYW55IG1vcmUgKGZyb21cbiAgICAvLyB3aGljaCB3ZSdyZSBndWFyYW50ZWVkIHRvIG5lZWQgYXQgbGVhc3QgaysxIG1vcmUgZWRpdHMpLlxuICAgIC8vIFNpbWlsYXJseSwgb25jZSB3ZSd2ZSByZWFjaGVkIHRoZSBib3R0b20gb2YgdGhlIGVkaXQgZ3JhcGgsIHRoZXJlJ3Mgbm9cbiAgICAvLyBwb2ludCBjb25zaWRlcmluZyBtb3ZlcyB0byBsb3dlciBkaWFnb25hbHMuXG4gICAgLy8gV2UgcmVjb3JkIHRoaXMgZmFjdCBieSBzZXR0aW5nIG1pbkRpYWdvbmFsVG9Db25zaWRlciBhbmRcbiAgICAvLyBtYXhEaWFnb25hbFRvQ29uc2lkZXIgdG8gc29tZSBmaW5pdGUgdmFsdWUgb25jZSB3ZSd2ZSBoaXQgdGhlIGVkZ2Ugb2ZcbiAgICAvLyB0aGUgZWRpdCBncmFwaC5cbiAgICAvLyBUaGlzIG9wdGltaXphdGlvbiBpcyBub3QgZmFpdGhmdWwgdG8gdGhlIG9yaWdpbmFsIGFsZ29yaXRobSBwcmVzZW50ZWQgaW5cbiAgICAvLyBNeWVycydzIHBhcGVyLCB3aGljaCBpbnN0ZWFkIHBvaW50bGVzc2x5IGV4dGVuZHMgRC1wYXRocyBvZmYgdGhlIGVuZCBvZlxuICAgIC8vIHRoZSBlZGl0IGdyYXBoIC0gc2VlIHBhZ2UgNyBvZiBNeWVycydzIHBhcGVyIHdoaWNoIG5vdGVzIHRoaXMgcG9pbnRcbiAgICAvLyBleHBsaWNpdGx5IGFuZCBpbGx1c3RyYXRlcyBpdCB3aXRoIGEgZGlhZ3JhbS4gVGhpcyBoYXMgbWFqb3IgcGVyZm9ybWFuY2VcbiAgICAvLyBpbXBsaWNhdGlvbnMgZm9yIHNvbWUgY29tbW9uIHNjZW5hcmlvcy4gRm9yIGluc3RhbmNlLCB0byBjb21wdXRlIGEgZGlmZlxuICAgIC8vIHdoZXJlIHRoZSBuZXcgdGV4dCBzaW1wbHkgYXBwZW5kcyBkIGNoYXJhY3RlcnMgb24gdGhlIGVuZCBvZiB0aGVcbiAgICAvLyBvcmlnaW5hbCB0ZXh0IG9mIGxlbmd0aCBuLCB0aGUgdHJ1ZSBNeWVycyBhbGdvcml0aG0gd2lsbCB0YWtlIE8obitkXjIpXG4gICAgLy8gdGltZSB3aGlsZSB0aGlzIG9wdGltaXphdGlvbiBuZWVkcyBvbmx5IE8obitkKSB0aW1lLlxuICAgIGxldCBtaW5EaWFnb25hbFRvQ29uc2lkZXIgPSAtSW5maW5pdHksIG1heERpYWdvbmFsVG9Db25zaWRlciA9IEluZmluaXR5O1xuXG4gICAgLy8gTWFpbiB3b3JrZXIgbWV0aG9kLiBjaGVja3MgYWxsIHBlcm11dGF0aW9ucyBvZiBhIGdpdmVuIGVkaXQgbGVuZ3RoIGZvciBhY2NlcHRhbmNlLlxuICAgIGZ1bmN0aW9uIGV4ZWNFZGl0TGVuZ3RoKCkge1xuICAgICAgZm9yIChcbiAgICAgICAgbGV0IGRpYWdvbmFsUGF0aCA9IE1hdGgubWF4KG1pbkRpYWdvbmFsVG9Db25zaWRlciwgLWVkaXRMZW5ndGgpO1xuICAgICAgICBkaWFnb25hbFBhdGggPD0gTWF0aC5taW4obWF4RGlhZ29uYWxUb0NvbnNpZGVyLCBlZGl0TGVuZ3RoKTtcbiAgICAgICAgZGlhZ29uYWxQYXRoICs9IDJcbiAgICAgICkge1xuICAgICAgICBsZXQgYmFzZVBhdGg7XG4gICAgICAgIGxldCByZW1vdmVQYXRoID0gYmVzdFBhdGhbZGlhZ29uYWxQYXRoIC0gMV0sXG4gICAgICAgICAgICBhZGRQYXRoID0gYmVzdFBhdGhbZGlhZ29uYWxQYXRoICsgMV07XG4gICAgICAgIGlmIChyZW1vdmVQYXRoKSB7XG4gICAgICAgICAgLy8gTm8gb25lIGVsc2UgaXMgZ29pbmcgdG8gYXR0ZW1wdCB0byB1c2UgdGhpcyB2YWx1ZSwgY2xlYXIgaXRcbiAgICAgICAgICBiZXN0UGF0aFtkaWFnb25hbFBhdGggLSAxXSA9IHVuZGVmaW5lZDtcbiAgICAgICAgfVxuXG4gICAgICAgIGxldCBjYW5BZGQgPSBmYWxzZTtcbiAgICAgICAgaWYgKGFkZFBhdGgpIHtcbiAgICAgICAgICAvLyB3aGF0IG5ld1BvcyB3aWxsIGJlIGFmdGVyIHdlIGRvIGFuIGluc2VydGlvbjpcbiAgICAgICAgICBjb25zdCBhZGRQYXRoTmV3UG9zID0gYWRkUGF0aC5vbGRQb3MgLSBkaWFnb25hbFBhdGg7XG4gICAgICAgICAgY2FuQWRkID0gYWRkUGF0aCAmJiAwIDw9IGFkZFBhdGhOZXdQb3MgJiYgYWRkUGF0aE5ld1BvcyA8IG5ld0xlbjtcbiAgICAgICAgfVxuXG4gICAgICAgIGxldCBjYW5SZW1vdmUgPSByZW1vdmVQYXRoICYmIHJlbW92ZVBhdGgub2xkUG9zICsgMSA8IG9sZExlbjtcbiAgICAgICAgaWYgKCFjYW5BZGQgJiYgIWNhblJlbW92ZSkge1xuICAgICAgICAgIC8vIElmIHRoaXMgcGF0aCBpcyBhIHRlcm1pbmFsIHRoZW4gcHJ1bmVcbiAgICAgICAgICBiZXN0UGF0aFtkaWFnb25hbFBhdGhdID0gdW5kZWZpbmVkO1xuICAgICAgICAgIGNvbnRpbnVlO1xuICAgICAgICB9XG5cbiAgICAgICAgLy8gU2VsZWN0IHRoZSBkaWFnb25hbCB0aGF0IHdlIHdhbnQgdG8gYnJhbmNoIGZyb20uIFdlIHNlbGVjdCB0aGUgcHJpb3JcbiAgICAgICAgLy8gcGF0aCB3aG9zZSBwb3NpdGlvbiBpbiB0aGUgb2xkIHN0cmluZyBpcyB0aGUgZmFydGhlc3QgZnJvbSB0aGUgb3JpZ2luXG4gICAgICAgIC8vIGFuZCBkb2VzIG5vdCBwYXNzIHRoZSBib3VuZHMgb2YgdGhlIGRpZmYgZ3JhcGhcbiAgICAgICAgaWYgKCFjYW5SZW1vdmUgfHwgKGNhbkFkZCAmJiByZW1vdmVQYXRoLm9sZFBvcyA8IGFkZFBhdGgub2xkUG9zKSkge1xuICAgICAgICAgIGJhc2VQYXRoID0gc2VsZi5hZGRUb1BhdGgoYWRkUGF0aCwgdHJ1ZSwgZmFsc2UsIDAsIG9wdGlvbnMpO1xuICAgICAgICB9IGVsc2Uge1xuICAgICAgICAgIGJhc2VQYXRoID0gc2VsZi5hZGRUb1BhdGgocmVtb3ZlUGF0aCwgZmFsc2UsIHRydWUsIDEsIG9wdGlvbnMpO1xuICAgICAgICB9XG5cbiAgICAgICAgbmV3UG9zID0gc2VsZi5leHRyYWN0Q29tbW9uKGJhc2VQYXRoLCBuZXdTdHJpbmcsIG9sZFN0cmluZywgZGlhZ29uYWxQYXRoLCBvcHRpb25zKTtcblxuICAgICAgICBpZiAoYmFzZVBhdGgub2xkUG9zICsgMSA+PSBvbGRMZW4gJiYgbmV3UG9zICsgMSA+PSBuZXdMZW4pIHtcbiAgICAgICAgICAvLyBJZiB3ZSBoYXZlIGhpdCB0aGUgZW5kIG9mIGJvdGggc3RyaW5ncywgdGhlbiB3ZSBhcmUgZG9uZVxuICAgICAgICAgIHJldHVybiBkb25lKGJ1aWxkVmFsdWVzKHNlbGYsIGJhc2VQYXRoLmxhc3RDb21wb25lbnQsIG5ld1N0cmluZywgb2xkU3RyaW5nLCBzZWxmLnVzZUxvbmdlc3RUb2tlbikpO1xuICAgICAgICB9IGVsc2Uge1xuICAgICAgICAgIGJlc3RQYXRoW2RpYWdvbmFsUGF0aF0gPSBiYXNlUGF0aDtcbiAgICAgICAgICBpZiAoYmFzZVBhdGgub2xkUG9zICsgMSA+PSBvbGRMZW4pIHtcbiAgICAgICAgICAgIG1heERpYWdvbmFsVG9Db25zaWRlciA9IE1hdGgubWluKG1heERpYWdvbmFsVG9Db25zaWRlciwgZGlhZ29uYWxQYXRoIC0gMSk7XG4gICAgICAgICAgfVxuICAgICAgICAgIGlmIChuZXdQb3MgKyAxID49IG5ld0xlbikge1xuICAgICAgICAgICAgbWluRGlhZ29uYWxUb0NvbnNpZGVyID0gTWF0aC5tYXgobWluRGlhZ29uYWxUb0NvbnNpZGVyLCBkaWFnb25hbFBhdGggKyAxKTtcbiAgICAgICAgICB9XG4gICAgICAgIH1cbiAgICAgIH1cblxuICAgICAgZWRpdExlbmd0aCsrO1xuICAgIH1cblxuICAgIC8vIFBlcmZvcm1zIHRoZSBsZW5ndGggb2YgZWRpdCBpdGVyYXRpb24uIElzIGEgYml0IGZ1Z2x5IGFzIHRoaXMgaGFzIHRvIHN1cHBvcnQgdGhlXG4gICAgLy8gc3luYyBhbmQgYXN5bmMgbW9kZSB3aGljaCBpcyBuZXZlciBmdW4uIExvb3BzIG92ZXIgZXhlY0VkaXRMZW5ndGggdW50aWwgYSB2YWx1ZVxuICAgIC8vIGlzIHByb2R1Y2VkLCBvciB1bnRpbCB0aGUgZWRpdCBsZW5ndGggZXhjZWVkcyBvcHRpb25zLm1heEVkaXRMZW5ndGggKGlmIGdpdmVuKSxcbiAgICAvLyBpbiB3aGljaCBjYXNlIGl0IHdpbGwgcmV0dXJuIHVuZGVmaW5lZC5cbiAgICBpZiAoY2FsbGJhY2spIHtcbiAgICAgIChmdW5jdGlvbiBleGVjKCkge1xuICAgICAgICBzZXRUaW1lb3V0KGZ1bmN0aW9uKCkge1xuICAgICAgICAgIGlmIChlZGl0TGVuZ3RoID4gbWF4RWRpdExlbmd0aCB8fCBEYXRlLm5vdygpID4gYWJvcnRBZnRlclRpbWVzdGFtcCkge1xuICAgICAgICAgICAgcmV0dXJuIGNhbGxiYWNrKCk7XG4gICAgICAgICAgfVxuXG4gICAgICAgICAgaWYgKCFleGVjRWRpdExlbmd0aCgpKSB7XG4gICAgICAgICAgICBleGVjKCk7XG4gICAgICAgICAgfVxuICAgICAgICB9LCAwKTtcbiAgICAgIH0oKSk7XG4gICAgfSBlbHNlIHtcbiAgICAgIHdoaWxlIChlZGl0TGVuZ3RoIDw9IG1heEVkaXRMZW5ndGggJiYgRGF0ZS5ub3coKSA8PSBhYm9ydEFmdGVyVGltZXN0YW1wKSB7XG4gICAgICAgIGxldCByZXQgPSBleGVjRWRpdExlbmd0aCgpO1xuICAgICAgICBpZiAocmV0KSB7XG4gICAgICAgICAgcmV0dXJuIHJldDtcbiAgICAgICAgfVxuICAgICAgfVxuICAgIH1cbiAgfSxcblxuICBhZGRUb1BhdGgocGF0aCwgYWRkZWQsIHJlbW92ZWQsIG9sZFBvc0luYywgb3B0aW9ucykge1xuICAgIGxldCBsYXN0ID0gcGF0aC5sYXN0Q29tcG9uZW50O1xuICAgIGlmIChsYXN0ICYmICFvcHRpb25zLm9uZUNoYW5nZVBlclRva2VuICYmIGxhc3QuYWRkZWQgPT09IGFkZGVkICYmIGxhc3QucmVtb3ZlZCA9PT0gcmVtb3ZlZCkge1xuICAgICAgcmV0dXJuIHtcbiAgICAgICAgb2xkUG9zOiBwYXRoLm9sZFBvcyArIG9sZFBvc0luYyxcbiAgICAgICAgbGFzdENvbXBvbmVudDoge2NvdW50OiBsYXN0LmNvdW50ICsgMSwgYWRkZWQ6IGFkZGVkLCByZW1vdmVkOiByZW1vdmVkLCBwcmV2aW91c0NvbXBvbmVudDogbGFzdC5wcmV2aW91c0NvbXBvbmVudCB9XG4gICAgICB9O1xuICAgIH0gZWxzZSB7XG4gICAgICByZXR1cm4ge1xuICAgICAgICBvbGRQb3M6IHBhdGgub2xkUG9zICsgb2xkUG9zSW5jLFxuICAgICAgICBsYXN0Q29tcG9uZW50OiB7Y291bnQ6IDEsIGFkZGVkOiBhZGRlZCwgcmVtb3ZlZDogcmVtb3ZlZCwgcHJldmlvdXNDb21wb25lbnQ6IGxhc3QgfVxuICAgICAgfTtcbiAgICB9XG4gIH0sXG4gIGV4dHJhY3RDb21tb24oYmFzZVBhdGgsIG5ld1N0cmluZywgb2xkU3RyaW5nLCBkaWFnb25hbFBhdGgsIG9wdGlvbnMpIHtcbiAgICBsZXQgbmV3TGVuID0gbmV3U3RyaW5nLmxlbmd0aCxcbiAgICAgICAgb2xkTGVuID0gb2xkU3RyaW5nLmxlbmd0aCxcbiAgICAgICAgb2xkUG9zID0gYmFzZVBhdGgub2xkUG9zLFxuICAgICAgICBuZXdQb3MgPSBvbGRQb3MgLSBkaWFnb25hbFBhdGgsXG5cbiAgICAgICAgY29tbW9uQ291bnQgPSAwO1xuICAgIHdoaWxlIChuZXdQb3MgKyAxIDwgbmV3TGVuICYmIG9sZFBvcyArIDEgPCBvbGRMZW4gJiYgdGhpcy5lcXVhbHMob2xkU3RyaW5nW29sZFBvcyArIDFdLCBuZXdTdHJpbmdbbmV3UG9zICsgMV0sIG9wdGlvbnMpKSB7XG4gICAgICBuZXdQb3MrKztcbiAgICAgIG9sZFBvcysrO1xuICAgICAgY29tbW9uQ291bnQrKztcbiAgICAgIGlmIChvcHRpb25zLm9uZUNoYW5nZVBlclRva2VuKSB7XG4gICAgICAgIGJhc2VQYXRoLmxhc3RDb21wb25lbnQgPSB7Y291bnQ6IDEsIHByZXZpb3VzQ29tcG9uZW50OiBiYXNlUGF0aC5sYXN0Q29tcG9uZW50LCBhZGRlZDogZmFsc2UsIHJlbW92ZWQ6IGZhbHNlfTtcbiAgICAgIH1cbiAgICB9XG5cbiAgICBpZiAoY29tbW9uQ291bnQgJiYgIW9wdGlvbnMub25lQ2hhbmdlUGVyVG9rZW4pIHtcbiAgICAgIGJhc2VQYXRoLmxhc3RDb21wb25lbnQgPSB7Y291bnQ6IGNvbW1vbkNvdW50LCBwcmV2aW91c0NvbXBvbmVudDogYmFzZVBhdGgubGFzdENvbXBvbmVudCwgYWRkZWQ6IGZhbHNlLCByZW1vdmVkOiBmYWxzZX07XG4gICAgfVxuXG4gICAgYmFzZVBhdGgub2xkUG9zID0gb2xkUG9zO1xuICAgIHJldHVybiBuZXdQb3M7XG4gIH0sXG5cbiAgZXF1YWxzKGxlZnQsIHJpZ2h0LCBvcHRpb25zKSB7XG4gICAgaWYgKG9wdGlvbnMuY29tcGFyYXRvcikge1xuICAgICAgcmV0dXJuIG9wdGlvbnMuY29tcGFyYXRvcihsZWZ0LCByaWdodCk7XG4gICAgfSBlbHNlIHtcbiAgICAgIHJldHVybiBsZWZ0ID09PSByaWdodFxuICAgICAgICB8fCAob3B0aW9ucy5pZ25vcmVDYXNlICYmIGxlZnQudG9Mb3dlckNhc2UoKSA9PT0gcmlnaHQudG9Mb3dlckNhc2UoKSk7XG4gICAgfVxuICB9LFxuICByZW1vdmVFbXB0eShhcnJheSkge1xuICAgIGxldCByZXQgPSBbXTtcbiAgICBmb3IgKGxldCBpID0gMDsgaSA8IGFycmF5Lmxlbmd0aDsgaSsrKSB7XG4gICAgICBpZiAoYXJyYXlbaV0pIHtcbiAgICAgICAgcmV0LnB1c2goYXJyYXlbaV0pO1xuICAgICAgfVxuICAgIH1cbiAgICByZXR1cm4gcmV0O1xuICB9LFxuICBjYXN0SW5wdXQodmFsdWUpIHtcbiAgICByZXR1cm4gdmFsdWU7XG4gIH0sXG4gIHRva2VuaXplKHZhbHVlKSB7XG4gICAgcmV0dXJuIEFycmF5LmZyb20odmFsdWUpO1xuICB9LFxuICBqb2luKGNoYXJzKSB7XG4gICAgcmV0dXJuIGNoYXJzLmpvaW4oJycpO1xuICB9LFxuICBwb3N0UHJvY2VzcyhjaGFuZ2VPYmplY3RzKSB7XG4gICAgcmV0dXJuIGNoYW5nZU9iamVjdHM7XG4gIH1cbn07XG5cbmZ1bmN0aW9uIGJ1aWxkVmFsdWVzKGRpZmYsIGxhc3RDb21wb25lbnQsIG5ld1N0cmluZywgb2xkU3RyaW5nLCB1c2VMb25nZXN0VG9rZW4pIHtcbiAgLy8gRmlyc3Qgd2UgY29udmVydCBvdXIgbGlua2VkIGxpc3Qgb2YgY29tcG9uZW50cyBpbiByZXZlcnNlIG9yZGVyIHRvIGFuXG4gIC8vIGFycmF5IGluIHRoZSByaWdodCBvcmRlcjpcbiAgY29uc3QgY29tcG9uZW50cyA9IFtdO1xuICBsZXQgbmV4dENvbXBvbmVudDtcbiAgd2hpbGUgKGxhc3RDb21wb25lbnQpIHtcbiAgICBjb21wb25lbnRzLnB1c2gobGFzdENvbXBvbmVudCk7XG4gICAgbmV4dENvbXBvbmVudCA9IGxhc3RDb21wb25lbnQucHJldmlvdXNDb21wb25lbnQ7XG4gICAgZGVsZXRlIGxhc3RDb21wb25lbnQucHJldmlvdXNDb21wb25lbnQ7XG4gICAgbGFzdENvbXBvbmVudCA9IG5leHRDb21wb25lbnQ7XG4gIH1cbiAgY29tcG9uZW50cy5yZXZlcnNlKCk7XG5cbiAgbGV0IGNvbXBvbmVudFBvcyA9IDAsXG4gICAgICBjb21wb25lbnRMZW4gPSBjb21wb25lbnRzLmxlbmd0aCxcbiAgICAgIG5ld1BvcyA9IDAsXG4gICAgICBvbGRQb3MgPSAwO1xuXG4gIGZvciAoOyBjb21wb25lbnRQb3MgPCBjb21wb25lbnRMZW47IGNvbXBvbmVudFBvcysrKSB7XG4gICAgbGV0IGNvbXBvbmVudCA9IGNvbXBvbmVudHNbY29tcG9uZW50UG9zXTtcbiAgICBpZiAoIWNvbXBvbmVudC5yZW1vdmVkKSB7XG4gICAgICBpZiAoIWNvbXBvbmVudC5hZGRlZCAmJiB1c2VMb25nZXN0VG9rZW4pIHtcbiAgICAgICAgbGV0IHZhbHVlID0gbmV3U3RyaW5nLnNsaWNlKG5ld1BvcywgbmV3UG9zICsgY29tcG9uZW50LmNvdW50KTtcbiAgICAgICAgdmFsdWUgPSB2YWx1ZS5tYXAoZnVuY3Rpb24odmFsdWUsIGkpIHtcbiAgICAgICAgICBsZXQgb2xkVmFsdWUgPSBvbGRTdHJpbmdbb2xkUG9zICsgaV07XG4gICAgICAgICAgcmV0dXJuIG9sZFZhbHVlLmxlbmd0aCA+IHZhbHVlLmxlbmd0aCA/IG9sZFZhbHVlIDogdmFsdWU7XG4gICAgICAgIH0pO1xuXG4gICAgICAgIGNvbXBvbmVudC52YWx1ZSA9IGRpZmYuam9pbih2YWx1ZSk7XG4gICAgICB9IGVsc2Uge1xuICAgICAgICBjb21wb25lbnQudmFsdWUgPSBkaWZmLmpvaW4obmV3U3RyaW5nLnNsaWNlKG5ld1BvcywgbmV3UG9zICsgY29tcG9uZW50LmNvdW50KSk7XG4gICAgICB9XG4gICAgICBuZXdQb3MgKz0gY29tcG9uZW50LmNvdW50O1xuXG4gICAgICAvLyBDb21tb24gY2FzZVxuICAgICAgaWYgKCFjb21wb25lbnQuYWRkZWQpIHtcbiAgICAgICAgb2xkUG9zICs9IGNvbXBvbmVudC5jb3VudDtcbiAgICAgIH1cbiAgICB9IGVsc2Uge1xuICAgICAgY29tcG9uZW50LnZhbHVlID0gZGlmZi5qb2luKG9sZFN0cmluZy5zbGljZShvbGRQb3MsIG9sZFBvcyArIGNvbXBvbmVudC5jb3VudCkpO1xuICAgICAgb2xkUG9zICs9IGNvbXBvbmVudC5jb3VudDtcbiAgICB9XG4gIH1cblxuICByZXR1cm4gY29tcG9uZW50cztcbn1cbiJdLCJtYXBwaW5ncyI6Ijs7Ozs7Ozs7QUFBZSxTQUFTQSxJQUFJQSxDQUFBLEVBQUcsQ0FBQztBQUVoQ0EsSUFBSSxDQUFDQyxTQUFTLEdBQUc7RUFBQTtFQUFBO0VBQ2ZDLElBQUksV0FBQUEsS0FBQ0MsU0FBUyxFQUFFQyxTQUFTLEVBQWdCO0lBQUE7SUFBQSxJQUFBQyxnQkFBQTtJQUFBO0lBQUE7SUFBZEMsT0FBTyxHQUFBQyxTQUFBLENBQUFDLE1BQUEsUUFBQUQsU0FBQSxRQUFBRSxTQUFBLEdBQUFGLFNBQUEsTUFBRyxDQUFDLENBQUM7SUFDckMsSUFBSUcsUUFBUSxHQUFHSixPQUFPLENBQUNJLFFBQVE7SUFDL0IsSUFBSSxPQUFPSixPQUFPLEtBQUssVUFBVSxFQUFFO01BQ2pDSSxRQUFRLEdBQUdKLE9BQU87TUFDbEJBLE9BQU8sR0FBRyxDQUFDLENBQUM7SUFDZDtJQUVBLElBQUlLLElBQUksR0FBRyxJQUFJO0lBRWYsU0FBU0MsSUFBSUEsQ0FBQ0MsS0FBSyxFQUFFO01BQ25CQSxLQUFLLEdBQUdGLElBQUksQ0FBQ0csV0FBVyxDQUFDRCxLQUFLLEVBQUVQLE9BQU8sQ0FBQztNQUN4QyxJQUFJSSxRQUFRLEVBQUU7UUFDWkssVUFBVSxDQUFDLFlBQVc7VUFBRUwsUUFBUSxDQUFDRyxLQUFLLENBQUM7UUFBRSxDQUFDLEVBQUUsQ0FBQyxDQUFDO1FBQzlDLE9BQU8sSUFBSTtNQUNiLENBQUMsTUFBTTtRQUNMLE9BQU9BLEtBQUs7TUFDZDtJQUNGOztJQUVBO0lBQ0FWLFNBQVMsR0FBRyxJQUFJLENBQUNhLFNBQVMsQ0FBQ2IsU0FBUyxFQUFFRyxPQUFPLENBQUM7SUFDOUNGLFNBQVMsR0FBRyxJQUFJLENBQUNZLFNBQVMsQ0FBQ1osU0FBUyxFQUFFRSxPQUFPLENBQUM7SUFFOUNILFNBQVMsR0FBRyxJQUFJLENBQUNjLFdBQVcsQ0FBQyxJQUFJLENBQUNDLFFBQVEsQ0FBQ2YsU0FBUyxFQUFFRyxPQUFPLENBQUMsQ0FBQztJQUMvREYsU0FBUyxHQUFHLElBQUksQ0FBQ2EsV0FBVyxDQUFDLElBQUksQ0FBQ0MsUUFBUSxDQUFDZCxTQUFTLEVBQUVFLE9BQU8sQ0FBQyxDQUFDO0lBRS9ELElBQUlhLE1BQU0sR0FBR2YsU0FBUyxDQUFDSSxNQUFNO01BQUVZLE1BQU0sR0FBR2pCLFNBQVMsQ0FBQ0ssTUFBTTtJQUN4RCxJQUFJYSxVQUFVLEdBQUcsQ0FBQztJQUNsQixJQUFJQyxhQUFhLEdBQUdILE1BQU0sR0FBR0MsTUFBTTtJQUNuQyxJQUFHZCxPQUFPLENBQUNnQixhQUFhLElBQUksSUFBSSxFQUFFO01BQ2hDQSxhQUFhLEdBQUdDLElBQUksQ0FBQ0MsR0FBRyxDQUFDRixhQUFhLEVBQUVoQixPQUFPLENBQUNnQixhQUFhLENBQUM7SUFDaEU7SUFDQSxJQUFNRyxnQkFBZ0I7SUFBQTtJQUFBLENBQUFwQixnQkFBQTtJQUFBO0lBQUdDLE9BQU8sQ0FBQ29CLE9BQU8sY0FBQXJCLGdCQUFBLGNBQUFBLGdCQUFBLEdBQUlzQixRQUFRO0lBQ3BELElBQU1DLG1CQUFtQixHQUFHQyxJQUFJLENBQUNDLEdBQUcsQ0FBQyxDQUFDLEdBQUdMLGdCQUFnQjtJQUV6RCxJQUFJTSxRQUFRLEdBQUcsQ0FBQztNQUFFQyxNQUFNLEVBQUUsQ0FBQyxDQUFDO01BQUVDLGFBQWEsRUFBRXhCO0lBQVUsQ0FBQyxDQUFDOztJQUV6RDtJQUNBLElBQUl5QixNQUFNLEdBQUcsSUFBSSxDQUFDQyxhQUFhLENBQUNKLFFBQVEsQ0FBQyxDQUFDLENBQUMsRUFBRTNCLFNBQVMsRUFBRUQsU0FBUyxFQUFFLENBQUMsRUFBRUcsT0FBTyxDQUFDO0lBQzlFLElBQUl5QixRQUFRLENBQUMsQ0FBQyxDQUFDLENBQUNDLE1BQU0sR0FBRyxDQUFDLElBQUlaLE1BQU0sSUFBSWMsTUFBTSxHQUFHLENBQUMsSUFBSWYsTUFBTSxFQUFFO01BQzVEO01BQ0EsT0FBT1AsSUFBSSxDQUFDd0IsV0FBVyxDQUFDekIsSUFBSSxFQUFFb0IsUUFBUSxDQUFDLENBQUMsQ0FBQyxDQUFDRSxhQUFhLEVBQUU3QixTQUFTLEVBQUVELFNBQVMsRUFBRVEsSUFBSSxDQUFDMEIsZUFBZSxDQUFDLENBQUM7SUFDdkc7O0lBRUE7SUFDQTtJQUNBO0lBQ0E7SUFDQTtJQUNBO0lBQ0E7SUFDQTtJQUNBO0lBQ0E7SUFDQTtJQUNBO0lBQ0E7SUFDQTtJQUNBO0lBQ0E7SUFDQTtJQUNBLElBQUlDLHFCQUFxQixHQUFHLENBQUNYLFFBQVE7TUFBRVkscUJBQXFCLEdBQUdaLFFBQVE7O0lBRXZFO0lBQ0EsU0FBU2EsY0FBY0EsQ0FBQSxFQUFHO01BQ3hCLEtBQ0UsSUFBSUMsWUFBWSxHQUFHbEIsSUFBSSxDQUFDbUIsR0FBRyxDQUFDSixxQkFBcUIsRUFBRSxDQUFDakIsVUFBVSxDQUFDLEVBQy9Eb0IsWUFBWSxJQUFJbEIsSUFBSSxDQUFDQyxHQUFHLENBQUNlLHFCQUFxQixFQUFFbEIsVUFBVSxDQUFDLEVBQzNEb0IsWUFBWSxJQUFJLENBQUMsRUFDakI7UUFDQSxJQUFJRSxRQUFRO1FBQUE7UUFBQTtRQUFBO1FBQUE7UUFDWixJQUFJQyxVQUFVLEdBQUdiLFFBQVEsQ0FBQ1UsWUFBWSxHQUFHLENBQUMsQ0FBQztVQUN2Q0ksT0FBTyxHQUFHZCxRQUFRLENBQUNVLFlBQVksR0FBRyxDQUFDLENBQUM7UUFDeEMsSUFBSUcsVUFBVSxFQUFFO1VBQ2Q7VUFDQWIsUUFBUSxDQUFDVSxZQUFZLEdBQUcsQ0FBQyxDQUFDLEdBQUdoQyxTQUFTO1FBQ3hDO1FBRUEsSUFBSXFDLE1BQU0sR0FBRyxLQUFLO1FBQ2xCLElBQUlELE9BQU8sRUFBRTtVQUNYO1VBQ0EsSUFBTUUsYUFBYSxHQUFHRixPQUFPLENBQUNiLE1BQU0sR0FBR1MsWUFBWTtVQUNuREssTUFBTSxHQUFHRCxPQUFPLElBQUksQ0FBQyxJQUFJRSxhQUFhLElBQUlBLGFBQWEsR0FBRzVCLE1BQU07UUFDbEU7UUFFQSxJQUFJNkIsU0FBUyxHQUFHSixVQUFVLElBQUlBLFVBQVUsQ0FBQ1osTUFBTSxHQUFHLENBQUMsR0FBR1osTUFBTTtRQUM1RCxJQUFJLENBQUMwQixNQUFNLElBQUksQ0FBQ0UsU0FBUyxFQUFFO1VBQ3pCO1VBQ0FqQixRQUFRLENBQUNVLFlBQVksQ0FBQyxHQUFHaEMsU0FBUztVQUNsQztRQUNGOztRQUVBO1FBQ0E7UUFDQTtRQUNBLElBQUksQ0FBQ3VDLFNBQVMsSUFBS0YsTUFBTSxJQUFJRixVQUFVLENBQUNaLE1BQU0sR0FBR2EsT0FBTyxDQUFDYixNQUFPLEVBQUU7VUFDaEVXLFFBQVEsR0FBR2hDLElBQUksQ0FBQ3NDLFNBQVMsQ0FBQ0osT0FBTyxFQUFFLElBQUksRUFBRSxLQUFLLEVBQUUsQ0FBQyxFQUFFdkMsT0FBTyxDQUFDO1FBQzdELENBQUMsTUFBTTtVQUNMcUMsUUFBUSxHQUFHaEMsSUFBSSxDQUFDc0MsU0FBUyxDQUFDTCxVQUFVLEVBQUUsS0FBSyxFQUFFLElBQUksRUFBRSxDQUFDLEVBQUV0QyxPQUFPLENBQUM7UUFDaEU7UUFFQTRCLE1BQU0sR0FBR3ZCLElBQUksQ0FBQ3dCLGFBQWEsQ0FBQ1EsUUFBUSxFQUFFdkMsU0FBUyxFQUFFRCxTQUFTLEVBQUVzQyxZQUFZLEVBQUVuQyxPQUFPLENBQUM7UUFFbEYsSUFBSXFDLFFBQVEsQ0FBQ1gsTUFBTSxHQUFHLENBQUMsSUFBSVosTUFBTSxJQUFJYyxNQUFNLEdBQUcsQ0FBQyxJQUFJZixNQUFNLEVBQUU7VUFDekQ7VUFDQSxPQUFPUCxJQUFJLENBQUN3QixXQUFXLENBQUN6QixJQUFJLEVBQUVnQyxRQUFRLENBQUNWLGFBQWEsRUFBRTdCLFNBQVMsRUFBRUQsU0FBUyxFQUFFUSxJQUFJLENBQUMwQixlQUFlLENBQUMsQ0FBQztRQUNwRyxDQUFDLE1BQU07VUFDTE4sUUFBUSxDQUFDVSxZQUFZLENBQUMsR0FBR0UsUUFBUTtVQUNqQyxJQUFJQSxRQUFRLENBQUNYLE1BQU0sR0FBRyxDQUFDLElBQUlaLE1BQU0sRUFBRTtZQUNqQ21CLHFCQUFxQixHQUFHaEIsSUFBSSxDQUFDQyxHQUFHLENBQUNlLHFCQUFxQixFQUFFRSxZQUFZLEdBQUcsQ0FBQyxDQUFDO1VBQzNFO1VBQ0EsSUFBSVAsTUFBTSxHQUFHLENBQUMsSUFBSWYsTUFBTSxFQUFFO1lBQ3hCbUIscUJBQXFCLEdBQUdmLElBQUksQ0FBQ21CLEdBQUcsQ0FBQ0oscUJBQXFCLEVBQUVHLFlBQVksR0FBRyxDQUFDLENBQUM7VUFDM0U7UUFDRjtNQUNGO01BRUFwQixVQUFVLEVBQUU7SUFDZDs7SUFFQTtJQUNBO0lBQ0E7SUFDQTtJQUNBLElBQUlYLFFBQVEsRUFBRTtNQUNYLFVBQVN3QyxJQUFJQSxDQUFBLEVBQUc7UUFDZm5DLFVBQVUsQ0FBQyxZQUFXO1VBQ3BCLElBQUlNLFVBQVUsR0FBR0MsYUFBYSxJQUFJTyxJQUFJLENBQUNDLEdBQUcsQ0FBQyxDQUFDLEdBQUdGLG1CQUFtQixFQUFFO1lBQ2xFLE9BQU9sQixRQUFRLENBQUMsQ0FBQztVQUNuQjtVQUVBLElBQUksQ0FBQzhCLGNBQWMsQ0FBQyxDQUFDLEVBQUU7WUFDckJVLElBQUksQ0FBQyxDQUFDO1VBQ1I7UUFDRixDQUFDLEVBQUUsQ0FBQyxDQUFDO01BQ1AsQ0FBQyxFQUFDLENBQUM7SUFDTCxDQUFDLE1BQU07TUFDTCxPQUFPN0IsVUFBVSxJQUFJQyxhQUFhLElBQUlPLElBQUksQ0FBQ0MsR0FBRyxDQUFDLENBQUMsSUFBSUYsbUJBQW1CLEVBQUU7UUFDdkUsSUFBSXVCLEdBQUcsR0FBR1gsY0FBYyxDQUFDLENBQUM7UUFDMUIsSUFBSVcsR0FBRyxFQUFFO1VBQ1AsT0FBT0EsR0FBRztRQUNaO01BQ0Y7SUFDRjtFQUNGLENBQUM7RUFBQTtFQUFBO0VBRURGLFNBQVMsV0FBQUEsVUFBQ0csSUFBSSxFQUFFQyxLQUFLLEVBQUVDLE9BQU8sRUFBRUMsU0FBUyxFQUFFakQsT0FBTyxFQUFFO0lBQ2xELElBQUlrRCxJQUFJLEdBQUdKLElBQUksQ0FBQ25CLGFBQWE7SUFDN0IsSUFBSXVCLElBQUksSUFBSSxDQUFDbEQsT0FBTyxDQUFDbUQsaUJBQWlCLElBQUlELElBQUksQ0FBQ0gsS0FBSyxLQUFLQSxLQUFLLElBQUlHLElBQUksQ0FBQ0YsT0FBTyxLQUFLQSxPQUFPLEVBQUU7TUFDMUYsT0FBTztRQUNMdEIsTUFBTSxFQUFFb0IsSUFBSSxDQUFDcEIsTUFBTSxHQUFHdUIsU0FBUztRQUMvQnRCLGFBQWEsRUFBRTtVQUFDeUIsS0FBSyxFQUFFRixJQUFJLENBQUNFLEtBQUssR0FBRyxDQUFDO1VBQUVMLEtBQUssRUFBRUEsS0FBSztVQUFFQyxPQUFPLEVBQUVBLE9BQU87VUFBRUssaUJBQWlCLEVBQUVILElBQUksQ0FBQ0c7UUFBa0I7TUFDbkgsQ0FBQztJQUNILENBQUMsTUFBTTtNQUNMLE9BQU87UUFDTDNCLE1BQU0sRUFBRW9CLElBQUksQ0FBQ3BCLE1BQU0sR0FBR3VCLFNBQVM7UUFDL0J0QixhQUFhLEVBQUU7VUFBQ3lCLEtBQUssRUFBRSxDQUFDO1VBQUVMLEtBQUssRUFBRUEsS0FBSztVQUFFQyxPQUFPLEVBQUVBLE9BQU87VUFBRUssaUJBQWlCLEVBQUVIO1FBQUs7TUFDcEYsQ0FBQztJQUNIO0VBQ0YsQ0FBQztFQUFBO0VBQUE7RUFDRHJCLGFBQWEsV0FBQUEsY0FBQ1EsUUFBUSxFQUFFdkMsU0FBUyxFQUFFRCxTQUFTLEVBQUVzQyxZQUFZLEVBQUVuQyxPQUFPLEVBQUU7SUFDbkUsSUFBSWEsTUFBTSxHQUFHZixTQUFTLENBQUNJLE1BQU07TUFDekJZLE1BQU0sR0FBR2pCLFNBQVMsQ0FBQ0ssTUFBTTtNQUN6QndCLE1BQU0sR0FBR1csUUFBUSxDQUFDWCxNQUFNO01BQ3hCRSxNQUFNLEdBQUdGLE1BQU0sR0FBR1MsWUFBWTtNQUU5Qm1CLFdBQVcsR0FBRyxDQUFDO0lBQ25CLE9BQU8xQixNQUFNLEdBQUcsQ0FBQyxHQUFHZixNQUFNLElBQUlhLE1BQU0sR0FBRyxDQUFDLEdBQUdaLE1BQU0sSUFBSSxJQUFJLENBQUN5QyxNQUFNLENBQUMxRCxTQUFTLENBQUM2QixNQUFNLEdBQUcsQ0FBQyxDQUFDLEVBQUU1QixTQUFTLENBQUM4QixNQUFNLEdBQUcsQ0FBQyxDQUFDLEVBQUU1QixPQUFPLENBQUMsRUFBRTtNQUN2SDRCLE1BQU0sRUFBRTtNQUNSRixNQUFNLEVBQUU7TUFDUjRCLFdBQVcsRUFBRTtNQUNiLElBQUl0RCxPQUFPLENBQUNtRCxpQkFBaUIsRUFBRTtRQUM3QmQsUUFBUSxDQUFDVixhQUFhLEdBQUc7VUFBQ3lCLEtBQUssRUFBRSxDQUFDO1VBQUVDLGlCQUFpQixFQUFFaEIsUUFBUSxDQUFDVixhQUFhO1VBQUVvQixLQUFLLEVBQUUsS0FBSztVQUFFQyxPQUFPLEVBQUU7UUFBSyxDQUFDO01BQzlHO0lBQ0Y7SUFFQSxJQUFJTSxXQUFXLElBQUksQ0FBQ3RELE9BQU8sQ0FBQ21ELGlCQUFpQixFQUFFO01BQzdDZCxRQUFRLENBQUNWLGFBQWEsR0FBRztRQUFDeUIsS0FBSyxFQUFFRSxXQUFXO1FBQUVELGlCQUFpQixFQUFFaEIsUUFBUSxDQUFDVixhQUFhO1FBQUVvQixLQUFLLEVBQUUsS0FBSztRQUFFQyxPQUFPLEVBQUU7TUFBSyxDQUFDO0lBQ3hIO0lBRUFYLFFBQVEsQ0FBQ1gsTUFBTSxHQUFHQSxNQUFNO0lBQ3hCLE9BQU9FLE1BQU07RUFDZixDQUFDO0VBQUE7RUFBQTtFQUVEMkIsTUFBTSxXQUFBQSxPQUFDQyxJQUFJLEVBQUVDLEtBQUssRUFBRXpELE9BQU8sRUFBRTtJQUMzQixJQUFJQSxPQUFPLENBQUMwRCxVQUFVLEVBQUU7TUFDdEIsT0FBTzFELE9BQU8sQ0FBQzBELFVBQVUsQ0FBQ0YsSUFBSSxFQUFFQyxLQUFLLENBQUM7SUFDeEMsQ0FBQyxNQUFNO01BQ0wsT0FBT0QsSUFBSSxLQUFLQyxLQUFLLElBQ2Z6RCxPQUFPLENBQUMyRCxVQUFVLElBQUlILElBQUksQ0FBQ0ksV0FBVyxDQUFDLENBQUMsS0FBS0gsS0FBSyxDQUFDRyxXQUFXLENBQUMsQ0FBRTtJQUN6RTtFQUNGLENBQUM7RUFBQTtFQUFBO0VBQ0RqRCxXQUFXLFdBQUFBLFlBQUNrRCxLQUFLLEVBQUU7SUFDakIsSUFBSWhCLEdBQUcsR0FBRyxFQUFFO0lBQ1osS0FBSyxJQUFJaUIsQ0FBQyxHQUFHLENBQUMsRUFBRUEsQ0FBQyxHQUFHRCxLQUFLLENBQUMzRCxNQUFNLEVBQUU0RCxDQUFDLEVBQUUsRUFBRTtNQUNyQyxJQUFJRCxLQUFLLENBQUNDLENBQUMsQ0FBQyxFQUFFO1FBQ1pqQixHQUFHLENBQUNrQixJQUFJLENBQUNGLEtBQUssQ0FBQ0MsQ0FBQyxDQUFDLENBQUM7TUFDcEI7SUFDRjtJQUNBLE9BQU9qQixHQUFHO0VBQ1osQ0FBQztFQUFBO0VBQUE7RUFDRG5DLFNBQVMsV0FBQUEsVUFBQ0gsS0FBSyxFQUFFO0lBQ2YsT0FBT0EsS0FBSztFQUNkLENBQUM7RUFBQTtFQUFBO0VBQ0RLLFFBQVEsV0FBQUEsU0FBQ0wsS0FBSyxFQUFFO0lBQ2QsT0FBT3lELEtBQUssQ0FBQ0MsSUFBSSxDQUFDMUQsS0FBSyxDQUFDO0VBQzFCLENBQUM7RUFBQTtFQUFBO0VBQ0QyRCxJQUFJLFdBQUFBLEtBQUNDLEtBQUssRUFBRTtJQUNWLE9BQU9BLEtBQUssQ0FBQ0QsSUFBSSxDQUFDLEVBQUUsQ0FBQztFQUN2QixDQUFDO0VBQUE7RUFBQTtFQUNEMUQsV0FBVyxXQUFBQSxZQUFDNEQsYUFBYSxFQUFFO0lBQ3pCLE9BQU9BLGFBQWE7RUFDdEI7QUFDRixDQUFDO0FBRUQsU0FBU3RDLFdBQVdBLENBQUNsQyxJQUFJLEVBQUUrQixhQUFhLEVBQUU3QixTQUFTLEVBQUVELFNBQVMsRUFBRWtDLGVBQWUsRUFBRTtFQUMvRTtFQUNBO0VBQ0EsSUFBTXNDLFVBQVUsR0FBRyxFQUFFO0VBQ3JCLElBQUlDLGFBQWE7RUFDakIsT0FBTzNDLGFBQWEsRUFBRTtJQUNwQjBDLFVBQVUsQ0FBQ04sSUFBSSxDQUFDcEMsYUFBYSxDQUFDO0lBQzlCMkMsYUFBYSxHQUFHM0MsYUFBYSxDQUFDMEIsaUJBQWlCO0lBQy9DLE9BQU8xQixhQUFhLENBQUMwQixpQkFBaUI7SUFDdEMxQixhQUFhLEdBQUcyQyxhQUFhO0VBQy9CO0VBQ0FELFVBQVUsQ0FBQ0UsT0FBTyxDQUFDLENBQUM7RUFFcEIsSUFBSUMsWUFBWSxHQUFHLENBQUM7SUFDaEJDLFlBQVksR0FBR0osVUFBVSxDQUFDbkUsTUFBTTtJQUNoQzBCLE1BQU0sR0FBRyxDQUFDO0lBQ1ZGLE1BQU0sR0FBRyxDQUFDO0VBRWQsT0FBTzhDLFlBQVksR0FBR0MsWUFBWSxFQUFFRCxZQUFZLEVBQUUsRUFBRTtJQUNsRCxJQUFJRSxTQUFTLEdBQUdMLFVBQVUsQ0FBQ0csWUFBWSxDQUFDO0lBQ3hDLElBQUksQ0FBQ0UsU0FBUyxDQUFDMUIsT0FBTyxFQUFFO01BQ3RCLElBQUksQ0FBQzBCLFNBQVMsQ0FBQzNCLEtBQUssSUFBSWhCLGVBQWUsRUFBRTtRQUN2QyxJQUFJeEIsS0FBSyxHQUFHVCxTQUFTLENBQUM2RSxLQUFLLENBQUMvQyxNQUFNLEVBQUVBLE1BQU0sR0FBRzhDLFNBQVMsQ0FBQ3RCLEtBQUssQ0FBQztRQUM3RDdDLEtBQUssR0FBR0EsS0FBSyxDQUFDcUUsR0FBRyxDQUFDLFVBQVNyRSxLQUFLLEVBQUV1RCxDQUFDLEVBQUU7VUFDbkMsSUFBSWUsUUFBUSxHQUFHaEYsU0FBUyxDQUFDNkIsTUFBTSxHQUFHb0MsQ0FBQyxDQUFDO1VBQ3BDLE9BQU9lLFFBQVEsQ0FBQzNFLE1BQU0sR0FBR0ssS0FBSyxDQUFDTCxNQUFNLEdBQUcyRSxRQUFRLEdBQUd0RSxLQUFLO1FBQzFELENBQUMsQ0FBQztRQUVGbUUsU0FBUyxDQUFDbkUsS0FBSyxHQUFHWCxJQUFJLENBQUNzRSxJQUFJLENBQUMzRCxLQUFLLENBQUM7TUFDcEMsQ0FBQyxNQUFNO1FBQ0xtRSxTQUFTLENBQUNuRSxLQUFLLEdBQUdYLElBQUksQ0FBQ3NFLElBQUksQ0FBQ3BFLFNBQVMsQ0FBQzZFLEtBQUssQ0FBQy9DLE1BQU0sRUFBRUEsTUFBTSxHQUFHOEMsU0FBUyxDQUFDdEIsS0FBSyxDQUFDLENBQUM7TUFDaEY7TUFDQXhCLE1BQU0sSUFBSThDLFNBQVMsQ0FBQ3RCLEtBQUs7O01BRXpCO01BQ0EsSUFBSSxDQUFDc0IsU0FBUyxDQUFDM0IsS0FBSyxFQUFFO1FBQ3BCckIsTUFBTSxJQUFJZ0QsU0FBUyxDQUFDdEIsS0FBSztNQUMzQjtJQUNGLENBQUMsTUFBTTtNQUNMc0IsU0FBUyxDQUFDbkUsS0FBSyxHQUFHWCxJQUFJLENBQUNzRSxJQUFJLENBQUNyRSxTQUFTLENBQUM4RSxLQUFLLENBQUNqRCxNQUFNLEVBQUVBLE1BQU0sR0FBR2dELFNBQVMsQ0FBQ3RCLEtBQUssQ0FBQyxDQUFDO01BQzlFMUIsTUFBTSxJQUFJZ0QsU0FBUyxDQUFDdEIsS0FBSztJQUMzQjtFQUNGO0VBRUEsT0FBT2lCLFVBQVU7QUFDbkIiLCJpZ25vcmVMaXN0IjpbXX0=
