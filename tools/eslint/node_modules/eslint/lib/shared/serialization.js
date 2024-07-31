/**
 * @fileoverview Serialization utils.
 * @author Bryan Mishkin
 */

"use strict";

/**
 * Check if a value is a primitive or plain object created by the Object constructor.
 * @param {any} val the value to check
 * @returns {boolean} true if so
 * @private
 */
function isSerializablePrimitiveOrPlainObject(val) {
    return (
        val === null ||
        typeof val === "string" ||
         typeof val === "boolean" ||
         typeof val === "number" ||
         (typeof val === "object" && val.constructor === Object) ||
         Array.isArray(val)
    );
}

/**
 * Check if a value is serializable.
 * Functions or objects like RegExp cannot be serialized by JSON.stringify().
 * Inspired by: https://stackoverflow.com/questions/30579940/reliable-way-to-check-if-objects-is-serializable-in-javascript
 * @param {any} val the value
 * @returns {boolean} true if the value is serializable
 */
function isSerializable(val) {
    if (!isSerializablePrimitiveOrPlainObject(val)) {
        return false;
    }
    if (typeof val === "object") {
        for (const property in val) {
            if (Object.hasOwn(val, property)) {
                if (!isSerializablePrimitiveOrPlainObject(val[property])) {
                    return false;
                }
                if (typeof val[property] === "object") {
                    if (!isSerializable(val[property])) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

module.exports = {
    isSerializable
};
