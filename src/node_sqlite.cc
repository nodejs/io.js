#include "node_sqlite.h"
#include <path.h>
#include "base_object-inl.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "node.h"
#include "node_errors.h"
#include "node_mem-inl.h"
#include "sqlite3.h"
#include "util-inl.h"

#include <cinttypes>

namespace node {
namespace sqlite {

using v8::Array;
using v8::ArrayBuffer;
using v8::BigInt;
using v8::Boolean;
using v8::ConstructorBehavior;
using v8::Context;
using v8::DontDelete;
using v8::Exception;
using v8::External;
using v8::Function;
using v8::FunctionCallback;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Global;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::LocalVector;
using v8::MaybeLocal;
using v8::Name;
using v8::NewStringType;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::SideEffectType;
using v8::String;
using v8::TryCatch;
using v8::Uint8Array;
using v8::Value;

#define CHECK_ERROR_OR_THROW(isolate, db, expr, expected, ret)                 \
  do {                                                                         \
    int r_ = (expr);                                                           \
    if (r_ != (expected)) {                                                    \
      THROW_ERR_SQLITE_ERROR((isolate), (db));                                 \
      return (ret);                                                            \
    }                                                                          \
  } while (0)

#define THROW_AND_RETURN_ON_BAD_STATE(env, condition, msg)                     \
  do {                                                                         \
    if ((condition)) {                                                         \
      THROW_ERR_INVALID_STATE((env), (msg));                                   \
      return;                                                                  \
    }                                                                          \
  } while (0)

inline MaybeLocal<Object> CreateSQLiteError(Isolate* isolate,
                                            const char* message) {
  Local<String> js_msg;
  Local<Object> e;
  Environment* env = Environment::GetCurrent(isolate);
  if (!String::NewFromUtf8(isolate, message).ToLocal(&js_msg) ||
      !Exception::Error(js_msg)
           ->ToObject(isolate->GetCurrentContext())
           .ToLocal(&e) ||
      e->Set(isolate->GetCurrentContext(),
             env->code_string(),
             env->err_sqlite_error_string())
          .IsNothing()) {
    return MaybeLocal<Object>();
  }
  return e;
}

inline MaybeLocal<Object> CreateSQLiteError(Isolate* isolate, sqlite3* db) {
  int errcode = sqlite3_extended_errcode(db);
  const char* errstr = sqlite3_errstr(errcode);
  const char* errmsg = sqlite3_errmsg(db);
  Local<String> js_errmsg;
  Local<Object> e;
  Environment* env = Environment::GetCurrent(isolate);
  if (!String::NewFromUtf8(isolate, errstr).ToLocal(&js_errmsg) ||
      !CreateSQLiteError(isolate, errmsg).ToLocal(&e) ||
      e->Set(isolate->GetCurrentContext(),
             env->errcode_string(),
             Integer::New(isolate, errcode))
          .IsNothing() ||
      e->Set(isolate->GetCurrentContext(), env->errstr_string(), js_errmsg)
          .IsNothing()) {
    return MaybeLocal<Object>();
  }
  return e;
}

inline void THROW_ERR_SQLITE_ERROR(Isolate* isolate, sqlite3* db) {
  Local<Object> e;
  if (CreateSQLiteError(isolate, db).ToLocal(&e)) {
    isolate->ThrowException(e);
  }
}

inline void THROW_ERR_SQLITE_ERROR(Isolate* isolate, const char* message) {
  Local<Object> e;
  if (CreateSQLiteError(isolate, message).ToLocal(&e)) {
    isolate->ThrowException(e);
  }
}

inline void THROW_ERR_SQLITE_ERROR(Isolate* isolate, int errcode) {
  const char* errstr = sqlite3_errstr(errcode);

  Environment* env = Environment::GetCurrent(isolate);
  auto error = CreateSQLiteError(isolate, errstr).ToLocalChecked();
  error
      ->Set(isolate->GetCurrentContext(),
            env->errcode_string(),
            Integer::New(isolate, errcode))
      .ToChecked();
  isolate->ThrowException(error);
}

class UserDefinedFunction {
 public:
  explicit UserDefinedFunction(Environment* env,
                               Local<Function> fn,
                               bool use_bigint_args)
      : env_(env), fn_(env->isolate(), fn), use_bigint_args_(use_bigint_args) {}
  virtual ~UserDefinedFunction() {}

  static void xFunc(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    UserDefinedFunction* self =
        static_cast<UserDefinedFunction*>(sqlite3_user_data(ctx));
    Environment* env = self->env_;
    Isolate* isolate = env->isolate();
    auto recv = Undefined(isolate);
    auto fn = self->fn_.Get(isolate);
    LocalVector<Value> js_argv(isolate);

    for (int i = 0; i < argc; ++i) {
      sqlite3_value* value = argv[i];
      MaybeLocal<Value> js_val;

      switch (sqlite3_value_type(value)) {
        case SQLITE_INTEGER: {
          sqlite3_int64 val = sqlite3_value_int64(value);
          if (self->use_bigint_args_) {
            js_val = BigInt::New(isolate, val);
          } else if (std::abs(val) <= kMaxSafeJsInteger) {
            js_val = Number::New(isolate, val);
          } else {
            THROW_ERR_OUT_OF_RANGE(isolate,
                                   "Value is too large to be represented as a "
                                   "JavaScript number: %" PRId64,
                                   val);
            return;
          }
          break;
        }
        case SQLITE_FLOAT:
          js_val = Number::New(isolate, sqlite3_value_double(value));
          break;
        case SQLITE_TEXT: {
          const char* v =
              reinterpret_cast<const char*>(sqlite3_value_text(value));
          js_val = String::NewFromUtf8(isolate, v).As<Value>();
          break;
        }
        case SQLITE_NULL:
          js_val = Null(isolate);
          break;
        case SQLITE_BLOB: {
          size_t size = static_cast<size_t>(sqlite3_value_bytes(value));
          auto data =
              reinterpret_cast<const uint8_t*>(sqlite3_value_blob(value));
          auto store = ArrayBuffer::NewBackingStore(isolate, size);
          memcpy(store->Data(), data, size);
          auto ab = ArrayBuffer::New(isolate, std::move(store));
          js_val = Uint8Array::New(ab, 0, size);
          break;
        }
        default:
          UNREACHABLE("Bad SQLite value");
      }

      Local<Value> local;
      if (!js_val.ToLocal(&local)) {
        return;
      }

      js_argv.emplace_back(local);
    }

    MaybeLocal<Value> retval =
        fn->Call(env->context(), recv, argc, js_argv.data());
    Local<Value> result;
    if (!retval.ToLocal(&result)) {
      return;
    }

    if (result->IsUndefined() || result->IsNull()) {
      sqlite3_result_null(ctx);
    } else if (result->IsNumber()) {
      sqlite3_result_double(ctx, result.As<Number>()->Value());
    } else if (result->IsString()) {
      Utf8Value val(isolate, result.As<String>());
      sqlite3_result_text(ctx, *val, val.length(), SQLITE_TRANSIENT);
    } else if (result->IsUint8Array()) {
      ArrayBufferViewContents<uint8_t> buf(result);
      sqlite3_result_blob(ctx, buf.data(), buf.length(), SQLITE_TRANSIENT);
    } else if (result->IsBigInt()) {
      bool lossless;
      int64_t as_int = result.As<BigInt>()->Int64Value(&lossless);
      if (!lossless) {
        sqlite3_result_error(ctx, "BigInt value is too large for SQLite", -1);
        return;
      }
      sqlite3_result_int64(ctx, as_int);
    } else if (result->IsPromise()) {
      sqlite3_result_error(
          ctx, "Asynchronous user-defined functions are not supported", -1);
    } else {
      sqlite3_result_error(
          ctx,
          "Returned JavaScript value cannot be converted to a SQLite value",
          -1);
    }
  }

