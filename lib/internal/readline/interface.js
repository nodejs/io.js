'use strict';

const {
  ArrayFrom,
  ArrayPrototypeFilter,
  ArrayPrototypeIndexOf,
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypePop,
  ArrayPrototypePush,
  ArrayPrototypeReverse,
  ArrayPrototypeShift,
  ArrayPrototypeSplice,
  ArrayPrototypeUnshift,
  DateNow,
  FunctionPrototypeCall,
  MathCeil,
  MathFloor,
  MathMax,
  MathMaxApply,
  NumberIsFinite,
  ObjectSetPrototypeOf,
  RegExpPrototypeExec,
  SafeStringIterator,
  StringPrototypeCodePointAt,
  StringPrototypeEndsWith,
  StringPrototypeRepeat,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
  StringPrototypeTrim,
  Symbol,
  SymbolAsyncIterator,
  SymbolDispose,
} = primordials;

const { codes: {
  ERR_INVALID_ARG_VALUE,
  ERR_USE_AFTER_CLOSE,
} } = require('internal/errors');

const {
  validateAbortSignal,
  validateArray,
  validateNumber,
  validateString,
  validateUint32,
} = require('internal/validators');
const { kEmptyObject } = require('internal/util');
const {
  inspect,
  getStringWidth,
  stripVTControlCharacters,
} = require('internal/util/inspect');
const EventEmitter = require('events');
const { addAbortListener } = require('internal/events/abort_listener');
const {
  charLengthAt,
  charLengthLeft,
  commonPrefix,
  kSubstringSearch,
} = require('internal/readline/utils');
let emitKeypressEvents;
let kFirstEventParam;
const {
  clearScreenDown,
  cursorTo,
  moveCursor,
} = require('internal/readline/callbacks');

const { StringDecoder } = require('string_decoder');

const kHistorySize = 30;
const kMaxUndoRedoStackSize = 2048;
const kMincrlfDelay = 100;
// \r\n, \n, or \r followed by something other than \n
const lineEnding = /\r?\n|\r(?!\n)/g;

const kLineObjectStream = Symbol('line object stream');
const kQuestionCancel = Symbol('kQuestionCancel');
const kQuestion = Symbol('kQuestion');

// GNU readline library - keyseq-timeout is 500ms (default)
const ESCAPE_CODE_TIMEOUT = 500;

// Max length of the kill ring
const kMaxLengthOfKillRing = 32;

const kAddHistory = Symbol('_addHistory');
const kBeforeEdit = Symbol('_beforeEdit');
const kDecoder = Symbol('_decoder');
const kDeleteLeft = Symbol('_deleteLeft');
const kDeleteLineLeft = Symbol('_deleteLineLeft');
const kDeleteLineRight = Symbol('_deleteLineRight');
const kDeleteRight = Symbol('_deleteRight');
const kDeleteWordLeft = Symbol('_deleteWordLeft');
const kDeleteWordRight = Symbol('_deleteWordRight');
const kGetDisplayPos = Symbol('_getDisplayPos');
const kHistoryNext = Symbol('_historyNext');
const kHistoryPrev = Symbol('_historyPrev');
const kInsertString = Symbol('_insertString');
const kLine = Symbol('_line');
const kLine_buffer = Symbol('_line_buffer');
const kKillRing = Symbol('_killRing');
const kKillRingCursor = Symbol('_killRingCursor');
const kMoveCursor = Symbol('_moveCursor');
const kNormalWrite = Symbol('_normalWrite');
const kOldPrompt = Symbol('_oldPrompt');
const kOnLine = Symbol('_onLine');
const kPreviousKey = Symbol('_previousKey');
const kPrompt = Symbol('_prompt');
const kPushToKillRing = Symbol('_pushToKillRing');
const kPushToUndoStack = Symbol('_pushToUndoStack');
const kQuestionCallback = Symbol('_questionCallback');
const kRedo = Symbol('_redo');
const kRedoStack = Symbol('_redoStack');
const kRefreshLine = Symbol('_refreshLine');
const kSawKeyPress = Symbol('_sawKeyPress');
const kSawReturnAt = Symbol('_sawReturnAt');
const kSetRawMode = Symbol('_setRawMode');
const kTabComplete = Symbol('_tabComplete');
const kTabCompleter = Symbol('_tabCompleter');
const kTtyWrite = Symbol('_ttyWrite');
const kUndo = Symbol('_undo');
const kUndoStack = Symbol('_undoStack');
const kWordLeft = Symbol('_wordLeft');
const kWordRight = Symbol('_wordRight');
const kWriteToOutput = Symbol('_writeToOutput');
const kYank = Symbol('_yank');
const kYanking = Symbol('_yanking');
const kYankPop = Symbol('_yankPop');

