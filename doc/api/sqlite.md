# SQLite

<!--introduced_in=v22.5.0-->

<!-- YAML
added: v22.5.0
-->

> Stability: 1.1 - Active development.

<!-- source_link=lib/sqlite.js -->

The `node:sqlite` module facilitates working with SQLite databases.
To access it:

```mjs
import sqlite from 'node:sqlite';
```

```cjs
const sqlite = require('node:sqlite');
```

This module is only available under the `node:` scheme.

The following example shows the basic usage of the `node:sqlite` module to open
an in-memory database, write data to the database, and then read the data back.

```mjs
import { DatabaseSync } from 'node:sqlite';
const database = new DatabaseSync(':memory:');

// Execute SQL statements from strings.
database.exec(`
  CREATE TABLE data(
    key INTEGER PRIMARY KEY,
    value TEXT
  ) STRICT
`);
// Create a prepared statement to insert data into the database.
const insert = database.prepare('INSERT INTO data (key, value) VALUES (?, ?)');
// Execute the prepared statement with bound values.
insert.run(1, 'hello');
insert.run(2, 'world');
// Create a prepared statement to read data from the database.
const query = database.prepare('SELECT * FROM data ORDER BY key');
// Execute the prepared statement and log the result set.
console.log(query.all());
// Prints: [ { key: 1, value: 'hello' }, { key: 2, value: 'world' } ]
```

```cjs
'use strict';
const { DatabaseSync } = require('node:sqlite');
const database = new DatabaseSync(':memory:');

// Execute SQL statements from strings.
database.exec(`
  CREATE TABLE data(
    key INTEGER PRIMARY KEY,
    value TEXT
  ) STRICT
`);
// Create a prepared statement to insert data into the database.
const insert = database.prepare('INSERT INTO data (key, value) VALUES (?, ?)');
// Execute the prepared statement with bound values.
insert.run(1, 'hello');
insert.run(2, 'world');
// Create a prepared statement to read data from the database.
const query = database.prepare('SELECT * FROM data ORDER BY key');
// Execute the prepared statement and log the result set.
console.log(query.all());
// Prints: [ { key: 1, value: 'hello' }, { key: 2, value: 'world' } ]
```

## Class: `DatabaseSync`

<!-- YAML
added: v22.5.0
-->

This class represents a single [connection][] to a SQLite database. All APIs
exposed by this class execute synchronously.

### `new DatabaseSync(location[, options])`

<!-- YAML
added: v22.5.0
-->

* `location` {string} The location of the database. A SQLite database can be
  stored in a file or completely [in memory][]. To use a file-backed database,
  the location should be a file path. To use an in-memory database, the location
  should be the special name `':memory:'`.
* `options` {Object} Configuration options for the database connection. The
  following options are supported:
  * `open` {boolean} If `true`, the database is opened by the constructor. When
    this value is `false`, the database must be opened via the `open()` method.
    **Default:** `true`.
  * `readOnly` {boolean} If `true`, the database is opened in read-only mode.
    If the database does not exist, opening it will fail. **Default:** `false`.
  * `enableForeignKeyConstraints` {boolean} If `true`, foreign key constraints
    are enabled. This is recommended but can be disabled for compatibility with
    legacy database schemas. The enforcement of foreign key constraints can be
    enabled and disabled after opening the database using
    [`PRAGMA foreign_keys`][]. **Default:** `true`.
  * `enableDoubleQuotedStringLiterals` {boolean} If `true`, SQLite will accept
    [double-quoted string literals][]. This is not recommended but can be
    enabled for compatibility with legacy database schemas.
    **Default:** `false`.
  * `allowExtension` {boolean} If `true`, the `loadExtension` SQL function
    and the `loadExtension()` method are enabled.
    You can call `enableLoadExtension(false)` later to disable this feature.
    **Default:** `false`.

Constructs a new `DatabaseSync` instance.

### `database.close()`

<!-- YAML
added: v22.5.0
-->

Closes the database connection. An exception is thrown if the database is not
open. This method is a wrapper around [`sqlite3_close_v2()`][].

