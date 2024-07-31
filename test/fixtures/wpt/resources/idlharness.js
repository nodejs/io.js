/* For user documentation see docs/_writing-tests/idlharness.md */

/**
 * Notes for people who want to edit this file (not just use it as a library):
 *
 * Most of the interesting stuff happens in the derived classes of IdlObject,
 * especially IdlInterface.  The entry point for all IdlObjects is .test(),
 * which is called by IdlArray.test().  An IdlObject is conceptually just
 * "thing we want to run tests on", and an IdlArray is an array of IdlObjects
 * with some additional data thrown in.
 *
 * The object model is based on what WebIDLParser.js produces, which is in turn
 * based on its pegjs grammar.  If you want to figure out what properties an
 * object will have from WebIDLParser.js, the best way is to look at the
 * grammar:
 *
 *   https://github.com/darobin/webidl.js/blob/master/lib/grammar.peg
 *
 * So for instance:
 *
 *   // interface definition
 *   interface
 *       =   extAttrs:extendedAttributeList? S? "interface" S name:identifier w herit:ifInheritance? w "{" w mem:ifMember* w "}" w ";" w
 *           { return { type: "interface", name: name, inheritance: herit, members: mem, extAttrs: extAttrs }; }
 *
 * This means that an "interface" object will have a .type property equal to
 * the string "interface", a .name property equal to the identifier that the
 * parser found, an .inheritance property equal to either null or the result of
 * the "ifInheritance" production found elsewhere in the grammar, and so on.
 * After each grammatical production is a JavaScript function in curly braces
 * that gets called with suitable arguments and returns some JavaScript value.
 *
 * (Note that the version of WebIDLParser.js we use might sometimes be
 * out-of-date or forked.)
 *
 * The members and methods of the classes defined by this file are all at least
 * briefly documented, hopefully.
 */