  static void xDestroy(void* self) {
    delete static_cast<UserDefinedFunction*>(self);
  }

 private:
  Environment* env_;
  Global<Function> fn_;
  bool use_bigint_args_;
};

DatabaseSync::DatabaseSync(Environment* env,
                           Local<Object> object,
                           DatabaseOpenConfiguration&& open_config,
                           bool open,
                           bool allow_load_extension)
    : BaseObject(env, object), open_config_(std::move(open_config)) {
  MakeWeak();
  connection_ = nullptr;
  allow_load_extension_ = allow_load_extension;
  enable_load_extension_ = allow_load_extension;

  if (open) {
    Open();
  }
}

void DatabaseSync::DeleteSessions() {
  // all attached sessions need to be deleted before the database is closed
  // https://www.sqlite.org/session/sqlite3session_create.html
  for (auto* session : sessions_) {
    sqlite3session_delete(session);
  }
  sessions_.clear();
}

DatabaseSync::~DatabaseSync() {
  if (IsOpen()) {
    FinalizeStatements();
    DeleteSessions();
    sqlite3_close_v2(connection_);
    connection_ = nullptr;
  }
}

void DatabaseSync::MemoryInfo(MemoryTracker* tracker) const {
  // TODO(tniessen): more accurately track the size of all fields
  tracker->TrackFieldWithSize(
      "open_config", sizeof(open_config_), "DatabaseOpenConfiguration");
}

bool DatabaseSync::Open() {
  if (IsOpen()) {
    THROW_ERR_INVALID_STATE(env(), "database is already open");
    return false;
  }

  // TODO(cjihrig): Support additional flags.
  int flags = open_config_.get_read_only()
                  ? SQLITE_OPEN_READONLY
                  : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  int r = sqlite3_open_v2(
      open_config_.location().c_str(), &connection_, flags, nullptr);
  CHECK_ERROR_OR_THROW(env()->isolate(), connection_, r, SQLITE_OK, false);

  r = sqlite3_db_config(connection_,
                        SQLITE_DBCONFIG_DQS_DML,
                        static_cast<int>(open_config_.get_enable_dqs()),
                        nullptr);
  CHECK_ERROR_OR_THROW(env()->isolate(), connection_, r, SQLITE_OK, false);
  r = sqlite3_db_config(connection_,
                        SQLITE_DBCONFIG_DQS_DDL,
                        static_cast<int>(open_config_.get_enable_dqs()),
                        nullptr);
  CHECK_ERROR_OR_THROW(env()->isolate(), connection_, r, SQLITE_OK, false);

  int foreign_keys_enabled;
  r = sqlite3_db_config(
      connection_,
      SQLITE_DBCONFIG_ENABLE_FKEY,
      static_cast<int>(open_config_.get_enable_foreign_keys()),
      &foreign_keys_enabled);
  CHECK_ERROR_OR_THROW(env()->isolate(), connection_, r, SQLITE_OK, false);
  CHECK_EQ(foreign_keys_enabled, open_config_.get_enable_foreign_keys());

  if (allow_load_extension_) {
    if (env()->permission()->enabled()) [[unlikely]] {
      THROW_ERR_LOAD_SQLITE_EXTENSION(env(),
                                      "Cannot load SQLite extensions when the "
                                      "permission model is enabled.");
      return false;
    }
    const int load_extension_ret = sqlite3_db_config(
        connection_, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 1, nullptr);
    CHECK_ERROR_OR_THROW(
        env()->isolate(), connection_, load_extension_ret, SQLITE_OK, false);
  }

  return true;
}

void DatabaseSync::FinalizeStatements() {
  for (auto stmt : statements_) {
    stmt->Finalize();
  }

  statements_.clear();
}

void DatabaseSync::UntrackStatement(StatementSync* statement) {
  auto it = statements_.find(statement);
  if (it != statements_.end()) {
    statements_.erase(it);
  }
}

inline bool DatabaseSync::IsOpen() {
  return connection_ != nullptr;
}

inline sqlite3* DatabaseSync::Connection() {
  return connection_;
}

void DatabaseSync::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  if (!args.IsConstructCall()) {
    THROW_ERR_CONSTRUCT_CALL_REQUIRED(env);
    return;
  }

  if (!args[0]->IsString()) {
    THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                               "The \"path\" argument must be a string.");
    return;
  }

  std::string location =
      Utf8Value(env->isolate(), args[0].As<String>()).ToString();
  DatabaseOpenConfiguration open_config(std::move(location));

  bool open = true;
  bool allow_load_extension = false;

  if (args.Length() > 1) {
    if (!args[1]->IsObject()) {
      THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                                 "The \"options\" argument must be an object.");
      return;
    }

    Local<Object> options = args[1].As<Object>();
    Local<String> open_string = FIXED_ONE_BYTE_STRING(env->isolate(), "open");
    Local<Value> open_v;
    if (!options->Get(env->context(), open_string).ToLocal(&open_v)) {
      return;
    }
    if (!open_v->IsUndefined()) {
      if (!open_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(), "The \"options.open\" argument must be a boolean.");
        return;
      }
      open = open_v.As<Boolean>()->Value();
    }

    Local<String> read_only_string =
        FIXED_ONE_BYTE_STRING(env->isolate(), "readOnly");
    Local<Value> read_only_v;
    if (!options->Get(env->context(), read_only_string).ToLocal(&read_only_v)) {
      return;
    }
    if (!read_only_v->IsUndefined()) {
      if (!read_only_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.readOnly\" argument must be a boolean.");
        return;
      }
      open_config.set_read_only(read_only_v.As<Boolean>()->Value());
    }

    Local<String> enable_foreign_keys_string =
        FIXED_ONE_BYTE_STRING(env->isolate(), "enableForeignKeyConstraints");
    Local<Value> enable_foreign_keys_v;
    if (!options->Get(env->context(), enable_foreign_keys_string)
             .ToLocal(&enable_foreign_keys_v)) {
      return;
    }
    if (!enable_foreign_keys_v->IsUndefined()) {
      if (!enable_foreign_keys_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.enableForeignKeyConstraints\" argument must be a "
            "boolean.");
        return;
      }
      open_config.set_enable_foreign_keys(
          enable_foreign_keys_v.As<Boolean>()->Value());
    }

    Local<String> enable_dqs_string = FIXED_ONE_BYTE_STRING(
        env->isolate(), "enableDoubleQuotedStringLiterals");
    Local<Value> enable_dqs_v;
    if (!options->Get(env->context(), enable_dqs_string)
             .ToLocal(&enable_dqs_v)) {
      return;
    }
    if (!enable_dqs_v->IsUndefined()) {
      if (!enable_dqs_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.enableDoubleQuotedStringLiterals\" argument must be "
            "a boolean.");
        return;
      }
      open_config.set_enable_dqs(enable_dqs_v.As<Boolean>()->Value());
    }

    Local<String> allow_extension_string =
        FIXED_ONE_BYTE_STRING(env->isolate(), "allowExtension");
    Local<Value> allow_extension_v;
    if (!options->Get(env->context(), allow_extension_string)
             .ToLocal(&allow_extension_v)) {
      return;
    }

    if (!allow_extension_v->IsUndefined()) {
      if (!allow_extension_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.allowExtension\" argument must be a boolean.");
        return;
      }
      allow_load_extension = allow_extension_v.As<Boolean>()->Value();
    }
  }

  new DatabaseSync(
      env, args.This(), std::move(open_config), open, allow_load_extension);
}