### `database.loadExtension(path)`

<!-- YAML
added:
  - v23.5.0
  - v22.13.0
-->

* `path` {string} The path to the shared library to load.

Loads a shared library into the database connection. This method is a wrapper
around [`sqlite3_load_extension()`][]. It is required to enable the
`allowExtension` option when constructing the `DatabaseSync` instance.

### `database.enableLoadExtension(allow)`

<!-- YAML
added:
  - v23.5.0
  - v22.13.0
-->

* `allow` {boolean} Whether to allow loading extensions.

Enables or disables the `loadExtension` SQL function, and the `loadExtension()`
method. When `allowExtension` is `false` when constructing, you cannot enable
loading extensions for security reasons.

### `database.exec(sql)`

<!-- YAML
added: v22.5.0
-->

* `sql` {string} A SQL string to execute.

This method allows one or more SQL statements to be executed without returning
any results. This method is useful when executing SQL statements read from a
file. This method is a wrapper around [`sqlite3_exec()`][].

### `database.function(name[, options], function)`

<!-- YAML
added:
  - v23.5.0
  - v22.13.0
-->

* `name` {string} The name of the SQLite function to create.
* `options` {Object} Optional configuration settings for the function. The
  following properties are supported:
  * `deterministic` {boolean} If `true`, the [`SQLITE_DETERMINISTIC`][] flag is
    set on the created function. **Default:** `false`.
  * `directOnly` {boolean} If `true`, the [`SQLITE_DIRECTONLY`][] flag is set on
    the created function. **Default:** `false`.
  * `useBigIntArguments` {boolean} If `true`, integer arguments to `function`
    are converted to `BigInt`s. If `false`, integer arguments are passed as
    JavaScript numbers. **Default:** `false`.
  * `varargs` {boolean} If `true`, `function` may be invoked with any number of
    arguments (between zero and [`SQLITE_MAX_FUNCTION_ARG`][]). If `false`,
    `function` must be invoked with exactly `function.length` arguments.
    **Default:** `false`.
* `function` {Function} The JavaScript function to call when the SQLite
  function is invoked. The return value of this function should be a valid
  SQLite data type: see [Type conversion between JavaScript and SQLite][].
  The result defaults to `NULL` if the return value is `undefined`.

This method is used to create SQLite user-defined functions. This method is a
wrapper around [`sqlite3_create_function_v2()`][].

### `database.open()`

<!-- YAML
added: v22.5.0
-->

Opens the database specified in the `location` argument of the `DatabaseSync`
constructor. This method should only be used when the database is not opened via
the constructor. An exception is thrown if the database is already open.

### `database.prepare(sql)`

<!-- YAML
added: v22.5.0
-->

* `sql` {string} A SQL string to compile to a prepared statement.
* Returns: {StatementSync} The prepared statement.

Compiles a SQL statement into a [prepared statement][]. This method is a wrapper
around [`sqlite3_prepare_v2()`][].

### `database.createSession([options])`

<!-- YAML
added:
  - v23.3.0
  - v22.12.0
-->

* `options` {Object} The configuration options for the session.
  * `table` {string} A specific table to track changes for. By default, changes to all tables are tracked.
  * `db` {string} Name of the database to track. This is useful when multiple databases have been added using [`ATTACH DATABASE`][]. **Default**: `'main'`.
* Returns: {Session} A session handle.

Creates and attaches a session to the database. This method is a wrapper around [`sqlite3session_create()`][] and [`sqlite3session_attach()`][].

### `database.applyChangeset(changeset[, options])`

<!-- YAML
added:
  - v23.3.0
  - v22.12.0
-->

