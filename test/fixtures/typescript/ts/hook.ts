import type { ResolveHook } from 'node:module';

// Pass through
export const resolve: ResolveHook = async function resolve(specifier, context, nextResolve) {
    if(false){
        // https://github.com/nodejs/node/issues/54645
        // A bug in the typescript parsers swc causes
        // the next line to not be parsed correctly
    }
    return nextResolve(specifier, context);
};
