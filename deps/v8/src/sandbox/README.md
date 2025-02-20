# V8 Sandbox - Readme

A low-overhead, in-process sandbox for V8.

The sandbox limits the impact of typical V8 vulnerabilities by restricting the
code executed by V8 to a subset of the process' virtual address space ("the
sandbox"), thereby isolating it from the rest of the process. This works purely
in software (with options for hardware support, see the respective design
document linked below) by effectively converting raw pointers either into
offsets from the base of the sandbox or into indices into out-of-sandbox
pointer tables. In principle, these mechanisms are very similar to the
userland/kernel separation used by modern operating systems (e.g. the unix file
descriptor table).

The sandbox assumes that an attacker can arbitrarily and concurrently modify
any memory inside the sandbox address space as this primitive can be
constructed from typical V8 vulnerabilities. Further, it is assumed that an
attacker will be able to read memory outside of the sandbox, for example
through hardware side channels. The sandbox then aims to protect the rest of
the process from such an attacker. As such, any corruption of memory outside of
the sandbox address space is considered a sandbox violation.

## Usage

To enable the sandbox, simply use `v8_enable_sandbox = true` in the gn args.
The sandbox is only available in 64-bit configurations as it requires large
amounts of virtual address space. The sandbox is enabled by default on
supported configurations.

## Testing

The sandbox is designed to be testable, both manually and automatically.

To use a "sandbox testing" configurations, two steps are required:

1. V8 needs to be build with `v8_enable_memory_corruption_api = true`. This
   will, when sandbox testing mode is enabled, expose a JavaScript `Sandbox`
   object through which memory inside the sandbox can be arbitrarily modified.
   This API effectively emulates common exploit primitives that can be
   constructed from a typical V8 vulnerability.
2. The sandbox testing mode needs to be enabled at runtime via either
   `--sandbox-testing` or `--sandbox-fuzzing`. This will expose the memory
   corruption API to JavaScript code and enable the sandbox crash filter, which
   will filter out uninteresting (for the sandbox) crashes such as access
   violations inside the sandbox or other unexploitable crashes.
   Note that the sandbox crash filter is currently only available on Linux.

The following example demonstrates these two parts:

```JavaScript
// Create a DataView that can read and write inside the sandbox.
let memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

// Create an object to corrupt and obtain its address inside the sandbox.
let corruptMe = {};
let addr = Sandbox.getAddressOf(corruptMe);

// Corrupt the object.
memory.setUint32(addr, 0x41414141, true);
memory.setUint32(addr + 4, 0x41414141, true);
memory.setUint32(addr + 8, 0x41414141, true);

// Trigger a crash. The sandbox crash filter will catch it and determine that
// this is _not_ a sandbox violation as it crashes inside the sandbox.
corruptMe.crash();

// The process will now terminate cleanly with a message such as:
// "Caught harmless memory access violation (inside sandbox address space). Exiting process..."
```

The --sandbox-testing and --sandbox-fuzzing flags are mostly identical.
However, with --sandbox-testing, the process will terminate with exit status
zero when detecting a harmless crash. As such, tests will be treated as passing
if they crash safely such as for example through a CHECK failure. With
--sandbox-fuzzing on the other hand, the process instead exits with a non-zero
status so that fuzzers can determine that the execution terminated prematurely.
Furthermore, the --sandbox-fuzzing requires the memory corruption API to be
compiled in while --sandbox-testing can be enabled on any (sandbox-enabled)
build. As such, --sandbox-fuzzing should generally be used for fuzzers while
--sandbox-testing should be used for most other purposes.

Note that both --sandbox-testing and --sandbox-fuzzing will also report _reads_
outside the sandbox as "sandbox violations". This is not technically true as
the sandbox only attempts to prevent _writes_. However it is both difficult and
undesirable to filter out reads, for example because in some cases, the
underlying issue may also allow an attacker to perform a write instead.

## Resources

The following list contains further resources about the sandbox, in particular
various design documents related to it.

* [Sandbox Blog Post](https://v8.dev/blog/sandbox):
  Discusses the motivation behind the sandbox and its goals while also briefly
  covering the high-level design, performance characteristics, and testability
  aspects of the sandbox.
* [High-Level Design Document](https://docs.google.com/document/d/1FM4fQmIhEqPG8uGp5o9A-mnPB5BOeScZYpkHjo0KKA8/edit?usp=sharing):
  Discusses the attacker model, goal, and basic design of the sandbox, while
  linking to the more specific design documents below where appropriate.
* [Sandbox Address Space](https://docs.google.com/document/d/1PM4Zqmlt8ac5O8UNQfY7fOsem-6MhbsB-vjFI-9XK6w/edit?usp=sharing):
  Contains additional details about the address space reservation backing the
  sandbox and how it is allocated.
* [Sandboxed Pointers](https://docs.google.com/document/d/1HSap8-J3HcrZvT7-5NsbYWcjfc0BVoops5TDHZNsnko/edit?usp=sharing):
  Discusses the design of sandboxed pointers, which are pointers that are
  always guaranteed to point into the sandbox and are for example used to
  reference ArrayBuffer backing stores.
* [External Pointer Table](https://docs.google.com/document/d/1V3sxltuFjjhp_6grGHgfqZNK57qfzGzme0QTk0IXDHk/edit?usp=sharing):
  The external pointer table is used to securely reference non-V8 ("external")
  objects, for example objects owned by the Embedder, from inside the sandbox.
  This document discusses its design in more detail.
* [Code Pointer Sandboxing](https://docs.google.com/document/d/1CPs5PutbnmI-c5g7e_Td9CNGh5BvpLleKCqUnqmD82k/edit?usp=sharing):
  Similar to external objects, pointers to executable machine code also needs
  to be protected. This is done with the help of a dedicated code pointer
  table, which is discussed in this document.
* [Trusted Space](https://docs.google.com/document/d/1IrvzL4uX_Zv0k2Iakdp_q_z33bj-qlYF5IesGpXW0fM/edit?usp=sharing):
  Certain V8 objects are considered trusted and must not be corrupted by an
  attacker. Examples of such objects include bytecode containers and metadata
  for JIT-generated code. These objects are protected by allocating them
  outside of the sandbox in the trusted heap space, then referencing them
  through another pointer table indirection to ensure memory safe access. This
  document discusses both of these mechanisms.
* [Hardware Support](https://docs.google.com/document/d/12MsaG6BYRB-jQWNkZiuM3bY8X2B2cAsCMLLdgErvK4c/edit?usp=sharing):
  Instead of the purely software-based sandbox described by the preceeding
  documents, the sandbox could also be implemented (or augmented) with special
  hardware support. This document discusses various options for how that may
  work in practice, what the implications would be, and which requirements the
  hardware would have to fulfill.
