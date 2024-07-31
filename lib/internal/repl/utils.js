'use strict';

const {
  ArrayPrototypeFilter,
  ArrayPrototypeIncludes,
  ArrayPrototypeMap,
  Boolean,
  FunctionPrototypeBind,
  MathMin,
  RegExpPrototypeExec,
  SafeSet,
  SafeStringIterator,
  StringPrototypeEndsWith,
  StringPrototypeIndexOf,
  StringPrototypeLastIndexOf,
  StringPrototypeReplaceAll,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
  StringPrototypeToLowerCase,
  StringPrototypeTrim,
  Symbol,
} = primordials;

const { tokTypes: tt, Parser: AcornParser } =
  require('internal/deps/acorn/acorn/dist/acorn');

const { sendInspectorCommand } = require('internal/util/inspector');

const {
  ERR_INSPECTOR_NOT_AVAILABLE,
} = require('internal/errors').codes;

const {
  clearLine,
  clearScreenDown,
  cursorTo,
  moveCursor,
} = require('internal/readline/callbacks');

const {
  commonPrefix,
  kSubstringSearch,
} = require('internal/readline/utils');

const {
  getStringWidth,
  inspect,
} = require('internal/util/inspect');

let debug = require('internal/util/debuglog').debuglog('repl', (fn) => {
  debug = fn;
});

const previewOptions = {
  colors: false,
  depth: 1,
  showHidden: false,
};

const REPL_MODE_STRICT = Symbol('repl-strict');

