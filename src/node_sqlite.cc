#include "node_sqlite.h"
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
using v8::Context;
using v8::Exception;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
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
      node::THROW_ERR_INVALID_STATE((env), (msg));                             \
      return;                                                                  \
    }                                                                          \
  } while (0)

inline Local<Value> CreateSQLiteError(Isolate* isolate, sqlite3* db) {
  int errcode = sqlite3_extended_errcode(db);
  const char* errstr = sqlite3_errstr(errcode);
  const char* errmsg = sqlite3_errmsg(db);
  Local<String> js_msg = String::NewFromUtf8(isolate, errmsg).ToLocalChecked();
  Local<Object> e = Exception::Error(js_msg)
                        ->ToObject(isolate->GetCurrentContext())
                        .ToLocalChecked();
  e->Set(isolate->GetCurrentContext(),
         OneByteString(isolate, "code"),
         OneByteString(isolate, "ERR_SQLITE_ERROR"))
      .Check();
  e->Set(isolate->GetCurrentContext(),
         OneByteString(isolate, "errcode"),
         Integer::New(isolate, errcode))
      .Check();
  e->Set(isolate->GetCurrentContext(),
         OneByteString(isolate, "errstr"),
         String::NewFromUtf8(isolate, errstr).ToLocalChecked())
      .Check();
  return e;
}

inline void THROW_ERR_SQLITE_ERROR(Isolate* isolate, sqlite3* db) {
  isolate->ThrowException(CreateSQLiteError(isolate, db));
}

DatabaseSync::DatabaseSync(Environment* env,
                           Local<Object> object,
                           Local<String> location,
                           bool open)
    : BaseObject(env, object) {
  MakeWeak();
  node::Utf8Value utf8_location(env->isolate(), location);
  location_ = utf8_location.ToString();
  connection_ = nullptr;

  if (open) {
    Open();
  }
}

DatabaseSync::~DatabaseSync() {
  sqlite3_close_v2(connection_);
  connection_ = nullptr;
}

void DatabaseSync::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("location", location_);
}

bool DatabaseSync::Open() {
  if (connection_ != nullptr) {
    node::THROW_ERR_INVALID_STATE(env(), "database is already open");
    return false;
  }

  // TODO(cjihrig): Support additional flags.
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  int r = sqlite3_open_v2(location_.c_str(), &connection_, flags, nullptr);
  CHECK_ERROR_OR_THROW(env()->isolate(), connection_, r, SQLITE_OK, false);
  return true;
}

void DatabaseSync::New(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  if (!args.IsConstructCall()) {
    THROW_ERR_CONSTRUCT_CALL_REQUIRED(env);
    return;
  }

  if (!args[0]->IsString()) {
    node::THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                                     "The \"path\" argument must be a string.");
    return;
  }

  bool open = true;

  if (args.Length() > 1) {
    if (!args[1]->IsObject()) {
      node::THROW_ERR_INVALID_ARG_TYPE(
          env->isolate(), "The \"options\" argument must be an object.");
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
        node::THROW_ERR_INVALID_ARG_TYPE(
            env->isolate(), "The \"options.open\" argument must be a boolean.");
        return;
      }
      open = open_v.As<Boolean>()->Value();
    }
  }

  new DatabaseSync(env, args.This(), args[0].As<String>(), open);
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
  THROW_AND_RETURN_ON_BAD_STATE(
      env, db->connection_ == nullptr, "database is not open");
  int r = sqlite3_close_v2(db->connection_);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
  db->connection_ = nullptr;
}

void DatabaseSync::Prepare(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, db->connection_ == nullptr, "database is not open");

  if (!args[0]->IsString()) {
    node::THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                                     "The \"sql\" argument must be a string.");
    return;
  }

  auto sql = node::Utf8Value(env->isolate(), args[0].As<String>());
  sqlite3_stmt* s = nullptr;
  int r = sqlite3_prepare_v2(db->connection_, *sql, -1, &s, 0);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
  BaseObjectPtr<StatementSync> stmt =
      StatementSync::Create(env, db->connection_, s);
  args.GetReturnValue().Set(stmt->object());
}

void DatabaseSync::Exec(const FunctionCallbackInfo<Value>& args) {
  DatabaseSync* db;
  ASSIGN_OR_RETURN_UNWRAP(&db, args.This());
  Environment* env = Environment::GetCurrent(args);
  THROW_AND_RETURN_ON_BAD_STATE(
      env, db->connection_ == nullptr, "database is not open");

  if (!args[0]->IsString()) {
    node::THROW_ERR_INVALID_ARG_TYPE(env->isolate(),
                                     "The \"sql\" argument must be a string.");
    return;
  }

  auto sql = node::Utf8Value(env->isolate(), args[0].As<String>());
  int r = sqlite3_exec(db->connection_, *sql, nullptr, nullptr, nullptr);
  CHECK_ERROR_OR_THROW(env->isolate(), db->connection_, r, SQLITE_OK, void());
}