* `changeset` {Uint8Array} A binary changeset or patchset.
* `options` {Object} The configuration options for how the changes will be applied.
  * `filter` {Function} Skip changes that, when targeted table name is supplied to this function, return a truthy value.
    By default, all changes are attempted.
  * `onConflict` {Function} A function that determines how to handle conflicts. The function receives one argument,
    which can be one of the following values:

    * `SQLITE_CHANGESET_DATA`: A `DELETE` or `UPDATE` change does not contain the expected "before" values.
    * `SQLITE_CHANGESET_NOTFOUND`: A row matching the primary key of the `DELETE` or `UPDATE` change does not exist.
    * `SQLITE_CHANGESET_CONFLICT`: An `INSERT` change results in a duplicate primary key.
    * `SQLITE_CHANGESET_FOREIGN_KEY`: Applying a change would result in a foreign key violation.
    * `SQLITE_CHANGESET_CONSTRAINT`: Applying a change results in a `UNIQUE`, `CHECK`, or `NOT NULL` constraint
      violation.

    The function should return one of the following values:

    * `SQLITE_CHANGESET_OMIT`: Omit conflicting changes.
    * `SQLITE_CHANGESET_REPLACE`: Replace existing values with conflicting changes (only valid with
      `SQLITE_CHANGESET_DATA` or `SQLITE_CHANGESET_CONFLICT` conflicts).
    * `SQLITE_CHANGESET_ABORT`: Abort on conflict and roll back the database.

    When an error is thrown in the conflict handler or when any other value is returned from the handler,
    applying the changeset is aborted and the database is rolled back.

    **Default**: A function that returns `SQLITE_CHANGESET_ABORT`.
* Returns: {boolean} Whether the changeset was applied succesfully without being aborted.

An exception is thrown if the database is not
open. This method is a wrapper around [`sqlite3changeset_apply()`][].

```js
const sourceDb = new DatabaseSync(':memory:');
const targetDb = new DatabaseSync(':memory:');

sourceDb.exec('CREATE TABLE data(key INTEGER PRIMARY KEY, value TEXT)');
targetDb.exec('CREATE TABLE data(key INTEGER PRIMARY KEY, value TEXT)');

const session = sourceDb.createSession();

const insert = sourceDb.prepare('INSERT INTO data (key, value) VALUES (?, ?)');
insert.run(1, 'hello');
insert.run(2, 'world');

const changeset = session.changeset();
targetDb.applyChangeset(changeset);
// Now that the changeset has been applied, targetDb contains the same data as sourceDb.
```

## Class: `Session`

<!-- YAML
added:
  - v23.3.0
  - v22.12.0
-->

### `session.changeset()`

<!-- YAML
added:
  - v23.3.0
  - v22.12.0
-->

* Returns: {Uint8Array} Binary changeset that can be applied to other databases.

Retrieves a changeset containing all changes since the changeset was created. Can be called multiple times.
An exception is thrown if the database or the session is not open. This method is a wrapper around [`sqlite3session_changeset()`][].

### `session.patchset()`

<!-- YAML
added:
  - v23.3.0
  - v22.12.0
-->

* Returns: {Uint8Array} Binary patchset that can be applied to other databases.

Similar to the method above, but generates a more compact patchset. See [Changesets and Patchsets][]
in the documentation of SQLite. An exception is thrown if the database or the session is not open. This method is a
wrapper around [`sqlite3session_patchset()`][].

### `session.close()`.

Closes the session. An exception is thrown if the database or the session is not open. This method is a
wrapper around [`sqlite3session_delete()`][].

## Class: `StatementSync`

<!-- YAML
added: v22.5.0
-->

This class represents a single [prepared statement][]. This class cannot be
instantiated via its constructor. Instead, instances are created via the
`database.prepare()` method. All APIs exposed by this class execute
synchronously.

A prepared statement is an efficient binary representation of the SQL used to
create it. Prepared statements are parameterizable, and can be invoked multiple
times with different bound values. Parameters also offer protection against
[SQL injection][] attacks. For these reasons, prepared statements are preferred
over hand-crafted SQL strings when handling user input.

### `statement.all([namedParameters][, ...anonymousParameters])`

<!-- YAML
added: v22.5.0
changes:
  - version: v23.7.0
    pr-url: https://github.com/nodejs/node/pull/56385
    description: Add support for `DataView` and typed array objects for `anonymousParameters`.
-->

