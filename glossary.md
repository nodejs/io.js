# Glossary

This file documents various terms and definitions used throughout the Node.js community.

* **ABI**: [Application Binary Interface][] - Defines the interface between two binary program modules.
* **AFAICT**: As Far As I Can Tell.
* **AFAIK**: As Far As I Know.
* **API**: [Application Programming Interface][] - A set of rules and protocols that allows different software
  applications to communicate with each other. APIs are used to enable integration between different systems.
* **ASAP**: As Soon As Possible.
* **BE**: Big [Endian][] - A Byte Order where the largest bit comes first. The opposite of **LE**.
* **Bootstrap**: Early phase in the Node.js process startup - sets up the execution environment and loads internal
  modules.
* **CI**: [Continuous Integration][] - Development practice where code changes are frequently merged into a shared
  repository.
* **CITGM**: Canary In The Gold Mine - A smoke test that tests the code change with popular npm packages.
* **CJS**: [CommonJS][] - Standard for JavaScript modules, and in most cases, [CommonJS Modules][].
* **CLDR**: [Common Locale Data Repository][] - A repository of locale data used in software engineering.
* **CLI**: [Command Line Interface][] - A way to interact with a computer program using text commands.
* **Code cache**: Chunk of bytes storing compiled JS code and its metadata.
* **CVE**: [Common Vulnerabilities and Exposures][] - Database maintaining reported security vulnerabilities.
* **Deps**: Dependencies - Upstream projects that this project depends on.
* **DOM**: [Document Object Model][] - A programming interface for web documents. It represents the structure of a
  document as a tree of objects, allowing programmers to dynamically manipulate the content and structure of a web page.
* **ECMA**: [Ecma International][] - A nonprofit standards organization that develops and publishes international
  standards, including **ECMA-262**.
* **ECMA-262**: **Ecma**'s [specification document for **ECMAScript**][], maintained and updated by the **TC39**.
* **ECMAScript**: A standard for scripting languages, including **JavaScript**.
* **EOF**: [End-of-File][] - Indicates the end of a file or stream.
* **EOL**: [End-of-Life][] (when used within project documents), [End-of-Line][] (when used within a program),
  End-of-Life is usually how this term is used.
* **ESM**: [ECMAScript Module][] - The implementation of the **ECMA-262** module system.
* **ETW**: [Event Tracing for Windows][] - Provides a way to trace events in Windows systems.
* **FFDC**: First Failure Data Capture - Logs, traces, and dumps produced by default on program error.
* **FIPS**: [Federal Information Processing Standards][] - Set of standards for use in computer systems by non-military
  government agencies and government contractors.
* **FS**: File System.
* **Godbolt**: [Compiler Explorer][] - Tool for running compilers interactively from a web browser.
* **HTTP**: [HyperText Transfer Protocol][] - An application protocol for distributed, collaborative, hypermedia
  information systems. It is the foundation of data communication on the World Wide Web.
* **ICU**: [International Components for Unicode][] - Library providing support for Unicode.
* **IDE**: [Integrated Development Environment][] - A software application that provides comprehensive facilities to
  computer programmers for software development.
* **IETF**: [Internet Engineering Task Force][] - An international community responsible for developing and promoting
  Internet standards.
* **IIRC**: If I Recall Correctly.
* **IIUC**: If I Understand Correctly.
* **IMHO**: In My Humble/Honest Opinion.
* **IMO**: In My Opinion.
* **IPC**: [Inter-Process Communication][] - Mechanism allowing processes to communicate with each other.
* **JIT**: [Just In Time][] - Method of executing computer code during runtime.
* **JS**: [JavaScript][] - A high-level, interpreted programming language that conforms to the **ECMAScript**
  specification.
* **JS/C++ boundary**: Boundary between V8's runtime and JS code execution, often crossed when calling JS functions
  with C++ linkage.
