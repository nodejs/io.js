"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.Metadata = void 0;
const canonical_json_1 = require("@tufjs/canonical-json");
const util_1 = __importDefault(require("util"));
const base_1 = require("./base");
const error_1 = require("./error");
const root_1 = require("./root");
const signature_1 = require("./signature");
const snapshot_1 = require("./snapshot");
const targets_1 = require("./targets");
const timestamp_1 = require("./timestamp");
const utils_1 = require("./utils");
/***
 * A container for signed TUF metadata.
 *
 * Provides methods to convert to and from json, read and write to and
 * from JSON and to create and verify metadata signatures.
 *
 * ``Metadata[T]`` is a generic container type where T can be any one type of
 * [``Root``, ``Timestamp``, ``Snapshot``, ``Targets``]. The purpose of this
 * is to allow static type checking of the signed attribute in code using
 * Metadata::
 *
 * root_md = Metadata[Root].fromJSON("root.json")
 * # root_md type is now Metadata[Root]. This means signed and its
 * # attributes like consistent_snapshot are now statically typed and the
 * # types can be verified by static type checkers and shown by IDEs
 *
 * Using a type constraint is not required but not doing so means T is not a
 * specific type so static typing cannot happen. Note that the type constraint
 * ``[Root]`` is not validated at runtime (as pure annotations are not available
 * then).
 *
 * Apart from ``expires`` all of the arguments to the inner constructors have
 * reasonable default values for new metadata.
 */
class Metadata {
    constructor(signed, signatures, unrecognizedFields) {
        this.signed = signed;
        this.signatures = signatures || {};
        this.unrecognizedFields = unrecognizedFields || {};
    }
    sign(signer, append = true) {
        const bytes = Buffer.from((0, canonical_json_1.canonicalize)(this.signed.toJSON()));
        const signature = signer(bytes);
        if (!append) {
            this.signatures = {};
        }
        this.signatures[signature.keyID] = signature;
    }
    verifyDelegate(delegatedRole, delegatedMetadata) {
        let role;
        let keys = {};
        switch (this.signed.type) {
            case base_1.MetadataKind.Root:
                keys = this.signed.keys;
                role = this.signed.roles[delegatedRole];
                break;
            case base_1.MetadataKind.Targets:
                if (!this.signed.delegations) {
                    throw new error_1.ValueError(`No delegations found for ${delegatedRole}`);
                }
                keys = this.signed.delegations.keys;
                if (this.signed.delegations.roles) {
                    role = this.signed.delegations.roles[delegatedRole];
                }
                else if (this.signed.delegations.succinctRoles) {
                    if (this.signed.delegations.succinctRoles.isDelegatedRole(delegatedRole)) {
                        role = this.signed.delegations.succinctRoles;
                    }
                }
                break;
            default:
                throw new TypeError('invalid metadata type');
        }
        if (!role) {
            throw new error_1.ValueError(`no delegation found for ${delegatedRole}`);
        }
        const signingKeys = new Set();
        role.keyIDs.forEach((keyID) => {
            const key = keys[keyID];
            // If we dont' have the key, continue checking other keys
            if (!key) {
                return;
            }
            try {
                key.verifySignature(delegatedMetadata);
                signingKeys.add(key.keyID);
            }
            catch (error) {
                // continue
            }
        });
        if (signingKeys.size < role.threshold) {
            throw new error_1.UnsignedMetadataError(`${delegatedRole} was signed by ${signingKeys.size}/${role.threshold} keys`);
        }
    }
    equals(other) {
        if (!(other instanceof Metadata)) {
            return false;
        }
        return (this.signed.equals(other.signed) &&
            util_1.default.isDeepStrictEqual(this.signatures, other.signatures) &&
            util_1.default.isDeepStrictEqual(this.unrecognizedFields, other.unrecognizedFields));
    }
    toJSON() {
        const signatures = Object.values(this.signatures).map((signature) => {
            return signature.toJSON();
        });
        return {
            signatures,
            signed: this.signed.toJSON(),
            ...this.unrecognizedFields,
        };
    }
    static fromJSON(type, data) {
        const { signed, signatures, ...rest } = data;
        if (!utils_1.guard.isDefined(signed) || !utils_1.guard.isObject(signed)) {
            throw new TypeError('signed is not defined');
        }
        if (type !== signed._type) {
            throw new error_1.ValueError(`expected '${type}', got ${signed['_type']}`);
        }
        if (!utils_1.guard.isObjectArray(signatures)) {
            throw new TypeError('signatures is not an array');
        }
        let signedObj;
        switch (type) {
            case base_1.MetadataKind.Root:
                signedObj = root_1.Root.fromJSON(signed);
                break;
            case base_1.MetadataKind.Timestamp:
                signedObj = timestamp_1.Timestamp.fromJSON(signed);
                break;
            case base_1.MetadataKind.Snapshot:
                signedObj = snapshot_1.Snapshot.fromJSON(signed);
                break;
            case base_1.MetadataKind.Targets:
                signedObj = targets_1.Targets.fromJSON(signed);
                break;
            default:
                throw new TypeError('invalid metadata type');
        }
        const sigMap = {};
        // Ensure that each signature is unique
        signatures.forEach((sigData) => {
            const sig = signature_1.Signature.fromJSON(sigData);
            if (sigMap[sig.keyID]) {
                throw new error_1.ValueError(`multiple signatures found for keyid: ${sig.keyID}`);
            }
            sigMap[sig.keyID] = sig;
        });
        return new Metadata(signedObj, sigMap, rest);
    }
}
exports.Metadata = Metadata;
