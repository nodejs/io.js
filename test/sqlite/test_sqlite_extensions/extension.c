/*
** 2020-01-08
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This SQLite extension implements a noop() function used for testing.
**
** Variants:
**
**    noop(X)           The default.  Deterministic.
**    noop_i(X)         Deterministic and innocuous.
**    noop_do(X)        Deterministic and direct-only.
**    noop_nd(X)        Non-deterministic.
*/
#include <assert.h>
#include <sqlite3ext.h>
#include <stdio.h>
#include <string.h>

SQLITE_EXTENSION_INIT1

/*
** Implementation of the noop() function.
**
** The function returns its argument, unchanged.
*/
static void noopfunc(sqlite3_context* context, int argc, sqlite3_value** argv) {
  assert(argc == 1);
  sqlite3_result_value(context, argv[0]);
}

/*
** Implementation of the multitype_text() function.
**
** The function returns its argument.  The result will always have a
** TEXT value.  But if the original input is numeric, it will also
** have that numeric value.
*/
static void multitypeTextFunc(sqlite3_context* context,
                              int argc,
                              sqlite3_value** argv) {
  assert(argc == 1);
  (void)argc;
  (void)sqlite3_value_text(argv[0]);
  sqlite3_result_value(context, argv[0]);
}

#ifdef _WIN32
__declspec(dllexport)
#endif

    int sqlite3_extension_init(sqlite3* db,
                               char** pzErrMsg,
                               const sqlite3_api_routines* pApi) {
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);

  rc = sqlite3_create_function(
      db, "noop", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, noopfunc, 0, 0);
  if (rc) return rc;
  rc = sqlite3_create_function(
      db,
      "noop_i",
      1,
      SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS,
      0,
      noopfunc,
      0,
      0);
  if (rc) return rc;
  rc = sqlite3_create_function(
      db,
      "noop_do",
      1,
      SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_DIRECTONLY,
      0,
      noopfunc,
      0,
      0);
  if (rc) return rc;
  rc =
      sqlite3_create_function(db, "noop_nd", 1, SQLITE_UTF8, 0, noopfunc, 0, 0);
  if (rc) return rc;
  rc = sqlite3_create_function(
      db, "multitype_text", 1, SQLITE_UTF8, 0, multitypeTextFunc, 0, 0);
  return rc;
}