(function(){
"use strict";
// Support subsetTestByKey from /common/subset-tests-by-key.js, but make it optional
if (!('subsetTestByKey' in self)) {
    self.subsetTestByKey = function(key, callback, ...args) {
      return callback(...args);
    }
    self.shouldRunSubTest = () => true;
}
/// Helpers ///
function constValue (cnt)
{
    if (cnt.type === "null") return null;
    if (cnt.type === "NaN") return NaN;
    if (cnt.type === "Infinity") return cnt.negative ? -Infinity : Infinity;
    if (cnt.type === "number") return +cnt.value;
    return cnt.value;
}

function minOverloadLength(overloads)
{
    // "The value of the Function object’s “length” property is
    // a Number determined as follows:
    // ". . .
    // "Return the length of the shortest argument list of the
    // entries in S."
    if (!overloads.length) {
        return 0;
    }

    return overloads.map(function(attr) {
        return attr.arguments ? attr.arguments.filter(function(arg) {
            return !arg.optional && !arg.variadic;
        }).length : 0;
    })
    .reduce(function(m, n) { return Math.min(m, n); });
}

// A helper to get the global of a Function object.  This is needed to determine
// which global exceptions the function throws will come from.
function globalOf(func)
{
    try {
        // Use the fact that .constructor for a Function object is normally the
        // Function constructor, which can be used to mint a new function in the
        // right global.
        return func.constructor("return this;")();
    } catch (e) {
    }
    // If the above fails, because someone gave us a non-function, or a function
    // with a weird proto chain or weird .constructor property, just fall back
    // to 'self'.
    return self;
}

// https://esdiscuss.org/topic/isconstructor#content-11
function isConstructor(o) {
    try {
        new (new Proxy(o, {construct: () => ({})}));
        return true;
    } catch(e) {
        return false;
    }
}

function throwOrReject(a_test, operation, fn, obj, args, message, cb)
{
    if (operation.idlType.generic !== "Promise") {
        assert_throws_js(globalOf(fn).TypeError, function() {
            fn.apply(obj, args);
        }, message);
        cb();
    } else {
        try {
            promise_rejects_js(a_test, TypeError, fn.apply(obj, args), message).then(cb, cb);
        } catch (e){
            a_test.step(function() {
                assert_unreached("Throws \"" + e + "\" instead of rejecting promise");
                cb();
            });
        }
    }
}

function awaitNCallbacks(n, cb, ctx)
{
    var counter = 0;
    return function() {
        counter++;
        if (counter >= n) {
            cb();
        }
    };
}

/// IdlHarnessError ///
// Entry point
self.IdlHarnessError = function(message)
{
    /**
     * Message to be printed as the error's toString invocation.
     */
    this.message = message;
};

IdlHarnessError.prototype = Object.create(Error.prototype);

IdlHarnessError.prototype.toString = function()
{
    return this.message;
};


/// IdlArray ///
// Entry point
self.IdlArray = function()
{
    /**
     * A map from strings to the corresponding named IdlObject, such as
     * IdlInterface or IdlException.  These are the things that test() will run
     * tests on.
     */
    this.members = {};

    /**
     * A map from strings to arrays of strings.  The keys are interface or
     * exception names, and are expected to also exist as keys in this.members
     * (otherwise they'll be ignored).  This is populated by add_objects() --
     * see documentation at the start of the file.  The actual tests will be
     * run by calling this.members[name].test_object(obj) for each obj in
     * this.objects[name].  obj is a string that will be eval'd to produce a
     * JavaScript value, which is supposed to be an object implementing the
     * given IdlObject (interface, exception, etc.).
     */
    this.objects = {};

    /**
     * When adding multiple collections of IDLs one at a time, an earlier one
     * might contain a partial interface or includes statement that depends
     * on a later one.  Save these up and handle them right before we run
     * tests.
     *
     * Both this.partials and this.includes will be the objects as parsed by
     * WebIDLParser.js, not wrapped in IdlInterface or similar.
     */
    this.partials = [];
    this.includes = [];

    /**
     * Record of skipped IDL items, in case we later realize that they are a
     * dependency (to retroactively process them).
     */
    this.skipped = new Map();
};

IdlArray.prototype.add_idls = function(raw_idls, options)
{
    /** Entry point.  See documentation at beginning of file. */
    this.internal_add_idls(WebIDL2.parse(raw_idls), options);
};

IdlArray.prototype.add_untested_idls = function(raw_idls, options)
{
    /** Entry point.  See documentation at beginning of file. */
    var parsed_idls = WebIDL2.parse(raw_idls);
    this.mark_as_untested(parsed_idls);
    this.internal_add_idls(parsed_idls, options);
};

IdlArray.prototype.mark_as_untested = function (parsed_idls)
{
    for (var i = 0; i < parsed_idls.length; i++) {
        parsed_idls[i].untested = true;
        if ("members" in parsed_idls[i]) {
            for (var j = 0; j < parsed_idls[i].members.length; j++) {
                parsed_idls[i].members[j].untested = true;
            }
        }
    }
};

IdlArray.prototype.is_excluded_by_options = function (name, options)
{
    return options &&
        (options.except && options.except.includes(name)
         || options.only && !options.only.includes(name));
};

IdlArray.prototype.add_dependency_idls = function(raw_idls, options)
{
    return this.internal_add_dependency_idls(WebIDL2.parse(raw_idls), options);
};

IdlArray.prototype.internal_add_dependency_idls = function(parsed_idls, options)
{
    const new_options = { only: [] }

    const all_deps = new Set();
    Object.values(this.members).forEach(v => {
        if (v.base) {
            all_deps.add(v.base);
        }
    });
    // Add both 'A' and 'B' for each 'A includes B' entry.
    this.includes.forEach(i => {
        all_deps.add(i.target);
        all_deps.add(i.includes);
    });
    this.partials.forEach(p => all_deps.add(p.name));
    // Add 'TypeOfType' for each "typedef TypeOfType MyType;" entry.
    Object.entries(this.members).forEach(([k, v]) => {
        if (v instanceof IdlTypedef) {
            let defs = v.idlType.union
                ? v.idlType.idlType.map(t => t.idlType)
                : [v.idlType.idlType];
            defs.forEach(d => all_deps.add(d));
        }
    });

    // Add the attribute idlTypes of all the nested members of idls.
    const attrDeps = parsedIdls => {
        return parsedIdls.reduce((deps, parsed) => {
            if (parsed.members) {
                for (const attr of Object.values(parsed.members).filter(m => m.type === 'attribute')) {
                    let attrType = attr.idlType;
                    // Check for generic members (e.g. FrozenArray<MyType>)
                    if (attrType.generic) {
                        deps.add(attrType.generic);
                        attrType = attrType.idlType;
                    }
                    deps.add(attrType.idlType);
                }
            }
            if (parsed.base in this.members) {
                attrDeps([this.members[parsed.base]]).forEach(dep => deps.add(dep));
            }
            return deps;
        }, new Set());
    };

    const testedMembers = Object.values(this.members).filter(m => !m.untested && m.members);
    attrDeps(testedMembers).forEach(dep => all_deps.add(dep));

    const testedPartials = this.partials.filter(m => !m.untested && m.members);
    attrDeps(testedPartials).forEach(dep => all_deps.add(dep));


    if (options && options.except && options.only) {
        throw new IdlHarnessError("The only and except options can't be used together.");
    }

    const defined_or_untested = name => {
        // NOTE: Deps are untested, so we're lenient, and skip re-encountered definitions.
        // e.g. for 'idl' containing A:B, B:C, C:D
        //      array.add_idls(idl, {only: ['A','B']}).
        //      array.add_dependency_idls(idl);
        // B would be encountered as tested, and encountered as a dep, so we ignore.
        return name in this.members
            || this.is_excluded_by_options(name, options);
    }
    // Maps name -> [parsed_idl, ...]
    const process = function(parsed) {
        var deps = [];
        if (parsed.name) {
            deps.push(parsed.name);
        } else if (parsed.type === "includes") {
            deps.push(parsed.target);
            deps.push(parsed.includes);
        }

        deps = deps.filter(function(name) {
            if (!name
                || name === parsed.name && defined_or_untested(name)
                || !all_deps.has(name)) {
                // Flag as skipped, if it's not already processed, so we can
                // come back to it later if we retrospectively call it a dep.
                if (name && !(name in this.members)) {
                    this.skipped.has(name)
                        ? this.skipped.get(name).push(parsed)
                        : this.skipped.set(name, [parsed]);
                }
                return false;
            }
            return true;
        }.bind(this));

        deps.forEach(function(name) {
            if (!new_options.only.includes(name)) {
                new_options.only.push(name);
            }

            const follow_up = new Set();
            for (const dep_type of ["inheritance", "includes"]) {
                if (parsed[dep_type]) {
                    const inheriting = parsed[dep_type];
                    const inheritor = parsed.name || parsed.target;
                    const deps = [inheriting];
                    // For A includes B, we can ignore A, unless B (or some of its
                    // members) is being tested.
                    if (dep_type !== "includes"
                        || inheriting in this.members && !this.members[inheriting].untested
                        || this.partials.some(function(p) {
                                return p.name === inheriting;
                            })) {
                        deps.push(inheritor);
                    }
                    for (const dep of deps) {
                        if (!new_options.only.includes(dep)) {
                            new_options.only.push(dep);
                        }
                        all_deps.add(dep);
                        follow_up.add(dep);
                    }
                }
            }

            for (const deferred of follow_up) {
                if (this.skipped.has(deferred)) {
                    const next = this.skipped.get(deferred);
                    this.skipped.delete(deferred);
                    next.forEach(process);
                }
            }
        }.bind(this));
    }.bind(this);

    for (let parsed of parsed_idls) {
        process(parsed);
    }

    this.mark_as_untested(parsed_idls);

    if (new_options.only.length) {
        this.internal_add_idls(parsed_idls, new_options);
    }
}

IdlArray.prototype.internal_add_idls = function(parsed_idls, options)
{
    /**
     * Internal helper called by add_idls() and add_untested_idls().
     *
     * parsed_idls is an array of objects that come from WebIDLParser.js's
     * "definitions" production.  The add_untested_idls() entry point
     * additionally sets an .untested property on each object (and its
     * .members) so that they'll be skipped by test() -- they'll only be
     * used for base interfaces of tested interfaces, return types, etc.
     *
     * options is a dictionary that can have an only or except member which are
     * arrays. If only is given then only members, partials and interface
     * targets listed will be added, and if except is given only those that
     * aren't listed will be added. Only one of only and except can be used.
     */

    if (options && options.only && options.except)
    {
        throw new IdlHarnessError("The only and except options can't be used together.");
    }

    var should_skip = name => {
        return this.is_excluded_by_options(name, options);
    }

    parsed_idls.forEach(function(parsed_idl)
    {
        var partial_types = [
            "interface",
            "interface mixin",
            "dictionary",
            "namespace",
        ];
        if (parsed_idl.partial && partial_types.includes(parsed_idl.type))
        {
            if (should_skip(parsed_idl.name))
            {
                return;
            }
            this.partials.push(parsed_idl);
            return;
        }

        if (parsed_idl.type == "includes")
        {
            if (should_skip(parsed_idl.target))
            {
                return;
            }
            this.includes.push(parsed_idl);
            return;
        }

        parsed_idl.array = this;
        if (should_skip(parsed_idl.name))
        {
            return;
        }
        if (parsed_idl.name in this.members)
        {
            throw new IdlHarnessError("Duplicate identifier " + parsed_idl.name);
        }

        switch(parsed_idl.type)
        {
        case "interface":
            this.members[parsed_idl.name] =
                new IdlInterface(parsed_idl, /* is_callback = */ false, /* is_mixin = */ false);
            break;

        case "interface mixin":
            this.members[parsed_idl.name] =
                new IdlInterface(parsed_idl, /* is_callback = */ false, /* is_mixin = */ true);
            break;

        case "dictionary":
            // Nothing to test, but we need the dictionary info around for type
            // checks
            this.members[parsed_idl.name] = new IdlDictionary(parsed_idl);
            break;

        case "typedef":
            this.members[parsed_idl.name] = new IdlTypedef(parsed_idl);
            break;

        case "callback":
            this.members[parsed_idl.name] = new IdlCallback(parsed_idl);
            break;

        case "enum":
            this.members[parsed_idl.name] = new IdlEnum(parsed_idl);
            break;

        case "callback interface":
            this.members[parsed_idl.name] =
                new IdlInterface(parsed_idl, /* is_callback = */ true, /* is_mixin = */ false);
            break;

        case "namespace":
            this.members[parsed_idl.name] = new IdlNamespace(parsed_idl);
            break;

        default:
            throw parsed_idl.name + ": " + parsed_idl.type + " not yet supported";
        }
    }.bind(this));
};

IdlArray.prototype.add_objects = function(dict)
{
    /** Entry point.  See documentation at beginning of file. */
    for (var k in dict)
    {
        if (k in this.objects)
        {
            this.objects[k] = this.objects[k].concat(dict[k]);
        }
        else
        {
            this.objects[k] = dict[k];
        }
    }
};

IdlArray.prototype.prevent_multiple_testing = function(name)
{
    /** Entry point.  See documentation at beginning of file. */
    this.members[name].prevent_multiple_testing = true;
};

IdlArray.prototype.is_json_type = function(type)
{
    /**
     * Checks whether type is a JSON type as per
     * https://webidl.spec.whatwg.org/#dfn-json-types
     */

    var idlType = type.idlType;

    if (type.generic == "Promise") { return false; }

    //  nullable and annotated types don't need to be handled separately,
    //  as webidl2 doesn't represent them wrapped-up (as they're described
    //  in WebIDL).

    // union and record types
    if (type.union || type.generic == "record") {
        return idlType.every(this.is_json_type, this);
    }

    // sequence types
    if (type.generic == "sequence" || type.generic == "FrozenArray") {
        return this.is_json_type(idlType[0]);
    }

    if (typeof idlType != "string") { throw new Error("Unexpected type " + JSON.stringify(idlType)); }

    switch (idlType)
    {
       //  Numeric types
       case "byte":
       case "octet":
       case "short":
       case "unsigned short":
       case "long":
       case "unsigned long":
       case "long long":
       case "unsigned long long":
       case "float":
       case "double":
       case "unrestricted float":
       case "unrestricted double":
       // boolean
       case "boolean":
       // string types
       case "DOMString":
       case "ByteString":
       case "USVString":
       // object type
       case "object":
           return true;
       case "Error":
       case "DOMException":
       case "Int8Array":
       case "Int16Array":
       case "Int32Array":
       case "Uint8Array":
       case "Uint16Array":
       case "Uint32Array":
       case "Uint8ClampedArray":
       case "BigInt64Array":
       case "BigUint64Array":
       case "Float32Array":
       case "Float64Array":
       case "ArrayBuffer":
       case "DataView":
       case "any":
           return false;
       default:
           var thing = this.members[idlType];
           if (!thing) { throw new Error("Type " + idlType + " not found"); }
           if (thing instanceof IdlEnum) { return true; }

           if (thing instanceof IdlTypedef) {
               return this.is_json_type(thing.idlType);
           }

           //  dictionaries where all of their members are JSON types
           if (thing instanceof IdlDictionary) {
               const map = new Map();
               for (const dict of thing.get_reverse_inheritance_stack()) {
                   for (const m of dict.members) {
                       map.set(m.name, m.idlType);
                   }
               }
               return Array.from(map.values()).every(this.is_json_type, this);
           }

           //  interface types that have a toJSON operation declared on themselves or
           //  one of their inherited interfaces.
           if (thing instanceof IdlInterface) {
               var base;
               while (thing)
               {
                   if (thing.has_to_json_regular_operation()) { return true; }
                   var mixins = this.includes[thing.name];
                   if (mixins) {
                       mixins = mixins.map(function(id) {
                           var mixin = this.members[id];
                           if (!mixin) {
                               throw new Error("Interface " + id + " not found (implemented by " + thing.name + ")");
                           }
                           return mixin;
                       }, this);
                       if (mixins.some(function(m) { return m.has_to_json_regular_operation() } )) { return true; }
                   }
                   if (!thing.base) { return false; }
                   base = this.members[thing.base];
                   if (!base) {
                       throw new Error("Interface " + thing.base + " not found (inherited by " + thing.name + ")");
                   }
                   thing = base;
               }
               return false;
           }
           return false;
    }
};

function exposure_set(object, default_set) {
    var exposed = object.extAttrs && object.extAttrs.filter(a => a.name === "Exposed");
    if (exposed && exposed.length > 1) {
        throw new IdlHarnessError(
            `Multiple 'Exposed' extended attributes on ${object.name}`);
    }

    let result = default_set || ["Window"];
    if (result && !(result instanceof Set)) {
        result = new Set(result);
    }
    if (exposed && exposed.length) {
        const { rhs } = exposed[0];
        // Could be a list or a string.
        const set =
            rhs.type === "*" ?
            [ "*" ] :
            rhs.type === "identifier-list" ?
            rhs.value.map(id => id.value) :
            [ rhs.value ];
        result = new Set(set);
    }
    if (result && result.has("*")) {
        return "*";
    }
    if (result && result.has("Worker")) {
        result.delete("Worker");
        result.add("DedicatedWorker");
        result.add("ServiceWorker");
        result.add("SharedWorker");
    }
    return result;
}

function exposed_in(globals) {
    if (globals === "*") {
        return true;
    }
    if ('Window' in self) {
        return globals.has("Window");
    }
    if ('DedicatedWorkerGlobalScope' in self &&
        self instanceof DedicatedWorkerGlobalScope) {
        return globals.has("DedicatedWorker");
    }
    if ('SharedWorkerGlobalScope' in self &&
        self instanceof SharedWorkerGlobalScope) {
        return globals.has("SharedWorker");
    }
    if ('ServiceWorkerGlobalScope' in self &&
        self instanceof ServiceWorkerGlobalScope) {
        return globals.has("ServiceWorker");
    }
    if (Object.getPrototypeOf(self) === Object.prototype) {
        // ShadowRealm - only exposed with `"*"`.
        return false;
    }
    throw new IdlHarnessError("Unexpected global object");
}

/**
 * Asserts that the given error message is thrown for the given function.
 * @param {string|IdlHarnessError} error Expected Error message.
 * @param {Function} idlArrayFunc Function operating on an IdlArray that should throw.
 */
IdlArray.prototype.assert_throws = function(error, idlArrayFunc)
{
    try {
        idlArrayFunc.call(this, this);
    } catch (e) {
        if (e instanceof AssertionError) {
            throw e;
        }
        // Assertions for behaviour of the idlharness.js engine.
        if (error instanceof IdlHarnessError) {
            error = error.message;
        }
        if (e.message !== error) {
            throw new IdlHarnessError(`${idlArrayFunc} threw "${e}", not the expected IdlHarnessError "${error}"`);
        }
        return;
    }
    throw new IdlHarnessError(`${idlArrayFunc} did not throw the expected IdlHarnessError`);
}

IdlArray.prototype.test = function()
{
    /** Entry point.  See documentation at beginning of file. */

    // First merge in all partial definitions and interface mixins.
    this.merge_partials();
    this.merge_mixins();

    // Assert B defined for A : B
    for (const member of Object.values(this.members).filter(m => m.base)) {
        const lhs = member.name;
        const rhs = member.base;
        if (!(rhs in this.members)) throw new IdlHarnessError(`${lhs} inherits ${rhs}, but ${rhs} is undefined.`);
        const lhs_is_interface = this.members[lhs] instanceof IdlInterface;
        const rhs_is_interface = this.members[rhs] instanceof IdlInterface;
        if (rhs_is_interface != lhs_is_interface) {
            if (!lhs_is_interface) throw new IdlHarnessError(`${lhs} inherits ${rhs}, but ${lhs} is not an interface.`);
            if (!rhs_is_interface) throw new IdlHarnessError(`${lhs} inherits ${rhs}, but ${rhs} is not an interface.`);
        }
        // Check for circular dependencies.
        member.get_reverse_inheritance_stack();
    }

    Object.getOwnPropertyNames(this.members).forEach(function(memberName) {
        var member = this.members[memberName];
        if (!(member instanceof IdlInterface)) {
            return;
        }

        var globals = exposure_set(member);
        member.exposed = exposed_in(globals);
        member.exposureSet = globals;
    }.bind(this));

    // Now run test() on every member, and test_object() for every object.
    for (var name in this.members)
    {
        this.members[name].test();
        if (name in this.objects)
        {
            const objects = this.objects[name];
            if (!objects || !Array.isArray(objects)) {
                throw new IdlHarnessError(`Invalid or empty objects for member ${name}`);
            }
            objects.forEach(function(str)
            {
                if (!this.members[name] || !(this.members[name] instanceof IdlInterface)) {
                    throw new IdlHarnessError(`Invalid object member name ${name}`);
                }
                this.members[name].test_object(str);
            }.bind(this));
        }
    }
};

IdlArray.prototype.merge_partials = function()
{
    const testedPartials = new Map();
    this.partials.forEach(function(parsed_idl)
    {
        const originalExists = parsed_idl.name in this.members
            && (this.members[parsed_idl.name] instanceof IdlInterface
                || this.members[parsed_idl.name] instanceof IdlDictionary
                || this.members[parsed_idl.name] instanceof IdlNamespace);

        // Ensure unique test name in case of multiple partials.
        let partialTestName = parsed_idl.name;
        let partialTestCount = 1;
        if (testedPartials.has(parsed_idl.name)) {
            partialTestCount += testedPartials.get(parsed_idl.name);
            partialTestName = `${partialTestName}[${partialTestCount}]`;
        }
        testedPartials.set(parsed_idl.name, partialTestCount);

        if (!parsed_idl.untested) {
            test(function () {
                assert_true(originalExists, `Original ${parsed_idl.type} should be defined`);

                var expected;
                switch (parsed_idl.type) {
                    case 'dictionary': expected = IdlDictionary; break;
                    case 'namespace': expected = IdlNamespace; break;
                    case 'interface':
                    case 'interface mixin':
                    default:
                        expected = IdlInterface; break;
                }
                assert_true(
                    expected.prototype.isPrototypeOf(this.members[parsed_idl.name]),
                    `Original ${parsed_idl.name} definition should have type ${parsed_idl.type}`);
            }.bind(this), `Partial ${parsed_idl.type} ${partialTestName}: original ${parsed_idl.type} defined`);
        }
        if (!originalExists) {
            // Not good.. but keep calm and carry on.
            return;
        }

        if (parsed_idl.extAttrs)
        {
            // Special-case "Exposed". Must be a subset of original interface's exposure.
            // Exposed on a partial is the equivalent of having the same Exposed on all nested members.
            // See https://github.com/heycam/webidl/issues/154 for discrepency between Exposed and
            // other extended attributes on partial interfaces.
            const exposureAttr = parsed_idl.extAttrs.find(a => a.name === "Exposed");
            if (exposureAttr) {
                if (!parsed_idl.untested) {
                    test(function () {
                        const partialExposure = exposure_set(parsed_idl);
                        const memberExposure = exposure_set(this.members[parsed_idl.name]);
                        if (memberExposure === "*") {
                            return;
                        }
                        if (partialExposure === "*") {
                            throw new IdlHarnessError(
                                `Partial ${parsed_idl.name} ${parsed_idl.type} is exposed everywhere, the original ${parsed_idl.type} is not.`);
                        }
                        partialExposure.forEach(name => {
                            if (!memberExposure || !memberExposure.has(name)) {
                                throw new IdlHarnessError(
                                    `Partial ${parsed_idl.name} ${parsed_idl.type} is exposed to '${name}', the original ${parsed_idl.type} is not.`);
                            }
                        });
                    }.bind(this), `Partial ${parsed_idl.type} ${partialTestName}: valid exposure set`);
                }
                parsed_idl.members.forEach(function (member) {
                    member.extAttrs.push(exposureAttr);
                }.bind(this));
            }

            parsed_idl.extAttrs.forEach(function(extAttr)
            {
                // "Exposed" already handled above.
                if (extAttr.name === "Exposed") {
                    return;
                }
                this.members[parsed_idl.name].extAttrs.push(extAttr);
            }.bind(this));
        }
        if (parsed_idl.members.length) {
            test(function () {
                var clash = parsed_idl.members.find(function(member) {
                    return this.members[parsed_idl.name].members.find(function(m) {
                        return this.are_duplicate_members(m, member);
                    }.bind(this));
                }.bind(this));
                parsed_idl.members.forEach(function(member)
                {
                    this.members[parsed_idl.name].members.push(new IdlInterfaceMember(member));
                }.bind(this));
                assert_true(!clash, "member " + (clash && clash.name) + " is unique");
            }.bind(this), `Partial ${parsed_idl.type} ${partialTestName}: member names are unique`);
        }
    }.bind(this));
    this.partials = [];
}

IdlArray.prototype.merge_mixins = function()
{
    for (const parsed_idl of this.includes)
    {
        const lhs = parsed_idl.target;
        const rhs = parsed_idl.includes;

        var errStr = lhs + " includes " + rhs + ", but ";
        if (!(lhs in this.members)) throw errStr + lhs + " is undefined.";
        if (!(this.members[lhs] instanceof IdlInterface)) throw errStr + lhs + " is not an interface.";
        if (!(rhs in this.members)) throw errStr + rhs + " is undefined.";
        if (!(this.members[rhs] instanceof IdlInterface)) throw errStr + rhs + " is not an interface.";

        if (this.members[rhs].members.length) {
            test(function () {
                var clash = this.members[rhs].members.find(function(member) {
                    return this.members[lhs].members.find(function(m) {
                        return this.are_duplicate_members(m, member);
                    }.bind(this));
                }.bind(this));
                this.members[rhs].members.forEach(function(member) {
                    assert_true(
                        this.members[lhs].members.every(m => !this.are_duplicate_members(m, member)),
                        "member " + member.name + " is unique");
                    this.members[lhs].members.push(new IdlInterfaceMember(member));
                }.bind(this));
                assert_true(!clash, "member " + (clash && clash.name) + " is unique");
            }.bind(this), lhs + " includes " + rhs + ": member names are unique");
        }
    }
    this.includes = [];
}

IdlArray.prototype.are_duplicate_members = function(m1, m2) {
    if (m1.name !== m2.name) {
        return false;
    }
    if (m1.type === 'operation' && m2.type === 'operation'
        && m1.arguments.length !== m2.arguments.length) {
        // Method overload. TODO: Deep comparison of arguments.
        return false;
    }
    return true;
}

IdlArray.prototype.assert_type_is = function(value, type)
{
    if (type.idlType in this.members
    && this.members[type.idlType] instanceof IdlTypedef) {
        this.assert_type_is(value, this.members[type.idlType].idlType);
        return;
    }

    if (type.nullable && value === null)
    {
        // This is fine
        return;
    }

    if (type.union) {
        for (var i = 0; i < type.idlType.length; i++) {
            try {
                this.assert_type_is(value, type.idlType[i]);
                // No AssertionError, so we match one type in the union
                return;
            } catch(e) {
                if (e instanceof AssertionError) {
                    // We didn't match this type, let's try some others
                    continue;
                }
                throw e;
            }
        }
        // TODO: Is there a nice way to list the union's types in the message?
        assert_true(false, "Attribute has value " + format_value(value)
                    + " which doesn't match any of the types in the union");

    }

    /**
     * Helper function that tests that value is an instance of type according
     * to the rules of WebIDL.  value is any JavaScript value, and type is an
     * object produced by WebIDLParser.js' "type" production.  That production
     * is fairly elaborate due to the complexity of WebIDL's types, so it's
     * best to look at the grammar to figure out what properties it might have.
     */
    if (type.idlType == "any")
    {
        // No assertions to make
        return;
    }

    if (type.array)
    {
        // TODO: not supported yet
        return;
    }

    if (type.generic === "sequence" || type.generic == "ObservableArray")
    {
        assert_true(Array.isArray(value), "should be an Array");
        if (!value.length)
        {
            // Nothing we can do.
            return;
        }
        this.assert_type_is(value[0], type.idlType[0]);
        return;
    }

    if (type.generic === "Promise") {
        assert_true("then" in value, "Attribute with a Promise type should have a then property");
        // TODO: Ideally, we would check on project fulfillment
        // that we get the right type
        // but that would require making the type check async
        return;
    }

    if (type.generic === "FrozenArray") {
        assert_true(Array.isArray(value), "Value should be array");
        assert_true(Object.isFrozen(value), "Value should be frozen");
        if (!value.length)
        {
            // Nothing we can do.
            return;
        }
        this.assert_type_is(value[0], type.idlType[0]);
        return;
    }

    type = Array.isArray(type.idlType) ? type.idlType[0] : type.idlType;

    switch(type)
    {
        case "undefined":
            assert_equals(value, undefined);
            return;

        case "boolean":
            assert_equals(typeof value, "boolean");
            return;

        case "byte":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.floor(value), "should be an integer");
            assert_true(-128 <= value && value <= 127, "byte " + value + " should be in range [-128, 127]");
            return;

        case "octet":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.floor(value), "should be an integer");
            assert_true(0 <= value && value <= 255, "octet " + value + " should be in range [0, 255]");
            return;

        case "short":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.floor(value), "should be an integer");
            assert_true(-32768 <= value && value <= 32767, "short " + value + " should be in range [-32768, 32767]");
            return;

        case "unsigned short":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.floor(value), "should be an integer");
            assert_true(0 <= value && value <= 65535, "unsigned short " + value + " should be in range [0, 65535]");
            return;

        case "long":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.floor(value), "should be an integer");
            assert_true(-2147483648 <= value && value <= 2147483647, "long " + value + " should be in range [-2147483648, 2147483647]");
            return;

        case "unsigned long":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.floor(value), "should be an integer");
            assert_true(0 <= value && value <= 4294967295, "unsigned long " + value + " should be in range [0, 4294967295]");
            return;

        case "long long":
            assert_equals(typeof value, "number");
            return;

        case "unsigned long long":
        case "DOMTimeStamp":
            assert_equals(typeof value, "number");
            assert_true(0 <= value, "unsigned long long should be positive");
            return;

        case "float":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.fround(value), "float rounded to 32-bit float should be itself");
            assert_not_equals(value, Infinity);
            assert_not_equals(value, -Infinity);
            assert_not_equals(value, NaN);
            return;

        case "DOMHighResTimeStamp":
        case "double":
            assert_equals(typeof value, "number");
            assert_not_equals(value, Infinity);
            assert_not_equals(value, -Infinity);
            assert_not_equals(value, NaN);
            return;

        case "unrestricted float":
            assert_equals(typeof value, "number");
            assert_equals(value, Math.fround(value), "unrestricted float rounded to 32-bit float should be itself");
            return;

        case "unrestricted double":
            assert_equals(typeof value, "number");
            return;

        case "DOMString":
            assert_equals(typeof value, "string");
            return;

        case "ByteString":
            assert_equals(typeof value, "string");
            assert_regexp_match(value, /^[\x00-\x7F]*$/);
            return;

        case "USVString":
            assert_equals(typeof value, "string");
            assert_regexp_match(value, /^([\x00-\ud7ff\ue000-\uffff]|[\ud800-\udbff][\udc00-\udfff])*$/);
            return;

        case "ArrayBufferView":
            assert_true(ArrayBuffer.isView(value));
            return;

        case "object":
            assert_in_array(typeof value, ["object", "function"], "wrong type: not object or function");
            return;
    }

    // This is a catch-all for any IDL type name which follows JS class
    // semantics. This includes some non-interface IDL types (e.g. Int8Array,
    // Function, ...), as well as any interface types that are not in the IDL
    // that is fed to the harness. If an IDL type does not follow JS class
    // semantics then it should go in the switch statement above. If an IDL
    // type needs full checking, then the test should include it in the IDL it
    // feeds to the harness.
    if (!(type in this.members))
    {
        assert_true(value instanceof self[type], "wrong type: not a " + type);
        return;
    }

    if (this.members[type] instanceof IdlInterface)
    {
        // We don't want to run the full
        // IdlInterface.prototype.test_instance_of, because that could result
        // in an infinite loop.  TODO: This means we don't have tests for
        // LegacyNoInterfaceObject interfaces, and we also can't test objects
        // that come from another self.
        assert_in_array(typeof value, ["object", "function"], "wrong type: not object or function");
        if (value instanceof Object
        && !this.members[type].has_extended_attribute("LegacyNoInterfaceObject")
        && type in self)
        {
            assert_true(value instanceof self[type], "instanceof " + type);
        }
    }
    else if (this.members[type] instanceof IdlEnum)
    {
        assert_equals(typeof value, "string");
    }
    else if (this.members[type] instanceof IdlDictionary)
    {
        // TODO: Test when we actually have something to test this on
    }
    else if (this.members[type] instanceof IdlCallback)
    {
        assert_equals(typeof value, "function");
    }
    else
    {
        throw new IdlHarnessError("Type " + type + " isn't an interface, callback or dictionary");
    }
};

