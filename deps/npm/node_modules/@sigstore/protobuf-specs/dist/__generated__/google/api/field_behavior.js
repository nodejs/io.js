"use strict";
/* eslint-disable */
Object.defineProperty(exports, "__esModule", { value: true });
exports.FieldBehavior = void 0;
exports.fieldBehaviorFromJSON = fieldBehaviorFromJSON;
exports.fieldBehaviorToJSON = fieldBehaviorToJSON;
/**
 * An indicator of the behavior of a given field (for example, that a field
 * is required in requests, or given as output but ignored as input).
 * This **does not** change the behavior in protocol buffers itself; it only
 * denotes the behavior and may affect how API tooling handles the field.
 *
 * Note: This enum **may** receive new values in the future.
 */
var FieldBehavior;
(function (FieldBehavior) {
    /** FIELD_BEHAVIOR_UNSPECIFIED - Conventional default for enums. Do not use this. */
    FieldBehavior[FieldBehavior["FIELD_BEHAVIOR_UNSPECIFIED"] = 0] = "FIELD_BEHAVIOR_UNSPECIFIED";
    /**
     * OPTIONAL - Specifically denotes a field as optional.
     * While all fields in protocol buffers are optional, this may be specified
     * for emphasis if appropriate.
     */
    FieldBehavior[FieldBehavior["OPTIONAL"] = 1] = "OPTIONAL";
    /**
     * REQUIRED - Denotes a field as required.
     * This indicates that the field **must** be provided as part of the request,
     * and failure to do so will cause an error (usually `INVALID_ARGUMENT`).
     */
    FieldBehavior[FieldBehavior["REQUIRED"] = 2] = "REQUIRED";
    /**
     * OUTPUT_ONLY - Denotes a field as output only.
     * This indicates that the field is provided in responses, but including the
     * field in a request does nothing (the server *must* ignore it and
     * *must not* throw an error as a result of the field's presence).
     */
    FieldBehavior[FieldBehavior["OUTPUT_ONLY"] = 3] = "OUTPUT_ONLY";
    /**
     * INPUT_ONLY - Denotes a field as input only.
     * This indicates that the field is provided in requests, and the
     * corresponding field is not included in output.
     */
    FieldBehavior[FieldBehavior["INPUT_ONLY"] = 4] = "INPUT_ONLY";
    /**
     * IMMUTABLE - Denotes a field as immutable.
     * This indicates that the field may be set once in a request to create a
     * resource, but may not be changed thereafter.
     */
    FieldBehavior[FieldBehavior["IMMUTABLE"] = 5] = "IMMUTABLE";
    /**
     * UNORDERED_LIST - Denotes that a (repeated) field is an unordered list.
     * This indicates that the service may provide the elements of the list
     * in any arbitrary order, rather than the order the user originally
     * provided. Additionally, the list's order may or may not be stable.
     */
    FieldBehavior[FieldBehavior["UNORDERED_LIST"] = 6] = "UNORDERED_LIST";
})(FieldBehavior || (exports.FieldBehavior = FieldBehavior = {}));
function fieldBehaviorFromJSON(object) {
    switch (object) {
        case 0:
        case "FIELD_BEHAVIOR_UNSPECIFIED":
            return FieldBehavior.FIELD_BEHAVIOR_UNSPECIFIED;
        case 1:
        case "OPTIONAL":
            return FieldBehavior.OPTIONAL;
        case 2:
        case "REQUIRED":
            return FieldBehavior.REQUIRED;
        case 3:
        case "OUTPUT_ONLY":
            return FieldBehavior.OUTPUT_ONLY;
        case 4:
        case "INPUT_ONLY":
            return FieldBehavior.INPUT_ONLY;
        case 5:
        case "IMMUTABLE":
            return FieldBehavior.IMMUTABLE;
        case 6:
        case "UNORDERED_LIST":
            return FieldBehavior.UNORDERED_LIST;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldBehavior");
    }
}
function fieldBehaviorToJSON(object) {
    switch (object) {
        case FieldBehavior.FIELD_BEHAVIOR_UNSPECIFIED:
            return "FIELD_BEHAVIOR_UNSPECIFIED";
        case FieldBehavior.OPTIONAL:
            return "OPTIONAL";
        case FieldBehavior.REQUIRED:
            return "REQUIRED";
        case FieldBehavior.OUTPUT_ONLY:
            return "OUTPUT_ONLY";
        case FieldBehavior.INPUT_ONLY:
            return "INPUT_ONLY";
        case FieldBehavior.IMMUTABLE:
            return "IMMUTABLE";
        case FieldBehavior.UNORDERED_LIST:
            return "UNORDERED_LIST";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldBehavior");
    }
}
var tsProtoGlobalThis = (() => {
    if (typeof globalThis !== "undefined") {
        return globalThis;
    }
    if (typeof self !== "undefined") {
        return self;
    }
    if (typeof window !== "undefined") {
        return window;
    }
    if (typeof global !== "undefined") {
        return global;
    }
    throw "Unable to locate global object";
})();