* `namedParameters` {Object} An optional object used to bind named parameters.
  The keys of this object are used to configure the mapping.
* `...anonymousParameters` {null|number|bigint|string|Buffer|TypedArray|DataView} Zero or
  more values to bind to anonymous parameters.
* Returns: {Array} An array of objects. Each object corresponds to a row
  returned by executing the prepared statement. The keys and values of each
  object correspond to the column names and values of the row.

This method executes a prepared statement and returns all results as an array of
objects. If the prepared statement does not return any results, this method
returns an empty array. The prepared statement [parameters are bound][] using
the values in `namedParameters` and `anonymousParameters`.

### `statement.expandedSQL`

<!-- YAML
added: v22.5.0
-->

* {string} The source SQL expanded to include parameter values.

The source SQL text of the prepared statement with parameter
placeholders replaced by the values that were used during the most recent
execution of this prepared statement. This property is a wrapper around
[`sqlite3_expanded_sql()`][].

### `statement.get([namedParameters][, ...anonymousParameters])`

<!-- YAML
added: v22.5.0
changes:
  - version: v23.7.0
    pr-url: https://github.com/nodejs/node/pull/56385
    description: Add support for `DataView` and typed array objects for `anonymousParameters`.
-->

* `namedParameters` {Object} An optional object used to bind named parameters.
  The keys of this object are used to configure the mapping.
* `...anonymousParameters` {null|number|bigint|string|Buffer|TypedArray|DataView} Zero or
  more values to bind to anonymous parameters.
* Returns: {Object|undefined} An object corresponding to the first row returned
  by executing the prepared statement. The keys and values of the object
  correspond to the column names and values of the row. If no rows were returned
  from the database then this method returns `undefined`.

This method executes a prepared statement and returns the first result as an
object. If the prepared statement does not return any results, this method
returns `undefined`. The prepared statement [parameters are bound][] using the
values in `namedParameters` and `anonymousParameters`.

### `statement.iterate([namedParameters][, ...anonymousParameters])`

<!-- YAML
added:
  - v23.4.0
  - v22.13.0
changes:
  - version: v23.7.0
    pr-url: https://github.com/nodejs/node/pull/56385
    description: Add support for `DataView` and typed array objects for `anonymousParameters`.
-->

* `namedParameters` {Object} An optional object used to bind named parameters.
  The keys of this object are used to configure the mapping.
* `...anonymousParameters` {null|number|bigint|string|Buffer|TypedArray|DataView} Zero or
  more values to bind to anonymous parameters.
* Returns: {Iterator} An iterable iterator of objects. Each object corresponds to a row
  returned by executing the prepared statement. The keys and values of each
  object correspond to the column names and values of the row.

This method executes a prepared statement and returns an iterator of
objects. If the prepared statement does not return any results, this method
returns an empty iterator. The prepared statement [parameters are bound][] using
the values in `namedParameters` and `anonymousParameters`.

### `statement.run([namedParameters][, ...anonymousParameters])`

<!-- YAML
added: v22.5.0
changes:
  - version: v23.7.0
    pr-url: https://github.com/nodejs/node/pull/56385
    description: Add support for `DataView` and typed array objects for `anonymousParameters`.
-->

* `namedParameters` {Object} An optional object used to bind named parameters.
  The keys of this object are used to configure the mapping.
* `...anonymousParameters` {null|number|bigint|string|Buffer|TypedArray|DataView} Zero or
  more values to bind to anonymous parameters.
* Returns: {Object}
  * `changes`: {number|bigint} The number of rows modified, inserted, or deleted
    by the most recently completed `INSERT`, `UPDATE`, or `DELETE` statement.
    This field is either a number or a `BigInt` depending on the prepared
    statement's configuration. This property is the result of
    [`sqlite3_changes64()`][].
  * `lastInsertRowid`: {number|bigint} The most recently inserted rowid. This
    field is either a number or a `BigInt` depending on the prepared statement's
    configuration. This property is the result of
    [`sqlite3_last_insert_rowid()`][].