void DatabaseSync::Open(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  db->Open();
}

void DatabaseSync::Close(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(env, !db->IsOpen(), "database is not open");
  db->FinalizeStatements();
  db->DeleteSessions();
  int r = sqlite3_close_v2(db->connection_);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
  db->connection_ = nullptr;
}

void DatabaseSync::Prepare(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(env, !db->IsOpen(), "database is not open");

  if (!args[0]->IsString()) {
    THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                               "The \"sql\" argument must be a string.");
    return;
  }

  Utf8Value sql(env->isolate(), args[0].As<String>());
  sqlite3_stmt* s = nullptr;
  int r = sqlite3_prepare_v2(db->connection_, *sql, -1, &s, 0);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
  BaseObjectPtr<StatementSync> stmt = StatementSync::Create(env, db, s);
  db->statements_.insert(stmt.get());
  args.GetReturnValue().Set(stmt->object());
}

void DatabaseSync::Exec(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(env, !db->IsOpen(), "database is not open");

  if (!args[0]->IsString()) {
    THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                               "The \"sql\" argument must be a string.");
    return;
  }

  Utf8Value sql(env->isolate(), args[0].As<String>());
  int r = sqlite3_exec(db->connection_, *sql, nullptr, nullptr, nullptr);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
}

void DatabaseSync::CustomFunction(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(env, !db->IsOpen(), "database is not open");

  if (!args[0]->IsString()) {
    THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                               "The \"name\" argument must be a string.");
    return;
  }

  int fn_index = args.Length() < 3 ? 1 : 2;
  bool use_bigint_args = false;
  bool varargs = false;
  bool deterministic = false;
  bool direct_only = false;

  if (fn_index > 1) {
    if (!args[1]->IsObject()) {
      THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                                 "The \"options\" argument must be an object.");
      return;
    }

    Local<Object> options = args[1].As<Object>();
    Local<Value> use_bigint_args_v;
    if (!options
             ->Get(env->context(),
                   FIXED_ONE_BYTE_STRING(env->isolate(), "useBigIntArguments"))
             .ToLocal(&use_bigint_args_v)) {
      return;
    }

    if (!use_bigint_args_v->IsUndefined()) {
      if (!use_bigint_args_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.useBigIntArguments\" argument must be a boolean.");
        return;
      }
      use_bigint_args = use_bigint_args_v.As<Boolean>()->Value();
    }

    Local<Value> varargs_v;
    if (!options
             ->Get(env->context(),
                   FIXED_ONE_BYTE_STRING(env->isolate(), "varargs"))
             .ToLocal(&varargs_v)) {
      return;
    }

    if (!varargs_v->IsUndefined()) {
      if (!varargs_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.varargs\" argument must be a boolean.");
        return;
      }
      varargs = varargs_v.As<Boolean>()->Value();
    }

    Local<Value> deterministic_v;
    if (!options
             ->Get(env->context(),
                   FIXED_ONE_BYTE_STRING(env->isolate(), "deterministic"))
             .ToLocal(&deterministic_v)) {
      return;
    }

    if (!deterministic_v->IsUndefined()) {
      if (!deterministic_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.deterministic\" argument must be a boolean.");
        return;
      }
      deterministic = deterministic_v.As<Boolean>()->Value();
    }

    Local<Value> direct_only_v;
    if (!options
             ->Get(env->context(),
                   FIXED_ONE_BYTE_STRING(env->isolate(), "directOnly"))
             .ToLocal(&direct_only_v)) {
      return;
    }

    if (!direct_only_v->IsUndefined()) {
      if (!direct_only_v->IsBoolean()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.directOnly\" argument must be a boolean.");
        return;
      }
      direct_only = direct_only_v.As<Boolean>()->Value();
    }
  }

  if (!args[fn_index]->IsFunction()) {
    THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                               "The \"function\" argument must be a function.");
    return;
  }

  Utf8Value name(env->isolate(), args[0].As<String>());
  Local<Function> fn = args[fn_index].As<Function>();

  int argc = 0;
  if (varargs) {
    argc = -1;
  } else {
    Local<Value> js_len;
    if (!fn->Get(env->context(),
                 FIXED_ONE_BYTE_STRING(env->isolate(), "length"))
             .ToLocal(&js_len)) {
      return;
    }
    argc = js_len.As<Int32>()->Value();
  }

  UserDefinedFunction* user_data =
      new UserDefinedFunction(env, fn, use_bigint_args);
  int text_rep = SQLITE_UTF8;

  if (deterministic) {
    text_rep |= SQLITE_DETERMINISTIC;
  }

  if (direct_only) {
    text_rep |= SQLITE_DIRECTONLY;
  }

  int r = sqlite3_create_function_v2(db->connection_,
                                     *name,
                                     argc,
                                     text_rep,
                                     user_data,
                                     UserDefinedFunction::xFunc,
                                     nullptr,
                                     nullptr,
                                     UserDefinedFunction::xDestroy);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
}