function InterfaceConstructor(input, output, completer, terminal) {
  this[kSawReturnAt] = 0;
  // TODO(BridgeAR): Document this property. The name is not ideal, so we
  // might want to expose an alias and document that instead.
  this.isCompletionEnabled = true;
  this[kSawKeyPress] = false;
  this[kPreviousKey] = null;
  this.escapeCodeTimeout = ESCAPE_CODE_TIMEOUT;
  this.tabSize = 8;

  FunctionPrototypeCall(EventEmitter, this);

  let history;
  let historySize;
  let removeHistoryDuplicates = false;
  let crlfDelay;
  let prompt = '> ';
  let signal;

  if (input?.input) {
    // An options object was given
    output = input.output;
    completer = input.completer;
    terminal = input.terminal;
    history = input.history;
    historySize = input.historySize;
    signal = input.signal;
    if (input.tabSize !== undefined) {
      validateUint32(input.tabSize, 'tabSize', true);
      this.tabSize = input.tabSize;
    }
    removeHistoryDuplicates = input.removeHistoryDuplicates;
    if (input.prompt !== undefined) {
      prompt = input.prompt;
    }
    if (input.escapeCodeTimeout !== undefined) {
      if (NumberIsFinite(input.escapeCodeTimeout)) {
        this.escapeCodeTimeout = input.escapeCodeTimeout;
      } else {
        throw new ERR_INVALID_ARG_VALUE(
          'input.escapeCodeTimeout',
          this.escapeCodeTimeout,
        );
      }
    }

    if (signal) {
      validateAbortSignal(signal, 'options.signal');
    }

    crlfDelay = input.crlfDelay;
    input = input.input;
  }

  if (completer !== undefined && typeof completer !== 'function') {
    throw new ERR_INVALID_ARG_VALUE('completer', completer);
  }

  if (history === undefined) {
    history = [];
  } else {
    validateArray(history, 'history');
  }

  if (historySize === undefined) {
    historySize = kHistorySize;
  }

  validateNumber(historySize, 'historySize', 0);

  // Backwards compat; check the isTTY prop of the output stream
  //  when `terminal` was not specified
  if (terminal === undefined && !(output === null || output === undefined)) {
    terminal = !!output.isTTY;
  }

  const self = this;

  this.line = '';
  this[kSubstringSearch] = null;
  this.output = output;
  this.input = input;
  this[kUndoStack] = [];
  this[kRedoStack] = [];
  this.history = history;
  this.historySize = historySize;

  // The kill ring is a global list of blocks of text that were previously
  // killed (deleted). If its size exceeds kMaxLengthOfKillRing, the oldest
  // element will be removed to make room for the latest deletion. With kill
  // ring, users are able to recall (yank) or cycle (yank pop) among previously
  // killed texts, quite similar to the behavior of Emacs.
  this[kKillRing] = [];
  this[kKillRingCursor] = 0;

  this.removeHistoryDuplicates = !!removeHistoryDuplicates;
  this.crlfDelay = crlfDelay ?
    MathMax(kMincrlfDelay, crlfDelay) :
    kMincrlfDelay;
  this.completer = completer;

  this.setPrompt(prompt);

  this.terminal = !!terminal;


  function onerror(err) {
    self.emit('error', err);
  }

  function ondata(data) {
    self[kNormalWrite](data);
  }

  function onend() {
    if (
      typeof self[kLine_buffer] === 'string' &&
        self[kLine_buffer].length > 0
    ) {
      self.emit('line', self[kLine_buffer]);
    }
    self.close();
  }

  function ontermend() {
    if (typeof self.line === 'string' && self.line.length > 0) {
      self.emit('line', self.line);
    }
    self.close();
  }

  function onkeypress(s, key) {
    self[kTtyWrite](s, key);
    if (key?.sequence) {
      // If the key.sequence is half of a surrogate pair
      // (>= 0xd800 and <= 0xdfff), refresh the line so
      // the character is displayed appropriately.
      const ch = StringPrototypeCodePointAt(key.sequence, 0);
      if (ch >= 0xd800 && ch <= 0xdfff) self[kRefreshLine]();
    }
  }

  function onresize() {
    self[kRefreshLine]();
  }

  this[kLineObjectStream] = undefined;

  input.on('error', onerror);

  if (!this.terminal) {
    function onSelfCloseWithoutTerminal() {
      input.removeListener('data', ondata);
      input.removeListener('error', onerror);
      input.removeListener('end', onend);
    }

    input.on('data', ondata);
    input.on('end', onend);
    self.once('close', onSelfCloseWithoutTerminal);
    this[kDecoder] = new StringDecoder('utf8');
  } else {
    function onSelfCloseWithTerminal() {
      input.removeListener('keypress', onkeypress);
      input.removeListener('error', onerror);
      input.removeListener('end', ontermend);
      if (output !== null && output !== undefined) {
        output.removeListener('resize', onresize);
      }
    }

    emitKeypressEvents ??= require('internal/readline/emitKeypressEvents');
    emitKeypressEvents(input, this);

    // `input` usually refers to stdin
    input.on('keypress', onkeypress);
    input.on('end', ontermend);

    this[kSetRawMode](true);
    this.terminal = true;

    // Cursor position on the line.
    this.cursor = 0;

    this.historyIndex = -1;

    if (output !== null && output !== undefined)
      output.on('resize', onresize);

    self.once('close', onSelfCloseWithTerminal);
  }

  if (signal) {
    const onAborted = () => self.close();
    if (signal.aborted) {
      process.nextTick(onAborted);
    } else {
      const disposable = addAbortListener(signal, onAborted);
      self.once('close', disposable[SymbolDispose]);
    }
  }

  // Current line
  this.line = '';

  input.resume();
}