This method executes a prepared statement and returns an object summarizing the
resulting changes. The prepared statement [parameters are bound][] using the
values in `namedParameters` and `anonymousParameters`.

### `statement.setAllowBareNamedParameters(enabled)`

<!-- YAML
added: v22.5.0
-->

* `enabled` {boolean} Enables or disables support for binding named parameters
  without the prefix character.

The names of SQLite parameters begin with a prefix character. By default,
`node:sqlite` requires that this prefix character is present when binding
parameters. However, with the exception of dollar sign character, these
prefix characters also require extra quoting when used in object keys.

To improve ergonomics, this method can be used to also allow bare named
parameters, which do not require the prefix character in JavaScript code. There
are several caveats to be aware of when enabling bare named parameters:

* The prefix character is still required in SQL.
* The prefix character is still allowed in JavaScript. In fact, prefixed names
  will have slightly better binding performance.
* Using ambiguous named parameters, such as `$k` and `@k`, in the same prepared
  statement will result in an exception as it cannot be determined how to bind
  a bare name.

### `statement.setReadBigInts(enabled)`

<!-- YAML
added: v22.5.0
-->

* `enabled` {boolean} Enables or disables the use of `BigInt`s when reading
  `INTEGER` fields from the database.

When reading from the database, SQLite `INTEGER`s are mapped to JavaScript
numbers by default. However, SQLite `INTEGER`s can store values larger than
JavaScript numbers are capable of representing. In such cases, this method can
be used to read `INTEGER` data using JavaScript `BigInt`s. This method has no
impact on database write operations where numbers and `BigInt`s are both
supported at all times.

### `statement.sourceSQL`

<!-- YAML
added: v22.5.0
-->

* {string} The source SQL used to create this prepared statement.

The source SQL text of the prepared statement. This property is a
wrapper around [`sqlite3_sql()`][].

### Type conversion between JavaScript and SQLite

When Node.js writes to or reads from SQLite it is necessary to convert between
JavaScript data types and SQLite's [data types][]. Because JavaScript supports
more data types than SQLite, only a subset of JavaScript types are supported.
Attempting to write an unsupported data type to SQLite will result in an
exception.

| SQLite    | JavaScript                 |
| --------- | -------------------------- |
| `NULL`    | {null}                     |
| `INTEGER` | {number} or {bigint}       |
| `REAL`    | {number}                   |
| `TEXT`    | {string}                   |
| `BLOB`    | {TypedArray} or {DataView} |

## `sqlite.constants`

<!-- YAML
added:
  - v23.5.0
  - v22.13.0
-->

* {Object}

An object containing commonly used constants for SQLite operations.

### SQLite constants

The following constants are exported by the `sqlite.constants` object.

#### Conflict resolution constants

One of the following constants is available as an argument to the `onConflict`
conflict resolution handler passed to [`database.applyChangeset()`][]. See also
[Constants Passed To The Conflict Handler][] in the SQLite documentation.

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_DATA</code></td>
    <td>The conflict handler is invoked with this constant when processing a DELETE or UPDATE change if a row with the required PRIMARY KEY fields is present in the database, but one or more other (non primary-key) fields modified by the update do not contain the expected "before" values.</td>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_NOTFOUND</code></td>
    <td>The conflict handler is invoked with this constant when processing a DELETE or UPDATE change if a row with the required PRIMARY KEY fields is not present in the database.</td>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_CONFLICT</code></td>
    <td>This constant is passed to the conflict handler while processing an INSERT change if the operation would result in duplicate primary key values.</td>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_CONSTRAINT</code></td>
    <td>If foreign key handling is enabled, and applying a changeset leaves the database in a state containing foreign key violations, the conflict handler is invoked with this constant exactly once before the changeset is committed. If the conflict handler returns <code>SQLITE_CHANGESET_OMIT</code>, the changes, including those that caused the foreign key constraint violation, are committed. Or, if it returns <code>SQLITE_CHANGESET_ABORT</code>, the changeset is rolled back.</td>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_FOREIGN_KEY</code></td>
    <td>If any other constraint violation occurs while applying a change (i.e. a UNIQUE, CHECK or NOT NULL constraint), the conflict handler is invoked with this constant.</td>
  </tr>