/// IdlObject ///
function IdlObject() {}
IdlObject.prototype.test = function()
{
    /**
     * By default, this does nothing, so no actual tests are run for IdlObjects
     * that don't define any (e.g., IdlDictionary at the time of this writing).
     */
};

IdlObject.prototype.has_extended_attribute = function(name)
{
    /**
     * This is only meaningful for things that support extended attributes,
     * such as interfaces, exceptions, and members.
     */
    return this.extAttrs.some(function(o)
    {
        return o.name == name;
    });
};


/// IdlDictionary ///
// Used for IdlArray.prototype.assert_type_is
function IdlDictionary(obj)
{
    /**
     * obj is an object produced by the WebIDLParser.js "dictionary"
     * production.
     */

    /** Self-explanatory. */
    this.name = obj.name;

    /** A back-reference to our IdlArray. */
    this.array = obj.array;

    /** An array of objects produced by the "dictionaryMember" production. */
    this.members = obj.members;

    /**
     * The name (as a string) of the dictionary type we inherit from, or null
     * if there is none.
     */
    this.base = obj.inheritance;
}

IdlDictionary.prototype = Object.create(IdlObject.prototype);

IdlDictionary.prototype.get_reverse_inheritance_stack = function() {
    return IdlInterface.prototype.get_reverse_inheritance_stack.call(this);
};

/// IdlInterface ///
function IdlInterface(obj, is_callback, is_mixin)
{
    /**
     * obj is an object produced by the WebIDLParser.js "interface" production.
     */

    /** Self-explanatory. */
    this.name = obj.name;

    /** A back-reference to our IdlArray. */
    this.array = obj.array;

    /**
     * An indicator of whether we should run tests on the interface object and
     * interface prototype object. Tests on members are controlled by .untested
     * on each member, not this.
     */
    this.untested = obj.untested;

    /** An array of objects produced by the "ExtAttr" production. */
    this.extAttrs = obj.extAttrs;

    /** An array of IdlInterfaceMembers. */
    this.members = obj.members.map(function(m){return new IdlInterfaceMember(m); });
    if (this.has_extended_attribute("LegacyUnforgeable")) {
        this.members
            .filter(function(m) { return m.special !== "static" && (m.type == "attribute" || m.type == "operation"); })
            .forEach(function(m) { return m.isUnforgeable = true; });
    }

    /**
     * The name (as a string) of the type we inherit from, or null if there is
     * none.
     */
    this.base = obj.inheritance;

    this._is_callback = is_callback;
    this._is_mixin = is_mixin;
}
IdlInterface.prototype = Object.create(IdlObject.prototype);
IdlInterface.prototype.is_callback = function()
{
    return this._is_callback;
};

IdlInterface.prototype.is_mixin = function()
{
    return this._is_mixin;
};

IdlInterface.prototype.has_constants = function()
{
    return this.members.some(function(member) {
        return member.type === "const";
    });
};

IdlInterface.prototype.get_unscopables = function()
{
    return this.members.filter(function(member) {
        return member.isUnscopable;
    });
};

IdlInterface.prototype.is_global = function()
{
    return this.extAttrs.some(function(attribute) {
        return attribute.name === "Global";
    });
};

/**
 * Value of the LegacyNamespace extended attribute, if any.
 *
 * https://webidl.spec.whatwg.org/#LegacyNamespace
 */
IdlInterface.prototype.get_legacy_namespace = function()
{
    var legacyNamespace = this.extAttrs.find(function(attribute) {
        return attribute.name === "LegacyNamespace";
    });
    return legacyNamespace ? legacyNamespace.rhs.value : undefined;
};

IdlInterface.prototype.get_interface_object_owner = function()
{
    var legacyNamespace = this.get_legacy_namespace();
    return legacyNamespace ? self[legacyNamespace] : self;
};

IdlInterface.prototype.should_have_interface_object = function()
{
    // "For every interface that is exposed in a given ECMAScript global
    // environment and:
    // * is a callback interface that has constants declared on it, or
    // * is a non-callback interface that is not declared with the
    //   [LegacyNoInterfaceObject] extended attribute,
    // a corresponding property MUST exist on the ECMAScript global object.

    return this.is_callback() ? this.has_constants() : !this.has_extended_attribute("LegacyNoInterfaceObject");
};

IdlInterface.prototype.assert_interface_object_exists = function()
{
    var owner = this.get_legacy_namespace() || "self";
    assert_own_property(self[owner], this.name, owner + " does not have own property " + format_value(this.name));
};

IdlInterface.prototype.get_interface_object = function() {
    if (!this.should_have_interface_object()) {
        var reason = this.is_callback() ? "lack of declared constants" : "declared [LegacyNoInterfaceObject] attribute";
        throw new IdlHarnessError(this.name + " has no interface object due to " + reason);
    }

    return this.get_interface_object_owner()[this.name];
};

IdlInterface.prototype.get_qualified_name = function() {
    // https://webidl.spec.whatwg.org/#qualified-name
    var legacyNamespace = this.get_legacy_namespace();
    if (legacyNamespace) {
        return legacyNamespace + "." + this.name;
    }
    return this.name;
};

IdlInterface.prototype.has_to_json_regular_operation = function() {
    return this.members.some(function(m) {
        return m.is_to_json_regular_operation();
    });
};

IdlInterface.prototype.has_default_to_json_regular_operation = function() {
    return this.members.some(function(m) {
        return m.is_to_json_regular_operation() && m.has_extended_attribute("Default");
    });
};

/**
 * Implementation of https://webidl.spec.whatwg.org/#create-an-inheritance-stack
 * with the order reversed.
 *
 * The order is reversed so that the base class comes first in the list, because
 * this is what all call sites need.
 *
 * So given:
 *
 *   A : B {};
 *   B : C {};
 *   C {};
 *
 * then A.get_reverse_inheritance_stack() returns [C, B, A],
 * and B.get_reverse_inheritance_stack() returns [C, B].
 *
 * Note: as dictionary inheritance is expressed identically by the AST,
 * this works just as well for getting a stack of inherited dictionaries.
 */
IdlInterface.prototype.get_reverse_inheritance_stack = function() {
    const stack = [this];
    let idl_interface = this;
    while (idl_interface.base) {
        const base = this.array.members[idl_interface.base];
        if (!base) {
            throw new Error(idl_interface.type + " " + idl_interface.base + " not found (inherited by " + idl_interface.name + ")");
        } else if (stack.indexOf(base) > -1) {
            stack.unshift(base);
            const dep_chain = stack.map(i => i.name).join(',');
            throw new IdlHarnessError(`${this.name} has a circular dependency: ${dep_chain}`);
        }
        idl_interface = base;
        stack.unshift(idl_interface);
    }
    return stack;
};

/**
 * Implementation of
 * https://webidl.spec.whatwg.org/#default-tojson-operation
 * for testing purposes.
 *
 * Collects the IDL types of the attributes that meet the criteria
 * for inclusion in the default toJSON operation for easy
 * comparison with actual value
 */
IdlInterface.prototype.default_to_json_operation = function() {
    const map = new Map()
    let isDefault = false;
    for (const I of this.get_reverse_inheritance_stack()) {
        if (I.has_default_to_json_regular_operation()) {
            isDefault = true;
            for (const m of I.members) {
                if (m.special !== "static" && m.type == "attribute" && I.array.is_json_type(m.idlType)) {
                    map.set(m.name, m.idlType);
                }
            }
        } else if (I.has_to_json_regular_operation()) {
            isDefault = false;
        }
    }
    return isDefault ? map : null;
};

IdlInterface.prototype.test = function()
{
    if (this.has_extended_attribute("LegacyNoInterfaceObject") || this.is_mixin())
    {
        // No tests to do without an instance.  TODO: We should still be able
        // to run tests on the prototype object, if we obtain one through some
        // other means.
        return;
    }

    // If the interface object is not exposed, only test that. Members can't be
    // tested either, but objects could still be tested in |test_object|.
    if (!this.exposed)
    {
        if (!this.untested)
        {
            subsetTestByKey(this.name, test, function() {
                assert_false(this.name in self);
            }.bind(this), this.name + " interface: existence and properties of interface object");
        }
        return;
    }

    if (!this.untested)
    {
        // First test things to do with the exception/interface object and
        // exception/interface prototype object.
        this.test_self();
    }
    // Then test things to do with its members (constants, fields, attributes,
    // operations, . . .).  These are run even if .untested is true, because
    // members might themselves be marked as .untested.  This might happen to
    // interfaces if the interface itself is untested but a partial interface
    // that extends it is tested -- then the interface itself and its initial
    // members will be marked as untested, but the members added by the partial
    // interface are still tested.
    this.test_members();
};

IdlInterface.prototype.constructors = function()
{
    return this.members
        .filter(function(m) { return m.type == "constructor"; });
}

