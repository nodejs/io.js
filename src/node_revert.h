#ifndef SRC_NODE_REVERT_H_
#define SRC_NODE_REVERT_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node.h"

/**
 * Note that it is expected for this list to vary across specific LTS and
 * Stable versions! Only CVE's whose fixes require *breaking* changes within
 * a given LTS or Stable may be added to this list, and only with TSC
 * consensus.
 *
 * For *main* this list should always be empty!
 **/
namespace node {

#define SECURITY_REVERSIONS(XX)                                                \
  //  XX(CVE_2016_PEND, "CVE-2016-PEND", "Vulnerability Title")

enum reversion {
#define V(code, ...) SECURITY_REVERT_##code,
  SECURITY_REVERSIONS(V)
#undef V
};

namespace per_process {
extern unsigned int reverted_cve;
}

#ifdef _MSC_VER
#pragma warning(push)
// MSVC C4065: switch statement contains 'default' but no 'case' labels
#pragma warning(disable : 4065)
#endif

inline const char* RevertMessage(const reversion cve) {
#define V(code, label, msg) case SECURITY_REVERT_##code: return label ": " msg;
  switch (cve) {
    SECURITY_REVERSIONS(V)
    default:
      return "Unknown";
  }
#undef V
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

inline void Revert(const reversion cve) {
  per_process::reverted_cve |= 1 << cve;
  printf("SECURITY WARNING: Reverting %s\n", RevertMessage(cve));
}

inline void Revert(const char* cve, std::string* error) {
#define V(code, label, _)                                                     \
  if (strcmp(cve, label) == 0) return Revert(SECURITY_REVERT_##code);
  SECURITY_REVERSIONS(V)
#undef V
  *error = "Error: Attempt to revert an unknown CVE [";
  *error += cve;
  *error += ']';
}

inline bool IsReverted(const reversion cve) {
  return per_process::reverted_cve & (1 << cve);
}

inline bool IsReverted(const char* cve) {
#define V(code, label, _)                                                     \
  if (strcmp(cve, label) == 0) return IsReverted(SECURITY_REVERT_##code);
  SECURITY_REVERSIONS(V)
  return false;
#undef V
}

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_REVERT_H_
