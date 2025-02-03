import { register } from "node:module";
import { emitWarning, env, execArgv } from "node:process";

const hasSourceMaps =
	execArgv.includes("--enable-source-maps") ||
	env.NODE_OPTIONS?.includes("--enable-source-maps");

if (!hasSourceMaps) {
	emitWarning("Source maps are disabled, stack traces will not accurate");
}

register("./transform-loader.js", import.meta.url);