void DatabaseSync::CreateSession(const FunctionCallbackInfo<Value>& args) {
  std::string table;
  std::string db_name = "main";

  Environment* env = Environment::GetCurrent(args);
  if (args.Length() > 0) {
    if (!args[0]->IsObject()) {
      THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                                 "The \"options\" argument must be an object.");
      return;
    }

    Local<Object> options = args[0].As<Object>();

    Local<String> table_key = FIXED_ONE_BYTE_STRING(env->isolate(), "table");
    if (options->HasOwnProperty(env->context(), table_key).FromJust()) {
      Local<Value> table_value;
      if (!options->Get(env->context(), table_key).ToLocal(&table_value)) {
        return;
      }

      if (table_value->IsString()) {
        String::Utf8Value str(env->isolate(), table_value);
        table = *str;
      } else {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(), "The \"options.table\" argument must be a string.");
        return;
      }
    }

    Local<String> db_key =
        String::NewFromUtf8(env->isolate(), "db", NewStringType::kNormal)
            .ToLocalChecked();
    if (options->HasOwnProperty(env->context(), db_key).FromJust()) {
      Local<Value> db_value =
          options->Get(env->context(), db_key).ToLocalChecked();
      if (db_value->IsString()) {
        String::Utf8Value str(env->isolate(), db_value);
        db_name = std::string(*str);
      } else {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(), "The \"options.db\" argument must be a string.");
        return;
      }
    }
  }

  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  THROW_AND_RETURN_ON_BAD_STATE(env, !db->IsOpen(), "database is not open");

  sqlite3_session* pSession;
  int r = sqlite3session_create(db->connection_, db_name.c_str(), &pSession);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
  db->sessions_.insert(pSession);

  r = sqlite3session_attach(pSession, table == "" ? nullptr : table.c_str());
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());

  BaseObjectPtr<Session> session =
      Session::Create(env, BaseObjectWeakPtr<DatabaseSync>(db), pSession);
  args.GetReturnValue().Set(session->object());
}

// the reason for using static functions here is that SQLite needs a
// function pointer
static std::function<int(int)> conflictCallback;

static int xConflict(void* pCtx, int eConflict, sqlite3_changeset_iter* pIter) {
  if (!conflictCallback) return SQLITE_CHANGESET_ABORT;
  return conflictCallback(eConflict);
}

static std::function<bool(std::string)> filterCallback;

static int xFilter(void* pCtx, const char* zTab) {
  if (!filterCallback) return 1;

  return filterCallback(zTab) ? 1 : 0;
}

void DatabaseSync::ApplyChangeset(const FunctionCallbackInfo<Value>& args) {
  conflictCallback = nullptr;
  filterCallback = nullptr;

  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(env, !db->IsOpen(), "database is not open");

  if (!args[0]->IsUint8Array()) {
    THROW_ERR_INVALID_ARG_TYPE(
        env->isolate(), "The \"changeset\" argument must be a Uint8Array.");
    return;
  }

  if (args.Length() > 1 && !args[1]->IsUndefined()) {
    if (!args[1]->IsObject()) {
      THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                                 "The \"options\" argument must be an object.");
      return;
    }

    Local<Object> options = args[1].As<Object>();
    Local<Value> conflictValue =
        options->Get(env->context(), env->onconflict_string()).ToLocalChecked();

    if (!conflictValue->IsUndefined()) {
      if (!conflictValue->IsFunction()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.onConflict\" argument must be a function.");
        return;
      }
      Local<Function> conflictFunc = conflictValue.As<Function>();
      conflictCallback = [env, conflictFunc](int conflictType) -> int {
        Local<Value> argv[] = {Integer::New(env->isolate(), conflictType)};
        TryCatch try_catch(env->isolate());
        Local<Value> result =
            conflictFunc->Call(env->context(), Null(env->isolate()), 1, argv)
                .FromMaybe(Local<Value>());
        if (try_catch.HasCaught()) {
          try_catch.ReThrow();
          return SQLITE_CHANGESET_ABORT;
        }
        constexpr auto invalid_value = -1;
        if (!result->IsInt32()) return invalid_value;
        return result->Int32Value(env->context()).FromJust();
      };
    }

    if (options->HasOwnProperty(env->context(), env->filter_string())
            .FromJust()) {
      Local<Value> filterValue =
          options->Get(env->context(), env->filter_string()).ToLocalChecked();

      if (!filterValue->IsFunction()) {
        THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(),
            "The \"options.filter\" argument must be a function.");
        return;
      }

      Local<Function> filterFunc = filterValue.As<Function>();

      filterCallback = [env, filterFunc](std::string item) -> bool {
        Local<Value> argv[] = {String::NewFromUtf8(env->isolate(),
                                                   item.c_str(),
                                                   NewStringType::kNormal)
                                   .ToLocalChecked()};
        Local<Value> result =
            filterFunc->Call(env->context(), Null(env->isolate()), 1, argv)
                .ToLocalChecked();
        return result->BooleanValue(env->isolate());
      };
    }
  }

  ArrayBufferViewContents<uint8_t> buf(args[0]);
  int r = sqlite3changeset_apply(
      db->connection_,
      buf.length(),
      const_cast<void*>(static_cast<const void*>(buf.data())),
      xFilter,
      xConflict,
      nullptr);
  if (r == SQLITE_OK) {
    args.GetReturnValue().Set(true);
    return;
  }
  if (r == SQLITE_ABORT) {
    // this is not an error, return false
    args.GetReturnValue().Set(false);
    return;
  }
  THROW_ERR_SQLITE_ERROR(env->isolate(), r);
}

void DatabaseSync::EnableLoadExtension(
    const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  if (!args[0]->IsBoolean()) {
    THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                               "The \"allow\" argument must be a boolean.");
    return;
  }

  const int enable = args[0].As<Boolean>()->Value();
  auto isolate = env->isolate();

  if (db->allow_load_extension_ == false && enable == true) {
    THROW_ERR_INVALID_STATE(
        isolate,
        "Cannot enable extension loading because it was disabled at database "
        "creation.");
    return;
  }
  db->enable_load_extension_ = enable;
  const int load_extension_ret = sqlite3_db_config(
      db->connection_, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, enable, nullptr);
  CHECK_ERROR_OR_THROW(
      isolate, db->connection_, load_extension_ret, SQLITE_OK, void());
}

void DatabaseSync::LoadExtension(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, db->connection_ == nullptr, "database is not open");
  THROW_AND_RETURN_ON_BAD_STATE(
      env, !db->allow_load_extension_, "extension loading is not allowed");
  THROW_AND_RETURN_ON_BAD_STATE(
      env, !db->enable_load_extension_, "extension loading is not allowed");

  if (!args[0]->IsString()) {
    THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                               "The \"path\" argument must be a string.");
    return;
  }

  auto isolate = env->isolate();

  BufferValue path(isolate, args[0]);
  BufferValue entryPoint(isolate, args[1]);
  CHECK_NOT_NULL(*path);
  ToNamespacedPath(env, &path);
  if (*entryPoint == nullptr) {
    ToNamespacedPath(env, &entryPoint);
  }
  THROW_IF_INSUFFICIENT_PERMISSIONS(
      env, permission::PermissionScope::kFileSystemRead, path.ToStringView());
  char* errmsg = nullptr;
  const int r =
      sqlite3_load_extension(db->connection_, *path, *entryPoint, &errmsg);
  if (r != SQLITE_OK) {
    isolate->ThrowException(ERR_LOAD_SQLITE_EXTENSION(isolate, errmsg));
  }
}