IdlInterface.prototype.test_self = function()
{
    subsetTestByKey(this.name, test, function()
    {
        if (!this.should_have_interface_object()) {
            return;
        }

        // The name of the property is the identifier of the interface, and its
        // value is an object called the interface object.
        // The property has the attributes { [[Writable]]: true,
        // [[Enumerable]]: false, [[Configurable]]: true }."
        // TODO: Should we test here that the property is actually writable
        // etc., or trust getOwnPropertyDescriptor?
        this.assert_interface_object_exists();
        var desc = Object.getOwnPropertyDescriptor(this.get_interface_object_owner(), this.name);
        assert_false("get" in desc, "self's property " + format_value(this.name) + " should not have a getter");
        assert_false("set" in desc, "self's property " + format_value(this.name) + " should not have a setter");
        assert_true(desc.writable, "self's property " + format_value(this.name) + " should be writable");
        assert_false(desc.enumerable, "self's property " + format_value(this.name) + " should not be enumerable");
        assert_true(desc.configurable, "self's property " + format_value(this.name) + " should be configurable");

        if (this.is_callback()) {
            // "The internal [[Prototype]] property of an interface object for
            // a callback interface must be the Function.prototype object."
            assert_equals(Object.getPrototypeOf(this.get_interface_object()), Function.prototype,
                          "prototype of self's property " + format_value(this.name) + " is not Object.prototype");

            return;
        }

        // "The interface object for a given non-callback interface is a
        // function object."
        // "If an object is defined to be a function object, then it has
        // characteristics as follows:"

        // Its [[Prototype]] internal property is otherwise specified (see
        // below).

        // "* Its [[Get]] internal property is set as described in ECMA-262
        //    section 9.1.8."
        // Not much to test for this.

        // "* Its [[Construct]] internal property is set as described in
        //    ECMA-262 section 19.2.2.3."

        // "* Its @@hasInstance property is set as described in ECMA-262
        //    section 19.2.3.8, unless otherwise specified."
        // TODO

        // ES6 (rev 30) 19.1.3.6:
        // "Else, if O has a [[Call]] internal method, then let builtinTag be
        // "Function"."
        assert_class_string(this.get_interface_object(), "Function", "class string of " + this.name);

        // "The [[Prototype]] internal property of an interface object for a
        // non-callback interface is determined as follows:"
        var prototype = Object.getPrototypeOf(this.get_interface_object());
        if (this.base) {
            // "* If the interface inherits from some other interface, the
            //    value of [[Prototype]] is the interface object for that other
            //    interface."
            var inherited_interface = this.array.members[this.base];
            if (!inherited_interface.has_extended_attribute("LegacyNoInterfaceObject")) {
                inherited_interface.assert_interface_object_exists();
                assert_equals(prototype, inherited_interface.get_interface_object(),
                              'prototype of ' + this.name + ' is not ' +
                              this.base);
            }
        } else {
            // "If the interface doesn't inherit from any other interface, the
            // value of [[Prototype]] is %FunctionPrototype% ([ECMA-262],
            // section 6.1.7.4)."
            assert_equals(prototype, Function.prototype,
                          "prototype of self's property " + format_value(this.name) + " is not Function.prototype");
        }

        // Always test for [[Construct]]:
        // https://github.com/heycam/webidl/issues/698
        assert_true(isConstructor(this.get_interface_object()), "interface object must pass IsConstructor check");

        var interface_object = this.get_interface_object();
        assert_throws_js(globalOf(interface_object).TypeError, function() {
            interface_object();
        }, "interface object didn't throw TypeError when called as a function");

        if (!this.constructors().length) {
            assert_throws_js(globalOf(interface_object).TypeError, function() {
                new interface_object();
            }, "interface object didn't throw TypeError when called as a constructor");
        }
    }.bind(this), this.name + " interface: existence and properties of interface object");

    if (this.should_have_interface_object() && !this.is_callback()) {
        subsetTestByKey(this.name, test, function() {
            // This function tests WebIDL as of 2014-10-25.
            // https://webidl.spec.whatwg.org/#es-interface-call

            this.assert_interface_object_exists();

            // "Interface objects for non-callback interfaces MUST have a
            // property named “length” with attributes { [[Writable]]: false,
            // [[Enumerable]]: false, [[Configurable]]: true } whose value is
            // a Number."
            assert_own_property(this.get_interface_object(), "length");
            var desc = Object.getOwnPropertyDescriptor(this.get_interface_object(), "length");
            assert_false("get" in desc, this.name + ".length should not have a getter");
            assert_false("set" in desc, this.name + ".length should not have a setter");
            assert_false(desc.writable, this.name + ".length should not be writable");
            assert_false(desc.enumerable, this.name + ".length should not be enumerable");
            assert_true(desc.configurable, this.name + ".length should be configurable");

            var constructors = this.constructors();
            var expected_length = minOverloadLength(constructors);
            assert_equals(this.get_interface_object().length, expected_length, "wrong value for " + this.name + ".length");
        }.bind(this), this.name + " interface object length");
    }

    if (this.should_have_interface_object()) {
        subsetTestByKey(this.name, test, function() {
            // This function tests WebIDL as of 2015-11-17.
            // https://webidl.spec.whatwg.org/#interface-object

            this.assert_interface_object_exists();

            // "All interface objects must have a property named “name” with
            // attributes { [[Writable]]: false, [[Enumerable]]: false,
            // [[Configurable]]: true } whose value is the identifier of the
            // corresponding interface."

            assert_own_property(this.get_interface_object(), "name");
            var desc = Object.getOwnPropertyDescriptor(this.get_interface_object(), "name");
            assert_false("get" in desc, this.name + ".name should not have a getter");
            assert_false("set" in desc, this.name + ".name should not have a setter");
            assert_false(desc.writable, this.name + ".name should not be writable");
            assert_false(desc.enumerable, this.name + ".name should not be enumerable");
            assert_true(desc.configurable, this.name + ".name should be configurable");
            assert_equals(this.get_interface_object().name, this.name, "wrong value for " + this.name + ".name");
        }.bind(this), this.name + " interface object name");
    }


    if (this.has_extended_attribute("LegacyWindowAlias")) {
        subsetTestByKey(this.name, test, function()
        {
            var aliasAttrs = this.extAttrs.filter(function(o) { return o.name === "LegacyWindowAlias"; });
            if (aliasAttrs.length > 1) {
                throw new IdlHarnessError("Invalid IDL: multiple LegacyWindowAlias extended attributes on " + this.name);
            }
            if (this.is_callback()) {
                throw new IdlHarnessError("Invalid IDL: LegacyWindowAlias extended attribute on non-interface " + this.name);
            }
            if (!(this.exposureSet === "*" || this.exposureSet.has("Window"))) {
                throw new IdlHarnessError("Invalid IDL: LegacyWindowAlias extended attribute on " + this.name + " which is not exposed in Window");
            }
            // TODO: when testing of [LegacyNoInterfaceObject] interfaces is supported,
            // check that it's not specified together with LegacyWindowAlias.

            // TODO: maybe check that [LegacyWindowAlias] is not specified on a partial interface.

            var rhs = aliasAttrs[0].rhs;
            if (!rhs) {
                throw new IdlHarnessError("Invalid IDL: LegacyWindowAlias extended attribute on " + this.name + " without identifier");
            }
            var aliases;
            if (rhs.type === "identifier-list") {
                aliases = rhs.value.map(id => id.value);
            } else { // rhs.type === identifier
                aliases = [ rhs.value ];
            }

            // OK now actually check the aliases...
            var alias;
            if (exposed_in(exposure_set(this, this.exposureSet)) && 'document' in self) {
                for (alias of aliases) {
                    assert_true(alias in self, alias + " should exist");
                    assert_equals(self[alias], this.get_interface_object(), "self." + alias + " should be the same value as self." + this.get_qualified_name());
                    var desc = Object.getOwnPropertyDescriptor(self, alias);
                    assert_equals(desc.value, this.get_interface_object(), "wrong value in " + alias + " property descriptor");
                    assert_true(desc.writable, alias + " should be writable");
                    assert_false(desc.enumerable, alias + " should not be enumerable");
                    assert_true(desc.configurable, alias + " should be configurable");
                    assert_false('get' in desc, alias + " should not have a getter");
                    assert_false('set' in desc, alias + " should not have a setter");
                }
            } else {
                for (alias of aliases) {
                    assert_false(alias in self, alias + " should not exist");
                }
            }

        }.bind(this), this.name + " interface: legacy window alias");
    }

    if (this.has_extended_attribute("LegacyFactoryFunction")) {
        var constructors = this.extAttrs
            .filter(function(attr) { return attr.name == "LegacyFactoryFunction"; });
        if (constructors.length !== 1) {
            throw new IdlHarnessError("Internal error: missing support for multiple LegacyFactoryFunction extended attributes");
        }
        var constructor = constructors[0];
        var min_length = minOverloadLength([constructor]);

        subsetTestByKey(this.name, test, function()
        {
            // This function tests WebIDL as of 2019-01-14.

            // "for every [LegacyFactoryFunction] extended attribute on an exposed
            // interface, a corresponding property must exist on the ECMAScript
            // global object. The name of the property is the
            // [LegacyFactoryFunction]'s identifier, and its value is an object
            // called a named constructor, ... . The property has the attributes
            // { [[Writable]]: true, [[Enumerable]]: false,
            // [[Configurable]]: true }."
            var name = constructor.rhs.value;
            assert_own_property(self, name);
            var desc = Object.getOwnPropertyDescriptor(self, name);
            assert_equals(desc.value, self[name], "wrong value in " + name + " property descriptor");
            assert_true(desc.writable, name + " should be writable");
            assert_false(desc.enumerable, name + " should not be enumerable");
            assert_true(desc.configurable, name + " should be configurable");
            assert_false("get" in desc, name + " should not have a getter");
            assert_false("set" in desc, name + " should not have a setter");
        }.bind(this), this.name + " interface: named constructor");

        subsetTestByKey(this.name, test, function()
        {
            // This function tests WebIDL as of 2019-01-14.

            // "2. Let F be ! CreateBuiltinFunction(realm, steps,
            //     realm.[[Intrinsics]].[[%FunctionPrototype%]])."
            var name = constructor.rhs.value;
            var value = self[name];
            assert_equals(typeof value, "function", "type of value in " + name + " property descriptor");
            assert_not_equals(value, this.get_interface_object(), "wrong value in " + name + " property descriptor");
            assert_equals(Object.getPrototypeOf(value), Function.prototype, "wrong value for " + name + "'s prototype");
        }.bind(this), this.name + " interface: named constructor object");

        subsetTestByKey(this.name, test, function()
        {
            // This function tests WebIDL as of 2019-01-14.

            // "7. Let proto be the interface prototype object of interface I
            //     in realm.
            // "8. Perform ! DefinePropertyOrThrow(F, "prototype",
            //     PropertyDescriptor{
            //         [[Value]]: proto, [[Writable]]: false,
            //         [[Enumerable]]: false, [[Configurable]]: false
            //     })."
            var name = constructor.rhs.value;
            var expected = this.get_interface_object().prototype;
            var desc = Object.getOwnPropertyDescriptor(self[name], "prototype");
            assert_equals(desc.value, expected, "wrong value for " + name + ".prototype");
            assert_false(desc.writable, "prototype should not be writable");
            assert_false(desc.enumerable, "prototype should not be enumerable");
            assert_false(desc.configurable, "prototype should not be configurable");
            assert_false("get" in desc, "prototype should not have a getter");
            assert_false("set" in desc, "prototype should not have a setter");
        }.bind(this), this.name + " interface: named constructor prototype property");

        subsetTestByKey(this.name, test, function()
        {
            // This function tests WebIDL as of 2019-01-14.

            // "3. Perform ! SetFunctionName(F, id)."
            var name = constructor.rhs.value;
            var desc = Object.getOwnPropertyDescriptor(self[name], "name");
            assert_equals(desc.value, name, "wrong value for " + name + ".name");
            assert_false(desc.writable, "name should not be writable");
            assert_false(desc.enumerable, "name should not be enumerable");
            assert_true(desc.configurable, "name should be configurable");
            assert_false("get" in desc, "name should not have a getter");
            assert_false("set" in desc, "name should not have a setter");
        }.bind(this), this.name + " interface: named constructor name");

        subsetTestByKey(this.name, test, function()
        {
            // This function tests WebIDL as of 2019-01-14.

            // "4. Initialize S to the effective overload set for constructors
            //     with identifier id on interface I and with argument count 0.
            // "5. Let length be the length of the shortest argument list of
            //     the entries in S.
            // "6. Perform ! SetFunctionLength(F, length)."
            var name = constructor.rhs.value;
            var desc = Object.getOwnPropertyDescriptor(self[name], "length");
            assert_equals(desc.value, min_length, "wrong value for " + name + ".length");
            assert_false(desc.writable, "length should not be writable");
            assert_false(desc.enumerable, "length should not be enumerable");
            assert_true(desc.configurable, "length should be configurable");
            assert_false("get" in desc, "length should not have a getter");
            assert_false("set" in desc, "length should not have a setter");
        }.bind(this), this.name + " interface: named constructor length");

        subsetTestByKey(this.name, test, function()
        {
            // This function tests WebIDL as of 2019-01-14.

            // "1. Let steps be the following steps:
            // "    1. If NewTarget is undefined, then throw a TypeError."
            var name = constructor.rhs.value;
            var args = constructor.arguments.map(function(arg) {
                return create_suitable_object(arg.idlType);
            });
            assert_throws_js(globalOf(self[name]).TypeError, function() {
                self[name](...args);
            }.bind(this));
        }.bind(this), this.name + " interface: named constructor without 'new'");
    }

    subsetTestByKey(this.name, test, function()
    {
        // This function tests WebIDL as of 2015-01-21.
        // https://webidl.spec.whatwg.org/#interface-object

        if (!this.should_have_interface_object()) {
            return;
        }

        this.assert_interface_object_exists();

        if (this.is_callback()) {
            assert_false("prototype" in this.get_interface_object(),
                         this.name + ' should not have a "prototype" property');
            return;
        }

        // "An interface object for a non-callback interface must have a
        // property named “prototype” with attributes { [[Writable]]: false,
        // [[Enumerable]]: false, [[Configurable]]: false } whose value is an
        // object called the interface prototype object. This object has
        // properties that correspond to the regular attributes and regular
        // operations defined on the interface, and is described in more detail
        // in section 4.5.4 below."
        assert_own_property(this.get_interface_object(), "prototype",
                            'interface "' + this.name + '" does not have own property "prototype"');
        var desc = Object.getOwnPropertyDescriptor(this.get_interface_object(), "prototype");
        assert_false("get" in desc, this.name + ".prototype should not have a getter");
        assert_false("set" in desc, this.name + ".prototype should not have a setter");
        assert_false(desc.writable, this.name + ".prototype should not be writable");
        assert_false(desc.enumerable, this.name + ".prototype should not be enumerable");
        assert_false(desc.configurable, this.name + ".prototype should not be configurable");

        // Next, test that the [[Prototype]] of the interface prototype object
        // is correct. (This is made somewhat difficult by the existence of
        // [LegacyNoInterfaceObject].)
        // TODO: Aryeh thinks there's at least other place in this file where
        //       we try to figure out if an interface prototype object is
        //       correct. Consolidate that code.

        // "The interface prototype object for a given interface A must have an
        // internal [[Prototype]] property whose value is returned from the
        // following steps:
        // "If A is declared with the [Global] extended
        // attribute, and A supports named properties, then return the named
        // properties object for A, as defined in §3.6.4 Named properties
        // object.
        // "Otherwise, if A is declared to inherit from another interface, then
        // return the interface prototype object for the inherited interface.
        // "Otherwise, return %ObjectPrototype%.
        //
        // "In the ECMAScript binding, the DOMException type has some additional
        // requirements:
        //
        //     "Unlike normal interface types, the interface prototype object
        //     for DOMException must have as its [[Prototype]] the intrinsic
        //     object %ErrorPrototype%."
        //
        if (this.name === "Window") {
            assert_class_string(Object.getPrototypeOf(this.get_interface_object().prototype),
                                'WindowProperties',
                                'Class name for prototype of Window' +
                                '.prototype is not "WindowProperties"');
        } else {
            var inherit_interface, inherit_interface_interface_object;
            if (this.base) {
                inherit_interface = this.base;
                var parent = this.array.members[inherit_interface];
                if (!parent.has_extended_attribute("LegacyNoInterfaceObject")) {
                    parent.assert_interface_object_exists();
                    inherit_interface_interface_object = parent.get_interface_object();
                }
            } else if (this.name === "DOMException") {
                inherit_interface = 'Error';
                inherit_interface_interface_object = self.Error;
            } else {
                inherit_interface = 'Object';
                inherit_interface_interface_object = self.Object;
            }
            if (inherit_interface_interface_object) {
                assert_not_equals(inherit_interface_interface_object, undefined,
                                  'should inherit from ' + inherit_interface + ', but there is no such property');
                assert_own_property(inherit_interface_interface_object, 'prototype',
                                    'should inherit from ' + inherit_interface + ', but that object has no "prototype" property');
                assert_equals(Object.getPrototypeOf(this.get_interface_object().prototype),
                              inherit_interface_interface_object.prototype,
                              'prototype of ' + this.name + '.prototype is not ' + inherit_interface + '.prototype');
            } else {
                // We can't test that we get the correct object, because this is the
                // only way to get our hands on it. We only test that its class
                // string, at least, is correct.
                assert_class_string(Object.getPrototypeOf(this.get_interface_object().prototype),
                                    inherit_interface + 'Prototype',
                                    'Class name for prototype of ' + this.name +
                                    '.prototype is not "' + inherit_interface + 'Prototype"');
            }
        }

        // "The class string of an interface prototype object is the
        // concatenation of the interface’s qualified identifier and the string
        // “Prototype”."

        // Skip these tests for now due to a specification issue about
        // prototype name.
        // https://www.w3.org/Bugs/Public/show_bug.cgi?id=28244

        // assert_class_string(this.get_interface_object().prototype, this.get_qualified_name() + "Prototype",
        //                     "class string of " + this.name + ".prototype");

        // String() should end up calling {}.toString if nothing defines a
        // stringifier.
        if (!this.has_stringifier()) {
            // assert_equals(String(this.get_interface_object().prototype), "[object " + this.get_qualified_name() + "Prototype]",
            //         "String(" + this.name + ".prototype)");
        }
    }.bind(this), this.name + " interface: existence and properties of interface prototype object");

    // "If the interface is declared with the [Global]
    // extended attribute, or the interface is in the set of inherited
    // interfaces for any other interface that is declared with one of these
    // attributes, then the interface prototype object must be an immutable
    // prototype exotic object."
    // https://webidl.spec.whatwg.org/#interface-prototype-object
    if (this.is_global()) {
        this.test_immutable_prototype("interface prototype object", this.get_interface_object().prototype);
    }

    subsetTestByKey(this.name, test, function()
    {
        if (!this.should_have_interface_object()) {
            return;
        }

        this.assert_interface_object_exists();

        if (this.is_callback()) {
            assert_false("prototype" in this.get_interface_object(),
                         this.name + ' should not have a "prototype" property');
            return;
        }

        assert_own_property(this.get_interface_object(), "prototype",
                            'interface "' + this.name + '" does not have own property "prototype"');

        // "If the [LegacyNoInterfaceObject] extended attribute was not specified
        // on the interface, then the interface prototype object must also have a
        // property named “constructor” with attributes { [[Writable]]: true,
        // [[Enumerable]]: false, [[Configurable]]: true } whose value is a
        // reference to the interface object for the interface."
        assert_own_property(this.get_interface_object().prototype, "constructor",
                            this.name + '.prototype does not have own property "constructor"');
        var desc = Object.getOwnPropertyDescriptor(this.get_interface_object().prototype, "constructor");
        assert_false("get" in desc, this.name + ".prototype.constructor should not have a getter");
        assert_false("set" in desc, this.name + ".prototype.constructor should not have a setter");
        assert_true(desc.writable, this.name + ".prototype.constructor should be writable");
        assert_false(desc.enumerable, this.name + ".prototype.constructor should not be enumerable");
        assert_true(desc.configurable, this.name + ".prototype.constructor should be configurable");
        assert_equals(this.get_interface_object().prototype.constructor, this.get_interface_object(),
                      this.name + '.prototype.constructor is not the same object as ' + this.name);
    }.bind(this), this.name + ' interface: existence and properties of interface prototype object\'s "constructor" property');


    subsetTestByKey(this.name, test, function()
    {
        if (!this.should_have_interface_object()) {
            return;
        }

        this.assert_interface_object_exists();

        if (this.is_callback()) {
            assert_false("prototype" in this.get_interface_object(),
                         this.name + ' should not have a "prototype" property');
            return;
        }

        assert_own_property(this.get_interface_object(), "prototype",
                            'interface "' + this.name + '" does not have own property "prototype"');

        // If the interface has any member declared with the [Unscopable] extended
        // attribute, then there must be a property on the interface prototype object
        // whose name is the @@unscopables symbol, which has the attributes
        // { [[Writable]]: false, [[Enumerable]]: false, [[Configurable]]: true },
        // and whose value is an object created as follows...
        var unscopables = this.get_unscopables().map(m => m.name);
        var proto = this.get_interface_object().prototype;
        if (unscopables.length != 0) {
            assert_own_property(
                proto, Symbol.unscopables,
                this.name + '.prototype should have an @@unscopables property');
            var desc = Object.getOwnPropertyDescriptor(proto, Symbol.unscopables);
            assert_false("get" in desc,
                         this.name + ".prototype[Symbol.unscopables] should not have a getter");
            assert_false("set" in desc, this.name + ".prototype[Symbol.unscopables] should not have a setter");
            assert_false(desc.writable, this.name + ".prototype[Symbol.unscopables] should not be writable");
            assert_false(desc.enumerable, this.name + ".prototype[Symbol.unscopables] should not be enumerable");
            assert_true(desc.configurable, this.name + ".prototype[Symbol.unscopables] should be configurable");
            assert_equals(desc.value, proto[Symbol.unscopables],
                          this.name + '.prototype[Symbol.unscopables] should be in the descriptor');
            assert_equals(typeof desc.value, "object",
                          this.name + '.prototype[Symbol.unscopables] should be an object');
            assert_equals(Object.getPrototypeOf(desc.value), null,
                          this.name + '.prototype[Symbol.unscopables] should have a null prototype');
            assert_equals(Object.getOwnPropertySymbols(desc.value).length,
                          0,
                          this.name + '.prototype[Symbol.unscopables] should have the right number of symbol-named properties');

            // Check that we do not have _extra_ unscopables.  Checking that we
            // have all the ones we should will happen in the per-member tests.
            var observed = Object.getOwnPropertyNames(desc.value);
            for (var prop of observed) {
                assert_not_equals(unscopables.indexOf(prop),
                                  -1,
                                  this.name + '.prototype[Symbol.unscopables] has unexpected property "' + prop + '"');
            }
        } else {
            assert_equals(Object.getOwnPropertyDescriptor(this.get_interface_object().prototype, Symbol.unscopables),
                          undefined,
                          this.name + '.prototype should not have @@unscopables');
        }
    }.bind(this), this.name + ' interface: existence and properties of interface prototype object\'s @@unscopables property');
};

