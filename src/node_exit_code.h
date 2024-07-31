#ifndef SRC_NODE_EXIT_CODE_H_
#define SRC_NODE_EXIT_CODE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

namespace node {
#define EXIT_CODE_LIST(V)                                                      \
  V(NoFailure, 0)                                                              \
  /* 1 was intended for uncaught JS exceptions from the user land but we */    \
  /* actually use this for all kinds of generic errors. */                     \
  V(GenericUserError, 1)                                                       \
  /* 2 is unused */                                                            \
  /* 3 is actually unused because we pre-compile all builtins during */        \
  /* snapshot building, when we exit with 1 if there's any error.  */          \
  V(InternalJSParseError, 3)                                                   \
  /* 4 is actually unused. We exit with 1 in this case. */                     \
  V(InternalJSEvaluationFailure, 4)                                            \
  /* 5 is actually unused. We exit with 133 (128+SIGTRAP) or 134 */            \
  /* (128+SIGABRT) in this case. */                                            \
  V(V8FatalError, 5)                                                           \
  V(InvalidFatalExceptionMonkeyPatching, 6)                                    \
  V(ExceptionInFatalExceptionHandler, 7)                                       \
  /* 8 is unused */                                                            \
  V(InvalidCommandLineArgument, 9)                                             \
  V(BootstrapFailure, 10)                                                      \
  /* 11 is unused */                                                           \
  /* This was intended for invalid inspector arguments but is actually now */  \
  /* just a duplicate of InvalidCommandLineArgument */                         \
  V(InvalidCommandLineArgument2, 12)                                           \
  V(UnsettledTopLevelAwait, 13)                                                \
  V(StartupSnapshotFailure, 14)                                                \
  /* If the process exits from unhandled signals e.g. SIGABRT, SIGTRAP, */     \
  /* typically the exit codes are 128 + signal number. We also exit with */    \
  /* certain error codes directly for legacy reasons. Here we define those */  \
  /* that are used to normalize the exit code on Windows. */                   \
  V(Abort, 134)

// TODO(joyeecheung): expose this to user land when the codes are stable.
// The underlying type should be an int, or we can get undefined behavior when
// casting error codes into exit codes (technically we shouldn't do that,
// but that's how things have been).
enum class ExitCode : int {
#define V(Name, Code) k##Name = Code,
  EXIT_CODE_LIST(V)
#undef V
};

[[noreturn]] void Exit(ExitCode exit_code);

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_EXIT_CODE_H_
