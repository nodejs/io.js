"use strict";
/* eslint-disable */
Object.defineProperty(exports, "__esModule", { value: true });
exports.GeneratedCodeInfo_Annotation = exports.GeneratedCodeInfo = exports.SourceCodeInfo_Location = exports.SourceCodeInfo = exports.UninterpretedOption_NamePart = exports.UninterpretedOption = exports.MethodOptions = exports.ServiceOptions = exports.EnumValueOptions = exports.EnumOptions = exports.OneofOptions = exports.FieldOptions = exports.MessageOptions = exports.FileOptions = exports.MethodDescriptorProto = exports.ServiceDescriptorProto = exports.EnumValueDescriptorProto = exports.EnumDescriptorProto_EnumReservedRange = exports.EnumDescriptorProto = exports.OneofDescriptorProto = exports.FieldDescriptorProto = exports.ExtensionRangeOptions = exports.DescriptorProto_ReservedRange = exports.DescriptorProto_ExtensionRange = exports.DescriptorProto = exports.FileDescriptorProto = exports.FileDescriptorSet = exports.MethodOptions_IdempotencyLevel = exports.FieldOptions_JSType = exports.FieldOptions_CType = exports.FileOptions_OptimizeMode = exports.FieldDescriptorProto_Label = exports.FieldDescriptorProto_Type = void 0;
exports.fieldDescriptorProto_TypeFromJSON = fieldDescriptorProto_TypeFromJSON;
exports.fieldDescriptorProto_TypeToJSON = fieldDescriptorProto_TypeToJSON;
exports.fieldDescriptorProto_LabelFromJSON = fieldDescriptorProto_LabelFromJSON;
exports.fieldDescriptorProto_LabelToJSON = fieldDescriptorProto_LabelToJSON;
exports.fileOptions_OptimizeModeFromJSON = fileOptions_OptimizeModeFromJSON;
exports.fileOptions_OptimizeModeToJSON = fileOptions_OptimizeModeToJSON;
exports.fieldOptions_CTypeFromJSON = fieldOptions_CTypeFromJSON;
exports.fieldOptions_CTypeToJSON = fieldOptions_CTypeToJSON;
exports.fieldOptions_JSTypeFromJSON = fieldOptions_JSTypeFromJSON;
exports.fieldOptions_JSTypeToJSON = fieldOptions_JSTypeToJSON;
exports.methodOptions_IdempotencyLevelFromJSON = methodOptions_IdempotencyLevelFromJSON;
exports.methodOptions_IdempotencyLevelToJSON = methodOptions_IdempotencyLevelToJSON;
var FieldDescriptorProto_Type;
(function (FieldDescriptorProto_Type) {
    /**
     * TYPE_DOUBLE - 0 is reserved for errors.
     * Order is weird for historical reasons.
     */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_DOUBLE"] = 1] = "TYPE_DOUBLE";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_FLOAT"] = 2] = "TYPE_FLOAT";
    /**
     * TYPE_INT64 - Not ZigZag encoded.  Negative numbers take 10 bytes.  Use TYPE_SINT64 if
     * negative values are likely.
     */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_INT64"] = 3] = "TYPE_INT64";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_UINT64"] = 4] = "TYPE_UINT64";
    /**
     * TYPE_INT32 - Not ZigZag encoded.  Negative numbers take 10 bytes.  Use TYPE_SINT32 if
     * negative values are likely.
     */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_INT32"] = 5] = "TYPE_INT32";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_FIXED64"] = 6] = "TYPE_FIXED64";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_FIXED32"] = 7] = "TYPE_FIXED32";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_BOOL"] = 8] = "TYPE_BOOL";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_STRING"] = 9] = "TYPE_STRING";
    /**
     * TYPE_GROUP - Tag-delimited aggregate.
     * Group type is deprecated and not supported in proto3. However, Proto3
     * implementations should still be able to parse the group wire format and
     * treat group fields as unknown fields.
     */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_GROUP"] = 10] = "TYPE_GROUP";
    /** TYPE_MESSAGE - Length-delimited aggregate. */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_MESSAGE"] = 11] = "TYPE_MESSAGE";
    /** TYPE_BYTES - New in version 2. */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_BYTES"] = 12] = "TYPE_BYTES";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_UINT32"] = 13] = "TYPE_UINT32";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_ENUM"] = 14] = "TYPE_ENUM";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_SFIXED32"] = 15] = "TYPE_SFIXED32";
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_SFIXED64"] = 16] = "TYPE_SFIXED64";
    /** TYPE_SINT32 - Uses ZigZag encoding. */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_SINT32"] = 17] = "TYPE_SINT32";
    /** TYPE_SINT64 - Uses ZigZag encoding. */
    FieldDescriptorProto_Type[FieldDescriptorProto_Type["TYPE_SINT64"] = 18] = "TYPE_SINT64";
})(FieldDescriptorProto_Type || (exports.FieldDescriptorProto_Type = FieldDescriptorProto_Type = {}));
function fieldDescriptorProto_TypeFromJSON(object) {
    switch (object) {
        case 1:
        case "TYPE_DOUBLE":
            return FieldDescriptorProto_Type.TYPE_DOUBLE;
        case 2:
        case "TYPE_FLOAT":
            return FieldDescriptorProto_Type.TYPE_FLOAT;
        case 3:
        case "TYPE_INT64":
            return FieldDescriptorProto_Type.TYPE_INT64;
        case 4:
        case "TYPE_UINT64":
            return FieldDescriptorProto_Type.TYPE_UINT64;
        case 5:
        case "TYPE_INT32":
            return FieldDescriptorProto_Type.TYPE_INT32;
        case 6:
        case "TYPE_FIXED64":
            return FieldDescriptorProto_Type.TYPE_FIXED64;
        case 7:
        case "TYPE_FIXED32":
            return FieldDescriptorProto_Type.TYPE_FIXED32;
        case 8:
        case "TYPE_BOOL":
            return FieldDescriptorProto_Type.TYPE_BOOL;
        case 9:
        case "TYPE_STRING":
            return FieldDescriptorProto_Type.TYPE_STRING;
        case 10:
        case "TYPE_GROUP":
            return FieldDescriptorProto_Type.TYPE_GROUP;
        case 11:
        case "TYPE_MESSAGE":
            return FieldDescriptorProto_Type.TYPE_MESSAGE;
        case 12:
        case "TYPE_BYTES":
            return FieldDescriptorProto_Type.TYPE_BYTES;
        case 13:
        case "TYPE_UINT32":
            return FieldDescriptorProto_Type.TYPE_UINT32;
        case 14:
        case "TYPE_ENUM":
            return FieldDescriptorProto_Type.TYPE_ENUM;
        case 15:
        case "TYPE_SFIXED32":
            return FieldDescriptorProto_Type.TYPE_SFIXED32;
        case 16:
        case "TYPE_SFIXED64":
            return FieldDescriptorProto_Type.TYPE_SFIXED64;
        case 17:
        case "TYPE_SINT32":
            return FieldDescriptorProto_Type.TYPE_SINT32;
        case 18:
        case "TYPE_SINT64":
            return FieldDescriptorProto_Type.TYPE_SINT64;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldDescriptorProto_Type");
    }
}
function fieldDescriptorProto_TypeToJSON(object) {
    switch (object) {
        case FieldDescriptorProto_Type.TYPE_DOUBLE:
            return "TYPE_DOUBLE";
        case FieldDescriptorProto_Type.TYPE_FLOAT:
            return "TYPE_FLOAT";
        case FieldDescriptorProto_Type.TYPE_INT64:
            return "TYPE_INT64";
        case FieldDescriptorProto_Type.TYPE_UINT64:
            return "TYPE_UINT64";
        case FieldDescriptorProto_Type.TYPE_INT32:
            return "TYPE_INT32";
        case FieldDescriptorProto_Type.TYPE_FIXED64:
            return "TYPE_FIXED64";
        case FieldDescriptorProto_Type.TYPE_FIXED32:
            return "TYPE_FIXED32";
        case FieldDescriptorProto_Type.TYPE_BOOL:
            return "TYPE_BOOL";
        case FieldDescriptorProto_Type.TYPE_STRING:
            return "TYPE_STRING";
        case FieldDescriptorProto_Type.TYPE_GROUP:
            return "TYPE_GROUP";
        case FieldDescriptorProto_Type.TYPE_MESSAGE:
            return "TYPE_MESSAGE";
        case FieldDescriptorProto_Type.TYPE_BYTES:
            return "TYPE_BYTES";
        case FieldDescriptorProto_Type.TYPE_UINT32:
            return "TYPE_UINT32";
        case FieldDescriptorProto_Type.TYPE_ENUM:
            return "TYPE_ENUM";
        case FieldDescriptorProto_Type.TYPE_SFIXED32:
            return "TYPE_SFIXED32";
        case FieldDescriptorProto_Type.TYPE_SFIXED64:
            return "TYPE_SFIXED64";
        case FieldDescriptorProto_Type.TYPE_SINT32:
            return "TYPE_SINT32";
        case FieldDescriptorProto_Type.TYPE_SINT64:
            return "TYPE_SINT64";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldDescriptorProto_Type");
    }
}
var FieldDescriptorProto_Label;
(function (FieldDescriptorProto_Label) {
    /** LABEL_OPTIONAL - 0 is reserved for errors */
    FieldDescriptorProto_Label[FieldDescriptorProto_Label["LABEL_OPTIONAL"] = 1] = "LABEL_OPTIONAL";
    FieldDescriptorProto_Label[FieldDescriptorProto_Label["LABEL_REQUIRED"] = 2] = "LABEL_REQUIRED";
    FieldDescriptorProto_Label[FieldDescriptorProto_Label["LABEL_REPEATED"] = 3] = "LABEL_REPEATED";
})(FieldDescriptorProto_Label || (exports.FieldDescriptorProto_Label = FieldDescriptorProto_Label = {}));
function fieldDescriptorProto_LabelFromJSON(object) {
    switch (object) {
        case 1:
        case "LABEL_OPTIONAL":
            return FieldDescriptorProto_Label.LABEL_OPTIONAL;
        case 2:
        case "LABEL_REQUIRED":
            return FieldDescriptorProto_Label.LABEL_REQUIRED;
        case 3:
        case "LABEL_REPEATED":
            return FieldDescriptorProto_Label.LABEL_REPEATED;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldDescriptorProto_Label");
    }
}
function fieldDescriptorProto_LabelToJSON(object) {
    switch (object) {
        case FieldDescriptorProto_Label.LABEL_OPTIONAL:
            return "LABEL_OPTIONAL";
        case FieldDescriptorProto_Label.LABEL_REQUIRED:
            return "LABEL_REQUIRED";
        case FieldDescriptorProto_Label.LABEL_REPEATED:
            return "LABEL_REPEATED";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldDescriptorProto_Label");
    }
}
/** Generated classes can be optimized for speed or code size. */
var FileOptions_OptimizeMode;
(function (FileOptions_OptimizeMode) {
    /** SPEED - Generate complete code for parsing, serialization, */
    FileOptions_OptimizeMode[FileOptions_OptimizeMode["SPEED"] = 1] = "SPEED";
    /** CODE_SIZE - etc. */
    FileOptions_OptimizeMode[FileOptions_OptimizeMode["CODE_SIZE"] = 2] = "CODE_SIZE";
    /** LITE_RUNTIME - Generate code using MessageLite and the lite runtime. */
    FileOptions_OptimizeMode[FileOptions_OptimizeMode["LITE_RUNTIME"] = 3] = "LITE_RUNTIME";
})(FileOptions_OptimizeMode || (exports.FileOptions_OptimizeMode = FileOptions_OptimizeMode = {}));
function fileOptions_OptimizeModeFromJSON(object) {
    switch (object) {
        case 1:
        case "SPEED":
            return FileOptions_OptimizeMode.SPEED;
        case 2:
        case "CODE_SIZE":
            return FileOptions_OptimizeMode.CODE_SIZE;
        case 3:
        case "LITE_RUNTIME":
            return FileOptions_OptimizeMode.LITE_RUNTIME;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FileOptions_OptimizeMode");
    }
}
function fileOptions_OptimizeModeToJSON(object) {
    switch (object) {
        case FileOptions_OptimizeMode.SPEED:
            return "SPEED";
        case FileOptions_OptimizeMode.CODE_SIZE:
            return "CODE_SIZE";
        case FileOptions_OptimizeMode.LITE_RUNTIME:
            return "LITE_RUNTIME";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FileOptions_OptimizeMode");
    }
}
var FieldOptions_CType;
(function (FieldOptions_CType) {
    /** STRING - Default mode. */
    FieldOptions_CType[FieldOptions_CType["STRING"] = 0] = "STRING";
    FieldOptions_CType[FieldOptions_CType["CORD"] = 1] = "CORD";
    FieldOptions_CType[FieldOptions_CType["STRING_PIECE"] = 2] = "STRING_PIECE";
})(FieldOptions_CType || (exports.FieldOptions_CType = FieldOptions_CType = {}));
function fieldOptions_CTypeFromJSON(object) {
    switch (object) {
        case 0:
        case "STRING":
            return FieldOptions_CType.STRING;
        case 1:
        case "CORD":
            return FieldOptions_CType.CORD;
        case 2:
        case "STRING_PIECE":
            return FieldOptions_CType.STRING_PIECE;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldOptions_CType");
    }
}
function fieldOptions_CTypeToJSON(object) {
    switch (object) {
        case FieldOptions_CType.STRING:
            return "STRING";
        case FieldOptions_CType.CORD:
            return "CORD";
        case FieldOptions_CType.STRING_PIECE:
            return "STRING_PIECE";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldOptions_CType");
    }
}
var FieldOptions_JSType;
(function (FieldOptions_JSType) {
    /** JS_NORMAL - Use the default type. */
    FieldOptions_JSType[FieldOptions_JSType["JS_NORMAL"] = 0] = "JS_NORMAL";
    /** JS_STRING - Use JavaScript strings. */
    FieldOptions_JSType[FieldOptions_JSType["JS_STRING"] = 1] = "JS_STRING";
    /** JS_NUMBER - Use JavaScript numbers. */
    FieldOptions_JSType[FieldOptions_JSType["JS_NUMBER"] = 2] = "JS_NUMBER";
})(FieldOptions_JSType || (exports.FieldOptions_JSType = FieldOptions_JSType = {}));
function fieldOptions_JSTypeFromJSON(object) {
    switch (object) {
        case 0:
        case "JS_NORMAL":
            return FieldOptions_JSType.JS_NORMAL;
        case 1:
        case "JS_STRING":
            return FieldOptions_JSType.JS_STRING;
        case 2:
        case "JS_NUMBER":
            return FieldOptions_JSType.JS_NUMBER;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldOptions_JSType");
    }
}
function fieldOptions_JSTypeToJSON(object) {
    switch (object) {
        case FieldOptions_JSType.JS_NORMAL:
            return "JS_NORMAL";
        case FieldOptions_JSType.JS_STRING:
            return "JS_STRING";
        case FieldOptions_JSType.JS_NUMBER:
            return "JS_NUMBER";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum FieldOptions_JSType");
    }
}
/**
 * Is this method side-effect-free (or safe in HTTP parlance), or idempotent,
 * or neither? HTTP based RPC implementation may choose GET verb for safe
 * methods, and PUT verb for idempotent methods instead of the default POST.
 */
