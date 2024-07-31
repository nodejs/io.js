# Generate Your Projects (gyp-next)

GYP is a Meta-Build system: a build system that generates other build systems.

* [User documentation](./UserDocumentation.md)
* [Input Format Reference](./InputFormatReference.md)
* [Language specification](./LanguageSpecification.md)
* [Hacking](./Hacking.md)
* [Testing](./Testing.md)
* [GYP vs. CMake](./GypVsCMake.md)

GYP is intended to support large projects that need to be built on multiple
platforms (e.g., Mac, Windows, Linux), and where it is important that
the project can be built using the IDEs that are popular on each platform
as if the project is a "native" one.

It can be used to generate XCode projects, Visual Studio projects, Ninja
build files, and Makefiles. In each case GYP's goal is to replicate as
closely as possible the way one would set up a native build of the project
using the IDE.

GYP can also be used to generate "hybrid" projects that provide the IDE
scaffolding for a nice user experience but call out to Ninja to do the actual
building (which is usually much faster than the native build systems of the
IDEs).

For more information on GYP, click on the links above.
