'use strict';

const {
  MapPrototypeEntries,
  NumberIsNaN,
  NumberMAX_SAFE_INTEGER,
  ObjectFromEntries,
  ReflectConstruct,
  SafeMap,
  Symbol,
} = primordials;

const {
  Histogram: _Histogram,
} = internalBinding('performance');

const {
  customInspectSymbol: kInspect,
  kEmptyObject,
} = require('internal/util');

const { inspect } = require('util');

const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_THIS,
    ERR_OUT_OF_RANGE,
  },
} = require('internal/errors');

const {
  validateInteger,
  validateNumber,
  validateObject,
} = require('internal/validators');

const kDestroy = Symbol('kDestroy');
const kHandle = Symbol('kHandle');
const kMap = Symbol('kMap');
const kRecordable = Symbol('kRecordable');

const {
  kClone,
  kDeserialize,
  markTransferMode,
} = require('internal/worker/js_transferable');

function isHistogram(object) {
  return object?.[kHandle] !== undefined;
}

const kSkipThrow = Symbol('kSkipThrow');

class Histogram {
  constructor(skipThrowSymbol = undefined) {
    if (skipThrowSymbol !== kSkipThrow) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `Histogram ${inspect({
      min: this.min,
      max: this.max,
      mean: this.mean,
      exceeds: this.exceeds,
      stddev: this.stddev,
      count: this.count,
      percentiles: this.percentiles,
    }, opts)}`;
  }

  /**
   * @readonly
   * @type {number}
   */
  get count() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.count();
  }

  /**
   * @readonly
   * @type {bigint}
   */
  get countBigInt() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.countBigInt();
  }

  /**
   * @readonly
   * @type {number}
   */
  get min() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.min();
  }

  /**
   * @readonly
   * @type {bigint}
   */
  get minBigInt() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.minBigInt();
  }

  /**
   * @readonly
   * @type {number}
   */
  get max() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.max();
  }

  /**
   * @readonly
   * @type {bigint}
   */
  get maxBigInt() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.maxBigInt();
  }

  /**
   * @readonly
   * @type {number}
   */
  get mean() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.mean();
  }

  /**
   * @readonly
   * @type {number}
   */
  get exceeds() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.exceeds();
  }

  /**
   * @readonly
   * @type {bigint}
   */
  get exceedsBigInt() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.exceedsBigInt();
  }

  /**
   * @readonly
   * @type {number}
   */
  get stddev() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    return this[kHandle]?.stddev();
  }

  /**
   * @param {number} percentile
   * @returns {number}
   */
  percentile(percentile) {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    validateNumber(percentile, 'percentile');
    if (NumberIsNaN(percentile) || percentile <= 0 || percentile > 100)
      throw new ERR_OUT_OF_RANGE('percentile', '> 0 && <= 100', percentile);

    return this[kHandle]?.percentile(percentile);
  }

  /**
   * @param {number} percentile
   * @returns {bigint}
   */
  percentileBigInt(percentile) {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    validateNumber(percentile, 'percentile');
    if (NumberIsNaN(percentile) || percentile <= 0 || percentile > 100)
      throw new ERR_OUT_OF_RANGE('percentile', '> 0 && <= 100', percentile);

    return this[kHandle]?.percentileBigInt(percentile);
  }

  /**
   * @readonly
   * @type {Map<number,number>}
   */
  get percentiles() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    this[kMap].clear();
    this[kHandle]?.percentiles(this[kMap]);
    return this[kMap];
  }

  /**
   * @readonly
   * @type {Map<number,bigint>}
   */
  get percentilesBigInt() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    this[kMap].clear();
    this[kHandle]?.percentilesBigInt(this[kMap]);
    return this[kMap];
  }

  /**
   * @returns {void}
   */
  reset() {
    if (!isHistogram(this))
      throw new ERR_INVALID_THIS('Histogram');
    this[kHandle]?.reset();
  }

  [kClone]() {
    const handle = this[kHandle];
    return {
      data: { handle },
      deserializeInfo: 'internal/histogram:ClonedHistogram',
    };
  }

  [kDeserialize]({ handle }) {
    this[kHandle] = handle;
  }

  toJSON() {
    return {
      count: this.count,
      min: this.min,
      max: this.max,
      mean: this.mean,
      exceeds: this.exceeds,
      stddev: this.stddev,
      percentiles: ObjectFromEntries(MapPrototypeEntries(this.percentiles)),
    };
  }
}

class RecordableHistogram extends Histogram {
  constructor(skipThrowSymbol = undefined) {
    if (skipThrowSymbol !== kSkipThrow) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }

    super(skipThrowSymbol);
  }

  /**
   * @param {number|bigint} val
   * @returns {void}
   */
  record(val) {
    if (this[kRecordable] === undefined)
      throw new ERR_INVALID_THIS('RecordableHistogram');
    if (typeof val === 'bigint') {
      this[kHandle]?.record(val);
      return;
    }

    validateInteger(val, 'val', 1);

    this[kHandle]?.record(val);
  }

  /**
   * @returns {void}
   */
  recordDelta() {
    if (this[kRecordable] === undefined)
      throw new ERR_INVALID_THIS('RecordableHistogram');
    this[kHandle]?.recordDelta();
  }

  /**
   * @param {RecordableHistogram} other
   */
  add(other) {
    if (this[kRecordable] === undefined)
      throw new ERR_INVALID_THIS('RecordableHistogram');
    if (other[kRecordable] === undefined)
      throw new ERR_INVALID_ARG_TYPE('other', 'RecordableHistogram', other);
    this[kHandle]?.add(other[kHandle]);
  }

  [kClone]() {
    const handle = this[kHandle];
    return {
      data: { handle },
      deserializeInfo: 'internal/histogram:ClonedRecordableHistogram',
    };
  }

  [kDeserialize]({ handle }) {
    this[kHandle] = handle;
  }
}

function ClonedHistogram(handle) {
  return ReflectConstruct(
    function() {
      markTransferMode(this, true, false);
      this[kHandle] = handle;
      this[kMap] = new SafeMap();
    }, [], Histogram);
}

ClonedHistogram.prototype[kDeserialize] = () => { };

function ClonedRecordableHistogram(handle) {
  const histogram = new RecordableHistogram(kSkipThrow);

  markTransferMode(histogram, true, false);
  histogram[kRecordable] = true;
  histogram[kMap] = new SafeMap();
  histogram[kHandle] = handle;
  histogram.constructor = RecordableHistogram;

  return histogram;
}

ClonedRecordableHistogram.prototype[kDeserialize] = () => { };

function createRecordableHistogram(handle) {
  return new ClonedRecordableHistogram(handle);
}

/**
 * @param {{
 *   lowest? : number,
 *   highest? : number,
 *   figures? : number
 * }} [options]
 * @returns {RecordableHistogram}
 */
function createHistogram(options = kEmptyObject) {
  validateObject(options, 'options');
  const {
    lowest = 1,
    highest = NumberMAX_SAFE_INTEGER,
    figures = 3,
  } = options;
  if (typeof lowest !== 'bigint')
    validateInteger(lowest, 'options.lowest', 1, NumberMAX_SAFE_INTEGER);
  if (typeof highest !== 'bigint') {
    validateInteger(highest, 'options.highest',
                    2 * lowest, NumberMAX_SAFE_INTEGER);
  } else if (highest < 2n * lowest) {
    throw new ERR_INVALID_ARG_VALUE.RangeError('options.highest', highest);
  }
  validateInteger(figures, 'options.figures', 1, 5);
  return createRecordableHistogram(new _Histogram(lowest, highest, figures));
}

module.exports = {
  Histogram,
  RecordableHistogram,
  ClonedHistogram,
  ClonedRecordableHistogram,
  isHistogram,
  kDestroy,
  kHandle,
  kMap,
  createHistogram,
};