StatementSync::StatementSync(Environment* env,
                             Local<Object> object,
                             DatabaseSync* db,
                             sqlite3_stmt* stmt)
    : BaseObject(env, object) {
  MakeWeak();
  db_ = db;
  statement_ = stmt;
  // In the future, some of these options could be set at the database
  // connection level and inherited by statements to reduce boilerplate.
  use_big_ints_ = false;
  allow_bare_named_params_ = true;
  bare_named_params_ = std::nullopt;
}

StatementSync::~StatementSync() {
  if (!IsFinalized()) {
    db_->UntrackStatement(this);
    Finalize();
  }
}

void StatementSync::Finalize() {
  sqlite3_finalize(statement_);
  statement_ = nullptr;
}

inline bool StatementSync::IsFinalized() {
  return statement_ == nullptr;
}

bool StatementSync::BindParams(const FunctionCallbackInfo<Value>& args) {
  int r = sqlite3_clear_bindings(statement_);
  CHECK_ERROR_OR_THROW(
      env()->isolate(), db_->Connection(), r, SQLITE_OK, false);

  int anon_idx = 1;
  int anon_start = 0;

  if (args[0]->IsObject() && !args[0]->IsArrayBufferView()) {
    Local<Object> obj = args[0].As<Object>();
    Local<Context> context = obj->GetIsolate()->GetCurrentContext();
    Local<Array> keys;
    if (!obj->GetOwnPropertyNames(context).ToLocal(&keys)) {
      return false;
    }

    if (allow_bare_named_params_ && !bare_named_params_.has_value()) {
      bare_named_params_.emplace();
      int param_count = sqlite3_bind_parameter_count(statement_);
      // Parameter indexing starts at one.
      for (int i = 1; i <= param_count; ++i) {
        const char* name = sqlite3_bind_parameter_name(statement_, i);
        if (name == nullptr) {
          continue;
        }

        auto bare_name = std::string(name + 1);
        auto full_name = std::string(name);
        auto insertion = bare_named_params_->insert({bare_name, full_name});
        if (insertion.second == false) {
          auto existing_full_name = (*insertion.first).second;
          if (full_name != existing_full_name) {
            THROW_ERR_INVALID_STATE(
                env(),
                "Cannot create bare named parameter '%s' because of "
                "conflicting names '%s' and '%s'.",
                bare_name,
                existing_full_name,
                full_name);
            return false;
          }
        }
      }
    }

    uint32_t len = keys->Length();
    for (uint32_t j = 0; j < len; j++) {
      Local<Value> key;
      if (!keys->Get(context, j).ToLocal(&key)) {
        return false;
      }

      Utf8Value utf8_key(env()->isolate(), key);
      int r = sqlite3_bind_parameter_index(statement_, *utf8_key);
      if (r == 0) {
        if (allow_bare_named_params_) {
          auto lookup = bare_named_params_->find(std::string(*utf8_key));
          if (lookup != bare_named_params_->end()) {
            r = sqlite3_bind_parameter_index(statement_,
                                             lookup->second.c_str());
          }
        }

        if (r == 0) {
          THROW_ERR_INVALID_STATE(
              env(), "Unknown named parameter '%s'", *utf8_key);
          return false;
        }
      }

      Local<Value> value;
      if (!obj->Get(context, key).ToLocal(&value)) {
        return false;
      }

      if (!BindValue(value, r)) {
        return false;
      }
    }
    anon_start++;
  }

  for (int i = anon_start; i < args.Length(); ++i) {
    while (sqlite3_bind_parameter_name(statement_, anon_idx) != nullptr) {
      anon_idx++;
    }

    if (!BindValue(args[i], anon_idx)) {
      return false;
    }

    anon_idx++;
  }

  return true;
}

bool StatementSync::BindValue(const Local<Value>& value, const int index) {
  // SQLite only supports a subset of JavaScript types. Some JS types such as
  // functions don't make sense to support. Other JS types such as booleans and
  // Dates could be supported by converting them to numbers. However, there
  // would not be a good way to read the values back from SQLite with the
  // original type.
  int r;
  if (value->IsNumber()) {
    double val = value.As<Number>()->Value();
    r = sqlite3_bind_double(statement_, index, val);
  } else if (value->IsString()) {
    Utf8Value val(env()->isolate(), value.As<String>());
    r = sqlite3_bind_text(
        statement_, index, *val, val.length(), SQLITE_TRANSIENT);
  } else if (value->IsNull()) {
    r = sqlite3_bind_null(statement_, index);
  } else if (value->IsArrayBufferView()) {
    ArrayBufferViewContents<uint8_t> buf(value);
    r = sqlite3_bind_blob(
        statement_, index, buf.data(), buf.length(), SQLITE_TRANSIENT);
  } else if (value->IsBigInt()) {
    bool lossless;
    int64_t as_int = value.As<BigInt>()->Int64Value(&lossless);
    if (!lossless) {
      THROW_ERR_INVALID_ARG_VALUE(env(), "BigInt value is too large to bind.");
      return false;
    }
    r = sqlite3_bind_int64(statement_, index, as_int);
  } else {
    THROW_ERR_INVALID_ARG_TYPE(
        env()->isolate(),
        "Provided value cannot be bound to SQLite parameter %d.",
        index);
    return false;
  }

  CHECK_ERROR_OR_THROW(
      env()->isolate(), db_->Connection(), r, SQLITE_OK, false);
  return true;
}

MaybeLocal<Value> StatementSync::ColumnToValue(const int column) {
  switch (sqlite3_column_type(statement_, column)) {
    case SQLITE_INTEGER: {
      sqlite3_int64 value = sqlite3_column_int64(statement_, column);
      if (use_big_ints_) {
        return BigInt::New(env()->isolate(), value);
      } else if (std::abs(value) <= kMaxSafeJsInteger) {
        return Number::New(env()->isolate(), value);
      } else {
        THROW_ERR_OUT_OF_RANGE(env()->isolate(),
                               "The value of column %d is too large to be "
                               "represented as a JavaScript number: %" PRId64,
                               column,
                               value);
        return MaybeLocal<Value>();
      }
    }
    case SQLITE_FLOAT:
      return Number::New(env()->isolate(),
                         sqlite3_column_double(statement_, column));
    case SQLITE_TEXT: {
      const char* value = reinterpret_cast<const char*>(
          sqlite3_column_text(statement_, column));
      return String::NewFromUtf8(env()->isolate(), value).As<Value>();
    }
    case SQLITE_NULL:
      return Null(env()->isolate());
    case SQLITE_BLOB: {
      size_t size =
          static_cast<size_t>(sqlite3_column_bytes(statement_, column));
      auto data = reinterpret_cast<const uint8_t*>(
          sqlite3_column_blob(statement_, column));
      auto store = ArrayBuffer::NewBackingStore(env()->isolate(), size);
      memcpy(store->Data(), data, size);
      auto ab = ArrayBuffer::New(env()->isolate(), std::move(store));
      return Uint8Array::New(ab, 0, size);
    }
    default:
      UNREACHABLE("Bad SQLite column type");
  }
}