IdlInterface.prototype.test_immutable_prototype = function(type, obj)
{
    if (typeof Object.setPrototypeOf !== "function") {
        return;
    }

    subsetTestByKey(this.name, test, function(t) {
        var originalValue = Object.getPrototypeOf(obj);
        var newValue = Object.create(null);

        t.add_cleanup(function() {
            try {
                Object.setPrototypeOf(obj, originalValue);
            } catch (err) {}
        });

        assert_throws_js(TypeError, function() {
            Object.setPrototypeOf(obj, newValue);
        });

        assert_equals(
                Object.getPrototypeOf(obj),
                originalValue,
                "original value not modified"
            );
    }.bind(this), this.name + " interface: internal [[SetPrototypeOf]] method " +
        "of " + type + " - setting to a new value via Object.setPrototypeOf " +
        "should throw a TypeError");

    subsetTestByKey(this.name, test, function(t) {
        var originalValue = Object.getPrototypeOf(obj);
        var newValue = Object.create(null);

        t.add_cleanup(function() {
            let setter = Object.getOwnPropertyDescriptor(
                Object.prototype, '__proto__'
            ).set;

            try {
                setter.call(obj, originalValue);
            } catch (err) {}
        });

        // We need to find the actual setter for the '__proto__' property, so we
        // can determine the right global for it.  Walk up the prototype chain
        // looking for that property until we find it.
        let setter;
        {
            let cur = obj;
            while (cur) {
                const desc = Object.getOwnPropertyDescriptor(cur, "__proto__");
                if (desc) {
                    setter = desc.set;
                    break;
                }
                cur = Object.getPrototypeOf(cur);
            }
        }
        assert_throws_js(globalOf(setter).TypeError, function() {
            obj.__proto__ = newValue;
        });

        assert_equals(
                Object.getPrototypeOf(obj),
                originalValue,
                "original value not modified"
            );
    }.bind(this), this.name + " interface: internal [[SetPrototypeOf]] method " +
        "of " + type + " - setting to a new value via __proto__ " +
        "should throw a TypeError");

    subsetTestByKey(this.name, test, function(t) {
        var originalValue = Object.getPrototypeOf(obj);
        var newValue = Object.create(null);

        t.add_cleanup(function() {
            try {
                Reflect.setPrototypeOf(obj, originalValue);
            } catch (err) {}
        });

        assert_false(Reflect.setPrototypeOf(obj, newValue));

        assert_equals(
                Object.getPrototypeOf(obj),
                originalValue,
                "original value not modified"
            );
    }.bind(this), this.name + " interface: internal [[SetPrototypeOf]] method " +
        "of " + type + " - setting to a new value via Reflect.setPrototypeOf " +
        "should return false");

    subsetTestByKey(this.name, test, function() {
        var originalValue = Object.getPrototypeOf(obj);

        Object.setPrototypeOf(obj, originalValue);
    }.bind(this), this.name + " interface: internal [[SetPrototypeOf]] method " +
        "of " + type + " - setting to its original value via Object.setPrototypeOf " +
        "should not throw");

    subsetTestByKey(this.name, test, function() {
        var originalValue = Object.getPrototypeOf(obj);

        obj.__proto__ = originalValue;
    }.bind(this), this.name + " interface: internal [[SetPrototypeOf]] method " +
        "of " + type + " - setting to its original value via __proto__ " +
        "should not throw");

    subsetTestByKey(this.name, test, function() {
        var originalValue = Object.getPrototypeOf(obj);

        assert_true(Reflect.setPrototypeOf(obj, originalValue));
    }.bind(this), this.name + " interface: internal [[SetPrototypeOf]] method " +
        "of " + type + " - setting to its original value via Reflect.setPrototypeOf " +
        "should return true");
};

IdlInterface.prototype.test_member_const = function(member)
{
    if (!this.has_constants()) {
        throw new IdlHarnessError("Internal error: test_member_const called without any constants");
    }

    subsetTestByKey(this.name, test, function()
    {
        this.assert_interface_object_exists();

        // "For each constant defined on an interface A, there must be
        // a corresponding property on the interface object, if it
        // exists."
        assert_own_property(this.get_interface_object(), member.name);
        // "The value of the property is that which is obtained by
        // converting the constant’s IDL value to an ECMAScript
        // value."
        assert_equals(this.get_interface_object()[member.name], constValue(member.value),
                      "property has wrong value");
        // "The property has attributes { [[Writable]]: false,
        // [[Enumerable]]: true, [[Configurable]]: false }."
        var desc = Object.getOwnPropertyDescriptor(this.get_interface_object(), member.name);
        assert_false("get" in desc, "property should not have a getter");
        assert_false("set" in desc, "property should not have a setter");
        assert_false(desc.writable, "property should not be writable");
        assert_true(desc.enumerable, "property should be enumerable");
        assert_false(desc.configurable, "property should not be configurable");
    }.bind(this), this.name + " interface: constant " + member.name + " on interface object");

    // "In addition, a property with the same characteristics must
    // exist on the interface prototype object."
    subsetTestByKey(this.name, test, function()
    {
        this.assert_interface_object_exists();

        if (this.is_callback()) {
            assert_false("prototype" in this.get_interface_object(),
                         this.name + ' should not have a "prototype" property');
            return;
        }

        assert_own_property(this.get_interface_object(), "prototype",
                            'interface "' + this.name + '" does not have own property "prototype"');

        assert_own_property(this.get_interface_object().prototype, member.name);
        assert_equals(this.get_interface_object().prototype[member.name], constValue(member.value),
                      "property has wrong value");
        var desc = Object.getOwnPropertyDescriptor(this.get_interface_object(), member.name);
        assert_false("get" in desc, "property should not have a getter");
        assert_false("set" in desc, "property should not have a setter");
        assert_false(desc.writable, "property should not be writable");
        assert_true(desc.enumerable, "property should be enumerable");
        assert_false(desc.configurable, "property should not be configurable");
    }.bind(this), this.name + " interface: constant " + member.name + " on interface prototype object");
};


IdlInterface.prototype.test_member_attribute = function(member)
  {
    if (!shouldRunSubTest(this.name)) {
        return;
    }
    var a_test = subsetTestByKey(this.name, async_test, this.name + " interface: attribute " + member.name);
    a_test.step(function()
    {
        if (!this.should_have_interface_object()) {
            a_test.done();
            return;
        }

        this.assert_interface_object_exists();
        assert_own_property(this.get_interface_object(), "prototype",
                            'interface "' + this.name + '" does not have own property "prototype"');

        if (member.special === "static") {
            assert_own_property(this.get_interface_object(), member.name,
                "The interface object must have a property " +
                format_value(member.name));
            a_test.done();
            return;
        }

        this.do_member_unscopable_asserts(member);

        if (this.is_global()) {
            assert_own_property(self, member.name,
                "The global object must have a property " +
                format_value(member.name));
            assert_false(member.name in this.get_interface_object().prototype,
                "The prototype object should not have a property " +
                format_value(member.name));

            var getter = Object.getOwnPropertyDescriptor(self, member.name).get;
            assert_equals(typeof(getter), "function",
                          format_value(member.name) + " must have a getter");

            // Try/catch around the get here, since it can legitimately throw.
            // If it does, we obviously can't check for equality with direct
            // invocation of the getter.
            var gotValue;
            var propVal;
            try {
                propVal = self[member.name];
                gotValue = true;
            } catch (e) {
                gotValue = false;
            }
            if (gotValue) {
                assert_equals(propVal, getter.call(undefined),
                              "Gets on a global should not require an explicit this");
            }

            // do_interface_attribute_asserts must be the last thing we do,
            // since it will call done() on a_test.
            this.do_interface_attribute_asserts(self, member, a_test);
        } else {
            assert_true(member.name in this.get_interface_object().prototype,
                "The prototype object must have a property " +
                format_value(member.name));

            if (!member.has_extended_attribute("LegacyLenientThis")) {
                if (member.idlType.generic !== "Promise") {
                    // this.get_interface_object() returns a thing in our global
                    assert_throws_js(TypeError, function() {
                        this.get_interface_object().prototype[member.name];
                    }.bind(this), "getting property on prototype object must throw TypeError");
                    // do_interface_attribute_asserts must be the last thing we
                    // do, since it will call done() on a_test.
                    this.do_interface_attribute_asserts(this.get_interface_object().prototype, member, a_test);
                } else {
                    promise_rejects_js(a_test, TypeError,
                                    this.get_interface_object().prototype[member.name])
                        .then(a_test.step_func(function() {
                            // do_interface_attribute_asserts must be the last
                            // thing we do, since it will call done() on a_test.
                            this.do_interface_attribute_asserts(this.get_interface_object().prototype,
                                                                member, a_test);
                        }.bind(this)));
                }
            } else {
                assert_equals(this.get_interface_object().prototype[member.name], undefined,
                              "getting property on prototype object must return undefined");
              // do_interface_attribute_asserts must be the last thing we do,
              // since it will call done() on a_test.
              this.do_interface_attribute_asserts(this.get_interface_object().prototype, member, a_test);
            }
        }
    }.bind(this));
};