ObjectSetPrototypeOf(InterfaceConstructor.prototype, EventEmitter.prototype);
ObjectSetPrototypeOf(InterfaceConstructor, EventEmitter);

class Interface extends InterfaceConstructor {
  // eslint-disable-next-line no-useless-constructor
  constructor(input, output, completer, terminal) {
    super(input, output, completer, terminal);
  }
  get columns() {
    if (this.output?.columns) return this.output.columns;
    return Infinity;
  }

  /**
   * Sets the prompt written to the output.
   * @param {string} prompt
   * @returns {void}
   */
  setPrompt(prompt) {
    this[kPrompt] = prompt;
  }

  /**
   * Returns the current prompt used by `rl.prompt()`.
   * @returns {string}
   */
  getPrompt() {
    return this[kPrompt];
  }

  [kSetRawMode](mode) {
    const wasInRawMode = this.input.isRaw;

    if (typeof this.input.setRawMode === 'function') {
      this.input.setRawMode(mode);
    }

    return wasInRawMode;
  }

  /**
   * Writes the configured `prompt` to a new line in `output`.
   * @param {boolean} [preserveCursor]
   * @returns {void}
   */
  prompt(preserveCursor) {
    if (this.paused) this.resume();
    if (this.terminal && process.env.TERM !== 'dumb') {
      if (!preserveCursor) this.cursor = 0;
      this[kRefreshLine]();
    } else {
      this[kWriteToOutput](this[kPrompt]);
    }
  }

  [kQuestion](query, cb) {
    if (this.closed) {
      throw new ERR_USE_AFTER_CLOSE('readline');
    }
    if (this[kQuestionCallback]) {
      this.prompt();
    } else {
      this[kOldPrompt] = this[kPrompt];
      this.setPrompt(query);
      this[kQuestionCallback] = cb;
      this.prompt();
    }
  }

  [kOnLine](line) {
    if (this[kQuestionCallback]) {
      const cb = this[kQuestionCallback];
      this[kQuestionCallback] = null;
      this.setPrompt(this[kOldPrompt]);
      cb(line);
    } else {
      this.emit('line', line);
    }
  }

  [kBeforeEdit](oldText, oldCursor) {
    this[kPushToUndoStack](oldText, oldCursor);
  }

  [kQuestionCancel]() {
    if (this[kQuestionCallback]) {
      this[kQuestionCallback] = null;
      this.setPrompt(this[kOldPrompt]);
      this.clearLine();
    }
  }

  [kWriteToOutput](stringToWrite) {
    validateString(stringToWrite, 'stringToWrite');

    if (this.output !== null && this.output !== undefined) {
      this.output.write(stringToWrite);
    }
  }

  [kAddHistory]() {
    if (this.line.length === 0) return '';

    // If the history is disabled then return the line
    if (this.historySize === 0) return this.line;

    // If the trimmed line is empty then return the line
    if (StringPrototypeTrim(this.line).length === 0) return this.line;

    if (this.history.length === 0 || this.history[0] !== this.line) {
      if (this.removeHistoryDuplicates) {
        // Remove older history line if identical to new one
        const dupIndex = ArrayPrototypeIndexOf(this.history, this.line);
        if (dupIndex !== -1) ArrayPrototypeSplice(this.history, dupIndex, 1);
      }

      ArrayPrototypeUnshift(this.history, this.line);

      // Only store so many
      if (this.history.length > this.historySize)
        ArrayPrototypePop(this.history);
    }

    this.historyIndex = -1;

    // The listener could change the history object, possibly
    // to remove the last added entry if it is sensitive and should
    // not be persisted in the history, like a password
    const line = this.history[0];

    // Emit history event to notify listeners of update
    this.emit('history', this.history);

    return line;
  }

  [kRefreshLine]() {
    // line length
    const line = this[kPrompt] + this.line;
    const dispPos = this[kGetDisplayPos](line);
    const lineCols = dispPos.cols;
    const lineRows = dispPos.rows;

    // cursor position
    const cursorPos = this.getCursorPos();

    // First move to the bottom of the current line, based on cursor pos
    const prevRows = this.prevRows || 0;
    if (prevRows > 0) {
      moveCursor(this.output, 0, -prevRows);
    }

    // Cursor to left edge.
    cursorTo(this.output, 0);
    // erase data
    clearScreenDown(this.output);

    // Write the prompt and the current buffer content.
    this[kWriteToOutput](line);

    // Force terminal to allocate a new line
    if (lineCols === 0) {
      this[kWriteToOutput](' ');
    }

    // Move cursor to original position.
    cursorTo(this.output, cursorPos.cols);

    const diff = lineRows - cursorPos.rows;
    if (diff > 0) {
      moveCursor(this.output, 0, -diff);
    }

    this.prevRows = cursorPos.rows;
  }