* **JSON**: [JavaScript Object Notation][] - A lightweight data-interchange format that is easy for humans to read and
  write and for machines to parse and generate. It is commonly used for transmitting data between a server and a
  web application.
* **LE**: Little [Endian][] - A Byte Order where the smallest bit comes first. The opposite of **BE**.
* **LGTM/SGTM**: Looks/Sounds good to me.
* **LTS**: [Long Term Support][] - Support provided for a software version for an extended period.
* **MDN**: [Mozilla Development Network][] - Resource for web developers.
* **MVC**: [Model-View-Controller][] - A software design pattern commonly used for developing user interfaces. It
  separates the application into three interconnected components: the model (data), the view (presentation), and the
  controller (logic).
* **Native modules/addons**: Modules compiled to native code from a non-JavaScript language,
  such as C or C++, that expose interfaces callable from JavaScript.
* **npm**: [npm][] - A package manager and registry widely used for managing dependencies in
  Node.js projects and for sharing code with others.
* **OOB**: Out Of Bounds - Used in the context of array access.
* **OOM**: Out Of Memory - Situation where a computer program exceeds its memory allocation.
* **OOP**: [Object-Oriented Programming][] - A programming paradigm based on the concept of "objects," which can
  contain data and code to manipulate that data. OOP languages include features such as encapsulation, inheritance,
  and polymorphism.
* **PPC**: [PowerPC][] - A type of microprocessor architecture.
* **PTAL**: Please Take A Look.
* **Primordials**: Pristine built-ins in JavaScript that are not affected by prototype pollution.
* **Prototype Pollution**: Process in which a user mutating object prototypes affects other code.
* **RAII**: [Resource Acquisition Is Initialization][] - Programming idiom used to manage resources in C++.
* **REPL**: [Read Evaluate Print Loop][] - Environment for interactive programming.
* **RFC**: [Request For Comments][] - A Document used in standardization processes.
* **RSLGTM**: Rubber-Stamp Looks Good To Me - The reviewer approves without a full code review.
* **RSS**: [Resident Set Size][] - Amount of memory occupied by a process in RAM.
* **SMP**: [Symmetric Multi-Processor][] - Architecture where multiple processors share the same memory.
* **Snapshot**: Chunk of bytes containing data serialized from a V8 heap.
* **TBH**: To Be Honest.
* **TC39**: [Ecma Technical Committee 39][], governing body over **ECMAScript**.
* **TSC**: Technical Steering Committee - Governing body within a project.
* **UI**: [User Interface][] - The point of interaction between a user and a computer program. It includes elements
  such as buttons, menus, and other graphical elements that allow users to interact with the software.
* **URL**: [Uniform Resource Locator][] - A reference to a web resource that specifies its location on a computer
  network and the mechanism for retrieving it, typically using the HTTP or HTTPS protocol.
* **UTF-8**: [Unicode Transformation Format - 8-bit][] - A variable-width character encoding widely used for
  representing Unicode characters efficiently in byte-oriented systems.
* **V8**: [The JavaScript engine][] that powers Node.js and Chrome browser.
* **Vendoring**: Integrating external software into the project by copying its code source.
* **VM**: [The Node.js VM module][] - Provides a way of executing code within V8 Virtual Machine contexts.
* **W3C**: [World Wide Web Consortium][] - An international community that develops standards and guidelines for
  various aspects of the web ecosystem.
* **WASI**: [Web Assembly System Interface][] - Interface for WebAssembly.
* **WASM**: Web Assembly - Binary instruction format for a stack-based virtual machine.
* **WG**: Working Group - Autonomous teams in the project with specific focus areas.
* **WHATWG**: [Web Hypertext Application Technology Working Group][] - Community developing web standards.
* **WIP**: Work In Progress - Unfinished work that may be worth an early look.
* **WPT**: [web-platform-tests][] - Test suite for web platform APIs.