IdlInterface.prototype.test_member_operation = function(member)
{
    if (!shouldRunSubTest(this.name)) {
        return;
    }
    var a_test = subsetTestByKey(this.name, async_test, this.name + " interface: operation " + member);
    a_test.step(function()
    {
        // This function tests WebIDL as of 2015-12-29.
        // https://webidl.spec.whatwg.org/#es-operations

        if (!this.should_have_interface_object()) {
            a_test.done();
            return;
        }

        this.assert_interface_object_exists();

        if (this.is_callback()) {
            assert_false("prototype" in this.get_interface_object(),
                         this.name + ' should not have a "prototype" property');
            a_test.done();
            return;
        }

        assert_own_property(this.get_interface_object(), "prototype",
                            'interface "' + this.name + '" does not have own property "prototype"');

        // "For each unique identifier of an exposed operation defined on the
        // interface, there must exist a corresponding property, unless the
        // effective overload set for that identifier and operation and with an
        // argument count of 0 has no entries."

        // TODO: Consider [Exposed].

        // "The location of the property is determined as follows:"
        var memberHolderObject;
        // "* If the operation is static, then the property exists on the
        //    interface object."
        if (member.special === "static") {
            assert_own_property(this.get_interface_object(), member.name,
                    "interface object missing static operation");
            memberHolderObject = this.get_interface_object();
        // "* Otherwise, [...] if the interface was declared with the [Global]
        //    extended attribute, then the property exists
        //    on every object that implements the interface."
        } else if (this.is_global()) {
            assert_own_property(self, member.name,
                    "global object missing non-static operation");
            memberHolderObject = self;
        // "* Otherwise, the property exists solely on the interface’s
        //    interface prototype object."
        } else {
            assert_own_property(this.get_interface_object().prototype, member.name,
                    "interface prototype object missing non-static operation");
            memberHolderObject = this.get_interface_object().prototype;
        }
        this.do_member_unscopable_asserts(member);
        this.do_member_operation_asserts(memberHolderObject, member, a_test);
    }.bind(this));
};

IdlInterface.prototype.do_member_unscopable_asserts = function(member)
{
    // Check that if the member is unscopable then it's in the
    // @@unscopables object properly.
    if (!member.isUnscopable) {
        return;
    }

    var unscopables = this.get_interface_object().prototype[Symbol.unscopables];
    var prop = member.name;
    var propDesc = Object.getOwnPropertyDescriptor(unscopables, prop);
    assert_equals(typeof propDesc, "object",
                  this.name + '.prototype[Symbol.unscopables].' + prop + ' must exist')
    assert_false("get" in propDesc,
                 this.name + '.prototype[Symbol.unscopables].' + prop + ' must have no getter');
    assert_false("set" in propDesc,
                 this.name + '.prototype[Symbol.unscopables].' + prop + ' must have no setter');
    assert_true(propDesc.writable,
                this.name + '.prototype[Symbol.unscopables].' + prop + ' must be writable');
    assert_true(propDesc.enumerable,
                this.name + '.prototype[Symbol.unscopables].' + prop + ' must be enumerable');
    assert_true(propDesc.configurable,
                this.name + '.prototype[Symbol.unscopables].' + prop + ' must be configurable');
    assert_equals(propDesc.value, true,
                  this.name + '.prototype[Symbol.unscopables].' + prop + ' must have the value `true`');
};

IdlInterface.prototype.do_member_operation_asserts = function(memberHolderObject, member, a_test)
{
    var done = a_test.done.bind(a_test);
    var operationUnforgeable = member.isUnforgeable;
    var desc = Object.getOwnPropertyDescriptor(memberHolderObject, member.name);
    // "The property has attributes { [[Writable]]: B,
    // [[Enumerable]]: true, [[Configurable]]: B }, where B is false if the
    // operation is unforgeable on the interface, and true otherwise".
    assert_false("get" in desc, "property should not have a getter");
    assert_false("set" in desc, "property should not have a setter");
    assert_equals(desc.writable, !operationUnforgeable,
                  "property should be writable if and only if not unforgeable");
    assert_true(desc.enumerable, "property should be enumerable");
    assert_equals(desc.configurable, !operationUnforgeable,
                  "property should be configurable if and only if not unforgeable");
    // "The value of the property is a Function object whose
    // behavior is as follows . . ."
    assert_equals(typeof memberHolderObject[member.name], "function",
                  "property must be a function");

    const operationOverloads = this.members.filter(function(m) {
        return m.type == "operation" && m.name == member.name &&
            (m.special === "static") === (member.special === "static");
    });
    assert_equals(
        memberHolderObject[member.name].length,
        minOverloadLength(operationOverloads),
        "property has wrong .length");
    assert_equals(
        memberHolderObject[member.name].name,
        member.name,
        "property has wrong .name");

    // Make some suitable arguments
    var args = member.arguments.map(function(arg) {
        return create_suitable_object(arg.idlType);
    });

    // "Let O be a value determined as follows:
    // ". . .
    // "Otherwise, throw a TypeError."
    // This should be hit if the operation is not static, there is
    // no [ImplicitThis] attribute, and the this value is null.
    //
    // TODO: We currently ignore the [ImplicitThis] case.  Except we manually
    // check for globals, since otherwise we'll invoke window.close().  And we
    // have to skip this test for anything that on the proto chain of "self",
    // since that does in fact have implicit-this behavior.
    if (member.special !== "static") {
        var cb;
        if (!this.is_global() &&
            memberHolderObject[member.name] != self[member.name])
        {
            cb = awaitNCallbacks(2, done);
            throwOrReject(a_test, member, memberHolderObject[member.name], null, args,
                          "calling operation with this = null didn't throw TypeError", cb);
        } else {
            cb = awaitNCallbacks(1, done);
        }

        // ". . . If O is not null and is also not a platform object
        // that implements interface I, throw a TypeError."
        //
        // TODO: Test a platform object that implements some other
        // interface.  (Have to be sure to get inheritance right.)
        throwOrReject(a_test, member, memberHolderObject[member.name], {}, args,
                      "calling operation with this = {} didn't throw TypeError", cb);
    } else {
        done();
    }
}

IdlInterface.prototype.test_to_json_operation = function(desc, memberHolderObject, member) {
    var instanceName = memberHolderObject && memberHolderObject.constructor.name
        || member.name + " object";
    if (member.has_extended_attribute("Default")) {
        subsetTestByKey(this.name, test, function() {
            var map = this.default_to_json_operation();
            var json = memberHolderObject.toJSON();
            map.forEach(function(type, k) {
                assert_true(k in json, "property " + JSON.stringify(k) + " should be present in the output of " + this.name + ".prototype.toJSON()");
                var descriptor = Object.getOwnPropertyDescriptor(json, k);
                assert_true(descriptor.writable, "property " + k + " should be writable");
                assert_true(descriptor.configurable, "property " + k + " should be configurable");
                assert_true(descriptor.enumerable, "property " + k + " should be enumerable");
                this.array.assert_type_is(json[k], type);
                delete json[k];
            }, this);
        }.bind(this), this.name + " interface: default toJSON operation on " + desc);
    } else {
        subsetTestByKey(this.name, test, function() {
            assert_true(this.array.is_json_type(member.idlType), JSON.stringify(member.idlType) + " is not an appropriate return value for the toJSON operation of " + instanceName);
            this.array.assert_type_is(memberHolderObject.toJSON(), member.idlType);
        }.bind(this), this.name + " interface: toJSON operation on " + desc);
    }
};

IdlInterface.prototype.test_member_maplike = function(member) {
    subsetTestByKey(this.name, test, () => {
        const proto = this.get_interface_object().prototype;

        const methods = [
            ["entries", 0],
            ["keys", 0],
            ["values", 0],
            ["forEach", 1],
            ["get", 1],
            ["has", 1]
        ];
        if (!member.readonly) {
            methods.push(
                ["set", 2],
                ["delete", 1],
                ["clear", 0]
            );
        }

        for (const [name, length] of methods) {
            const desc = Object.getOwnPropertyDescriptor(proto, name);
            assert_equals(typeof desc.value, "function", `${name} should be a function`);
            assert_equals(desc.enumerable, true, `${name} enumerable`);
            assert_equals(desc.configurable, true, `${name} configurable`);
            assert_equals(desc.writable, true, `${name} writable`);
            assert_equals(desc.value.length, length, `${name} function object length should be ${length}`);
            assert_equals(desc.value.name, name, `${name} function object should have the right name`);
        }

        const iteratorDesc = Object.getOwnPropertyDescriptor(proto, Symbol.iterator);
        assert_equals(iteratorDesc.value, proto.entries, `@@iterator should equal entries`);
        assert_equals(iteratorDesc.enumerable, false, `@@iterator enumerable`);
        assert_equals(iteratorDesc.configurable, true, `@@iterator configurable`);
        assert_equals(iteratorDesc.writable, true, `@@iterator writable`);

        const sizeDesc = Object.getOwnPropertyDescriptor(proto, "size");
        assert_equals(typeof sizeDesc.get, "function", `size getter should be a function`);
        assert_equals(sizeDesc.set, undefined, `size should not have a setter`);
        assert_equals(sizeDesc.enumerable, true, `size enumerable`);
        assert_equals(sizeDesc.configurable, true, `size configurable`);
        assert_equals(sizeDesc.get.length, 0, `size getter length`);
        assert_equals(sizeDesc.get.name, "get size", `size getter name`);
    }, `${this.name} interface: maplike<${member.idlType.map(t => t.idlType).join(", ")}>`);
};

IdlInterface.prototype.test_member_setlike = function(member) {
    subsetTestByKey(this.name, test, () => {
        const proto = this.get_interface_object().prototype;

        const methods = [
            ["entries", 0],
            ["keys", 0],
            ["values", 0],
            ["forEach", 1],
            ["has", 1]
        ];
        if (!member.readonly) {
            methods.push(
                ["add", 1],
                ["delete", 1],
                ["clear", 0]
            );
        }

        for (const [name, length] of methods) {
            const desc = Object.getOwnPropertyDescriptor(proto, name);
            assert_equals(typeof desc.value, "function", `${name} should be a function`);
            assert_equals(desc.enumerable, true, `${name} enumerable`);
            assert_equals(desc.configurable, true, `${name} configurable`);
            assert_equals(desc.writable, true, `${name} writable`);
            assert_equals(desc.value.length, length, `${name} function object length should be ${length}`);
            assert_equals(desc.value.name, name, `${name} function object should have the right name`);
        }

        const iteratorDesc = Object.getOwnPropertyDescriptor(proto, Symbol.iterator);
        assert_equals(iteratorDesc.value, proto.values, `@@iterator should equal values`);
        assert_equals(iteratorDesc.enumerable, false, `@@iterator enumerable`);
        assert_equals(iteratorDesc.configurable, true, `@@iterator configurable`);
        assert_equals(iteratorDesc.writable, true, `@@iterator writable`);

        const sizeDesc = Object.getOwnPropertyDescriptor(proto, "size");
        assert_equals(typeof sizeDesc.get, "function", `size getter should be a function`);
        assert_equals(sizeDesc.set, undefined, `size should not have a setter`);
        assert_equals(sizeDesc.enumerable, true, `size enumerable`);
        assert_equals(sizeDesc.configurable, true, `size configurable`);
        assert_equals(sizeDesc.get.length, 0, `size getter length`);
        assert_equals(sizeDesc.get.name, "get size", `size getter name`);
    }, `${this.name} interface: setlike<${member.idlType.map(t => t.idlType).join(", ")}>`);
};

IdlInterface.prototype.test_member_iterable = function(member) {
    subsetTestByKey(this.name, test, () => {
        const isPairIterator = member.idlType.length === 2;
        const proto = this.get_interface_object().prototype;

        const methods = [
            ["entries", 0],
            ["keys", 0],
            ["values", 0],
            ["forEach", 1]
        ];

        for (const [name, length] of methods) {
            const desc = Object.getOwnPropertyDescriptor(proto, name);
            assert_equals(typeof desc.value, "function", `${name} should be a function`);
            assert_equals(desc.enumerable, true, `${name} enumerable`);
            assert_equals(desc.configurable, true, `${name} configurable`);
            assert_equals(desc.writable, true, `${name} writable`);
            assert_equals(desc.value.length, length, `${name} function object length should be ${length}`);
            assert_equals(desc.value.name, name, `${name} function object should have the right name`);

            if (!isPairIterator) {
                assert_equals(desc.value, Array.prototype[name], `${name} equality with Array.prototype version`);
            }
        }

        const iteratorDesc = Object.getOwnPropertyDescriptor(proto, Symbol.iterator);
        assert_equals(iteratorDesc.enumerable, false, `@@iterator enumerable`);
        assert_equals(iteratorDesc.configurable, true, `@@iterator configurable`);
        assert_equals(iteratorDesc.writable, true, `@@iterator writable`);

        if (isPairIterator) {
            assert_equals(iteratorDesc.value, proto.entries, `@@iterator equality with entries`);
        } else {
            assert_equals(iteratorDesc.value, Array.prototype[Symbol.iterator], `@@iterator equality with Array.prototype version`);
        }
    }, `${this.name} interface: iterable<${member.idlType.map(t => t.idlType).join(", ")}>`);
};

IdlInterface.prototype.test_member_async_iterable = function(member) {
    subsetTestByKey(this.name, test, () => {
        const isPairIterator = member.idlType.length === 2;
        const proto = this.get_interface_object().prototype;

        // Note that although the spec allows arguments, which will be passed to the @@asyncIterator
        // method (which is either values or entries), those arguments must always be optional. So
        // length of 0 is still correct for values and entries.
        const methods = [
            ["values", 0],
        ];

        if (isPairIterator) {
            methods.push(
                ["entries", 0],
                ["keys", 0]
            );
        }

        for (const [name, length] of methods) {
            const desc = Object.getOwnPropertyDescriptor(proto, name);
            assert_equals(typeof desc.value, "function", `${name} should be a function`);
            assert_equals(desc.enumerable, true, `${name} enumerable`);
            assert_equals(desc.configurable, true, `${name} configurable`);
            assert_equals(desc.writable, true, `${name} writable`);
            assert_equals(desc.value.length, length, `${name} function object length should be ${length}`);
            assert_equals(desc.value.name, name, `${name} function object should have the right name`);
        }

        const iteratorDesc = Object.getOwnPropertyDescriptor(proto, Symbol.asyncIterator);
        assert_equals(iteratorDesc.enumerable, false, `@@iterator enumerable`);
        assert_equals(iteratorDesc.configurable, true, `@@iterator configurable`);
        assert_equals(iteratorDesc.writable, true, `@@iterator writable`);

        if (isPairIterator) {
            assert_equals(iteratorDesc.value, proto.entries, `@@iterator equality with entries`);
        } else {
            assert_equals(iteratorDesc.value, proto.values, `@@iterator equality with values`);
        }
    }, `${this.name} interface: async iterable<${member.idlType.map(t => t.idlType).join(", ")}>`);
};