  /**
   * Closes the `readline.Interface` instance.
   * @returns {void}
   */
  close() {
    if (this.closed) return;
    this.pause();
    if (this.terminal) {
      this[kSetRawMode](false);
    }
    this.closed = true;
    this.emit('close');
  }

  /**
   * Pauses the `input` stream.
   * @returns {void | Interface}
   */
  pause() {
    if (this.paused) return;
    this.input.pause();
    this.paused = true;
    this.emit('pause');
    return this;
  }

  /**
   * Resumes the `input` stream if paused.
   * @returns {void | Interface}
   */
  resume() {
    if (!this.paused) return;
    this.input.resume();
    this.paused = false;
    this.emit('resume');
    return this;
  }

  /**
   * Writes either `data` or a `key` sequence identified by
   * `key` to the `output`.
   * @param {string} d
   * @param {{
   *   ctrl?: boolean;
   *   meta?: boolean;
   *   shift?: boolean;
   *   name?: string;
   *   }} [key]
   * @returns {void}
   */
  write(d, key) {
    if (this.paused) this.resume();
    if (this.terminal) {
      this[kTtyWrite](d, key);
    } else {
      this[kNormalWrite](d);
    }
  }

  [kNormalWrite](b) {
    if (b === undefined) {
      return;
    }
    let string = this[kDecoder].write(b);
    if (
      this[kSawReturnAt] &&
      DateNow() - this[kSawReturnAt] <= this.crlfDelay
    ) {
      if (StringPrototypeCodePointAt(string) === 10) string = StringPrototypeSlice(string, 1);
      this[kSawReturnAt] = 0;
    }

    // Run test() on the new string chunk, not on the entire line buffer.
    let newPartContainsEnding = RegExpPrototypeExec(lineEnding, string);
    if (newPartContainsEnding !== null) {
      if (this[kLine_buffer]) {
        string = this[kLine_buffer] + string;
        this[kLine_buffer] = null;
        lineEnding.lastIndex = 0; // Start the search from the beginning of the string.
        newPartContainsEnding = RegExpPrototypeExec(lineEnding, string);
      }
      this[kSawReturnAt] = StringPrototypeEndsWith(string, '\r') ?
        DateNow() :
        0;

      const indexes = [0, newPartContainsEnding.index, lineEnding.lastIndex];
      let nextMatch;
      while ((nextMatch = RegExpPrototypeExec(lineEnding, string)) !== null) {
        ArrayPrototypePush(indexes, nextMatch.index, lineEnding.lastIndex);
      }
      const lastIndex = indexes.length - 1;
      // Either '' or (conceivably) the unfinished portion of the next line
      this[kLine_buffer] = StringPrototypeSlice(string, indexes[lastIndex]);
      for (let i = 1; i < lastIndex; i += 2) {
        this[kOnLine](StringPrototypeSlice(string, indexes[i - 1], indexes[i]));
      }
    } else if (string) {
      // No newlines this time, save what we have for next time
      if (this[kLine_buffer]) {
        this[kLine_buffer] += string;
      } else {
        this[kLine_buffer] = string;
      }
    }
  }

  [kInsertString](c) {
    this[kBeforeEdit](this.line, this.cursor);
    if (this.cursor < this.line.length) {
      const beg = StringPrototypeSlice(this.line, 0, this.cursor);
      const end = StringPrototypeSlice(
        this.line,
        this.cursor,
        this.line.length,
      );
      this.line = beg + c + end;
      this.cursor += c.length;
      this[kRefreshLine]();
    } else {
      const oldPos = this.getCursorPos();
      this.line += c;
      this.cursor += c.length;
      const newPos = this.getCursorPos();

      if (oldPos.rows < newPos.rows) {
        this[kRefreshLine]();
      } else {
        this[kWriteToOutput](c);
      }
    }
  }

  async [kTabComplete](lastKeypressWasTab) {
    this.pause();
    const string = StringPrototypeSlice(this.line, 0, this.cursor);
    let value;
    try {
      value = await this.completer(string);
    } catch (err) {
      this[kWriteToOutput](`Tab completion error: ${inspect(err)}`);
      return;
    } finally {
      this.resume();
    }
    this[kTabCompleter](lastKeypressWasTab, value);
  }

