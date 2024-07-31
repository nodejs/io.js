// c.mjs
import { createRequire } from "module";
console.log("Start c");
createRequire(import.meta.url)("./d.mjs");
throw new Error("Error from c");
