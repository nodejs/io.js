// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/modules.h"

#include "src/ast/ast-value-factory.h"
#include "src/ast/scopes.h"
#include "src/common/globals.h"
#include "src/heap/local-factory-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/pending-compilation-error-handler.h"

namespace v8 {
namespace internal {

bool SourceTextModuleDescriptor::AstRawStringComparer::operator()(
    const AstRawString* lhs, const AstRawString* rhs) const {
  return AstRawString::Compare(lhs, rhs) < 0;
}

bool SourceTextModuleDescriptor::ModuleRequestComparer::operator()(
    const AstModuleRequest* lhs, const AstModuleRequest* rhs) const {
  if (int specifier_comparison =
          AstRawString::Compare(lhs->specifier(), rhs->specifier())) {
    return specifier_comparison < 0;
  }

  auto lhsIt = lhs->import_attributes()->cbegin();
  auto rhsIt = rhs->import_attributes()->cbegin();
  for (; lhsIt != lhs->import_attributes()->cend() &&
         rhsIt != rhs->import_attributes()->cend();
       ++lhsIt, ++rhsIt) {
    if (int assertion_key_comparison =
            AstRawString::Compare(lhsIt->first, rhsIt->first)) {
      return assertion_key_comparison < 0;
    }

    if (int assertion_value_comparison =
            AstRawString::Compare(lhsIt->second.first, rhsIt->second.first)) {
      return assertion_value_comparison < 0;
    }
  }

  if (lhs->import_attributes()->size() != rhs->import_attributes()->size()) {
    return (lhs->import_attributes()->size() <
            rhs->import_attributes()->size());
  }

  return false;
}

void SourceTextModuleDescriptor::AddImport(
    const AstRawString* import_name, const AstRawString* local_name,
    const AstRawString* module_request,
    const ImportAttributes* import_attributes, const Scanner::Location loc,
    const Scanner::Location specifier_loc, Zone* zone) {
  Entry* entry = zone->New<Entry>(loc);
  entry->local_name = local_name;
  entry->import_name = import_name;
  entry->module_request =
      AddModuleRequest(module_request, import_attributes, specifier_loc, zone);
  AddRegularImport(entry);
}

void SourceTextModuleDescriptor::AddStarImport(
    const AstRawString* local_name, const AstRawString* module_request,
    const ImportAttributes* import_attributes, const Scanner::Location loc,
    const Scanner::Location specifier_loc, Zone* zone) {
  Entry* entry = zone->New<Entry>(loc);
  entry->local_name = local_name;
  entry->module_request =
      AddModuleRequest(module_request, import_attributes, specifier_loc, zone);
  AddNamespaceImport(entry, zone);
}

void SourceTextModuleDescriptor::AddEmptyImport(
    const AstRawString* module_request,
    const ImportAttributes* import_attributes,
    const Scanner::Location specifier_loc, Zone* zone) {
  AddModuleRequest(module_request, import_attributes, specifier_loc, zone);
}

void SourceTextModuleDescriptor::AddExport(const AstRawString* local_name,
                                           const AstRawString* export_name,
                                           Scanner::Location loc, Zone* zone) {
  Entry* entry = zone->New<Entry>(loc);
  entry->export_name = export_name;
  entry->local_name = local_name;
  AddRegularExport(entry);
}

void SourceTextModuleDescriptor::AddExport(
    const AstRawString* import_name, const AstRawString* export_name,
    const AstRawString* module_request,
    const ImportAttributes* import_attributes, const Scanner::Location loc,
    const Scanner::Location specifier_loc, Zone* zone) {
  DCHECK_NOT_NULL(import_name);
  DCHECK_NOT_NULL(export_name);
  Entry* entry = zone->New<Entry>(loc);
  entry->export_name = export_name;
  entry->import_name = import_name;
  entry->module_request =
      AddModuleRequest(module_request, import_attributes, specifier_loc, zone);
  AddSpecialExport(entry, zone);
}

void SourceTextModuleDescriptor::AddStarExport(
    const AstRawString* module_request,
    const ImportAttributes* import_attributes, const Scanner::Location loc,
    const Scanner::Location specifier_loc, Zone* zone) {
  Entry* entry = zone->New<Entry>(loc);
  entry->module_request =
      AddModuleRequest(module_request, import_attributes, specifier_loc, zone);
  AddSpecialExport(entry, zone);
}

namespace {
template <typename IsolateT>
Handle<PrimitiveHeapObject> ToStringOrUndefined(IsolateT* isolate,
                                                const AstRawString* s) {
  if (s == nullptr) return isolate->factory()->undefined_value();
  return s->string();
}
}  // namespace

template <typename IsolateT>
Handle<ModuleRequest> SourceTextModuleDescriptor::AstModuleRequest::Serialize(
    IsolateT* isolate) const {
  // The import attributes will be stored in this array in the form:
  // [key1, value1, location1, key2, value2, location2, ...]
  Handle<FixedArray> import_attributes_array =
      isolate->factory()->NewFixedArray(
          static_cast<int>(import_attributes()->size() *
                           ModuleRequest::kAttributeEntrySize),
          AllocationType::kOld);
  {
    DisallowGarbageCollection no_gc;
    Tagged<FixedArray> raw_import_attributes = *import_attributes_array;
    int i = 0;
    for (auto iter = import_attributes()->cbegin();
         iter != import_attributes()->cend();
         ++iter, i += ModuleRequest::kAttributeEntrySize) {
      raw_import_attributes->set(i, *iter->first->string());
      raw_import_attributes->set(i + 1, *iter->second.first->string());
      raw_import_attributes->set(i + 2,
                                 Smi::FromInt(iter->second.second.beg_pos));
    }
  }
  return v8::internal::ModuleRequest::New(isolate, specifier()->string(),
                                          import_attributes_array, position());
}
template Handle<ModuleRequest>
SourceTextModuleDescriptor::AstModuleRequest::Serialize(Isolate* isolate) const;
template Handle<ModuleRequest>
SourceTextModuleDescriptor::AstModuleRequest::Serialize(
    LocalIsolate* isolate) const;

template <typename IsolateT>
Handle<SourceTextModuleInfoEntry> SourceTextModuleDescriptor::Entry::Serialize(
    IsolateT* isolate) const {
  CHECK(Smi::IsValid(module_request));  // TODO(neis): Check earlier?
  return SourceTextModuleInfoEntry::New(
      isolate, ToStringOrUndefined(isolate, export_name),
      ToStringOrUndefined(isolate, local_name),
      ToStringOrUndefined(isolate, import_name), module_request, cell_index,
      location.beg_pos, location.end_pos);
}
template Handle<SourceTextModuleInfoEntry>
SourceTextModuleDescriptor::Entry::Serialize(Isolate* isolate) const;
template Handle<SourceTextModuleInfoEntry>
SourceTextModuleDescriptor::Entry::Serialize(LocalIsolate* isolate) const;

template <typename IsolateT>
Handle<FixedArray> SourceTextModuleDescriptor::SerializeRegularExports(
    IsolateT* isolate, Zone* zone) const {
  // We serialize regular exports in a way that lets us later iterate over their
  // local names and for each local name immediately access all its export
  // names.  (Regular exports have neither import name nor module request.)

  ZoneVector<Handle<Object>> data(
      SourceTextModuleInfo::kRegularExportLength * regular_exports_.size(),
      zone);
  int index = 0;

  for (auto it = regular_exports_.begin(); it != regular_exports_.end();) {
    // Find out how many export names this local name has.
    auto next = it;
    int count = 0;
    do {
      DCHECK_EQ(it->second->local_name, next->second->local_name);
      DCHECK_EQ(it->second->cell_index, next->second->cell_index);
      ++next;
      ++count;
    } while (next != regular_exports_.end() && next->first == it->first);

    Handle<FixedArray> export_names =
        isolate->factory()->NewFixedArray(count, AllocationType::kOld);
    data[index + SourceTextModuleInfo::kRegularExportLocalNameOffset] =
        it->second->local_name->string();
    data[index + SourceTextModuleInfo::kRegularExportCellIndexOffset] =
        handle(Smi::FromInt(it->second->cell_index), isolate);
    data[index + SourceTextModuleInfo::kRegularExportExportNamesOffset] =
        export_names;
    index += SourceTextModuleInfo::kRegularExportLength;

    // Collect the export names.
    int i = 0;
    for (; it != next; ++it) {
      export_names->set(i++, *it->second->export_name->string());
    }
    DCHECK_EQ(i, count);

    // Continue with the next distinct key.
    DCHECK(it == next);
  }
  DCHECK_LE(index, static_cast<int>(data.size()));
  data.resize(index);

  // We cannot create the FixedArray earlier because we only now know the
  // precise size.
  Handle<FixedArray> result =
      isolate->factory()->NewFixedArray(index, AllocationType::kOld);
  for (int i = 0; i < index; ++i) {
    result->set(i, *data[i]);
  }
  return result;
}
template Handle<FixedArray> SourceTextModuleDescriptor::SerializeRegularExports(
    Isolate* isolate, Zone* zone) const;
template Handle<FixedArray> SourceTextModuleDescriptor::SerializeRegularExports(
    LocalIsolate* isolate, Zone* zone) const;

void SourceTextModuleDescriptor::MakeIndirectExportsExplicit(Zone* zone) {
  for (auto it = regular_exports_.begin(); it != regular_exports_.end();) {
    Entry* entry = it->second;
    DCHECK_NOT_NULL(entry->local_name);
    auto import = regular_imports_.find(entry->local_name);
    if (import != regular_imports_.end()) {
      // Found an indirect export.  Patch export entry and move it from regular
      // to special.
      DCHECK_NULL(entry->import_name);
      DCHECK_LT(entry->module_request, 0);
      DCHECK_NOT_NULL(import->second->import_name);
      DCHECK_LE(0, import->second->module_request);
      DCHECK_LT(import->second->module_request,
                static_cast<int>(module_requests_.size()));
      entry->import_name = import->second->import_name;
      entry->module_request = import->second->module_request;
      // Hack: When the indirect export cannot be resolved, we want the error
      // message to point at the import statement, not at the export statement.
      // Therefore we overwrite [entry]'s location here.  Note that Validate()
      // has already checked for duplicate exports, so it's guaranteed that we
      // won't need to report any error pointing at the (now lost) export
      // location.
      entry->location = import->second->location;
      entry->local_name = nullptr;
      AddSpecialExport(entry, zone);
      it = regular_exports_.erase(it);
    } else {
      it++;
    }
  }
}

SourceTextModuleDescriptor::CellIndexKind
SourceTextModuleDescriptor::GetCellIndexKind(int cell_index) {
  if (cell_index > 0) return kExport;
  if (cell_index < 0) return kImport;
  return kInvalid;
}

void SourceTextModuleDescriptor::AssignCellIndices() {
  int export_index = 1;
  for (auto it = regular_exports_.begin(); it != regular_exports_.end();) {
    auto current_key = it->first;
    // This local name may be exported under multiple export names.  Assign the
    // same index to each such entry.
    do {
      Entry* entry = it->second;
      DCHECK_NOT_NULL(entry->local_name);
      DCHECK_NULL(entry->import_name);
      DCHECK_LT(entry->module_request, 0);
      DCHECK_EQ(entry->cell_index, 0);
      entry->cell_index = export_index;
      it++;
    } while (it != regular_exports_.end() && it->first == current_key);
    export_index++;
  }

  int import_index = -1;
  for (const auto& elem : regular_imports_) {
    Entry* entry = elem.second;
    DCHECK_NOT_NULL(entry->local_name);
    DCHECK_NOT_NULL(entry->import_name);
    DCHECK_LE(0, entry->module_request);
    DCHECK_EQ(entry->cell_index, 0);
    entry->cell_index = import_index;
    import_index--;
  }
}

namespace {

const SourceTextModuleDescriptor::Entry* BetterDuplicate(
    const SourceTextModuleDescriptor::Entry* candidate,
    ZoneMap<const AstRawString*, const SourceTextModuleDescriptor::Entry*>&
        export_names,
    const SourceTextModuleDescriptor::Entry* current_duplicate) {
  DCHECK_NOT_NULL(candidate->export_name);
  DCHECK(candidate->location.IsValid());
  auto insert_result =
      export_names.insert(std::make_pair(candidate->export_name, candidate));
  if (insert_result.second) return current_duplicate;
  if (current_duplicate == nullptr) {
    current_duplicate = insert_result.first->second;
  }
  return (candidate->location.beg_pos > current_duplicate->location.beg_pos)
             ? candidate
             : current_duplicate;
}

}  // namespace

const SourceTextModuleDescriptor::Entry*
SourceTextModuleDescriptor::FindDuplicateExport(Zone* zone) const {
  const SourceTextModuleDescriptor::Entry* duplicate = nullptr;
  ZoneMap<const AstRawString*, const SourceTextModuleDescriptor::Entry*>
      export_names(zone);
  for (const auto& elem : regular_exports_) {
    duplicate = BetterDuplicate(elem.second, export_names, duplicate);
  }
  for (auto entry : special_exports_) {
    if (entry->export_name == nullptr) continue;  // Star export.
    duplicate = BetterDuplicate(entry, export_names, duplicate);
  }
  return duplicate;
}

bool SourceTextModuleDescriptor::Validate(
    ModuleScope* module_scope, PendingCompilationErrorHandler* error_handler,
    Zone* zone) {
  DCHECK_EQ(this, module_scope->module());
  DCHECK_NOT_NULL(error_handler);

  // Report error iff there are duplicate exports.
  {
    const Entry* entry = FindDuplicateExport(zone);
    if (entry != nullptr) {
      error_handler->ReportMessageAt(
          entry->location.beg_pos, entry->location.end_pos,
          MessageTemplate::kDuplicateExport, entry->export_name);
      return false;
    }
  }

  // Report error iff there are exports of non-existent local names.
  for (const auto& elem : regular_exports_) {
    const Entry* entry = elem.second;
    DCHECK_NOT_NULL(entry->local_name);
    if (module_scope->LookupLocal(entry->local_name) == nullptr) {
      error_handler->ReportMessageAt(
          entry->location.beg_pos, entry->location.end_pos,
          MessageTemplate::kModuleExportUndefined, entry->local_name);
      return false;
    }
  }

  MakeIndirectExportsExplicit(zone);
  AssignCellIndices();
  return true;
}

}  // namespace internal
}  // namespace v8