IdlInterface.prototype.test_member_stringifier = function(member)
{
    subsetTestByKey(this.name, test, function()
    {
        if (!this.should_have_interface_object()) {
            return;
        }

        this.assert_interface_object_exists();

        if (this.is_callback()) {
            assert_false("prototype" in this.get_interface_object(),
                         this.name + ' should not have a "prototype" property');
            return;
        }

        assert_own_property(this.get_interface_object(), "prototype",
                            'interface "' + this.name + '" does not have own property "prototype"');

        // ". . . the property exists on the interface prototype object."
        var interfacePrototypeObject = this.get_interface_object().prototype;
        assert_own_property(interfacePrototypeObject, "toString",
                "interface prototype object missing non-static operation");

        var stringifierUnforgeable = member.isUnforgeable;
        var desc = Object.getOwnPropertyDescriptor(interfacePrototypeObject, "toString");
        // "The property has attributes { [[Writable]]: B,
        // [[Enumerable]]: true, [[Configurable]]: B }, where B is false if the
        // stringifier is unforgeable on the interface, and true otherwise."
        assert_false("get" in desc, "property should not have a getter");
        assert_false("set" in desc, "property should not have a setter");
        assert_equals(desc.writable, !stringifierUnforgeable,
                      "property should be writable if and only if not unforgeable");
        assert_true(desc.enumerable, "property should be enumerable");
        assert_equals(desc.configurable, !stringifierUnforgeable,
                      "property should be configurable if and only if not unforgeable");
        // "The value of the property is a Function object, which behaves as
        // follows . . ."
        assert_equals(typeof interfacePrototypeObject.toString, "function",
                      "property must be a function");
        // "The value of the Function object’s “length” property is the Number
        // value 0."
        assert_equals(interfacePrototypeObject.toString.length, 0,
            "property has wrong .length");

        // "Let O be the result of calling ToObject on the this value."
        assert_throws_js(globalOf(interfacePrototypeObject.toString).TypeError, function() {
            interfacePrototypeObject.toString.apply(null, []);
        }, "calling stringifier with this = null didn't throw TypeError");

        // "If O is not an object that implements the interface on which the
        // stringifier was declared, then throw a TypeError."
        //
        // TODO: Test a platform object that implements some other
        // interface.  (Have to be sure to get inheritance right.)
        assert_throws_js(globalOf(interfacePrototypeObject.toString).TypeError, function() {
            interfacePrototypeObject.toString.apply({}, []);
        }, "calling stringifier with this = {} didn't throw TypeError");
    }.bind(this), this.name + " interface: stringifier");
};

IdlInterface.prototype.test_members = function()
{
    var unexposed_members = new Set();
    for (var i = 0; i < this.members.length; i++)
    {
        var member = this.members[i];
        if (member.untested) {
            continue;
        }

        if (!exposed_in(exposure_set(member, this.exposureSet))) {
            if (!unexposed_members.has(member.name)) {
                unexposed_members.add(member.name);
                subsetTestByKey(this.name, test, function() {
                    // It's not exposed, so we shouldn't find it anywhere.
                    assert_false(member.name in this.get_interface_object(),
                                "The interface object must not have a property " +
                                format_value(member.name));
                    assert_false(member.name in this.get_interface_object().prototype,
                                "The prototype object must not have a property " +
                                format_value(member.name));
                }.bind(this), this.name + " interface: member " + member.name);
            }
            continue;
        }

        switch (member.type) {
        case "const":
            this.test_member_const(member);
            break;

        case "attribute":
            // For unforgeable attributes, we do the checks in
            // test_interface_of instead.
            if (!member.isUnforgeable)
            {
                this.test_member_attribute(member);
            }
            if (member.special === "stringifier") {
                this.test_member_stringifier(member);
            }
            break;

        case "operation":
            // TODO: Need to correctly handle multiple operations with the same
            // identifier.
            // For unforgeable operations, we do the checks in
            // test_interface_of instead.
            if (member.name) {
                if (!member.isUnforgeable)
                {
                    this.test_member_operation(member);
                }
            } else if (member.special === "stringifier") {
                this.test_member_stringifier(member);
            }
            break;

        case "iterable":
            if (member.async) {
                this.test_member_async_iterable(member);
            } else {
                this.test_member_iterable(member);
            }
            break;
        case "maplike":
            this.test_member_maplike(member);
            break;
        case "setlike":
            this.test_member_setlike(member);
            break;
        default:
            // TODO: check more member types.
            break;
        }
    }
};

IdlInterface.prototype.test_object = function(desc)
{
    var obj, exception = null;
    try
    {
        obj = eval(desc);
    }
    catch(e)
    {
        exception = e;
    }

    var expected_typeof;
    if (this.name == "HTMLAllCollection")
    {
        // Result of [[IsHTMLDDA]] slot
        expected_typeof = "undefined";
    }
    else
    {
        expected_typeof = "object";
    }

    if (this.is_callback()) {
        assert_equals(exception, null, "Unexpected exception when evaluating object");
        assert_equals(typeof obj, expected_typeof, "wrong typeof object");
    } else {
        this.test_primary_interface_of(desc, obj, exception, expected_typeof);

        var current_interface = this;
        while (current_interface)
        {
            if (!(current_interface.name in this.array.members))
            {
                throw new IdlHarnessError("Interface " + current_interface.name + " not found (inherited by " + this.name + ")");
            }
            if (current_interface.prevent_multiple_testing && current_interface.already_tested)
            {
                return;
            }
            current_interface.test_interface_of(desc, obj, exception, expected_typeof);
            current_interface = this.array.members[current_interface.base];
        }
    }
};

IdlInterface.prototype.test_primary_interface_of = function(desc, obj, exception, expected_typeof)
{
    // Only the object itself, not its members, are tested here, so if the
    // interface is untested, there is nothing to do.
    if (this.untested)
    {
        return;
    }

    // "The internal [[SetPrototypeOf]] method of every platform object that
    // implements an interface with the [Global] extended
    // attribute must execute the same algorithm as is defined for the
    // [[SetPrototypeOf]] internal method of an immutable prototype exotic
    // object."
    // https://webidl.spec.whatwg.org/#platform-object-setprototypeof
    if (this.is_global())
    {
        this.test_immutable_prototype("global platform object", obj);
    }


    // We can't easily test that its prototype is correct if there's no
    // interface object, or the object is from a different global environment
    // (not instanceof Object).  TODO: test in this case that its prototype at
    // least looks correct, even if we can't test that it's actually correct.
    if (this.should_have_interface_object()
    && (typeof obj != expected_typeof || obj instanceof Object))
    {
        subsetTestByKey(this.name, test, function()
        {
            assert_equals(exception, null, "Unexpected exception when evaluating object");
            assert_equals(typeof obj, expected_typeof, "wrong typeof object");
            this.assert_interface_object_exists();
            assert_own_property(this.get_interface_object(), "prototype",
                                'interface "' + this.name + '" does not have own property "prototype"');

            // "The value of the internal [[Prototype]] property of the
            // platform object is the interface prototype object of the primary
            // interface from the platform object’s associated global
            // environment."
            assert_equals(Object.getPrototypeOf(obj),
                          this.get_interface_object().prototype,
                          desc + "'s prototype is not " + this.name + ".prototype");
        }.bind(this), this.name + " must be primary interface of " + desc);
    }

    // "The class string of a platform object that implements one or more
    // interfaces must be the qualified name of the primary interface of the
    // platform object."
    subsetTestByKey(this.name, test, function()
    {
        assert_equals(exception, null, "Unexpected exception when evaluating object");
        assert_equals(typeof obj, expected_typeof, "wrong typeof object");
        assert_class_string(obj, this.get_qualified_name(), "class string of " + desc);
        if (!this.has_stringifier())
        {
            assert_equals(String(obj), "[object " + this.get_qualified_name() + "]", "String(" + desc + ")");
        }
    }.bind(this), "Stringification of " + desc);
};

IdlInterface.prototype.test_interface_of = function(desc, obj, exception, expected_typeof)
{
    // TODO: Indexed and named properties, more checks on interface members
    this.already_tested = true;
    if (!shouldRunSubTest(this.name)) {
        return;
    }

    var unexposed_properties = new Set();
    for (var i = 0; i < this.members.length; i++)
    {
        var member = this.members[i];
        if (member.untested) {
            continue;
        }
        if (!exposed_in(exposure_set(member, this.exposureSet)))
        {
            if (!unexposed_properties.has(member.name))
            {
                unexposed_properties.add(member.name);
                subsetTestByKey(this.name, test, function() {
                    assert_equals(exception, null, "Unexpected exception when evaluating object");
                    assert_false(member.name in obj);
                }.bind(this), this.name + " interface: " + desc + ' must not have property "' + member.name + '"');
            }
            continue;
        }
        if (member.type == "attribute" && member.isUnforgeable)
        {
            var a_test = subsetTestByKey(this.name, async_test, this.name + " interface: " + desc + ' must have own property "' + member.name + '"');
            a_test.step(function() {
                assert_equals(exception, null, "Unexpected exception when evaluating object");
                assert_equals(typeof obj, expected_typeof, "wrong typeof object");
                // Call do_interface_attribute_asserts last, since it will call a_test.done()
                this.do_interface_attribute_asserts(obj, member, a_test);
            }.bind(this));
        }
        else if (member.type == "operation" &&
                 member.name &&
                 member.isUnforgeable)
        {
            var a_test = subsetTestByKey(this.name, async_test, this.name + " interface: " + desc + ' must have own property "' + member.name + '"');
            a_test.step(function()
            {
                assert_equals(exception, null, "Unexpected exception when evaluating object");
                assert_equals(typeof obj, expected_typeof, "wrong typeof object");
                assert_own_property(obj, member.name,
                                    "Doesn't have the unforgeable operation property");
                this.do_member_operation_asserts(obj, member, a_test);
            }.bind(this));
        }
        else if ((member.type == "const"
        || member.type == "attribute"
        || member.type == "operation")
        && member.name)
        {
            subsetTestByKey(this.name, test, function()
            {
                assert_equals(exception, null, "Unexpected exception when evaluating object");
                assert_equals(typeof obj, expected_typeof, "wrong typeof object");
                if (member.special !== "static") {
                    if (!this.is_global()) {
                        assert_inherits(obj, member.name);
                    } else {
                        assert_own_property(obj, member.name);
                    }

                    if (member.type == "const")
                    {
                        assert_equals(obj[member.name], constValue(member.value));
                    }
                    if (member.type == "attribute")
                    {
                        // Attributes are accessor properties, so they might
                        // legitimately throw an exception rather than returning
                        // anything.
                        var property, thrown = false;
                        try
                        {
                            property = obj[member.name];
                        }
                        catch (e)
                        {
                            thrown = true;
                        }
                        if (!thrown)
                        {
                            if (this.name == "Document" && member.name == "all")
                            {
                                // Result of [[IsHTMLDDA]] slot
                                assert_equals(typeof property, "undefined");
                            }
                            else
                            {
                                this.array.assert_type_is(property, member.idlType);
                            }
                        }
                    }
                    if (member.type == "operation")
                    {
                        assert_equals(typeof obj[member.name], "function");
                    }
                }
            }.bind(this), this.name + " interface: " + desc + ' must inherit property "' + member + '" with the proper type');
        }
        // TODO: This is wrong if there are multiple operations with the same
        // identifier.
        // TODO: Test passing arguments of the wrong type.
        if (member.type == "operation" && member.name && member.arguments.length)
        {
            var description =
                this.name + " interface: calling " + member + " on " + desc +
                " with too few arguments must throw TypeError";
            var a_test = subsetTestByKey(this.name, async_test, description);
            a_test.step(function()
            {
                assert_equals(exception, null, "Unexpected exception when evaluating object");
                assert_equals(typeof obj, expected_typeof, "wrong typeof object");
                var fn;
                if (member.special !== "static") {
                    if (!this.is_global() && !member.isUnforgeable) {
                        assert_inherits(obj, member.name);
                    } else {
                        assert_own_property(obj, member.name);
                    }
                    fn = obj[member.name];
                }
                else
                {
                    assert_own_property(obj.constructor, member.name, "interface object must have static operation as own property");
                    fn = obj.constructor[member.name];
                }

                var minLength = minOverloadLength(this.members.filter(function(m) {
                    return m.type == "operation" && m.name == member.name;
                }));
                var args = [];
                var cb = awaitNCallbacks(minLength, a_test.done.bind(a_test));
                for (var i = 0; i < minLength; i++) {
                    throwOrReject(a_test, member, fn, obj, args, "Called with " + i + " arguments", cb);

                    args.push(create_suitable_object(member.arguments[i].idlType));
                }
                if (minLength === 0) {
                    cb();
                }
            }.bind(this));
        }

        if (member.is_to_json_regular_operation()) {
            this.test_to_json_operation(desc, obj, member);
        }
    }
};

IdlInterface.prototype.has_stringifier = function()
{
    if (this.name === "DOMException") {
        // toString is inherited from Error, so don't assume we have the
        // default stringifer
        return true;
    }
    if (this.members.some(function(member) { return member.special === "stringifier"; })) {
        return true;
    }
    if (this.base &&
        this.array.members[this.base].has_stringifier()) {
        return true;
    }
    return false;
};

