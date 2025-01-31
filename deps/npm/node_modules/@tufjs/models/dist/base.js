"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.Signed = exports.MetadataKind = void 0;
exports.isMetadataKind = isMetadataKind;
const util_1 = __importDefault(require("util"));
const error_1 = require("./error");
const utils_1 = require("./utils");
const SPECIFICATION_VERSION = ['1', '0', '31'];
var MetadataKind;
(function (MetadataKind) {
    MetadataKind["Root"] = "root";
    MetadataKind["Timestamp"] = "timestamp";
    MetadataKind["Snapshot"] = "snapshot";
    MetadataKind["Targets"] = "targets";
})(MetadataKind || (exports.MetadataKind = MetadataKind = {}));
function isMetadataKind(value) {
    return (typeof value === 'string' &&
        Object.values(MetadataKind).includes(value));
}
/***
 * A base class for the signed part of TUF metadata.
 *
 * Objects with base class Signed are usually included in a ``Metadata`` object
 * on the signed attribute. This class provides attributes and methods that
 * are common for all TUF metadata types (roles).
 */
class Signed {
    constructor(options) {
        this.specVersion = options.specVersion || SPECIFICATION_VERSION.join('.');
        const specList = this.specVersion.split('.');
        if (!(specList.length === 2 || specList.length === 3) ||
            !specList.every((item) => isNumeric(item))) {
            throw new error_1.ValueError('Failed to parse specVersion');
        }
        // major version must match
        if (specList[0] != SPECIFICATION_VERSION[0]) {
            throw new error_1.ValueError('Unsupported specVersion');
        }
        this.expires = options.expires;
        this.version = options.version;
        this.unrecognizedFields = options.unrecognizedFields || {};
    }
    equals(other) {
        if (!(other instanceof Signed)) {
            return false;
        }
        return (this.specVersion === other.specVersion &&
            this.expires === other.expires &&
            this.version === other.version &&
            util_1.default.isDeepStrictEqual(this.unrecognizedFields, other.unrecognizedFields));
    }
    isExpired(referenceTime) {
        if (!referenceTime) {
            referenceTime = new Date();
        }
        return referenceTime >= new Date(this.expires);
    }
    static commonFieldsFromJSON(data) {
        const { spec_version, expires, version, ...rest } = data;
        if (!utils_1.guard.isDefined(spec_version)) {
            throw new error_1.ValueError('spec_version is not defined');
        }
        else if (typeof spec_version !== 'string') {
            throw new TypeError('spec_version must be a string');
        }
        if (!utils_1.guard.isDefined(expires)) {
            throw new error_1.ValueError('expires is not defined');
        }
        else if (!(typeof expires === 'string')) {
            throw new TypeError('expires must be a string');
        }
        if (!utils_1.guard.isDefined(version)) {
            throw new error_1.ValueError('version is not defined');
        }
        else if (!(typeof version === 'number')) {
            throw new TypeError('version must be a number');
        }
        return {
            specVersion: spec_version,
            expires,
            version,
            unrecognizedFields: rest,
        };
    }
}
exports.Signed = Signed;
function isNumeric(str) {
    return !isNaN(Number(str));
}
