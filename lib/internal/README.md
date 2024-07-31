# Internal Modules

The modules in `lib/internal` are intended for internal use in Node.js core
only, and are not accessible with `require()` from user modules. These modules
can be changed at **any** time. Reliance on these modules outside of core
is **not supported** in any way.