  [kTabCompleter](lastKeypressWasTab, { 0: completions, 1: completeOn }) {
    // Result and the text that was completed.

    if (!completions || completions.length === 0) {
      return;
    }

    // If there is a common prefix to all matches, then apply that portion.
    const prefix = commonPrefix(
      ArrayPrototypeFilter(completions, (e) => e !== ''),
    );
    if (StringPrototypeStartsWith(prefix, completeOn) &&
        prefix.length > completeOn.length) {
      this[kInsertString](StringPrototypeSlice(prefix, completeOn.length));
      return;
    } else if (!StringPrototypeStartsWith(completeOn, prefix)) {
      this.line = StringPrototypeSlice(this.line,
                                       0,
                                       this.cursor - completeOn.length) +
                  prefix +
                  StringPrototypeSlice(this.line,
                                       this.cursor,
                                       this.line.length);
      this.cursor = this.cursor - completeOn.length + prefix.length;
      this[kRefreshLine]();
      return;
    }

    if (!lastKeypressWasTab) {
      return;
    }

    this[kBeforeEdit](this.line, this.cursor);

    // Apply/show completions.
    const completionsWidth = ArrayPrototypeMap(completions, (e) =>
      getStringWidth(e),
    );
    const width = MathMaxApply(completionsWidth) + 2; // 2 space padding
    let maxColumns = MathFloor(this.columns / width) || 1;
    if (maxColumns === Infinity) {
      maxColumns = 1;
    }
    let output = '\r\n';
    let lineIndex = 0;
    let whitespace = 0;
    for (let i = 0; i < completions.length; i++) {
      const completion = completions[i];
      if (completion === '' || lineIndex === maxColumns) {
        output += '\r\n';
        lineIndex = 0;
        whitespace = 0;
      } else {
        output += StringPrototypeRepeat(' ', whitespace);
      }
      if (completion !== '') {
        output += completion;
        whitespace = width - completionsWidth[i];
        lineIndex++;
      } else {
        output += '\r\n';
      }
    }
    if (lineIndex !== 0) {
      output += '\r\n\r\n';
    }
    this[kWriteToOutput](output);
    this[kRefreshLine]();
  }

  [kWordLeft]() {
    if (this.cursor > 0) {
      // Reverse the string and match a word near beginning
      // to avoid quadratic time complexity
      const leading = StringPrototypeSlice(this.line, 0, this.cursor);
      const reversed = ArrayPrototypeJoin(
        ArrayPrototypeReverse(ArrayFrom(leading)),
        '',
      );
      const match = RegExpPrototypeExec(/^\s*(?:[^\w\s]+|\w+)?/, reversed);
      this[kMoveCursor](-match[0].length);
    }
  }

  [kWordRight]() {
    if (this.cursor < this.line.length) {
      const trailing = StringPrototypeSlice(this.line, this.cursor);
      const match = RegExpPrototypeExec(/^(?:\s+|[^\w\s]+|\w+)\s*/, trailing);
      this[kMoveCursor](match[0].length);
    }
  }

  [kDeleteLeft]() {
    if (this.cursor > 0 && this.line.length > 0) {
      this[kBeforeEdit](this.line, this.cursor);
      // The number of UTF-16 units comprising the character to the left
      const charSize = charLengthLeft(this.line, this.cursor);
      this.line =
        StringPrototypeSlice(this.line, 0, this.cursor - charSize) +
        StringPrototypeSlice(this.line, this.cursor, this.line.length);

      this.cursor -= charSize;
      this[kRefreshLine]();
    }
  }

  [kDeleteRight]() {
    if (this.cursor < this.line.length) {
      this[kBeforeEdit](this.line, this.cursor);
      // The number of UTF-16 units comprising the character to the left
      const charSize = charLengthAt(this.line, this.cursor);
      this.line =
        StringPrototypeSlice(this.line, 0, this.cursor) +
        StringPrototypeSlice(
          this.line,
          this.cursor + charSize,
          this.line.length,
        );
      this[kRefreshLine]();
    }
  }

  [kDeleteWordLeft]() {
    if (this.cursor > 0) {
      this[kBeforeEdit](this.line, this.cursor);
      // Reverse the string and match a word near beginning
      // to avoid quadratic time complexity
      let leading = StringPrototypeSlice(this.line, 0, this.cursor);
      const reversed = ArrayPrototypeJoin(
        ArrayPrototypeReverse(ArrayFrom(leading)),
        '',
      );
      const match = RegExpPrototypeExec(/^\s*(?:[^\w\s]+|\w+)?/, reversed);
      leading = StringPrototypeSlice(
        leading,
        0,
        leading.length - match[0].length,
      );
      this.line =
        leading +
        StringPrototypeSlice(this.line, this.cursor, this.line.length);
      this.cursor = leading.length;
      this[kRefreshLine]();
    }
  }

  [kDeleteWordRight]() {
    if (this.cursor < this.line.length) {
      this[kBeforeEdit](this.line, this.cursor);
      const trailing = StringPrototypeSlice(this.line, this.cursor);
      const match = RegExpPrototypeExec(/^(?:\s+|\W+|\w+)\s*/, trailing);
      this.line =
        StringPrototypeSlice(this.line, 0, this.cursor) +
        StringPrototypeSlice(trailing, match[0].length);
      this[kRefreshLine]();
    }
  }

  [kDeleteLineLeft]() {
    this[kBeforeEdit](this.line, this.cursor);
    const del = StringPrototypeSlice(this.line, 0, this.cursor);
    this.line = StringPrototypeSlice(this.line, this.cursor);
    this.cursor = 0;
    this[kPushToKillRing](del);
    this[kRefreshLine]();
  }

  [kDeleteLineRight]() {
    this[kBeforeEdit](this.line, this.cursor);
    const del = StringPrototypeSlice(this.line, this.cursor);
    this.line = StringPrototypeSlice(this.line, 0, this.cursor);
    this[kPushToKillRing](del);
    this[kRefreshLine]();
  }