IdlInterface.prototype.do_interface_attribute_asserts = function(obj, member, a_test)
{
    // This function tests WebIDL as of 2015-01-27.
    // TODO: Consider [Exposed].

    // This is called by test_member_attribute() with the prototype as obj if
    // it is not a global, and the global otherwise, and by test_interface_of()
    // with the object as obj.

    var pendingPromises = [];

    // "The name of the property is the identifier of the attribute."
    assert_own_property(obj, member.name);

    // "The property has attributes { [[Get]]: G, [[Set]]: S, [[Enumerable]]:
    // true, [[Configurable]]: configurable }, where:
    // "configurable is false if the attribute was declared with the
    // [LegacyUnforgeable] extended attribute and true otherwise;
    // "G is the attribute getter, defined below; and
    // "S is the attribute setter, also defined below."
    var desc = Object.getOwnPropertyDescriptor(obj, member.name);
    assert_false("value" in desc, 'property descriptor should not have a "value" field');
    assert_false("writable" in desc, 'property descriptor should not have a "writable" field');
    assert_true(desc.enumerable, "property should be enumerable");
    if (member.isUnforgeable)
    {
        assert_false(desc.configurable, "[LegacyUnforgeable] property must not be configurable");
    }
    else
    {
        assert_true(desc.configurable, "property must be configurable");
    }


    // "The attribute getter is a Function object whose behavior when invoked
    // is as follows:"
    assert_equals(typeof desc.get, "function", "getter must be Function");

    // "If the attribute is a regular attribute, then:"
    if (member.special !== "static") {
        // "If O is not a platform object that implements I, then:
        // "If the attribute was specified with the [LegacyLenientThis] extended
        // attribute, then return undefined.
        // "Otherwise, throw a TypeError."
        if (!member.has_extended_attribute("LegacyLenientThis")) {
            if (member.idlType.generic !== "Promise") {
                assert_throws_js(globalOf(desc.get).TypeError, function() {
                    desc.get.call({});
                }.bind(this), "calling getter on wrong object type must throw TypeError");
            } else {
                pendingPromises.push(
                    promise_rejects_js(a_test, TypeError, desc.get.call({}),
                                    "calling getter on wrong object type must reject the return promise with TypeError"));
            }
        } else {
            assert_equals(desc.get.call({}), undefined,
                          "calling getter on wrong object type must return undefined");
        }
    }

    // "The value of the Function object’s “length” property is the Number
    // value 0."
    assert_equals(desc.get.length, 0, "getter length must be 0");

    // "Let name be the string "get " prepended to attribute’s identifier."
    // "Perform ! SetFunctionName(F, name)."
    assert_equals(desc.get.name, "get " + member.name,
        "getter must have the name 'get " + member.name + "'");


    // TODO: Test calling setter on the interface prototype (should throw
    // TypeError in most cases).
    if (member.readonly
    && !member.has_extended_attribute("LegacyLenientSetter")
    && !member.has_extended_attribute("PutForwards")
    && !member.has_extended_attribute("Replaceable"))
    {
        // "The attribute setter is undefined if the attribute is declared
        // readonly and has neither a [PutForwards] nor a [Replaceable]
        // extended attribute declared on it."
        assert_equals(desc.set, undefined, "setter must be undefined for readonly attributes");
    }
    else
    {
        // "Otherwise, it is a Function object whose behavior when
        // invoked is as follows:"
        assert_equals(typeof desc.set, "function", "setter must be function for PutForwards, Replaceable, or non-readonly attributes");

        // "If the attribute is a regular attribute, then:"
        if (member.special !== "static") {
            // "If /validThis/ is false and the attribute was not specified
            // with the [LegacyLenientThis] extended attribute, then throw a
            // TypeError."
            // "If the attribute is declared with a [Replaceable] extended
            // attribute, then: ..."
            // "If validThis is false, then return."
            if (!member.has_extended_attribute("LegacyLenientThis")) {
                assert_throws_js(globalOf(desc.set).TypeError, function() {
                    desc.set.call({});
                }.bind(this), "calling setter on wrong object type must throw TypeError");
            } else {
                assert_equals(desc.set.call({}), undefined,
                              "calling setter on wrong object type must return undefined");
            }
        }

        // "The value of the Function object’s “length” property is the Number
        // value 1."
        assert_equals(desc.set.length, 1, "setter length must be 1");

        // "Let name be the string "set " prepended to id."
        // "Perform ! SetFunctionName(F, name)."
        assert_equals(desc.set.name, "set " + member.name,
            "The attribute setter must have the name 'set " + member.name + "'");
    }

    Promise.all(pendingPromises).then(a_test.done.bind(a_test));
}

/// IdlInterfaceMember ///
function IdlInterfaceMember(obj)
{
    /**
     * obj is an object produced by the WebIDLParser.js "ifMember" production.
     * We just forward all properties to this object without modification,
     * except for special extAttrs handling.
     */
    for (var k in obj.toJSON())
    {
        this[k] = obj[k];
    }
    if (!("extAttrs" in this))
    {
        this.extAttrs = [];
    }

    this.isUnforgeable = this.has_extended_attribute("LegacyUnforgeable");
    this.isUnscopable = this.has_extended_attribute("Unscopable");
}

IdlInterfaceMember.prototype = Object.create(IdlObject.prototype);

IdlInterfaceMember.prototype.toJSON = function() {
    return this;
};

IdlInterfaceMember.prototype.is_to_json_regular_operation = function() {
    return this.type == "operation" && this.special !== "static" && this.name == "toJSON";
};

IdlInterfaceMember.prototype.toString = function() {
    function formatType(type) {
        var result;
        if (type.generic) {
            result = type.generic + "<" + type.idlType.map(formatType).join(", ") + ">";
        } else if (type.union) {
            result = "(" + type.subtype.map(formatType).join(" or ") + ")";
        } else {
            result = type.idlType;
        }
        if (type.nullable) {
            result += "?"
        }
        return result;
    }

    if (this.type === "operation") {
        var args = this.arguments.map(function(m) {
            return [
                m.optional ? "optional " : "",
                formatType(m.idlType),
                m.variadic ? "..." : "",
            ].join("");
        }).join(", ");
        return this.name + "(" + args + ")";
    }

    return this.name;
}

/// Internal helper functions ///
function create_suitable_object(type)
{
    /**
     * type is an object produced by the WebIDLParser.js "type" production.  We
     * return a JavaScript value that matches the type, if we can figure out
     * how.
     */
    if (type.nullable)
    {
        return null;
    }
    switch (type.idlType)
    {
        case "any":
        case "boolean":
            return true;

        case "byte": case "octet": case "short": case "unsigned short":
        case "long": case "unsigned long": case "long long":
        case "unsigned long long": case "float": case "double":
        case "unrestricted float": case "unrestricted double":
            return 7;

        case "DOMString":
        case "ByteString":
        case "USVString":
            return "foo";

        case "object":
            return {a: "b"};

        case "Node":
            return document.createTextNode("abc");
    }
    return null;
}

/// IdlEnum ///
// Used for IdlArray.prototype.assert_type_is
function IdlEnum(obj)
{
    /**
     * obj is an object produced by the WebIDLParser.js "dictionary"
     * production.
     */

    /** Self-explanatory. */
    this.name = obj.name;

    /** An array of values produced by the "enum" production. */
    this.values = obj.values;

}

IdlEnum.prototype = Object.create(IdlObject.prototype);

/// IdlCallback ///
// Used for IdlArray.prototype.assert_type_is
function IdlCallback(obj)
{
    /**
     * obj is an object produced by the WebIDLParser.js "callback"
     * production.
     */

    /** Self-explanatory. */
    this.name = obj.name;

    /** Arguments for the callback. */
    this.arguments = obj.arguments;
}

IdlCallback.prototype = Object.create(IdlObject.prototype);

/// IdlTypedef ///
// Used for IdlArray.prototype.assert_type_is
function IdlTypedef(obj)
{
    /**
     * obj is an object produced by the WebIDLParser.js "typedef"
     * production.
     */

    /** Self-explanatory. */
    this.name = obj.name;

    /** The idlType that we are supposed to be typedeffing to. */
    this.idlType = obj.idlType;

}

IdlTypedef.prototype = Object.create(IdlObject.prototype);

/// IdlNamespace ///
function IdlNamespace(obj)
{
    this.name = obj.name;
    this.extAttrs = obj.extAttrs;
    this.untested = obj.untested;
    /** A back-reference to our IdlArray. */
    this.array = obj.array;

    /** An array of IdlInterfaceMembers. */
    this.members = obj.members.map(m => new IdlInterfaceMember(m));
}

IdlNamespace.prototype = Object.create(IdlObject.prototype);

IdlNamespace.prototype.do_member_operation_asserts = function (memberHolderObject, member, a_test)
{
    var desc = Object.getOwnPropertyDescriptor(memberHolderObject, member.name);

    assert_false("get" in desc, "property should not have a getter");
    assert_false("set" in desc, "property should not have a setter");
    assert_equals(
        desc.writable,
        !member.isUnforgeable,
        "property should be writable if and only if not unforgeable");
    assert_true(desc.enumerable, "property should be enumerable");
    assert_equals(
        desc.configurable,
        !member.isUnforgeable,
        "property should be configurable if and only if not unforgeable");

    assert_equals(
        typeof memberHolderObject[member.name],
        "function",
         "property must be a function");

    assert_equals(
        memberHolderObject[member.name].length,
        minOverloadLength(this.members.filter(function(m) {
            return m.type == "operation" && m.name == member.name;
        })),
        "operation has wrong .length");
    a_test.done();
}

IdlNamespace.prototype.test_member_operation = function(member)
{
    if (!shouldRunSubTest(this.name)) {
        return;
    }
    var a_test = subsetTestByKey(
        this.name,
        async_test,
        this.name + ' namespace: operation ' + member);
    a_test.step(function() {
        assert_own_property(
            self[this.name],
            member.name,
            'namespace object missing operation ' + format_value(member.name));

        this.do_member_operation_asserts(self[this.name], member, a_test);
    }.bind(this));
};

IdlNamespace.prototype.test_member_attribute = function (member)
{
    if (!shouldRunSubTest(this.name)) {
        return;
    }
    var a_test = subsetTestByKey(
        this.name,
        async_test,
        this.name + ' namespace: attribute ' + member.name);
    a_test.step(function()
    {
        assert_own_property(
            self[this.name],
            member.name,
            this.name + ' does not have property ' + format_value(member.name));

        var desc = Object.getOwnPropertyDescriptor(self[this.name], member.name);
        assert_equals(desc.set, undefined, "setter must be undefined for namespace members");
        a_test.done();
    }.bind(this));
};

IdlNamespace.prototype.test_self = function ()
{
    /**
     * TODO(lukebjerring): Assert:
     * - "Note that unlike interfaces or dictionaries, namespaces do not create types."
     */

    subsetTestByKey(this.name, test, () => {
        assert_true(this.extAttrs.every(o => o.name === "Exposed" || o.name === "SecureContext"),
            "Only the [Exposed] and [SecureContext] extended attributes are applicable to namespaces");
        assert_true(this.has_extended_attribute("Exposed"),
            "Namespaces must be annotated with the [Exposed] extended attribute");
    }, `${this.name} namespace: extended attributes`);

    const namespaceObject = self[this.name];

    subsetTestByKey(this.name, test, () => {
        const desc = Object.getOwnPropertyDescriptor(self, this.name);
        assert_equals(desc.value, namespaceObject, `wrong value for ${this.name} namespace object`);
        assert_true(desc.writable, "namespace object should be writable");
        assert_false(desc.enumerable, "namespace object should not be enumerable");
        assert_true(desc.configurable, "namespace object should be configurable");
        assert_false("get" in desc, "namespace object should not have a getter");
        assert_false("set" in desc, "namespace object should not have a setter");
    }, `${this.name} namespace: property descriptor`);

    subsetTestByKey(this.name, test, () => {
        assert_true(Object.isExtensible(namespaceObject));
    }, `${this.name} namespace: [[Extensible]] is true`);

    subsetTestByKey(this.name, test, () => {
        assert_true(namespaceObject instanceof Object);

        if (this.name === "console") {
            // https://console.spec.whatwg.org/#console-namespace
            const namespacePrototype = Object.getPrototypeOf(namespaceObject);
            assert_equals(Reflect.ownKeys(namespacePrototype).length, 0);
            assert_equals(Object.getPrototypeOf(namespacePrototype), Object.prototype);
        } else {
            assert_equals(Object.getPrototypeOf(namespaceObject), Object.prototype);
        }
    }, `${this.name} namespace: [[Prototype]] is Object.prototype`);

    subsetTestByKey(this.name, test, () => {
        assert_equals(typeof namespaceObject, "object");
    }, `${this.name} namespace: typeof is "object"`);

    subsetTestByKey(this.name, test, () => {
        assert_equals(
            Object.getOwnPropertyDescriptor(namespaceObject, "length"),
            undefined,
            "length property must be undefined"
        );
    }, `${this.name} namespace: has no length property`);

    subsetTestByKey(this.name, test, () => {
        assert_equals(
            Object.getOwnPropertyDescriptor(namespaceObject, "name"),
            undefined,
            "name property must be undefined"
        );
    }, `${this.name} namespace: has no name property`);
};

IdlNamespace.prototype.test = function ()
{
    if (!this.untested) {
        this.test_self();
    }

    for (const v of Object.values(this.members)) {
        switch (v.type) {

        case 'operation':
            this.test_member_operation(v);
            break;

        case 'attribute':
            this.test_member_attribute(v);
            break;

        default:
            throw 'Invalid namespace member ' + v.name + ': ' + v.type + ' not supported';
        }
    };
};

}());

/**
 * idl_test is a promise_test wrapper that handles the fetching of the IDL,
 * avoiding repetitive boilerplate.
 *
 * @param {String[]} srcs Spec name(s) for source idl files (fetched from
 *      /interfaces/{name}.idl).
 * @param {String[]} deps Spec name(s) for dependency idl files (fetched
 *      from /interfaces/{name}.idl). Order is important - dependencies from
 *      each source will only be included if they're already know to be a
 *      dependency (i.e. have already been seen).
 * @param {Function} setup_func Function for extra setup of the idl_array, such
 *      as adding objects. Do not call idl_array.test() in the setup; it is
 *      called by this function (idl_test).
 */
function idl_test(srcs, deps, idl_setup_func) {
    return promise_test(function (t) {
        var idl_array = new IdlArray();
        var setup_error = null;
        const validationIgnored = [
            "constructor-member",
            "dict-arg-default",
            "require-exposed"
        ];
        return Promise.all(
            srcs.concat(deps).map(fetch_spec))
            .then(function(results) {
                const astArray = results.map(result =>
                    WebIDL2.parse(result.idl, { sourceName: result.spec })
                );
                test(() => {
                    const validations = WebIDL2.validate(astArray)
                        .filter(v => !validationIgnored.includes(v.ruleName));
                    if (validations.length) {
                        const message = validations.map(v => v.message).join("\n\n");
                        throw new Error(message);
                    }
                }, "idl_test validation");
                for (var i = 0; i < srcs.length; i++) {
                    idl_array.internal_add_idls(astArray[i]);
                }
                for (var i = srcs.length; i < srcs.length + deps.length; i++) {
                    idl_array.internal_add_dependency_idls(astArray[i]);
                }
            })
            .then(function() {
                if (idl_setup_func) {
                    return idl_setup_func(idl_array, t);
                }
            })
            .catch(function(e) { setup_error = e || 'IDL setup failed.'; })
            .then(function () {
                var error = setup_error;
                try {
                    idl_array.test(); // Test what we can.
                } catch (e) {
                    // If testing fails hard here, the original setup error
                    // is more likely to be the real cause.
                    error = error || e;
                }
                if (error) {
                    throw error;
                }
            });
    }, 'idl_test setup');
}

/**
 * fetch_spec is a shorthand for a Promise that fetches the spec's content.
 */
function fetch_spec(spec) {
    var url = '/interfaces/' + spec + '.idl';
    return fetch(url).then(function (r) {
        if (!r.ok) {
            throw new IdlHarnessError("Error fetching " + url + ".");
        }
        return r.text();
    }).then(idl => ({ spec, idl }));
}
// vim: set expandtab shiftwidth=4 tabstop=4 foldmarker=@{,@} foldmethod=marker:
