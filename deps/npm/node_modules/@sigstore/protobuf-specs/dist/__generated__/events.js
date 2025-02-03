"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.CloudEventBatch = exports.CloudEvent_CloudEventAttributeValue = exports.CloudEvent_AttributesEntry = exports.CloudEvent = void 0;
/* eslint-disable */
const any_1 = require("./google/protobuf/any");
const timestamp_1 = require("./google/protobuf/timestamp");
function createBaseCloudEvent() {
    return { id: "", source: "", specVersion: "", type: "", attributes: {}, data: undefined };
}
exports.CloudEvent = {
    fromJSON(object) {
        return {
            id: isSet(object.id) ? String(object.id) : "",
            source: isSet(object.source) ? String(object.source) : "",
            specVersion: isSet(object.specVersion) ? String(object.specVersion) : "",
            type: isSet(object.type) ? String(object.type) : "",
            attributes: isObject(object.attributes)
                ? Object.entries(object.attributes).reduce((acc, [key, value]) => {
                    acc[key] = exports.CloudEvent_CloudEventAttributeValue.fromJSON(value);
                    return acc;
                }, {})
                : {},
            data: isSet(object.binaryData)
                ? { $case: "binaryData", binaryData: Buffer.from(bytesFromBase64(object.binaryData)) }
                : isSet(object.textData)
                    ? { $case: "textData", textData: String(object.textData) }
                    : isSet(object.protoData)
                        ? { $case: "protoData", protoData: any_1.Any.fromJSON(object.protoData) }
                        : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.id !== undefined && (obj.id = message.id);
        message.source !== undefined && (obj.source = message.source);
        message.specVersion !== undefined && (obj.specVersion = message.specVersion);
        message.type !== undefined && (obj.type = message.type);
        obj.attributes = {};
        if (message.attributes) {
            Object.entries(message.attributes).forEach(([k, v]) => {
                obj.attributes[k] = exports.CloudEvent_CloudEventAttributeValue.toJSON(v);
            });
        }
        message.data?.$case === "binaryData" &&
            (obj.binaryData = message.data?.binaryData !== undefined ? base64FromBytes(message.data?.binaryData) : undefined);
        message.data?.$case === "textData" && (obj.textData = message.data?.textData);
        message.data?.$case === "protoData" &&
            (obj.protoData = message.data?.protoData ? any_1.Any.toJSON(message.data?.protoData) : undefined);
        return obj;
    },
};
function createBaseCloudEvent_AttributesEntry() {
    return { key: "", value: undefined };
}
exports.CloudEvent_AttributesEntry = {
    fromJSON(object) {
        return {
            key: isSet(object.key) ? String(object.key) : "",
            value: isSet(object.value) ? exports.CloudEvent_CloudEventAttributeValue.fromJSON(object.value) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.key !== undefined && (obj.key = message.key);
        message.value !== undefined &&
            (obj.value = message.value ? exports.CloudEvent_CloudEventAttributeValue.toJSON(message.value) : undefined);
        return obj;
    },
};
function createBaseCloudEvent_CloudEventAttributeValue() {
    return { attr: undefined };
}
exports.CloudEvent_CloudEventAttributeValue = {
    fromJSON(object) {
        return {
            attr: isSet(object.ceBoolean)
                ? { $case: "ceBoolean", ceBoolean: Boolean(object.ceBoolean) }
                : isSet(object.ceInteger)
                    ? { $case: "ceInteger", ceInteger: Number(object.ceInteger) }
                    : isSet(object.ceString)
                        ? { $case: "ceString", ceString: String(object.ceString) }
                        : isSet(object.ceBytes)
                            ? { $case: "ceBytes", ceBytes: Buffer.from(bytesFromBase64(object.ceBytes)) }
                            : isSet(object.ceUri)
                                ? { $case: "ceUri", ceUri: String(object.ceUri) }
                                : isSet(object.ceUriRef)
                                    ? { $case: "ceUriRef", ceUriRef: String(object.ceUriRef) }
                                    : isSet(object.ceTimestamp)
                                        ? { $case: "ceTimestamp", ceTimestamp: fromJsonTimestamp(object.ceTimestamp) }
                                        : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.attr?.$case === "ceBoolean" && (obj.ceBoolean = message.attr?.ceBoolean);
        message.attr?.$case === "ceInteger" && (obj.ceInteger = Math.round(message.attr?.ceInteger));
        message.attr?.$case === "ceString" && (obj.ceString = message.attr?.ceString);
        message.attr?.$case === "ceBytes" &&
            (obj.ceBytes = message.attr?.ceBytes !== undefined ? base64FromBytes(message.attr?.ceBytes) : undefined);
        message.attr?.$case === "ceUri" && (obj.ceUri = message.attr?.ceUri);
        message.attr?.$case === "ceUriRef" && (obj.ceUriRef = message.attr?.ceUriRef);
        message.attr?.$case === "ceTimestamp" && (obj.ceTimestamp = message.attr?.ceTimestamp.toISOString());
        return obj;
    },
};
function createBaseCloudEventBatch() {
    return { events: [] };
}
exports.CloudEventBatch = {
    fromJSON(object) {
        return { events: Array.isArray(object?.events) ? object.events.map((e) => exports.CloudEvent.fromJSON(e)) : [] };
    },
    toJSON(message) {
        const obj = {};
        if (message.events) {
            obj.events = message.events.map((e) => e ? exports.CloudEvent.toJSON(e) : undefined);
        }
        else {
            obj.events = [];
        }
        return obj;
    },
};
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
function bytesFromBase64(b64) {
    if (tsProtoGlobalThis.Buffer) {
        return Uint8Array.from(tsProtoGlobalThis.Buffer.from(b64, "base64"));
    }
    else {
        const bin = tsProtoGlobalThis.atob(b64);
        const arr = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; ++i) {
            arr[i] = bin.charCodeAt(i);
        }
        return arr;
    }
}
function base64FromBytes(arr) {
    if (tsProtoGlobalThis.Buffer) {
        return tsProtoGlobalThis.Buffer.from(arr).toString("base64");
    }
    else {
        const bin = [];
        arr.forEach((byte) => {
            bin.push(String.fromCharCode(byte));
        });
        return tsProtoGlobalThis.btoa(bin.join(""));
    }
}
function fromTimestamp(t) {
    let millis = Number(t.seconds) * 1_000;
    millis += t.nanos / 1_000_000;
    return new Date(millis);
}
function fromJsonTimestamp(o) {
    if (o instanceof Date) {
        return o;
    }
    else if (typeof o === "string") {
        return new Date(o);
    }
    else {
        return fromTimestamp(timestamp_1.Timestamp.fromJSON(o));
    }
}
function isObject(value) {
    return typeof value === "object" && value !== null;
}
function isSet(value) {
    return value !== null && value !== undefined;
}
