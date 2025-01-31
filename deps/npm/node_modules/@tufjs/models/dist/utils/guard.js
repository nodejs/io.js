"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.isDefined = isDefined;
exports.isObject = isObject;
exports.isStringArray = isStringArray;
exports.isObjectArray = isObjectArray;
exports.isStringRecord = isStringRecord;
exports.isObjectRecord = isObjectRecord;
function isDefined(val) {
    return val !== undefined;
}
function isObject(value) {
    return typeof value === 'object' && value !== null;
}
function isStringArray(value) {
    return Array.isArray(value) && value.every((v) => typeof v === 'string');
}
function isObjectArray(value) {
    return Array.isArray(value) && value.every(isObject);
}
function isStringRecord(value) {
    return (typeof value === 'object' &&
        value !== null &&
        Object.keys(value).every((k) => typeof k === 'string') &&
        Object.values(value).every((v) => typeof v === 'string'));
}
function isObjectRecord(value) {
    return (typeof value === 'object' &&
        value !== null &&
        Object.keys(value).every((k) => typeof k === 'string') &&
        Object.values(value).every((v) => typeof v === 'object' && v !== null));
}