[Application Binary Interface]: https://en.wikipedia.org/wiki/Application_binary_interface
[Application Programming Interface]: https://en.wikipedia.org/wiki/Application_programming_interface
[Command Line Interface]: https://en.wikipedia.org/wiki/Command-line_interface
[Common Locale Data Repository]: https://en.wikipedia.org/wiki/Common_Locale_Data_Repository
[Common Vulnerabilities and Exposures]: https://cve.org
[CommonJS]: https://en.wikipedia.org/wiki/CommonJS
[CommonJS Modules]: https://nodejs.org/api/modules.html#modules-commonjs-modules
[Compiler Explorer]: https://godbolt.org/
[Continuous Integration]: https://en.wikipedia.org/wiki/Continuous_integration
[Document Object Model]: https://en.wikipedia.org/wiki/Document_Object_Model
[ECMAScript Module]: https://nodejs.org/api/esm.html#modules-ecmascript-modules
[Ecma International]: https://ecma.org
[Ecma Technical Committee 39]: https://tc39.es/
[End-of-File]: https://en.wikipedia.org/wiki/End-of-file
[End-of-Life]: https://en.wikipedia.org/wiki/End-of-life_product
[End-of-Line]: https://en.wikipedia.org/wiki/Newline
[Endian]: https://en.wikipedia.org/wiki/Endianness
[Event Tracing for Windows]: https://en.wikipedia.org/wiki/Event_Viewer
[Federal Information Processing Standards]: https://en.wikipedia.org/wiki/Federal_Information_Processing_Standards
[Hypertext Transfer Protocol]: https://en.wikipedia.org/wiki/Hypertext_Transfer_Protocol
[Integrated Development Environment]: https://en.wikipedia.org/wiki/Integrated_development_environment
[Inter-Process Communication]: https://en.wikipedia.org/wiki/Inter-process_communication
[International Components for Unicode]: https://icu.unicode.org/
[Internet Engineering Task Force]: https://www.ietf.org/
[JavaScript]: https://developer.mozilla.org/en-US/docs/Web/JavaScript
[JavaScript Object Notation]: https://www.json.org/
[Just In Time]: https://en.wikipedia.org/wiki/Just-in-time_compilation
[Long Term Support]: https://en.wikipedia.org/wiki/Long-term_support
[Model-View-Controller]: https://en.wikipedia.org/wiki/Model%E2%80%93view%E2%80%93controller
[Mozilla Development Network]: https://developer.mozilla.org/en-US
[NPM]: https://www.npmjs.com/
[Object-Oriented Programming]: https://en.wikipedia.org/wiki/Object-oriented_programming
[PowerPC]: https://en.wikipedia.org/wiki/PowerPC
[Read Evaluate Print Loop]: https://en.wikipedia.org/wiki/Read%E2%80%93eval%E2%80%93print_loop
[Request For Comments]: https://en.wikipedia.org/wiki/Request_for_Comments
[Resident Set Size]: https://en.wikipedia.org/wiki/Resident_set_size
[Resource Acquisition Is Initialization]: https://en.wikipedia.org/wiki/Resource_acquisition_is_initialization
[Symmetric Multi-Processor]: https://en.wikipedia.org/wiki/Symmetric_multiprocessing
[The JavaScript Engine]: https://en.wikipedia.org/wiki/V8_\(JavaScript_engine\)
[The Node.js VM Module]: https://nodejs.org/api/vm.html
[Unicode Transformation Format - 8-bit]: https://en.wikipedia.org/wiki/UTF-8
[Uniform Resource Locator]: https://en.wikipedia.org/wiki/URL
[User Interface]: https://en.wikipedia.org/wiki/User_interface
[Web Assembly System Interface]: https://github.com/WebAssembly/WASI
[Web Hypertext Application Technology Working Group]: https://en.wikipedia.org/wiki/WHATWG
[World Wide Web Consortium]: https://www.w3.org/
[specification document for **ECMAScript**]: https://ecma-international.org/publications-and-standards/standards/ecma-262/
[web-platform-tests]: https://github.com/web-platform-tests/wpt