StatementSync::StatementSync(Environment* env,
                             Local<Object> object,
                             sqlite3* db,
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
  sqlite3_finalize(statement_);
  statement_ = nullptr;
}

bool StatementSync::BindParams(const FunctionCallbackInfo<Value>& args) {
  int r = sqlite3_clear_bindings(statement_);
  CHECK_ERROR_OR_THROW(env()->isolate(), db_, r, SQLITE_OK, false);

  int anon_idx = 1;
  int anon_start = 0;

  if (args[0]->IsObject() && !args[0]->IsUint8Array()) {
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
            node::THROW_ERR_INVALID_STATE(
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

      auto utf8_key = node::Utf8Value(env()->isolate(), key);
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
          node::THROW_ERR_INVALID_STATE(
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
    auto val = node::Utf8Value(env()->isolate(), value.As<String>());
    r = sqlite3_bind_text(
        statement_, index, *val, val.length(), SQLITE_TRANSIENT);
  } else if (value->IsNull()) {
    r = sqlite3_bind_null(statement_, index);
  } else if (value->IsUint8Array()) {
    ArrayBufferViewContents<uint8_t> buf(value);
    r = sqlite3_bind_blob(
        statement_, index, buf.data(), buf.length(), SQLITE_TRANSIENT);
  } else if (value->IsBigInt()) {
    bool lossless;
    int64_t as_int = value.As<BigInt>()->Int64Value(&lossless);
    if (!lossless) {
      node::THROW_ERR_INVALID_ARG_VALUE(env(),
                                        "BigInt value is too large to bind.");
      return false;
    }
    r = sqlite3_bind_int64(statement_, index, as_int);
  } else {
    node::THROW_ERR_INVALID_ARG_TYPE(
        env()->isolate(),
        "Provided value cannot be bound to SQLite parameter %d.",
        index);
    return false;
  }

  CHECK_ERROR_OR_THROW(env()->isolate(), db_, r, SQLITE_OK, false);
  return true;
}

Local<Value> StatementSync::ColumnToValue(const int column) {
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
        return Local<Value>();
      }
    }
    case SQLITE_FLOAT:
      return Number::New(env()->isolate(),
                         sqlite3_column_double(statement_, column));
    case SQLITE_TEXT: {
      const char* value = reinterpret_cast<const char*>(
          sqlite3_column_text(statement_, column));
      Local<Value> val;
      if (!String::NewFromUtf8(env()->isolate(), value).ToLocal(&val)) {
        return Local<Value>();
      }
      return val;
    }
    case SQLITE_NULL:
      return v8::Null(env()->isolate());
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

Local<Value> StatementSync::ColumnNameToValue(const int column) {
  const char* col_name = sqlite3_column_name(statement_, column);
  if (col_name == nullptr) {
    node::THROW_ERR_INVALID_STATE(
        env(), "Cannot get name of column %d", column);
    return Local<Value>();
  }

  Local<String> key;
  if (!String::NewFromUtf8(env()->isolate(), col_name).ToLocal(&key)) {
    return Local<Value>();
  }
  return key;
}

void StatementSync::MemoryInfo(MemoryTracker* tracker) const {}

void StatementSync::All(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  int r = sqlite3_reset(stmt->statement_);
  CHECK_ERROR_OR_THROW(env->isolate(), stmt->db_, r, SQLITE_OK, void());

  if (!stmt->BindParams(args)) {
    return;
  }

  auto reset = OnScopeLeave([&]() { sqlite3_reset(stmt->statement_); });
  int num_cols = sqlite3_column_count(stmt->statement_);
  std::vector<Local<Value>> rows;
  while ((r = sqlite3_step(stmt->statement_)) == SQLITE_ROW) {
    Local<Object> row = Object::New(env->isolate());

    for (int i = 0; i < num_cols; ++i) {
      Local<Value> key = stmt->ColumnNameToValue(i);
      if (key.IsEmpty()) return;
      Local<Value> val = stmt->ColumnToValue(i);
      if (val.IsEmpty()) return;

      if (row->Set(env->context(), key, val).IsNothing()) {
        return;
      }
    }

    rows.emplace_back(row);
  }

  CHECK_ERROR_OR_THROW(env->isolate(), stmt->db_, r, SQLITE_DONE, void());
  args.GetReturnValue().Set(
      Array::New(env->isolate(), rows.data(), rows.size()));
}

void StatementSync::Get(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  int r = sqlite3_reset(stmt->statement_);
  CHECK_ERROR_OR_THROW(env->isolate(), stmt->db_, r, SQLITE_OK, void());

  if (!stmt->BindParams(args)) {
    return;
  }

  auto reset = OnScopeLeave([&]() { sqlite3_reset(stmt->statement_); });
  r = sqlite3_step(stmt->statement_);
  if (r == SQLITE_DONE) return;
  if (r != SQLITE_ROW) {
    THROW_ERR_SQLITE_ERROR(env->isolate(), stmt->db_);
    return;
  }

  int num_cols = sqlite3_column_count(stmt->statement_);
  if (num_cols == 0) {
    return;
  }

  Local<Object> result = Object::New(env->isolate());

  for (int i = 0; i < num_cols; ++i) {
    Local<Value> key = stmt->ColumnNameToValue(i);
    if (key.IsEmpty()) return;
    Local<Value> val = stmt->ColumnToValue(i);
    if (val.IsEmpty()) return;

    if (result->Set(env->context(), key, val).IsNothing()) {
      return;
    }
  }

  args.GetReturnValue().Set(result);
}

void StatementSync::Run(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  int r = sqlite3_reset(stmt->statement_);
  CHECK_ERROR_OR_THROW(env->isolate(), stmt->db_, r, SQLITE_OK, void());

  if (!stmt->BindParams(args)) {
    return;
  }

  auto reset = OnScopeLeave([&]() { sqlite3_reset(stmt->statement_); });
  r = sqlite3_step(stmt->statement_);
  if (r != SQLITE_ROW && r != SQLITE_DONE) {
    THROW_ERR_SQLITE_ERROR(env->isolate(), stmt->db_);
    return;
  }

  Local<Object> result = Object::New(env->isolate());
  Local<String> last_insert_rowid_string =
      FIXED_ONE_BYTE_STRING(env->isolate(), "lastInsertRowid");
  Local<String> changes_string =
      FIXED_ONE_BYTE_STRING(env->isolate(), "changes");
  sqlite3_int64 last_insert_rowid = sqlite3_last_insert_rowid(stmt->db_);
  sqlite3_int64 changes = sqlite3_changes64(stmt->db_);
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
          ->Set(env->context(), last_insert_rowid_string, last_insert_rowid_val)
          .IsNothing() ||
      result->Set(env->context(), changes_string, changes_val).IsNothing()) {
    return;
  }

  args.GetReturnValue().Set(result);
}