MaybeLocal<Name> StatementSync::ColumnNameToName(const int column) {
  const char* col_name = sqlite3_column_name(statement_, column);
  if (col_name == nullptr) {
    THROW_ERR_INVALID_STATE(env(), "Cannot get name of column %d", column);
    return MaybeLocal<Name>();
  }

  return String::NewFromUtf8(env()->isolate(), col_name).As<Name>();
}

void StatementSync::MemoryInfo(MemoryTracker* tracker) const {}

void StatementSync::All(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");
  Isolate* isolate = env->isolate();
  int r = sqlite3_reset(stmt->statement_);
  CHECK_ERROR_OR_THROW(isolate, stmt->db_->Connection(), r, SQLITE_OK, void());

  if (!stmt->BindParams(args)) {
    return;
  }

  auto reset = OnScopeLeave([&]() { sqlite3_reset(stmt->statement_); });
  int num_cols = sqlite3_column_count(stmt->statement_);
  LocalVector<Value> rows(isolate);
  while ((r = sqlite3_step(stmt->statement_)) == SQLITE_ROW) {
    LocalVector<Name> row_keys(isolate);
    row_keys.reserve(num_cols);
    LocalVector<Value> row_values(isolate);
    row_values.reserve(num_cols);

    for (int i = 0; i < num_cols; ++i) {
      Local<Name> key;
      if (!stmt->ColumnNameToName(i).ToLocal(&key)) return;
      Local<Value> val;
      if (!stmt->ColumnToValue(i).ToLocal(&val)) return;
      row_keys.emplace_back(key);
      row_values.emplace_back(val);
    }

    Local<Object> row = Object::New(
        isolate, Null(isolate), row_keys.data(), row_values.data(), num_cols);
    rows.emplace_back(row);
  }

  CHECK_ERROR_OR_THROW(
      isolate, stmt->db_->Connection(), r, SQLITE_DONE, void());
  args.GetReturnValue().Set(Array::New(isolate, rows.data(), rows.size()));
}

void StatementSync::IterateReturnCallback(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  auto isolate = env->isolate();
  auto context = isolate->GetCurrentContext();

  auto self = args.This();
  // iterator has fetch all result or break, prevent next func to return result
  self->Set(context, env->isfinished_string(), Boolean::New(isolate, true))
      .ToChecked();

  auto external_stmt = Local<External>::Cast(
      self->Get(context, env->statement_string()).ToLocalChecked());
  auto stmt = static_cast<StatementSync*>(external_stmt->Value());
  if (!stmt->IsFinalized()) {
    sqlite3_reset(stmt->statement_);
  }

  LocalVector<Name> keys(isolate, {env->done_string(), env->value_string()});
  LocalVector<Value> values(isolate,
                            {Boolean::New(isolate, true), Null(isolate)});

  DCHECK_EQ(keys.size(), values.size());
  Local<Object> result = Object::New(
      isolate, Null(isolate), keys.data(), values.data(), keys.size());
  args.GetReturnValue().Set(result);
}

void StatementSync::IterateNextCallback(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  auto isolate = env->isolate();
  auto context = isolate->GetCurrentContext();

  auto self = args.This();

  // skip iteration if is_finished
  auto is_finished = Local<Boolean>::Cast(
      self->Get(context, env->isfinished_string()).ToLocalChecked());
  if (is_finished->Value()) {
    LocalVector<Name> keys(isolate, {env->done_string(), env->value_string()});
    LocalVector<Value> values(isolate,
                              {Boolean::New(isolate, true), Null(isolate)});

    DCHECK_EQ(keys.size(), values.size());
    Local<Object> result = Object::New(
        isolate, Null(isolate), keys.data(), values.data(), keys.size());
    args.GetReturnValue().Set(result);
    return;
  }

  auto external_stmt = Local<External>::Cast(
      self->Get(context, env->statement_string()).ToLocalChecked());
  auto stmt = static_cast<StatementSync*>(external_stmt->Value());
  auto num_cols =
      Local<Integer>::Cast(
          self->Get(context, env->num_cols_string()).ToLocalChecked())
          ->Value();

  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");

  int r = sqlite3_step(stmt->statement_);
  if (r != SQLITE_ROW) {
    CHECK_ERROR_OR_THROW(
        env->isolate(), stmt->db_->Connection(), r, SQLITE_DONE, void());

    // cleanup when no more rows to fetch
    sqlite3_reset(stmt->statement_);
    self->Set(context, env->isfinished_string(), Boolean::New(isolate, true))
        .ToChecked();

    LocalVector<Name> keys(isolate, {env->done_string(), env->value_string()});
    LocalVector<Value> values(isolate,
                              {Boolean::New(isolate, true), Null(isolate)});

    DCHECK_EQ(keys.size(), values.size());
    Local<Object> result = Object::New(
        isolate, Null(isolate), keys.data(), values.data(), keys.size());
    args.GetReturnValue().Set(result);
    return;
  }

  LocalVector<Name> row_keys(isolate);
  row_keys.reserve(num_cols);
  LocalVector<Value> row_values(isolate);
  row_values.reserve(num_cols);
  for (int i = 0; i < num_cols; ++i) {
    Local<Name> key;
    if (!stmt->ColumnNameToName(i).ToLocal(&key)) return;
    Local<Value> val;
    if (!stmt->ColumnToValue(i).ToLocal(&val)) return;
    row_keys.emplace_back(key);
    row_values.emplace_back(val);
  }

  Local<Object> row = Object::New(
      isolate, Null(isolate), row_keys.data(), row_values.data(), num_cols);

  LocalVector<Name> keys(isolate, {env->done_string(), env->value_string()});
  LocalVector<Value> values(isolate, {Boolean::New(isolate, false), row});

  DCHECK_EQ(keys.size(), values.size());
  Local<Object> result = Object::New(
      isolate, Null(isolate), keys.data(), values.data(), keys.size());
  args.GetReturnValue().Set(result);
}