  [kPushToKillRing](del) {
    if (!del || del === this[kKillRing][0]) return;
    ArrayPrototypeUnshift(this[kKillRing], del);
    this[kKillRingCursor] = 0;
    while (this[kKillRing].length > kMaxLengthOfKillRing)
      ArrayPrototypePop(this[kKillRing]);
  }

  [kYank]() {
    if (this[kKillRing].length > 0) {
      this[kYanking] = true;
      this[kInsertString](this[kKillRing][this[kKillRingCursor]]);
    }
  }

  [kYankPop]() {
    if (!this[kYanking]) {
      return;
    }
    if (this[kKillRing].length > 1) {
      const lastYank = this[kKillRing][this[kKillRingCursor]];
      this[kKillRingCursor]++;
      if (this[kKillRingCursor] >= this[kKillRing].length) {
        this[kKillRingCursor] = 0;
      }
      const currentYank = this[kKillRing][this[kKillRingCursor]];
      const head =
            StringPrototypeSlice(this.line, 0, this.cursor - lastYank.length);
      const tail =
            StringPrototypeSlice(this.line, this.cursor);
      this.line = head + currentYank + tail;
      this.cursor = head.length + currentYank.length;
      this[kRefreshLine]();
    }
  }

  clearLine() {
    this[kMoveCursor](+Infinity);
    this[kWriteToOutput]('\r\n');
    this.line = '';
    this.cursor = 0;
    this.prevRows = 0;
  }

  [kLine]() {
    const line = this[kAddHistory]();
    this[kUndoStack] = [];
    this[kRedoStack] = [];
    this.clearLine();
    this[kOnLine](line);
  }

  [kPushToUndoStack](text, cursor) {
    if (ArrayPrototypePush(this[kUndoStack], { text, cursor }) >
        kMaxUndoRedoStackSize) {
      ArrayPrototypeShift(this[kUndoStack]);
    }
  }

  [kUndo]() {
    if (this[kUndoStack].length <= 0) return;

    ArrayPrototypePush(
      this[kRedoStack],
      { text: this.line, cursor: this.cursor },
    );

    const entry = ArrayPrototypePop(this[kUndoStack]);
    this.line = entry.text;
    this.cursor = entry.cursor;

    this[kRefreshLine]();
  }

  [kRedo]() {
    if (this[kRedoStack].length <= 0) return;

    ArrayPrototypePush(
      this[kUndoStack],
      { text: this.line, cursor: this.cursor },
    );

    const entry = ArrayPrototypePop(this[kRedoStack]);
    this.line = entry.text;
    this.cursor = entry.cursor;

    this[kRefreshLine]();
  }

  // TODO(BridgeAR): Add underscores to the search part and a red background in
  // case no match is found. This should only be the visual part and not the
  // actual line content!
  // TODO(BridgeAR): In case the substring based search is active and the end is
  // reached, show a comment how to search the history as before. E.g., using
  // <ctrl> + N. Only show this after two/three UPs or DOWNs, not on the first
  // one.
  [kHistoryNext]() {
    if (this.historyIndex >= 0) {
      this[kBeforeEdit](this.line, this.cursor);
      const search = this[kSubstringSearch] || '';
      let index = this.historyIndex - 1;
      while (
        index >= 0 &&
        (!StringPrototypeStartsWith(this.history[index], search) ||
          this.line === this.history[index])
      ) {
        index--;
      }
      if (index === -1) {
        this.line = search;
      } else {
        this.line = this.history[index];
      }
      this.historyIndex = index;
      this.cursor = this.line.length; // Set cursor to end of line.
      this[kRefreshLine]();
    }
  }

  [kHistoryPrev]() {
    if (this.historyIndex < this.history.length && this.history.length) {
      this[kBeforeEdit](this.line, this.cursor);
      const search = this[kSubstringSearch] || '';
      let index = this.historyIndex + 1;
      while (
        index < this.history.length &&
        (!StringPrototypeStartsWith(this.history[index], search) ||
          this.line === this.history[index])
      ) {
        index++;
      }
      if (index === this.history.length) {
        this.line = search;
      } else {
        this.line = this.history[index];
      }
      this.historyIndex = index;
      this.cursor = this.line.length; // Set cursor to end of line.
      this[kRefreshLine]();
    }
  }

  // Returns the last character's display position of the given string
  [kGetDisplayPos](str) {
    let offset = 0;
    const col = this.columns;
    let rows = 0;
    str = stripVTControlCharacters(str);
    for (const char of new SafeStringIterator(str)) {
      if (char === '\n') {
        // Rows must be incremented by 1 even if offset = 0 or col = +Infinity.
        rows += MathCeil(offset / col) || 1;
        offset = 0;
        continue;
      }
      // Tabs must be aligned by an offset of the tab size.
      if (char === '\t') {
        offset += this.tabSize - (offset % this.tabSize);
        continue;
      }
      const width = getStringWidth(char, false /* stripVTControlCharacters */);
      if (width === 0 || width === 1) {
        offset += width;
      } else {
        // width === 2
        if ((offset + 1) % col === 0) {
          offset++;
        }
        offset += 2;
      }
    }
    const cols = offset % col;
    rows += (offset - cols) / col;
    return { cols, rows };
  }