</table>

One of the following constants must be returned from the `onConflict` conflict
resolution handler passed to [`database.applyChangeset()`][]. See also
[Constants Returned From The Conflict Handler][] in the SQLite documentation.

<table>
  <tr>
    <th>Constant</th>
    <th>Description</th>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_OMIT</code></td>
    <td>Conflicting changes are omitted.</td>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_REPLACE</code></td>
    <td>Conflicting changes replace existing values. Note that this value can only be returned when the type of conflict is either <code>SQLITE_CHANGESET_DATA</code> or <code>SQLITE_CHANGESET_CONFLICT</code>.</td>
  </tr>
  <tr>
    <td><code>SQLITE_CHANGESET_ABORT</code></td>
    <td>Abort when a change encounters a conflict and roll back database.</td>
  </tr>
</table>

[Changesets and Patchsets]: https://www.sqlite.org/sessionintro.html#changesets_and_patchsets
[Constants Passed To The Conflict Handler]: https://www.sqlite.org/session/c_changeset_conflict.html
[Constants Returned From The Conflict Handler]: https://www.sqlite.org/session/c_changeset_abort.html
[SQL injection]: https://en.wikipedia.org/wiki/SQL_injection
[Type conversion between JavaScript and SQLite]: #type-conversion-between-javascript-and-sqlite
[`ATTACH DATABASE`]: https://www.sqlite.org/lang_attach.html
[`PRAGMA foreign_keys`]: https://www.sqlite.org/pragma.html#pragma_foreign_keys
[`SQLITE_DETERMINISTIC`]: https://www.sqlite.org/c3ref/c_deterministic.html
[`SQLITE_DIRECTONLY`]: https://www.sqlite.org/c3ref/c_deterministic.html
[`SQLITE_MAX_FUNCTION_ARG`]: https://www.sqlite.org/limits.html#max_function_arg
[`database.applyChangeset()`]: #databaseapplychangesetchangeset-options
[`sqlite3_changes64()`]: https://www.sqlite.org/c3ref/changes.html
[`sqlite3_close_v2()`]: https://www.sqlite.org/c3ref/close.html
[`sqlite3_create_function_v2()`]: https://www.sqlite.org/c3ref/create_function.html
[`sqlite3_exec()`]: https://www.sqlite.org/c3ref/exec.html
[`sqlite3_expanded_sql()`]: https://www.sqlite.org/c3ref/expanded_sql.html
[`sqlite3_last_insert_rowid()`]: https://www.sqlite.org/c3ref/last_insert_rowid.html
[`sqlite3_load_extension()`]: https://www.sqlite.org/c3ref/load_extension.html
[`sqlite3_prepare_v2()`]: https://www.sqlite.org/c3ref/prepare.html
[`sqlite3_sql()`]: https://www.sqlite.org/c3ref/expanded_sql.html
[`sqlite3changeset_apply()`]: https://www.sqlite.org/session/sqlite3changeset_apply.html
[`sqlite3session_attach()`]: https://www.sqlite.org/session/sqlite3session_attach.html
[`sqlite3session_changeset()`]: https://www.sqlite.org/session/sqlite3session_changeset.html
[`sqlite3session_create()`]: https://www.sqlite.org/session/sqlite3session_create.html
[`sqlite3session_delete()`]: https://www.sqlite.org/session/sqlite3session_delete.html
[`sqlite3session_patchset()`]: https://www.sqlite.org/session/sqlite3session_patchset.html
[connection]: https://www.sqlite.org/c3ref/sqlite3.html
[data types]: https://www.sqlite.org/datatype3.html
[double-quoted string literals]: https://www.sqlite.org/quirks.html#dblquote
[in memory]: https://www.sqlite.org/inmemorydb.html
[parameters are bound]: https://www.sqlite.org/c3ref/bind_blob.html
[prepared statement]: https://www.sqlite.org/c3ref/stmt.html
