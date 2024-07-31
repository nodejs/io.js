// Flags:  --enable-source-maps
import '../../../common/index.mjs';
async function Throw() {
    await 0;
    throw new Error('message');
}
(async function main() {
    await Promise.all([0, 1, 2, Throw()]);
})();
// To recreate:
//
// npx --package typescript tsc --module nodenext --target esnext --outDir test/fixtures/source-map/output --sourceMap test/fixtures/source-map/output/source_map_throw_async_stack_trace.mts
//# sourceMappingURL=source_map_throw_async_stack_trace.mjs.map