  /**
   * Returns the real position of the cursor in relation
   * to the input prompt + string.
   * @returns {{
   *   rows: number;
   *   cols: number;
   *   }}
   */
  getCursorPos() {
    const strBeforeCursor =
      this[kPrompt] + StringPrototypeSlice(this.line, 0, this.cursor);
    return this[kGetDisplayPos](strBeforeCursor);
  }

  // This function moves cursor dx places to the right
  // (-dx for left) and refreshes the line if it is needed.
  [kMoveCursor](dx) {
    if (dx === 0) {
      return;
    }
    const oldPos = this.getCursorPos();
    this.cursor += dx;

    // Bounds check
    if (this.cursor < 0) {
      this.cursor = 0;
    } else if (this.cursor > this.line.length) {
      this.cursor = this.line.length;
    }

    const newPos = this.getCursorPos();

    // Check if cursor stayed on the line.
    if (oldPos.rows === newPos.rows) {
      const diffWidth = newPos.cols - oldPos.cols;
      moveCursor(this.output, diffWidth, 0);
    } else {
      this[kRefreshLine]();
    }
  }

  // Handle a write from the tty
  [kTtyWrite](s, key) {
    const previousKey = this[kPreviousKey];
    key ||= kEmptyObject;
    this[kPreviousKey] = key;

    if (!key.meta || key.name !== 'y') {
      // Reset yanking state unless we are doing yank pop.
      this[kYanking] = false;
    }

    // Activate or deactivate substring search.
    if (
      (key.name === 'up' || key.name === 'down') &&
      !key.ctrl &&
      !key.meta &&
      !key.shift
    ) {
      if (this[kSubstringSearch] === null) {
        this[kSubstringSearch] = StringPrototypeSlice(
          this.line,
          0,
          this.cursor,
        );
      }
    } else if (this[kSubstringSearch] !== null) {
      this[kSubstringSearch] = null;
      // Reset the index in case there's no match.
      if (this.history.length === this.historyIndex) {
        this.historyIndex = -1;
      }
    }

    // Undo & Redo
    if (typeof key.sequence === 'string') {
      switch (StringPrototypeCodePointAt(key.sequence, 0)) {
        case 0x1f:
          this[kUndo]();
          return;
        case 0x1e:
          this[kRedo]();
          return;
        default:
          break;
      }
    }

    // Ignore escape key, fixes
    // https://github.com/nodejs/node-v0.x-archive/issues/2876.
    if (key.name === 'escape') return;

    if (key.ctrl && key.shift) {
      /* Control and shift pressed */
      switch (key.name) {
        // TODO(BridgeAR): The transmitted escape sequence is `\b` and that is
        // identical to <ctrl>-h. It should have a unique escape sequence.
        case 'backspace':
          this[kDeleteLineLeft]();
          break;

        case 'delete':
          this[kDeleteLineRight]();
          break;
      }
    } else if (key.ctrl) {
      /* Control key pressed */

      switch (key.name) {
        case 'c':
          if (this.listenerCount('SIGINT') > 0) {
            this.emit('SIGINT');
          } else {
            // This readline instance is finished
            this.close();
          }
          break;

        case 'h': // delete left
          this[kDeleteLeft]();
          break;

        case 'd': // delete right or EOF
          if (this.cursor === 0 && this.line.length === 0) {
            // This readline instance is finished
            this.close();
          } else if (this.cursor < this.line.length) {
            this[kDeleteRight]();
          }
          break;

        case 'u': // Delete from current to start of line
          this[kDeleteLineLeft]();
          break;

        case 'k': // Delete from current to end of line
          this[kDeleteLineRight]();
          break;

        case 'a': // Go to the start of the line
          this[kMoveCursor](-Infinity);
          break;

        case 'e': // Go to the end of the line
          this[kMoveCursor](+Infinity);
          break;

        case 'b': // back one character
          this[kMoveCursor](-charLengthLeft(this.line, this.cursor));
          break;

        case 'f': // Forward one character
          this[kMoveCursor](+charLengthAt(this.line, this.cursor));
          break;

        case 'l': // Clear the whole screen
          cursorTo(this.output, 0, 0);
          clearScreenDown(this.output);
          this[kRefreshLine]();
          break;

        case 'n': // next history item
          this[kHistoryNext]();
          break;

        case 'p': // Previous history item
          this[kHistoryPrev]();
          break;

        case 'y': // Yank killed string
          this[kYank]();
          break;

        case 'z':
          if (process.platform === 'win32') break;
          if (this.listenerCount('SIGTSTP') > 0) {
            this.emit('SIGTSTP');
          } else {
            process.once('SIGCONT', () => {
              // Don't raise events if stream has already been abandoned.
              if (!this.paused) {
                // Stream must be paused and resumed after SIGCONT to catch
                // SIGINT, SIGTSTP, and EOF.
                this.pause();
                this.emit('SIGCONT');
              }
              // Explicitly re-enable "raw mode" and move the cursor to
              // the correct position.
              // See https://github.com/joyent/node/issues/3295.
              this[kSetRawMode](true);
              this[kRefreshLine]();
            });
            this[kSetRawMode](false);
            process.kill(process.pid, 'SIGTSTP');
          }
          break;

        case 'w': // Delete backwards to a word boundary
        // TODO(BridgeAR): The transmitted escape sequence is `\b` and that is
        // identical to <ctrl>-h. It should have a unique escape sequence.
        // Falls through
        case 'backspace':
          this[kDeleteWordLeft]();
          break;

        case 'delete': // Delete forward to a word boundary
          this[kDeleteWordRight]();
          break;

        case 'left':
          this[kWordLeft]();
          break;

        case 'right':
          this[kWordRight]();
          break;
      }
    } else if (key.meta) {
      /* Meta key pressed */

      switch (key.name) {
        case 'b': // backward word
          this[kWordLeft]();
          break;

        case 'f': // forward word
          this[kWordRight]();
          break;

        case 'd': // delete forward word
        case 'delete':
          this[kDeleteWordRight]();
          break;

        case 'backspace': // Delete backwards to a word boundary
          this[kDeleteWordLeft]();
          break;

        case 'y': // Doing yank pop
          this[kYankPop]();
          break;
      }
    } else {
      /* No modifier keys used */

      // \r bookkeeping is only relevant if a \n comes right after.
      if (this[kSawReturnAt] && key.name !== 'enter') this[kSawReturnAt] = 0;

      switch (key.name) {
        case 'return': // Carriage return, i.e. \r
          this[kSawReturnAt] = DateNow();
          this[kLine]();
          break;

        case 'enter':
          // When key interval > crlfDelay
          if (
            this[kSawReturnAt] === 0 ||
            DateNow() - this[kSawReturnAt] > this.crlfDelay
          ) {
            this[kLine]();
          }
          this[kSawReturnAt] = 0;
          break;

        case 'backspace':
          this[kDeleteLeft]();
          break;

        case 'delete':
          this[kDeleteRight]();
          break;

        case 'left':
          // Obtain the code point to the left
          this[kMoveCursor](-charLengthLeft(this.line, this.cursor));
          break;

        case 'right':
          this[kMoveCursor](+charLengthAt(this.line, this.cursor));
          break;

        case 'home':
          this[kMoveCursor](-Infinity);
          break;

        case 'end':
          this[kMoveCursor](+Infinity);
          break;

        case 'up':
          this[kHistoryPrev]();
          break;

        case 'down':
          this[kHistoryNext]();
          break;

        case 'tab':
          // If tab completion enabled, do that...
          if (
            typeof this.completer === 'function' &&
            this.isCompletionEnabled
          ) {
            const lastKeypressWasTab =
              previousKey && previousKey.name === 'tab';
            this[kTabComplete](lastKeypressWasTab);
            break;
          }
        // falls through
        default:
          if (typeof s === 'string' && s) {
            // Erase state of previous searches.
            lineEnding.lastIndex = 0;
            let nextMatch;
            // Keep track of the end of the last match.
            let lastIndex = 0;
            while ((nextMatch = RegExpPrototypeExec(lineEnding, s)) !== null) {
              this[kInsertString](StringPrototypeSlice(s, lastIndex, nextMatch.index));
              ({ lastIndex } = lineEnding);
              this[kLine]();
              // Restore lastIndex as the call to kLine could have mutated it.
              lineEnding.lastIndex = lastIndex;
            }
            // This ensures that the last line is written if it doesn't end in a newline.
            // Note that the last line may be the first line, in which case this still works.
            this[kInsertString](StringPrototypeSlice(s, lastIndex));
          }
      }
    }
  }