void StatementSync::Iterate(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");
  auto isolate = env->isolate();
  auto context = env->context();
  int r = sqlite3_reset(stmt->statement_);
  CHECK_ERROR_OR_THROW(
      env->isolate(), stmt->db_->Connection(), r, SQLITE_OK, void());

  if (!stmt->BindParams(args)) {
    return;
  }

  Local<Function> next_func =
      Function::New(context, StatementSync::IterateNextCallback)
          .ToLocalChecked();
  Local<Function> return_func =
      Function::New(context, StatementSync::IterateReturnCallback)
          .ToLocalChecked();

  LocalVector<Name> keys(isolate, {env->next_string(), env->return_string()});
  LocalVector<Value> values(isolate, {next_func, return_func});

  Local<Object> global = context->Global();
  Local<Value> js_iterator;
  Local<Value> js_iterator_prototype;
  if (!global->Get(context, env->iterator_string()).ToLocal(&js_iterator))
    return;
  if (!js_iterator.As<Object>()
           ->Get(context, env->prototype_string())
           .ToLocal(&js_iterator_prototype))
    return;

  DCHECK_EQ(keys.size(), values.size());
  Local<Object> iterable_iterator = Object::New(
      isolate, js_iterator_prototype, keys.data(), values.data(), keys.size());

  auto num_cols_pd = v8::PropertyDescriptor(
      v8::Integer::New(isolate, sqlite3_column_count(stmt->statement_)), false);
  num_cols_pd.set_enumerable(false);
  num_cols_pd.set_configurable(false);
  iterable_iterator
      ->DefineProperty(context, env->num_cols_string(), num_cols_pd)
      .ToChecked();

  auto stmt_pd =
      v8::PropertyDescriptor(v8::External::New(isolate, stmt), false);
  stmt_pd.set_enumerable(false);
  stmt_pd.set_configurable(false);
  iterable_iterator->DefineProperty(context, env->statement_string(), stmt_pd)
      .ToChecked();

  auto is_finished_pd =
      v8::PropertyDescriptor(v8::Boolean::New(isolate, false), true);
  stmt_pd.set_enumerable(false);
  stmt_pd.set_configurable(false);
  iterable_iterator
      ->DefineProperty(context, env->isfinished_string(), is_finished_pd)
      .ToChecked();

  args.GetReturnValue().Set(iterable_iterator);
}

void StatementSync::Get(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");
  Isolate* isolate = env->isolate();
  int r = sqlite3_reset(stmt->statement_);
  CHECK_ERROR_OR_THROW(isolate, stmt->db_->Connection(), r, SQLITE_OK, void());

  if (!stmt->BindParams(args)) {
    return;
  }

  auto reset = OnScopeLeave([&]() { sqlite3_reset(stmt->statement_); });
  r = sqlite3_step(stmt->statement_);
  if (r == SQLITE_DONE) return;
  if (r != SQLITE_ROW) {
    THROW_ERR_SQLITE_ERROR(isolate, stmt->db_->Connection());
    return;
  }

  int num_cols = sqlite3_column_count(stmt->statement_);
  if (num_cols == 0) {
    return;
  }

  LocalVector<Name> keys(isolate);
  keys.reserve(num_cols);
  LocalVector<Value> values(isolate);
  values.reserve(num_cols);

  for (int i = 0; i < num_cols; ++i) {
    Local<Name> key;
    if (!stmt->ColumnNameToName(i).ToLocal(&key)) return;
    Local<Value> val;
    if (!stmt->ColumnToValue(i).ToLocal(&val)) return;
    keys.emplace_back(key);
    values.emplace_back(val);
  }

  Local<Object> result =
      Object::New(isolate, Null(isolate), keys.data(), values.data(), num_cols);

  args.GetReturnValue().Set(result);
}

void StatementSync::Run(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");
  int r = sqlite3_reset(stmt->statement_);
  CHECK_ERROR_OR_THROW(
      env->isolate(), stmt->db_->Connection(), r, SQLITE_OK, void());

  if (!stmt->BindParams(args)) {
    return;
  }

  auto reset = OnScopeLeave([&]() { sqlite3_reset(stmt->statement_); });
  r = sqlite3_step(stmt->statement_);
  if (r != SQLITE_ROW && r != SQLITE_DONE) {
    THROW_ERR_SQLITE_ERROR(env->isolate(), stmt->db_->Connection());
    return;
  }

  Local<Object> result = Object::New(env->isolate());
  sqlite3_int64 last_insert_rowid =
      sqlite3_last_insert_rowid(stmt->db_->Connection());
  sqlite3_int64 changes = sqlite3_changes64(stmt->db_->Connection());
  Local<Value> last_insert_rowid_val;
  Local<Value> changes_val;

  if (stmt->use_big_ints_) {
    last_insert_rowid_val = BigInt::New(env->isolate(), last_insert_rowid);
    changes_val = BigInt::New(env->isolate(), changes);
  } else {
    last_insert_rowid_val = Number::New(env->isolate(), last_insert_rowid);
    changes_val = Number::New(env->isolate(), changes);
  }

  if (result
          ->Set(env->context(),
                env->last_insert_rowid_string(),
                last_insert_rowid_val)
          .IsNothing() ||
      result->Set(env->context(), env->changes_string(), changes_val)
          .IsNothing()) {
    return;
  }

  args.GetReturnValue().Set(result);
}

void StatementSync::SourceSQLGetter(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");
  Local<String> sql;
  if (!String::NewFromUtf8(env->isolate(), sqlite3_sql(stmt->statement_))
           .ToLocal(&sql)) {
    return;
  }
  args.GetReturnValue().Set(sql);
}

void StatementSync::ExpandedSQLGetter(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");

  // sqlite3_expanded_sql may return nullptr without producing an error code.
  char* expanded = sqlite3_expanded_sql(stmt->statement_);
  if (expanded == nullptr) {
    return THROW_ERR_SQLITE_ERROR(
        env->isolate(), "Expanded SQL text would exceed configured limits");
  }
  auto maybe_expanded = String::NewFromUtf8(env->isolate(), expanded);
  sqlite3_free(expanded);
  Local<String> result;
  if (!maybe_expanded.ToLocal(&result)) {
    return;
  }
  args.GetReturnValue().Set(result);
}

void StatementSync::SetAllowBareNamedParameters(
    const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");

  if (!args[0]->IsBoolean()) {
    THROW_ERR_INVALID_ARG_TYPE(
        env->isolate(),
        "The \"allowBareNamedParameters\" argument must be a boolean.");
    return;
  }

  stmt->allow_bare_named_params_ = args[0]->IsTrue();
}

void StatementSync::SetReadBigInts(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, stmt->IsFinalized(), "statement has been finalized");

  if (!args[0]->IsBoolean()) {
    THROW_ERR_INVALID_ARG_TYPE(
        env->isolate(), "The \"readBigInts\" argument must be a boolean.");
    return;
  }

  stmt->use_big_ints_ = args[0]->IsTrue();
}

void IllegalConstructor(const FunctionCallbackInfo<Value>& args) {
  THROW_ERR_ILLEGAL_CONSTRUCTOR(Environment::GetCurrent(args));
}

static inline void SetSideEffectFreeGetter(
    Isolate* isolate,
    Local<FunctionTemplate> class_template,
    Local<String> name,
    FunctionCallback fn) {
  Local<FunctionTemplate> getter =
      FunctionTemplate::New(isolate,
                            fn,
                            Local<Value>(),
                            v8::Signature::New(isolate, class_template),
                            /* length */ 0,
                            ConstructorBehavior::kThrow,
                            SideEffectType::kHasNoSideEffect);
  class_template->InstanceTemplate()->SetAccessorProperty(
      name, getter, Local<FunctionTemplate>(), DontDelete);
}