var MethodOptions_IdempotencyLevel;
(function (MethodOptions_IdempotencyLevel) {
    MethodOptions_IdempotencyLevel[MethodOptions_IdempotencyLevel["IDEMPOTENCY_UNKNOWN"] = 0] = "IDEMPOTENCY_UNKNOWN";
    /** NO_SIDE_EFFECTS - implies idempotent */
    MethodOptions_IdempotencyLevel[MethodOptions_IdempotencyLevel["NO_SIDE_EFFECTS"] = 1] = "NO_SIDE_EFFECTS";
    /** IDEMPOTENT - idempotent, but may have side effects */
    MethodOptions_IdempotencyLevel[MethodOptions_IdempotencyLevel["IDEMPOTENT"] = 2] = "IDEMPOTENT";
})(MethodOptions_IdempotencyLevel || (exports.MethodOptions_IdempotencyLevel = MethodOptions_IdempotencyLevel = {}));
function methodOptions_IdempotencyLevelFromJSON(object) {
    switch (object) {
        case 0:
        case "IDEMPOTENCY_UNKNOWN":
            return MethodOptions_IdempotencyLevel.IDEMPOTENCY_UNKNOWN;
        case 1:
        case "NO_SIDE_EFFECTS":
            return MethodOptions_IdempotencyLevel.NO_SIDE_EFFECTS;
        case 2:
        case "IDEMPOTENT":
            return MethodOptions_IdempotencyLevel.IDEMPOTENT;
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum MethodOptions_IdempotencyLevel");
    }
}
function methodOptions_IdempotencyLevelToJSON(object) {
    switch (object) {
        case MethodOptions_IdempotencyLevel.IDEMPOTENCY_UNKNOWN:
            return "IDEMPOTENCY_UNKNOWN";
        case MethodOptions_IdempotencyLevel.NO_SIDE_EFFECTS:
            return "NO_SIDE_EFFECTS";
        case MethodOptions_IdempotencyLevel.IDEMPOTENT:
            return "IDEMPOTENT";
        default:
            throw new tsProtoGlobalThis.Error("Unrecognized enum value " + object + " for enum MethodOptions_IdempotencyLevel");
    }
}
function createBaseFileDescriptorSet() {
    return { file: [] };
}
exports.FileDescriptorSet = {
    fromJSON(object) {
        return { file: Array.isArray(object?.file) ? object.file.map((e) => exports.FileDescriptorProto.fromJSON(e)) : [] };
    },
    toJSON(message) {
        const obj = {};
        if (message.file) {
            obj.file = message.file.map((e) => e ? exports.FileDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.file = [];
        }
        return obj;
    },
};
function createBaseFileDescriptorProto() {
    return {
        name: "",
        package: "",
        dependency: [],
        publicDependency: [],
        weakDependency: [],
        messageType: [],
        enumType: [],
        service: [],
        extension: [],
        options: undefined,
        sourceCodeInfo: undefined,
        syntax: "",
    };
}
exports.FileDescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            package: isSet(object.package) ? String(object.package) : "",
            dependency: Array.isArray(object?.dependency) ? object.dependency.map((e) => String(e)) : [],
            publicDependency: Array.isArray(object?.publicDependency)
                ? object.publicDependency.map((e) => Number(e))
                : [],
            weakDependency: Array.isArray(object?.weakDependency) ? object.weakDependency.map((e) => Number(e)) : [],
            messageType: Array.isArray(object?.messageType)
                ? object.messageType.map((e) => exports.DescriptorProto.fromJSON(e))
                : [],
            enumType: Array.isArray(object?.enumType) ? object.enumType.map((e) => exports.EnumDescriptorProto.fromJSON(e)) : [],
            service: Array.isArray(object?.service) ? object.service.map((e) => exports.ServiceDescriptorProto.fromJSON(e)) : [],
            extension: Array.isArray(object?.extension)
                ? object.extension.map((e) => exports.FieldDescriptorProto.fromJSON(e))
                : [],
            options: isSet(object.options) ? exports.FileOptions.fromJSON(object.options) : undefined,
            sourceCodeInfo: isSet(object.sourceCodeInfo) ? exports.SourceCodeInfo.fromJSON(object.sourceCodeInfo) : undefined,
            syntax: isSet(object.syntax) ? String(object.syntax) : "",
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        message.package !== undefined && (obj.package = message.package);
        if (message.dependency) {
            obj.dependency = message.dependency.map((e) => e);
        }
        else {
            obj.dependency = [];
        }
        if (message.publicDependency) {
            obj.publicDependency = message.publicDependency.map((e) => Math.round(e));
        }
        else {
            obj.publicDependency = [];
        }
        if (message.weakDependency) {
            obj.weakDependency = message.weakDependency.map((e) => Math.round(e));
        }
        else {
            obj.weakDependency = [];
        }
        if (message.messageType) {
            obj.messageType = message.messageType.map((e) => e ? exports.DescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.messageType = [];
        }
        if (message.enumType) {
            obj.enumType = message.enumType.map((e) => e ? exports.EnumDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.enumType = [];
        }
        if (message.service) {
            obj.service = message.service.map((e) => e ? exports.ServiceDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.service = [];
        }
        if (message.extension) {
            obj.extension = message.extension.map((e) => e ? exports.FieldDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.extension = [];
        }
        message.options !== undefined && (obj.options = message.options ? exports.FileOptions.toJSON(message.options) : undefined);
        message.sourceCodeInfo !== undefined &&
            (obj.sourceCodeInfo = message.sourceCodeInfo ? exports.SourceCodeInfo.toJSON(message.sourceCodeInfo) : undefined);
        message.syntax !== undefined && (obj.syntax = message.syntax);
        return obj;
    },
};
function createBaseDescriptorProto() {
    return {
        name: "",
        field: [],
        extension: [],
        nestedType: [],
        enumType: [],
        extensionRange: [],
        oneofDecl: [],
        options: undefined,
        reservedRange: [],
        reservedName: [],
    };
}
exports.DescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            field: Array.isArray(object?.field) ? object.field.map((e) => exports.FieldDescriptorProto.fromJSON(e)) : [],
            extension: Array.isArray(object?.extension)
                ? object.extension.map((e) => exports.FieldDescriptorProto.fromJSON(e))
                : [],
            nestedType: Array.isArray(object?.nestedType)
                ? object.nestedType.map((e) => exports.DescriptorProto.fromJSON(e))
                : [],
            enumType: Array.isArray(object?.enumType) ? object.enumType.map((e) => exports.EnumDescriptorProto.fromJSON(e)) : [],
            extensionRange: Array.isArray(object?.extensionRange)
                ? object.extensionRange.map((e) => exports.DescriptorProto_ExtensionRange.fromJSON(e))
                : [],
            oneofDecl: Array.isArray(object?.oneofDecl)
                ? object.oneofDecl.map((e) => exports.OneofDescriptorProto.fromJSON(e))
                : [],
            options: isSet(object.options) ? exports.MessageOptions.fromJSON(object.options) : undefined,
            reservedRange: Array.isArray(object?.reservedRange)
                ? object.reservedRange.map((e) => exports.DescriptorProto_ReservedRange.fromJSON(e))
                : [],
            reservedName: Array.isArray(object?.reservedName) ? object.reservedName.map((e) => String(e)) : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        if (message.field) {
            obj.field = message.field.map((e) => e ? exports.FieldDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.field = [];
        }
        if (message.extension) {
            obj.extension = message.extension.map((e) => e ? exports.FieldDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.extension = [];
        }
        if (message.nestedType) {
            obj.nestedType = message.nestedType.map((e) => e ? exports.DescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.nestedType = [];
        }
        if (message.enumType) {
            obj.enumType = message.enumType.map((e) => e ? exports.EnumDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.enumType = [];
        }
        if (message.extensionRange) {
            obj.extensionRange = message.extensionRange.map((e) => e ? exports.DescriptorProto_ExtensionRange.toJSON(e) : undefined);
        }
        else {
            obj.extensionRange = [];
        }
        if (message.oneofDecl) {
            obj.oneofDecl = message.oneofDecl.map((e) => e ? exports.OneofDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.oneofDecl = [];
        }
        message.options !== undefined &&
            (obj.options = message.options ? exports.MessageOptions.toJSON(message.options) : undefined);
        if (message.reservedRange) {
            obj.reservedRange = message.reservedRange.map((e) => e ? exports.DescriptorProto_ReservedRange.toJSON(e) : undefined);
        }
        else {
            obj.reservedRange = [];
        }
        if (message.reservedName) {
            obj.reservedName = message.reservedName.map((e) => e);
        }
        else {
            obj.reservedName = [];
        }
        return obj;
    },
};
function createBaseDescriptorProto_ExtensionRange() {
    return { start: 0, end: 0, options: undefined };
}
exports.DescriptorProto_ExtensionRange = {
    fromJSON(object) {
        return {
            start: isSet(object.start) ? Number(object.start) : 0,
            end: isSet(object.end) ? Number(object.end) : 0,
            options: isSet(object.options) ? exports.ExtensionRangeOptions.fromJSON(object.options) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.start !== undefined && (obj.start = Math.round(message.start));
        message.end !== undefined && (obj.end = Math.round(message.end));
        message.options !== undefined &&
            (obj.options = message.options ? exports.ExtensionRangeOptions.toJSON(message.options) : undefined);
        return obj;
    },
};
function createBaseDescriptorProto_ReservedRange() {
    return { start: 0, end: 0 };
}
exports.DescriptorProto_ReservedRange = {
    fromJSON(object) {
        return { start: isSet(object.start) ? Number(object.start) : 0, end: isSet(object.end) ? Number(object.end) : 0 };
    },
    toJSON(message) {
        const obj = {};
        message.start !== undefined && (obj.start = Math.round(message.start));
        message.end !== undefined && (obj.end = Math.round(message.end));
        return obj;
    },
};
function createBaseExtensionRangeOptions() {
    return { uninterpretedOption: [] };
}
exports.ExtensionRangeOptions = {
    fromJSON(object) {
        return {
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseFieldDescriptorProto() {
    return {
        name: "",
        number: 0,
        label: 1,
        type: 1,
        typeName: "",
        extendee: "",
        defaultValue: "",
        oneofIndex: 0,
        jsonName: "",
        options: undefined,
        proto3Optional: false,
    };
}
exports.FieldDescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            number: isSet(object.number) ? Number(object.number) : 0,
            label: isSet(object.label) ? fieldDescriptorProto_LabelFromJSON(object.label) : 1,
            type: isSet(object.type) ? fieldDescriptorProto_TypeFromJSON(object.type) : 1,
            typeName: isSet(object.typeName) ? String(object.typeName) : "",
            extendee: isSet(object.extendee) ? String(object.extendee) : "",
            defaultValue: isSet(object.defaultValue) ? String(object.defaultValue) : "",
            oneofIndex: isSet(object.oneofIndex) ? Number(object.oneofIndex) : 0,
            jsonName: isSet(object.jsonName) ? String(object.jsonName) : "",
            options: isSet(object.options) ? exports.FieldOptions.fromJSON(object.options) : undefined,
            proto3Optional: isSet(object.proto3Optional) ? Boolean(object.proto3Optional) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        message.number !== undefined && (obj.number = Math.round(message.number));
        message.label !== undefined && (obj.label = fieldDescriptorProto_LabelToJSON(message.label));
        message.type !== undefined && (obj.type = fieldDescriptorProto_TypeToJSON(message.type));
        message.typeName !== undefined && (obj.typeName = message.typeName);
        message.extendee !== undefined && (obj.extendee = message.extendee);
        message.defaultValue !== undefined && (obj.defaultValue = message.defaultValue);
        message.oneofIndex !== undefined && (obj.oneofIndex = Math.round(message.oneofIndex));
        message.jsonName !== undefined && (obj.jsonName = message.jsonName);
        message.options !== undefined && (obj.options = message.options ? exports.FieldOptions.toJSON(message.options) : undefined);
        message.proto3Optional !== undefined && (obj.proto3Optional = message.proto3Optional);
        return obj;
    },
};
function createBaseOneofDescriptorProto() {
    return { name: "", options: undefined };
}
exports.OneofDescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            options: isSet(object.options) ? exports.OneofOptions.fromJSON(object.options) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        message.options !== undefined && (obj.options = message.options ? exports.OneofOptions.toJSON(message.options) : undefined);
        return obj;
    },
};
function createBaseEnumDescriptorProto() {
    return { name: "", value: [], options: undefined, reservedRange: [], reservedName: [] };
}
exports.EnumDescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            value: Array.isArray(object?.value) ? object.value.map((e) => exports.EnumValueDescriptorProto.fromJSON(e)) : [],
            options: isSet(object.options) ? exports.EnumOptions.fromJSON(object.options) : undefined,
            reservedRange: Array.isArray(object?.reservedRange)
                ? object.reservedRange.map((e) => exports.EnumDescriptorProto_EnumReservedRange.fromJSON(e))
                : [],
            reservedName: Array.isArray(object?.reservedName)
                ? object.reservedName.map((e) => String(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        if (message.value) {
            obj.value = message.value.map((e) => e ? exports.EnumValueDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.value = [];
        }
        message.options !== undefined && (obj.options = message.options ? exports.EnumOptions.toJSON(message.options) : undefined);
        if (message.reservedRange) {
            obj.reservedRange = message.reservedRange.map((e) => e ? exports.EnumDescriptorProto_EnumReservedRange.toJSON(e) : undefined);
        }
        else {
            obj.reservedRange = [];
        }
        if (message.reservedName) {
            obj.reservedName = message.reservedName.map((e) => e);
        }
        else {
            obj.reservedName = [];
        }
        return obj;
    },
};
function createBaseEnumDescriptorProto_EnumReservedRange() {
    return { start: 0, end: 0 };
}
exports.EnumDescriptorProto_EnumReservedRange = {
    fromJSON(object) {
        return { start: isSet(object.start) ? Number(object.start) : 0, end: isSet(object.end) ? Number(object.end) : 0 };
    },
    toJSON(message) {
        const obj = {};
        message.start !== undefined && (obj.start = Math.round(message.start));
        message.end !== undefined && (obj.end = Math.round(message.end));
        return obj;
    },
};
function createBaseEnumValueDescriptorProto() {
    return { name: "", number: 0, options: undefined };
}
exports.EnumValueDescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            number: isSet(object.number) ? Number(object.number) : 0,
            options: isSet(object.options) ? exports.EnumValueOptions.fromJSON(object.options) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        message.number !== undefined && (obj.number = Math.round(message.number));
        message.options !== undefined &&
            (obj.options = message.options ? exports.EnumValueOptions.toJSON(message.options) : undefined);
        return obj;
    },
};
function createBaseServiceDescriptorProto() {
    return { name: "", method: [], options: undefined };
}
exports.ServiceDescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            method: Array.isArray(object?.method) ? object.method.map((e) => exports.MethodDescriptorProto.fromJSON(e)) : [],
            options: isSet(object.options) ? exports.ServiceOptions.fromJSON(object.options) : undefined,
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        if (message.method) {
            obj.method = message.method.map((e) => e ? exports.MethodDescriptorProto.toJSON(e) : undefined);
        }
        else {
            obj.method = [];
        }
        message.options !== undefined &&
            (obj.options = message.options ? exports.ServiceOptions.toJSON(message.options) : undefined);
        return obj;
    },
};
function createBaseMethodDescriptorProto() {
    return {
        name: "",
        inputType: "",
        outputType: "",
        options: undefined,
        clientStreaming: false,
        serverStreaming: false,
    };
}
exports.MethodDescriptorProto = {
    fromJSON(object) {
        return {
            name: isSet(object.name) ? String(object.name) : "",
            inputType: isSet(object.inputType) ? String(object.inputType) : "",
            outputType: isSet(object.outputType) ? String(object.outputType) : "",
            options: isSet(object.options) ? exports.MethodOptions.fromJSON(object.options) : undefined,
            clientStreaming: isSet(object.clientStreaming) ? Boolean(object.clientStreaming) : false,
            serverStreaming: isSet(object.serverStreaming) ? Boolean(object.serverStreaming) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.name !== undefined && (obj.name = message.name);
        message.inputType !== undefined && (obj.inputType = message.inputType);
        message.outputType !== undefined && (obj.outputType = message.outputType);
        message.options !== undefined &&
            (obj.options = message.options ? exports.MethodOptions.toJSON(message.options) : undefined);
        message.clientStreaming !== undefined && (obj.clientStreaming = message.clientStreaming);
        message.serverStreaming !== undefined && (obj.serverStreaming = message.serverStreaming);
        return obj;
    },
};
function createBaseFileOptions() {
    return {
        javaPackage: "",
        javaOuterClassname: "",
        javaMultipleFiles: false,
        javaGenerateEqualsAndHash: false,
        javaStringCheckUtf8: false,
        optimizeFor: 1,
        goPackage: "",
        ccGenericServices: false,
        javaGenericServices: false,
        pyGenericServices: false,
        phpGenericServices: false,
        deprecated: false,
        ccEnableArenas: false,
        objcClassPrefix: "",
        csharpNamespace: "",
        swiftPrefix: "",
        phpClassPrefix: "",
        phpNamespace: "",
        phpMetadataNamespace: "",
        rubyPackage: "",
        uninterpretedOption: [],
    };
}
exports.FileOptions = {
    fromJSON(object) {
        return {
            javaPackage: isSet(object.javaPackage) ? String(object.javaPackage) : "",
            javaOuterClassname: isSet(object.javaOuterClassname) ? String(object.javaOuterClassname) : "",
            javaMultipleFiles: isSet(object.javaMultipleFiles) ? Boolean(object.javaMultipleFiles) : false,
            javaGenerateEqualsAndHash: isSet(object.javaGenerateEqualsAndHash)
                ? Boolean(object.javaGenerateEqualsAndHash)
                : false,
            javaStringCheckUtf8: isSet(object.javaStringCheckUtf8) ? Boolean(object.javaStringCheckUtf8) : false,
            optimizeFor: isSet(object.optimizeFor) ? fileOptions_OptimizeModeFromJSON(object.optimizeFor) : 1,
            goPackage: isSet(object.goPackage) ? String(object.goPackage) : "",
            ccGenericServices: isSet(object.ccGenericServices) ? Boolean(object.ccGenericServices) : false,
            javaGenericServices: isSet(object.javaGenericServices) ? Boolean(object.javaGenericServices) : false,
            pyGenericServices: isSet(object.pyGenericServices) ? Boolean(object.pyGenericServices) : false,
            phpGenericServices: isSet(object.phpGenericServices) ? Boolean(object.phpGenericServices) : false,
            deprecated: isSet(object.deprecated) ? Boolean(object.deprecated) : false,
            ccEnableArenas: isSet(object.ccEnableArenas) ? Boolean(object.ccEnableArenas) : false,
            objcClassPrefix: isSet(object.objcClassPrefix) ? String(object.objcClassPrefix) : "",
            csharpNamespace: isSet(object.csharpNamespace) ? String(object.csharpNamespace) : "",
            swiftPrefix: isSet(object.swiftPrefix) ? String(object.swiftPrefix) : "",
            phpClassPrefix: isSet(object.phpClassPrefix) ? String(object.phpClassPrefix) : "",
            phpNamespace: isSet(object.phpNamespace) ? String(object.phpNamespace) : "",
            phpMetadataNamespace: isSet(object.phpMetadataNamespace) ? String(object.phpMetadataNamespace) : "",
            rubyPackage: isSet(object.rubyPackage) ? String(object.rubyPackage) : "",
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.javaPackage !== undefined && (obj.javaPackage = message.javaPackage);
        message.javaOuterClassname !== undefined && (obj.javaOuterClassname = message.javaOuterClassname);
        message.javaMultipleFiles !== undefined && (obj.javaMultipleFiles = message.javaMultipleFiles);
        message.javaGenerateEqualsAndHash !== undefined &&
            (obj.javaGenerateEqualsAndHash = message.javaGenerateEqualsAndHash);
        message.javaStringCheckUtf8 !== undefined && (obj.javaStringCheckUtf8 = message.javaStringCheckUtf8);
        message.optimizeFor !== undefined && (obj.optimizeFor = fileOptions_OptimizeModeToJSON(message.optimizeFor));
        message.goPackage !== undefined && (obj.goPackage = message.goPackage);
        message.ccGenericServices !== undefined && (obj.ccGenericServices = message.ccGenericServices);
        message.javaGenericServices !== undefined && (obj.javaGenericServices = message.javaGenericServices);
        message.pyGenericServices !== undefined && (obj.pyGenericServices = message.pyGenericServices);
        message.phpGenericServices !== undefined && (obj.phpGenericServices = message.phpGenericServices);
        message.deprecated !== undefined && (obj.deprecated = message.deprecated);
        message.ccEnableArenas !== undefined && (obj.ccEnableArenas = message.ccEnableArenas);
        message.objcClassPrefix !== undefined && (obj.objcClassPrefix = message.objcClassPrefix);
        message.csharpNamespace !== undefined && (obj.csharpNamespace = message.csharpNamespace);
        message.swiftPrefix !== undefined && (obj.swiftPrefix = message.swiftPrefix);
        message.phpClassPrefix !== undefined && (obj.phpClassPrefix = message.phpClassPrefix);
        message.phpNamespace !== undefined && (obj.phpNamespace = message.phpNamespace);
        message.phpMetadataNamespace !== undefined && (obj.phpMetadataNamespace = message.phpMetadataNamespace);
        message.rubyPackage !== undefined && (obj.rubyPackage = message.rubyPackage);
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseMessageOptions() {
    return {
        messageSetWireFormat: false,
        noStandardDescriptorAccessor: false,
        deprecated: false,
        mapEntry: false,
        uninterpretedOption: [],
    };
}
exports.MessageOptions = {
    fromJSON(object) {
        return {
            messageSetWireFormat: isSet(object.messageSetWireFormat) ? Boolean(object.messageSetWireFormat) : false,
            noStandardDescriptorAccessor: isSet(object.noStandardDescriptorAccessor)
                ? Boolean(object.noStandardDescriptorAccessor)
                : false,
            deprecated: isSet(object.deprecated) ? Boolean(object.deprecated) : false,
            mapEntry: isSet(object.mapEntry) ? Boolean(object.mapEntry) : false,
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.messageSetWireFormat !== undefined && (obj.messageSetWireFormat = message.messageSetWireFormat);
        message.noStandardDescriptorAccessor !== undefined &&
            (obj.noStandardDescriptorAccessor = message.noStandardDescriptorAccessor);
        message.deprecated !== undefined && (obj.deprecated = message.deprecated);
        message.mapEntry !== undefined && (obj.mapEntry = message.mapEntry);
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseFieldOptions() {
    return {
        ctype: 0,
        packed: false,
        jstype: 0,
        lazy: false,
        unverifiedLazy: false,
        deprecated: false,
        weak: false,
        uninterpretedOption: [],
    };
}
exports.FieldOptions = {
    fromJSON(object) {
        return {
            ctype: isSet(object.ctype) ? fieldOptions_CTypeFromJSON(object.ctype) : 0,
            packed: isSet(object.packed) ? Boolean(object.packed) : false,
            jstype: isSet(object.jstype) ? fieldOptions_JSTypeFromJSON(object.jstype) : 0,
            lazy: isSet(object.lazy) ? Boolean(object.lazy) : false,
            unverifiedLazy: isSet(object.unverifiedLazy) ? Boolean(object.unverifiedLazy) : false,
            deprecated: isSet(object.deprecated) ? Boolean(object.deprecated) : false,
            weak: isSet(object.weak) ? Boolean(object.weak) : false,
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.ctype !== undefined && (obj.ctype = fieldOptions_CTypeToJSON(message.ctype));
        message.packed !== undefined && (obj.packed = message.packed);
        message.jstype !== undefined && (obj.jstype = fieldOptions_JSTypeToJSON(message.jstype));
        message.lazy !== undefined && (obj.lazy = message.lazy);
        message.unverifiedLazy !== undefined && (obj.unverifiedLazy = message.unverifiedLazy);
        message.deprecated !== undefined && (obj.deprecated = message.deprecated);
        message.weak !== undefined && (obj.weak = message.weak);
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseOneofOptions() {
    return { uninterpretedOption: [] };
}
exports.OneofOptions = {
    fromJSON(object) {
        return {
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseEnumOptions() {
    return { allowAlias: false, deprecated: false, uninterpretedOption: [] };
}
exports.EnumOptions = {
    fromJSON(object) {
        return {
            allowAlias: isSet(object.allowAlias) ? Boolean(object.allowAlias) : false,
            deprecated: isSet(object.deprecated) ? Boolean(object.deprecated) : false,
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.allowAlias !== undefined && (obj.allowAlias = message.allowAlias);
        message.deprecated !== undefined && (obj.deprecated = message.deprecated);
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseEnumValueOptions() {
    return { deprecated: false, uninterpretedOption: [] };
}
exports.EnumValueOptions = {
    fromJSON(object) {
        return {
            deprecated: isSet(object.deprecated) ? Boolean(object.deprecated) : false,
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.deprecated !== undefined && (obj.deprecated = message.deprecated);
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseServiceOptions() {
    return { deprecated: false, uninterpretedOption: [] };
}
exports.ServiceOptions = {
    fromJSON(object) {
        return {
            deprecated: isSet(object.deprecated) ? Boolean(object.deprecated) : false,
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.deprecated !== undefined && (obj.deprecated = message.deprecated);
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseMethodOptions() {
    return { deprecated: false, idempotencyLevel: 0, uninterpretedOption: [] };
}
exports.MethodOptions = {
    fromJSON(object) {
        return {
            deprecated: isSet(object.deprecated) ? Boolean(object.deprecated) : false,
            idempotencyLevel: isSet(object.idempotencyLevel)
                ? methodOptions_IdempotencyLevelFromJSON(object.idempotencyLevel)
                : 0,
            uninterpretedOption: Array.isArray(object?.uninterpretedOption)
                ? object.uninterpretedOption.map((e) => exports.UninterpretedOption.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        message.deprecated !== undefined && (obj.deprecated = message.deprecated);
        message.idempotencyLevel !== undefined &&
            (obj.idempotencyLevel = methodOptions_IdempotencyLevelToJSON(message.idempotencyLevel));
        if (message.uninterpretedOption) {
            obj.uninterpretedOption = message.uninterpretedOption.map((e) => e ? exports.UninterpretedOption.toJSON(e) : undefined);
        }
        else {
            obj.uninterpretedOption = [];
        }
        return obj;
    },
};
function createBaseUninterpretedOption() {
    return {
        name: [],
        identifierValue: "",
        positiveIntValue: "0",
        negativeIntValue: "0",
        doubleValue: 0,
        stringValue: Buffer.alloc(0),
        aggregateValue: "",
    };
}
exports.UninterpretedOption = {
    fromJSON(object) {
        return {
            name: Array.isArray(object?.name) ? object.name.map((e) => exports.UninterpretedOption_NamePart.fromJSON(e)) : [],
            identifierValue: isSet(object.identifierValue) ? String(object.identifierValue) : "",
            positiveIntValue: isSet(object.positiveIntValue) ? String(object.positiveIntValue) : "0",
            negativeIntValue: isSet(object.negativeIntValue) ? String(object.negativeIntValue) : "0",
            doubleValue: isSet(object.doubleValue) ? Number(object.doubleValue) : 0,
            stringValue: isSet(object.stringValue) ? Buffer.from(bytesFromBase64(object.stringValue)) : Buffer.alloc(0),
            aggregateValue: isSet(object.aggregateValue) ? String(object.aggregateValue) : "",
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.name) {
            obj.name = message.name.map((e) => e ? exports.UninterpretedOption_NamePart.toJSON(e) : undefined);
        }
        else {
            obj.name = [];
        }
        message.identifierValue !== undefined && (obj.identifierValue = message.identifierValue);
        message.positiveIntValue !== undefined && (obj.positiveIntValue = message.positiveIntValue);
        message.negativeIntValue !== undefined && (obj.negativeIntValue = message.negativeIntValue);
        message.doubleValue !== undefined && (obj.doubleValue = message.doubleValue);
        message.stringValue !== undefined &&
            (obj.stringValue = base64FromBytes(message.stringValue !== undefined ? message.stringValue : Buffer.alloc(0)));
        message.aggregateValue !== undefined && (obj.aggregateValue = message.aggregateValue);
        return obj;
    },
};
function createBaseUninterpretedOption_NamePart() {
    return { namePart: "", isExtension: false };
}
exports.UninterpretedOption_NamePart = {
    fromJSON(object) {
        return {
            namePart: isSet(object.namePart) ? String(object.namePart) : "",
            isExtension: isSet(object.isExtension) ? Boolean(object.isExtension) : false,
        };
    },
    toJSON(message) {
        const obj = {};
        message.namePart !== undefined && (obj.namePart = message.namePart);
        message.isExtension !== undefined && (obj.isExtension = message.isExtension);
        return obj;
    },
};
function createBaseSourceCodeInfo() {
    return { location: [] };
}
exports.SourceCodeInfo = {
    fromJSON(object) {
        return {
            location: Array.isArray(object?.location)
                ? object.location.map((e) => exports.SourceCodeInfo_Location.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.location) {
            obj.location = message.location.map((e) => e ? exports.SourceCodeInfo_Location.toJSON(e) : undefined);
        }
        else {
            obj.location = [];
        }
        return obj;
    },
};
function createBaseSourceCodeInfo_Location() {
    return { path: [], span: [], leadingComments: "", trailingComments: "", leadingDetachedComments: [] };
}
exports.SourceCodeInfo_Location = {
    fromJSON(object) {
        return {
            path: Array.isArray(object?.path) ? object.path.map((e) => Number(e)) : [],
            span: Array.isArray(object?.span) ? object.span.map((e) => Number(e)) : [],
            leadingComments: isSet(object.leadingComments) ? String(object.leadingComments) : "",
            trailingComments: isSet(object.trailingComments) ? String(object.trailingComments) : "",
            leadingDetachedComments: Array.isArray(object?.leadingDetachedComments)
                ? object.leadingDetachedComments.map((e) => String(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.path) {
            obj.path = message.path.map((e) => Math.round(e));
        }
        else {
            obj.path = [];
        }
        if (message.span) {
            obj.span = message.span.map((e) => Math.round(e));
        }
        else {
            obj.span = [];
        }
        message.leadingComments !== undefined && (obj.leadingComments = message.leadingComments);
        message.trailingComments !== undefined && (obj.trailingComments = message.trailingComments);
        if (message.leadingDetachedComments) {
            obj.leadingDetachedComments = message.leadingDetachedComments.map((e) => e);
        }
        else {
            obj.leadingDetachedComments = [];
        }
        return obj;
    },
};
function createBaseGeneratedCodeInfo() {
    return { annotation: [] };
}
exports.GeneratedCodeInfo = {
    fromJSON(object) {
        return {
            annotation: Array.isArray(object?.annotation)
                ? object.annotation.map((e) => exports.GeneratedCodeInfo_Annotation.fromJSON(e))
                : [],
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.annotation) {
            obj.annotation = message.annotation.map((e) => e ? exports.GeneratedCodeInfo_Annotation.toJSON(e) : undefined);
        }
        else {
            obj.annotation = [];
        }
        return obj;
    },
};
function createBaseGeneratedCodeInfo_Annotation() {
    return { path: [], sourceFile: "", begin: 0, end: 0 };
}
exports.GeneratedCodeInfo_Annotation = {
    fromJSON(object) {
        return {
            path: Array.isArray(object?.path) ? object.path.map((e) => Number(e)) : [],
            sourceFile: isSet(object.sourceFile) ? String(object.sourceFile) : "",
            begin: isSet(object.begin) ? Number(object.begin) : 0,
            end: isSet(object.end) ? Number(object.end) : 0,
        };
    },
    toJSON(message) {
        const obj = {};
        if (message.path) {
            obj.path = message.path.map((e) => Math.round(e));
        }
        else {
            obj.path = [];
        }
        message.sourceFile !== undefined && (obj.sourceFile = message.sourceFile);
        message.begin !== undefined && (obj.begin = Math.round(message.begin));
        message.end !== undefined && (obj.end = Math.round(message.end));
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
function isSet(value) {
    return value !== null && value !== undefined;
}