void StatementSync::SourceSQL(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  Local<String> sql;
  if (!String::NewFromUtf8(env->isolate(), sqlite3_sql(stmt->statement_))
           .ToLocal(&sql)) {
    return;
  }
  args.GetReturnValue().Set(sql);
}

void StatementSync::ExpandedSQL(const FunctionCallbackInfo<Value>& args) {
  StatementSync* stmt;
  ASSIGN_OR_RETURN_UNWRAP(&stmt, args.This());
  Environment* env = Environment::GetCurrent(args);
  char* expanded = sqlite3_expanded_sql(stmt->statement_);
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

  if (!args[0]->IsBoolean()) {
    node::THROW_ERR_INVALID_ARG_TYPE(
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

  if (!args[0]->IsBoolean()) {
    node::THROW_ERR_INVALID_ARG_TYPE(
        env->isolate(), "The \"readBigInts\" argument must be a boolean.");
    return;
  }

  stmt->use_big_ints_ = args[0]->IsTrue();
}

void IllegalConstructor(const FunctionCallbackInfo<Value>& args) {
  node::THROW_ERR_ILLEGAL_CONSTRUCTOR(Environment::GetCurrent(args));
}

Local<FunctionTemplate> StatementSync::GetConstructorTemplate(
    Environment* env) {
  Local<FunctionTemplate> tmpl =
      env->sqlite_statement_sync_constructor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, IllegalConstructor);
    tmpl->SetClassName(FIXED_ONE_BYTE_STRING(env->isolate(), "StatementSync"));
    tmpl->InstanceTemplate()->SetInternalFieldCount(
        StatementSync::kInternalFieldCount);
    SetProtoMethod(isolate, tmpl, "all", StatementSync::All);
    SetProtoMethod(isolate, tmpl, "get", StatementSync::Get);
    SetProtoMethod(isolate, tmpl, "run", StatementSync::Run);
    SetProtoMethod(isolate, tmpl, "sourceSQL", StatementSync::SourceSQL);
    SetProtoMethod(isolate, tmpl, "expandedSQL", StatementSync::ExpandedSQL);
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
                                                   sqlite3* db,
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

  SetProtoMethod(isolate, db_tmpl, "open", DatabaseSync::Open);
  SetProtoMethod(isolate, db_tmpl, "close", DatabaseSync::Close);
  SetProtoMethod(isolate, db_tmpl, "prepare", DatabaseSync::Prepare);
  SetProtoMethod(isolate, db_tmpl, "exec", DatabaseSync::Exec);
  SetConstructorFunction(context, target, "DatabaseSync", db_tmpl);
  SetConstructorFunction(context,
                         target,
                         "StatementSync",
                         StatementSync::GetConstructorTemplate(env));
}

}  // namespace sqlite
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(sqlite, node::sqlite::Initialize)