Local<FunctionTemplate> StatementSync::GetConstructorTemplate(
    Environment* env) {
  Local<FunctionTemplate> tmpl =
      env->sqlite_statement_sync_constructor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, IllegalConstructor);
    tmpl->SetClassName(FIXED_ONE_BYTE_STRING(isolate, "StatementSync"));
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        StatementSync::kInternalFieldCount);
    SetProtoMethod(isolate, tmpl, "iterate", StatementSync::Iterate);
    SetProtoMethod(isolate, tmpl, "all", StatementSync::All);
    SetProtoMethod(isolate, tmpl, "get", StatementSync::Get);
    SetProtoMethod(isolate, tmpl, "run", StatementSync::Run);
    SetSideEffectFreeGetter(isolate,
                            tmpl,
                            FIXED_ONE_BYTE_STRING(isolate, "sourceSQL"),
                            StatementSync::SourceSQLGetter);
    SetSideEffectFreeGetter(isolate,
                            tmpl,
                            FIXED_ONE_BYTE_STRING(isolate, "expandedSQL"),
                            StatementSync::ExpandedSQLGetter);
    SetProtoMethod(isolate,
                   tmpl,
                   "setAllowBareNamedParameters",
                   StatementSync::SetAllowBareNamedParameters);
    SetProtoMethod(
        isolate, tmpl, "setReadBigInts", StatementSync::SetReadBigInts);
    env->set_sqlite_statement_sync_constructor_template(tmpl);
  }
  return tmpl;
}

BaseObjectPtr<StatementSync> StatementSync::Create(Environment* env,
                                                   DatabaseSync* db,
                                                   sqlite3_stmt* stmt) {
  Local<Object> obj;
  if (!GetConstructorTemplate(env)
           ->InstanceTemplate()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return BaseObjectPtr<StatementSync>();
  }

  return MakeBaseObject<StatementSync>(env, obj, db, stmt);
}

Session::Session(Environment* env,
                 Local<Object> object,
                 BaseObjectWeakPtr<DatabaseSync> database,
                 sqlite3_session* session)
    : BaseObject(env, object),
      session_(session),
      database_(std::move(database)) {
  MakeWeak();
}

Session::~Session() {
  Delete();
}

BaseObjectPtr<Session> Session::Create(Environment* env,
                                       BaseObjectWeakPtr<DatabaseSync> database,
                                       sqlite3_session* session) {
  Local<Object> obj;
  if (!GetConstructorTemplate(env)
           ->InstanceTemplate()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return BaseObjectPtr<Session>();
  }

  return MakeBaseObject<Session>(env, obj, std::move(database), session);
}

Local<FunctionTemplate> Session::GetConstructorTemplate(Environment* env) {
  Local<FunctionTemplate> tmpl = env->sqlite_session_constructor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, IllegalConstructor);
    tmpl->SetClassName(FIXED_ONE_BYTE_STRING(env->isolate(), "Session"));
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        Session::kInternalFieldCount);
    SetProtoMethod(isolate,
                   tmpl,
                   "changeset",
                   Session::Changeset<sqlite3session_changeset>);
    SetProtoMethod(
        isolate, tmpl, "patchset", Session::Changeset<sqlite3session_patchset>);
    SetProtoMethod(isolate, tmpl, "close", Session::Close);
    env->set_sqlite_session_constructor_template(tmpl);
  }
  return tmpl;
}

void Session::MemoryInfo(MemoryTracker* tracker) const {}

template <Sqlite3ChangesetGenFunc sqliteChangesetFunc>
void Session::Changeset(const FunctionCallbackInfo<Value>& args) {
  Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  Environment* env = Environment::GetCurrent(args);
  sqlite3* db = session->database_ ? session->database_->connection_ : nullptr;
  THROW_AND_RETURN_ON_BAD_STATE(
      env, !session->database_->IsOpen(), "database is not open");
  THROW_AND_RETURN_ON_BAD_STATE(
      env, session->session_ == nullptr, "session is not open");

  int nChangeset;
  void* pChangeset;
  int r = sqliteChangesetFunc(session->session_, &nChangeset, &pChangeset);
  CHECK_ERROR_OR_THROW(env->isolate(), db, r, SQLITE_OK, void());

  auto freeChangeset = OnScopeLeave([&] { sqlite3_free(pChangeset); });

  Local<ArrayBuffer> buffer = ArrayBuffer::New(env->isolate(), nChangeset);
  std::memcpy(buffer->GetBackingStore()->Data(), pChangeset, nChangeset);
  Local<Uint8Array> uint8Array = Uint8Array::New(buffer, 0, nChangeset);

  args.GetReturnValue().Set(uint8Array);
}

void Session::Close(const FunctionCallbackInfo<Value>& args) {
  Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, !session->database_->IsOpen(), "database is not open");
  THROW_AND_RETURN_ON_BAD_STATE(
      env, session->session_ == nullptr, "session is not open");

  session->Delete();
}

void Session::Delete() {
  if (!database_ || !database_->connection_ || session_ == nullptr) return;
  sqlite3session_delete(session_);
  database_->sessions_.erase(session_);
  session_ = nullptr;
}

void DefineConstants(Local<Object> target) {
  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_OMIT);
  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_REPLACE);
  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_ABORT);

  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_DATA);
  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_NOTFOUND);
  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_CONFLICT);
  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_CONSTRAINT);
  NODE_DEFINE_CONSTANT(target, SQLITE_CHANGESET_FOREIGN_KEY);
}

static void Initialize(Local<Object> target,
                       Local<Value> unused,
                       Local<Context> context,
                       void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();
  Local<FunctionTemplate> db_tmpl =
      NewFunctionTemplate(isolate, DatabaseSync::New);
  db_tmpl->InstanceTemplate()->SetInternalFieldCount(
      DatabaseSync::kInternalFieldCount);
  Local<Object> constants = Object::New(isolate);

  DefineConstants(constants);

  SetProtoMethod(isolate, db_tmpl, "open", DatabaseSync::Open);
  SetProtoMethod(isolate, db_tmpl, "close", DatabaseSync::Close);
  SetProtoMethod(isolate, db_tmpl, "prepare", DatabaseSync::Prepare);
  SetProtoMethod(isolate, db_tmpl, "exec", DatabaseSync::Exec);
  SetProtoMethod(isolate, db_tmpl, "function", DatabaseSync::CustomFunction);
  SetProtoMethod(
      isolate, db_tmpl, "createSession", DatabaseSync::CreateSession);
  SetProtoMethod(
      isolate, db_tmpl, "applyChangeset", DatabaseSync::ApplyChangeset);
  SetProtoMethod(isolate,
                 db_tmpl,
                 "enableLoadExtension",
                 DatabaseSync::EnableLoadExtension);
  SetProtoMethod(
      isolate, db_tmpl, "loadExtension", DatabaseSync::LoadExtension);
  SetConstructorFunction(context, target, "DatabaseSync", db_tmpl);
  SetConstructorFunction(context,
                         target,
                         "StatementSync",
                         StatementSync::GetConstructorTemplate(env));

  target->Set(context, env->constants_string(), constants).Check();
}

}  // namespace sqlite
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(sqlite, node::sqlite::Initialize)