  /**
   * Creates an `AsyncIterator` object that iterates through
   * each line in the input stream as a string.
   * @typedef {{
   *   [Symbol.asyncIterator]: () => InterfaceAsyncIterator,
   *   next: () => Promise<string>
   * }} InterfaceAsyncIterator
   * @returns {InterfaceAsyncIterator}
   */
  [SymbolAsyncIterator]() {
    if (this[kLineObjectStream] === undefined) {
      kFirstEventParam ??= require('internal/events/symbols').kFirstEventParam;
      this[kLineObjectStream] = EventEmitter.on(
        this, 'line', {
          close: ['close'],
          highWaterMark: 1024,
          [kFirstEventParam]: true,
        });
    }
    return this[kLineObjectStream];
  }
}

module.exports = {
  Interface,
  InterfaceConstructor,
  kAddHistory,
  kDecoder,
  kDeleteLeft,
  kDeleteLineLeft,
  kDeleteLineRight,
  kDeleteRight,
  kDeleteWordLeft,
  kDeleteWordRight,
  kGetDisplayPos,
  kHistoryNext,
  kHistoryPrev,
  kInsertString,
  kLine,
  kLine_buffer,
  kMoveCursor,
  kNormalWrite,
  kOldPrompt,
  kOnLine,
  kPreviousKey,
  kPrompt,
  kQuestion,
  kQuestionCallback,
  kQuestionCancel,
  kRefreshLine,
  kSawKeyPress,
  kSawReturnAt,
  kSetRawMode,
  kTabComplete,
  kTabCompleter,
  kTtyWrite,
  kWordLeft,
  kWordRight,
  kWriteToOutput,
};