// If the error is that we've unexpectedly ended the input,
// then let the user try to recover by adding more input.
// Note: `e` (the original exception) is not used by the current implementation,
// but may be needed in the future.
function isRecoverableError(e, code) {
  // For similar reasons as `defaultEval`, wrap expressions starting with a
  // curly brace with parenthesis.  Note: only the open parenthesis is added
  // here as the point is to test for potentially valid but incomplete
  // expressions.
  if (RegExpPrototypeExec(/^\s*\{/, code) !== null &&
      isRecoverableError(e, `(${code}`))
    return true;

  let recoverable = false;

  // Determine if the point of any error raised is at the end of the input.
  // There are two cases to consider:
  //
  //   1.  Any error raised after we have encountered the 'eof' token.
  //       This prevents us from declaring partial tokens (like '2e') as
  //       recoverable.
  //
  //   2.  Three cases where tokens can legally span lines.  This is
  //       template, comment, and strings with a backslash at the end of
  //       the line, indicating a continuation.  Note that we need to look
  //       for the specific errors of 'unterminated' kind (not, for example,
  //       a syntax error in a ${} expression in a template), and the only
  //       way to do that currently is to look at the message.  Should Acorn
  //       change these messages in the future, this will lead to a test
  //       failure, indicating that this code needs to be updated.
  //
  const RecoverableParser = AcornParser
    .extend(
      (Parser) => {
        return class extends Parser {
          // eslint-disable-next-line no-useless-constructor
          constructor(options, input, startPos) {
            super(options, input, startPos);
          }
          nextToken() {
            super.nextToken();
            if (this.type === tt.eof)
              recoverable = true;
          }
          raise(pos, message) {
            switch (message) {
              case 'Unterminated template':
              case 'Unterminated comment':
                recoverable = true;
                break;

              case 'Unterminated string constant': {
                const token = StringPrototypeSlice(this.input,
                                                   this.lastTokStart, this.pos);
                // See https://www.ecma-international.org/ecma-262/#sec-line-terminators
                if (RegExpPrototypeExec(/\\(?:\r\n?|\n|\u2028|\u2029)$/,
                                        token) !== null) {
                  recoverable = true;
                }
              }
            }
            super.raise(pos, message);
          }
        };
      },
    );

  // Try to parse the code with acorn.  If the parse fails, ignore the acorn
  // error and return the recoverable status.
  try {
    RecoverableParser.parse(code, { ecmaVersion: 'latest' });

    // Odd case: the underlying JS engine (V8, Chakra) rejected this input
    // but Acorn detected no issue.  Presume that additional text won't
    // address this issue.
    return false;
  } catch {
    return recoverable;
  }
}

function setupPreview(repl, contextSymbol, bufferSymbol, active) {
  // Simple terminals can't handle previews.
  if (process.env.TERM === 'dumb' || !active) {
    return { showPreview() {}, clearPreview() {} };
  }

  let inputPreview = null;

  let previewCompletionCounter = 0;
  let completionPreview = null;

  let hasCompletions = false;

  let wrapped = false;

  let escaped = null;

  function getPreviewPos() {
    const displayPos = repl._getDisplayPos(`${repl.getPrompt()}${repl.line}`);
    const cursorPos = repl.line.length !== repl.cursor ?
      repl.getCursorPos() :
      displayPos;
    return { displayPos, cursorPos };
  }

  function isCursorAtInputEnd() {
    const { cursorPos, displayPos } = getPreviewPos();
    return cursorPos.rows === displayPos.rows &&
           cursorPos.cols === displayPos.cols;
  }

  const clearPreview = (key) => {
    if (inputPreview !== null) {
      const { displayPos, cursorPos } = getPreviewPos();
      const rows = displayPos.rows - cursorPos.rows + 1;
      moveCursor(repl.output, 0, rows);
      clearLine(repl.output);
      moveCursor(repl.output, 0, -rows);
      inputPreview = null;
    }
    if (completionPreview !== null) {
      // Prevent cursor moves if not necessary!
      const move = repl.line.length !== repl.cursor;
      let pos, rows;
      if (move) {
        pos = getPreviewPos();
        cursorTo(repl.output, pos.displayPos.cols);
        rows = pos.displayPos.rows - pos.cursorPos.rows;
        moveCursor(repl.output, 0, rows);
      }
      const totalLine = `${repl.getPrompt()}${repl.line}${completionPreview}`;
      const newPos = repl._getDisplayPos(totalLine);
      // Minimize work for the terminal. It is enough to clear the right part of
      // the current line in case the preview is visible on a single line.
      if (newPos.rows === 0 || (pos && pos.displayPos.rows === newPos.rows)) {
        clearLine(repl.output, 1);
      } else {
        clearScreenDown(repl.output);
      }
      if (move) {
        cursorTo(repl.output, pos.cursorPos.cols);
        moveCursor(repl.output, 0, -rows);
      }
      if (!key.ctrl && !key.shift) {
        if (key.name === 'escape') {
          if (escaped === null && key.meta) {
            escaped = repl.line;
          }
        } else if ((key.name === 'return' || key.name === 'enter') &&
                   !key.meta &&
                   escaped !== repl.line &&
                   isCursorAtInputEnd()) {
          repl._insertString(completionPreview);
        }
      }
      completionPreview = null;
    }
    if (escaped !== repl.line) {
      escaped = null;
    }
  };

  function showCompletionPreview(line, insertPreview) {
    previewCompletionCounter++;

    const count = previewCompletionCounter;

    repl.completer(line, (error, data) => {
      // Tab completion might be async and the result might already be outdated.
      if (count !== previewCompletionCounter) {
        return;
      }

      if (error) {
        debug('Error while generating completion preview', error);
        return;
      }

      // Result and the text that was completed.
      const { 0: rawCompletions, 1: completeOn } = data;

      if (!rawCompletions || rawCompletions.length === 0) {
        return;
      }

      hasCompletions = true;

      // If there is a common prefix to all matches, then apply that portion.
      const completions = ArrayPrototypeFilter(rawCompletions, Boolean);
      const prefix = commonPrefix(completions);

      // No common prefix found.
      if (prefix.length <= completeOn.length) {
        return;
      }

      const suffix = StringPrototypeSlice(prefix, completeOn.length);

      if (insertPreview) {
        repl._insertString(suffix);
        return;
      }

      completionPreview = suffix;

      const result = repl.useColors ?
        `\u001b[90m${suffix}\u001b[39m` :
        ` // ${suffix}`;

      const { cursorPos, displayPos } = getPreviewPos();
      if (repl.line.length !== repl.cursor) {
        cursorTo(repl.output, displayPos.cols);
        moveCursor(repl.output, 0, displayPos.rows - cursorPos.rows);
      }
      repl.output.write(result);
      cursorTo(repl.output, cursorPos.cols);
      const totalLine = `${repl.getPrompt()}${repl.line}${suffix}`;
      const newPos = repl._getDisplayPos(totalLine);
      const rows = newPos.rows - cursorPos.rows - (newPos.cols === 0 ? 1 : 0);
      moveCursor(repl.output, 0, -rows);
    });
  }

  function isInStrictMode(repl) {
    return repl.replMode === REPL_MODE_STRICT || ArrayPrototypeIncludes(
      ArrayPrototypeMap(process.execArgv,
                        (e) => StringPrototypeReplaceAll(
                          StringPrototypeToLowerCase(e),
                          '_',
                          '-',
                        )),
      '--use-strict');
  }

  // This returns a code preview for arbitrary input code.
  function getInputPreview(input, callback) {
    // For similar reasons as `defaultEval`, wrap expressions starting with a
    // curly brace with parenthesis.
    if (StringPrototypeStartsWith(input, '{') &&
        !StringPrototypeEndsWith(input, ';') && !wrapped) {
      input = `(${input})`;
      wrapped = true;
    }
    sendInspectorCommand((session) => {
      session.post('Runtime.evaluate', {
        expression: input,
        throwOnSideEffect: true,
        timeout: 333,
        contextId: repl[contextSymbol],
      }, (error, preview) => {
        if (error) {
          callback(error);
          return;
        }
        const { result } = preview;
        if (result.value !== undefined) {
          callback(null, inspect(result.value, previewOptions));
        // Ignore EvalErrors, SyntaxErrors and ReferenceErrors. It is not clear
        // where they came from and if they are recoverable or not. Other errors
        // may be inspected.
        } else if (preview.exceptionDetails &&
                   (result.className === 'EvalError' ||
                    result.className === 'SyntaxError' ||
                    // Report ReferenceError in case the strict mode is active
                    // for input that has no completions.
                    (result.className === 'ReferenceError' &&
                     (hasCompletions || !isInStrictMode(repl))))) {
          callback(null, null);
        } else if (result.objectId) {
          // The writer options might change and have influence on the inspect
          // output. The user might change e.g., `showProxy`, `getters` or
          // `showHidden`. Use `inspect` instead of `JSON.stringify` to keep
          // `Infinity` and similar intact.
          const inspectOptions = inspect({
            ...repl.writer.options,
            colors: false,
            depth: 1,
            compact: true,
            breakLength: Infinity,
          }, previewOptions);
          session.post('Runtime.callFunctionOn', {
            functionDeclaration:
              `(v) =>
                    Reflect
                    .getOwnPropertyDescriptor(globalThis, 'util')
                    .get().inspect(v, ${inspectOptions})`,
            objectId: result.objectId,
            arguments: [result],
          }, (error, preview) => {
            if (error) {
              callback(error);
            } else {
              callback(null, preview.result.value);
            }
          });
        } else {
          // Either not serializable or undefined.
          callback(null, result.unserializableValue || result.type);
        }
      });
    }, () => callback(new ERR_INSPECTOR_NOT_AVAILABLE()));
  }

  const showPreview = (showCompletion = true) => {
    // Prevent duplicated previews after a refresh.
    if (inputPreview !== null || !repl.isCompletionEnabled || !process.features.inspector) {
      return;
    }

    const line = StringPrototypeTrim(repl.line);

    // Do not preview in case the line only contains whitespace.
    if (line === '') {
      return;
    }

    hasCompletions = false;

    // Add the autocompletion preview.
    if (showCompletion) {
      const insertPreview = false;
      showCompletionPreview(repl.line, insertPreview);
    }

    // Do not preview if the command is buffered.
    if (repl[bufferSymbol]) {
      return;
    }

    const inputPreviewCallback = (error, inspected) => {
      if (inspected == null) {
        return;
      }

      wrapped = false;

      // Ignore the output if the value is identical to the current line.
      if (line === inspected) {
        return;
      }

      if (error) {
        debug('Error while generating preview', error);
        return;
      }
      // Do not preview `undefined` if colors are deactivated or explicitly
      // requested.
      if (inspected === 'undefined' &&
          (!repl.useColors || repl.ignoreUndefined)) {
        return;
      }

      inputPreview = inspected;

      // Limit the output to maximum 250 characters. Otherwise it becomes a)
      // difficult to read and b) non terminal REPLs would visualize the whole
      // output.
      let maxColumns = MathMin(repl.columns, 250);

      // Support unicode characters of width other than one by checking the
      // actual width.
      if (inspected.length * 2 >= maxColumns &&
          getStringWidth(inspected) > maxColumns) {
        maxColumns -= 4 + (repl.useColors ? 0 : 3);
        let res = '';
        for (const char of new SafeStringIterator(inspected)) {
          maxColumns -= getStringWidth(char);
          if (maxColumns < 0)
            break;
          res += char;
        }
        inspected = `${res}...`;
      }

      // Line breaks are very rare and probably only occur in case of error
      // messages with line breaks.
      const lineBreakPos = StringPrototypeIndexOf(inspected, '\n');
      if (lineBreakPos !== -1) {
        inspected = `${StringPrototypeSlice(inspected, 0, lineBreakPos)}`;
      }

      const result = repl.useColors ?
        `\u001b[90m${inspected}\u001b[39m` :
        `// ${inspected}`;

      const { cursorPos, displayPos } = getPreviewPos();
      const rows = displayPos.rows - cursorPos.rows;
      moveCursor(repl.output, 0, rows);
      repl.output.write(`\n${result}`);
      cursorTo(repl.output, cursorPos.cols);
      moveCursor(repl.output, 0, -rows - 1);
    };

    let previewLine = line;

    if (completionPreview !== null &&
        isCursorAtInputEnd() &&
        escaped !== repl.line) {
      previewLine += completionPreview;
    }

    getInputPreview(previewLine, inputPreviewCallback);
    if (wrapped) {
      getInputPreview(previewLine, inputPreviewCallback);
    }
    wrapped = false;
  };

  // -------------------------------------------------------------------------//
  // Replace multiple interface functions. This is required to fully support  //
  // previews without changing readlines behavior.                            //
  // -------------------------------------------------------------------------//

  // Refresh prints the whole screen again and the preview will be removed
  // during that procedure. Print the preview again. This also makes sure
  // the preview is always correct after resizing the terminal window.
  const originalRefresh = FunctionPrototypeBind(repl._refreshLine, repl);
  repl._refreshLine = () => {
    inputPreview = null;
    originalRefresh();
    showPreview();
  };

  let insertCompletionPreview = true;
  // Insert the longest common suffix of the current input in case the user
  // moves to the right while already being at the current input end.
  const originalMoveCursor = FunctionPrototypeBind(repl._moveCursor, repl);
  repl._moveCursor = (dx) => {
    const currentCursor = repl.cursor;
    originalMoveCursor(dx);
    if (currentCursor + dx > repl.line.length &&
        typeof repl.completer === 'function' &&
        insertCompletionPreview) {
      const insertPreview = true;
      showCompletionPreview(repl.line, insertPreview);
    }
  };

  // This is the only function that interferes with the completion insertion.
  // Monkey patch it to prevent inserting the completion when it shouldn't be.
  const originalClearLine = FunctionPrototypeBind(repl.clearLine, repl);
  repl.clearLine = () => {
    insertCompletionPreview = false;
    originalClearLine();
    insertCompletionPreview = true;
  };

  return { showPreview, clearPreview };
}

function setupReverseSearch(repl) {
  // Simple terminals can't use reverse search.
  if (process.env.TERM === 'dumb') {
    return { reverseSearch() { return false; } };
  }

  const alreadyMatched = new SafeSet();
  const labels = {
    r: 'bck-i-search: ',
    s: 'fwd-i-search: ',
  };
  let isInReverseSearch = false;
  let historyIndex = -1;
  let input = '';
  let cursor = -1;
  let dir = 'r';
  let lastMatch = -1;
  let lastCursor = -1;
  let promptPos;

  function checkAndSetDirectionKey(keyName) {
    if (!labels[keyName]) {
      return false;
    }
    if (dir !== keyName) {
      // Reset the already matched set in case the direction is changed. That
      // way it's possible to find those entries again.
      alreadyMatched.clear();
      dir = keyName;
    }
    return true;
  }

  function goToNextHistoryIndex() {
    // Ignore this entry for further searches and continue to the next
    // history entry.
    alreadyMatched.add(repl.history[historyIndex]);
    historyIndex += dir === 'r' ? 1 : -1;
    cursor = -1;
  }

  function search() {
    // Just print an empty line in case the user removed the search parameter.
    if (input === '') {
      print(repl.line, `${labels[dir]}_`);
      return;
    }
    // Fix the bounds in case the direction has changed in the meanwhile.
    if (dir === 'r') {
      if (historyIndex < 0) {
        historyIndex = 0;
      }
    } else if (historyIndex >= repl.history.length) {
      historyIndex = repl.history.length - 1;
    }
    // Check the history entries until a match is found.
    while (historyIndex >= 0 && historyIndex < repl.history.length) {
      let entry = repl.history[historyIndex];
      // Visualize all potential matches only once.
      if (alreadyMatched.has(entry)) {
        historyIndex += dir === 'r' ? 1 : -1;
        continue;
      }
      // Match the next entry either from the start or from the end, depending
      // on the current direction.
      if (dir === 'r') {
        // Update the cursor in case it's necessary.
        if (cursor === -1) {
          cursor = entry.length;
        }
        cursor = StringPrototypeLastIndexOf(entry, input, cursor - 1);
      } else {
        cursor = StringPrototypeIndexOf(entry, input, cursor + 1);
      }
      // Match not found.
      if (cursor === -1) {
        goToNextHistoryIndex();
      // Match found.
      } else {
        if (repl.useColors) {
          const start = StringPrototypeSlice(entry, 0, cursor);
          const end = StringPrototypeSlice(entry, cursor + input.length);
          entry = `${start}\x1B[4m${input}\x1B[24m${end}`;
        }
        print(entry, `${labels[dir]}${input}_`, cursor);
        lastMatch = historyIndex;
        lastCursor = cursor;
        // Explicitly go to the next history item in case no further matches are
        // possible with the current entry.
        if ((dir === 'r' && cursor === 0) ||
            (dir === 's' && entry.length === cursor + input.length)) {
          goToNextHistoryIndex();
        }
        return;
      }
    }
    print(repl.line, `failed-${labels[dir]}${input}_`);
  }

  function print(outputLine, inputLine, cursor = repl.cursor) {
    // TODO(BridgeAR): Resizing the terminal window hides the overlay. To fix
    // that, readline must be aware of this information. It's probably best to
    // add a couple of properties to readline that allow to do the following:
    // 1. Add arbitrary data to the end of the current line while not counting
    //    towards the line. This would be useful for the completion previews.
    // 2. Add arbitrary extra lines that do not count towards the regular line.
    //    This would be useful for both, the input preview and the reverse
    //    search. It might be combined with the first part?
    // 3. Add arbitrary input that is "on top" of the current line. That is
    //    useful for the reverse search.
    // 4. To trigger the line refresh, functions should be used to pass through
    //    the information. Alternatively, getters and setters could be used.
    //    That might even be more elegant.
    // The data would then be accounted for when calling `_refreshLine()`.
    // This function would then look similar to:
    //   repl.overlay(outputLine);
    //   repl.addTrailingLine(inputLine);
    //   repl.setCursor(cursor);
    // More potential improvements: use something similar to stream.cork().
    // Multiple cursor moves on the same tick could be prevented in case all
    // writes from the same tick are combined and the cursor is moved at the
    // tick end instead of after each operation.
    let rows = 0;
    if (lastMatch !== -1) {
      const line = StringPrototypeSlice(repl.history[lastMatch], 0, lastCursor);
      rows = repl._getDisplayPos(`${repl.getPrompt()}${line}`).rows;
      cursorTo(repl.output, promptPos.cols);
    } else if (isInReverseSearch && repl.line !== '') {
      rows = repl.getCursorPos().rows;
      cursorTo(repl.output, promptPos.cols);
    }
    if (rows !== 0)
      moveCursor(repl.output, 0, -rows);

    if (isInReverseSearch) {
      clearScreenDown(repl.output);
      repl.output.write(`${outputLine}\n${inputLine}`);
    } else {
      repl.output.write(`\n${inputLine}`);
    }

    lastMatch = -1;

    // To know exactly how many rows we have to move the cursor back we need the
    // cursor rows, the output rows and the input rows.
    const prompt = repl.getPrompt();
    const cursorLine = prompt + StringPrototypeSlice(outputLine, 0, cursor);
    const cursorPos = repl._getDisplayPos(cursorLine);
    const outputPos = repl._getDisplayPos(`${prompt}${outputLine}`);
    const inputPos = repl._getDisplayPos(inputLine);
    const inputRows = inputPos.rows - (inputPos.cols === 0 ? 1 : 0);

    rows = -1 - inputRows - (outputPos.rows - cursorPos.rows);

    moveCursor(repl.output, 0, rows);
    cursorTo(repl.output, cursorPos.cols);
  }

  function reset(string) {
    isInReverseSearch = string !== undefined;

    // In case the reverse search ends and a history entry is found, reset the
    // line to the found entry.
    if (!isInReverseSearch) {
      if (lastMatch !== -1) {
        repl.line = repl.history[lastMatch];
        repl.cursor = lastCursor;
        repl.historyIndex = lastMatch;
      }

      lastMatch = -1;

      // Clear screen and write the current repl.line before exiting.
      cursorTo(repl.output, promptPos.cols);
      moveCursor(repl.output, 0, promptPos.rows);
      clearScreenDown(repl.output);
      if (repl.line !== '') {
        repl.output.write(repl.line);
        if (repl.line.length !== repl.cursor) {
          const { cols, rows } = repl.getCursorPos();
          cursorTo(repl.output, cols);
          moveCursor(repl.output, 0, rows);
        }
      }
    }

    input = string || '';
    cursor = -1;
    historyIndex = repl.historyIndex;
    alreadyMatched.clear();
  }

  function reverseSearch(string, key) {
    if (!isInReverseSearch) {
      if (key.ctrl && checkAndSetDirectionKey(key.name)) {
        historyIndex = repl.historyIndex;
        promptPos = repl._getDisplayPos(`${repl.getPrompt()}`);
        print(repl.line, `${labels[dir]}_`);
        isInReverseSearch = true;
      }
    } else if (key.ctrl && checkAndSetDirectionKey(key.name)) {
      search();
    } else if (key.name === 'backspace' ||
        (key.ctrl && (key.name === 'h' || key.name === 'w'))) {
      reset(StringPrototypeSlice(input, 0, input.length - 1));
      search();
      // Special handle <ctrl> + c and escape. Those should only cancel the
      // reverse search. The original line is visible afterwards again.
    } else if ((key.ctrl && key.name === 'c') || key.name === 'escape') {
      lastMatch = -1;
      reset();
      return true;
      // End search in case either enter is pressed or if any non-reverse-search
      // key (combination) is pressed.
    } else if (key.ctrl ||
               key.meta ||
               key.name === 'return' ||
               key.name === 'enter' ||
               typeof string !== 'string' ||
               string === '') {
      reset();
      repl[kSubstringSearch] = '';
    } else {
      reset(`${input}${string}`);
      search();
    }
    return isInReverseSearch;
  }

  return { reverseSearch };
}

module.exports = {
  REPL_MODE_SLOPPY: Symbol('repl-sloppy'),
  REPL_MODE_STRICT,
  isRecoverableError,
  kStandaloneREPL: Symbol('kStandaloneREPL'),
  setupPreview,
  setupReverseSearch,
};
