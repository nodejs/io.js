// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sort-builtins.h"

#include <algorithm>
#include <fstream>

#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/embedded/embedded-data.h"

namespace v8 {
namespace internal {

Cluster::Cluster(uint32_t density, uint32_t size, Builtin target,
                 BuiltinsSorter* sorter)
    : density_(density), size_(size), sorter_(sorter) {
  CHECK(size_);
  targets_.push_back(target);
  sorter_->builtin_cluster_map_[target] = this;
}

BuiltinsSorter::BuiltinsSorter() {}

BuiltinsSorter::~BuiltinsSorter() {
  for (Cluster* cls : clusters_) {
    delete cls;
  }
}

void Cluster::Merge(Cluster* other) {
  for (Builtin builtin : other->targets_) {
    targets_.push_back(builtin);
    sorter_->builtin_cluster_map_.emplace(builtin, this);
  }
  density_ = static_cast<uint32_t>(
      (time_approximation() + other->time_approximation()) /
      (size_ + other->size_));
  size_ += other->size_;
  other->density_ = 0;
  other->size_ = 0;
  other->targets_.clear();
}

uint64_t Cluster::time_approximation() {
  return static_cast<uint64_t>(size_) * density_;
}

void BuiltinsSorter::InitializeClusters() {
  for (uint32_t i = 0; i < static_cast<uint32_t>(builtin_size_.size()); i++) {
    Builtin id = Builtins::FromInt(i);
    Builtins::Kind kind = Builtins::KindOf(id);
    if (kind == Builtins::Kind::ASM || kind == Builtins::Kind::CPP) {
      // CHECK there is no data for execution count for non TurboFan compiled
      // builtin.
      CHECK_EQ(builtin_density_map_[id], 0);
      continue;
    }
    Cluster* cls =
        new Cluster(builtin_density_map_[id], builtin_size_[i], id, this);
    clusters_.push_back(cls);
    builtin_density_order_.push_back(
        BuiltinDensitySlot{builtin_density_map_[id], id});
  }

  std::sort(builtin_density_order_.begin(), builtin_density_order_.end(),
            [](const BuiltinDensitySlot& x, const BuiltinDensitySlot& y) {
              return x.density_ > y.density_;
            });
}

Builtin BuiltinsSorter::FindBestPredecessorOf(Builtin callee) {
  Builtin bestPred = Builtin::kNoBuiltinId;
  int32_t bestProb = 0;

  for (auto caller_it = call_graph_.begin(); caller_it != call_graph_.end();
       caller_it++) {
    Builtin caller = caller_it->first;
    const CallProbabilities& callees_prob = caller_it->second;
    if (callees_prob.count(callee) > 0) {
      int32_t incoming_prob = callees_prob.at(callee).incoming_;
      if (incoming_prob == -1) {
        // We dont want to merge any cluster with -1 prob, because it means it's
        // either a non TurboFan compiled builtin or its execution count too
        // small.
        continue;
      }
      if (bestPred == Builtin::kNoBuiltinId || incoming_prob > bestProb) {
        bestPred = caller;
        bestProb = incoming_prob;
      }
    }

    if (bestProb < kMinEdgeProbabilityThreshold ||
        bestPred == Builtin::kNoBuiltinId)
      continue;

    Cluster* predCls = builtin_cluster_map_[bestPred];
    Cluster* succCls = builtin_cluster_map_[callee];

    // Don't merge if the caller and callee are already in same cluster.
    if (predCls == succCls) continue;
    // Don't merge clusters if the combined size is too big.
    if (predCls->size_ + succCls->size_ > kMaxClusterSize) continue;
    if (predCls->density_ == 0) {
      // Some density of cluster after normalized may be 0, in that case we dont
      // merge them.
      continue;
    }
    CHECK(predCls->size_);

    uint32_t new_density = static_cast<uint32_t>(
        (predCls->time_approximation() + succCls->time_approximation()) /
        (predCls->size_ + succCls->size_));

    // Don't merge clusters if the new merged density is lower too many times
    // than current cluster, to avoid a huge dropping in cluster density, it
    // will harm locality of builtins.
    if (predCls->density_ / kMaxDensityDecreaseThreshold > new_density)
      continue;
  }

  return bestPred;
}

void BuiltinsSorter::MergeBestPredecessors() {
  for (size_t i = 0; i < builtin_density_order_.size(); i++) {
    Builtin id = builtin_density_order_[i].builtin_;
    Cluster* succ_cluster = builtin_cluster_map_[id];

    Builtin bestPred = FindBestPredecessorOf(id);
    if (bestPred != Builtin::kNoBuiltinId) {
      Cluster* pred_cluster = builtin_cluster_map_[bestPred];
      pred_cluster->Merge(succ_cluster);
    }
  }
}

void BuiltinsSorter::SortClusters() {
  std::sort(clusters_.begin(), clusters_.end(),
            [](const Cluster* x, const Cluster* y) {
              return x->density_ > y->density_;
            });

  clusters_.erase(
      std::remove_if(clusters_.begin(), clusters_.end(),
                     [](const Cluster* x) { return x->targets_.empty(); }),
      clusters_.end());
}

bool AddBuiltinIfNotProcessed(Builtin builtin, std::vector<Builtin>& order,
                              std::unordered_set<Builtin>& processed_builtins) {
  if (processed_builtins.count(builtin) == 0) {
    order.push_back(builtin);
    processed_builtins.emplace(builtin);
    return true;
  }
  return false;
}

void BuiltinsSorter::ProcessBlockCountLineInfo(
    std::istringstream& line_stream,
    std::unordered_map<std::string, Builtin>& name2id) {
  // Any line starting with kBuiltinCallBlockDensityMarker is a normalized
  // execution count of block with call. The format is:
  //   literal kBuiltinCallBlockDensityMarker , caller , block ,
  //   normalized_count
  std::string token;
  std::string caller_name;
  CHECK(std::getline(line_stream, caller_name, ','));
  Builtin caller_id = name2id[caller_name];

  BuiltinsCallGraph* profiler = BuiltinsCallGraph::Get();

  char* end = nullptr;
  errno = 0;
  CHECK(std::getline(line_stream, token, ','));
  int32_t block_id = static_cast<int32_t>(strtoul(token.c_str(), &end, 0));
  CHECK(errno == 0 && end != token.c_str());

  CHECK(std::getline(line_stream, token, ','));
  int32_t normalized_count =
      static_cast<int32_t>(strtoul(token.c_str(), &end, 0));
  CHECK(errno == 0 && end != token.c_str());
  CHECK(line_stream.eof());

  const BuiltinCallees* block_callees = profiler->GetBuiltinCallees(caller_id);
  if (block_callees) {
    int32_t outgoing_prob = 0;
    int32_t incoming_prob = 0;
    int caller_density = 0;
    int callee_density = 0;

    CHECK(builtin_density_map_.count(caller_id));
    caller_density = builtin_density_map_.at(caller_id);

    // TODO(v8:13938): Remove the below if check when we just store
    // interesting blocks (contain call other builtins) execution count into
    // profiling file.
    if (block_callees->count(block_id)) {
      // If the line of block density make sense (means it contain call to
      // other builtins in this block).
      for (const auto& callee_id : block_callees->at(block_id)) {
        if (caller_density != 0) {
          outgoing_prob = normalized_count * 100 / caller_density;
        } else {
          // If the caller density was normalized as 0 but the block density
          // was not, we set caller prob as 100, otherwise it's 0. Because in
          // the normalization, we may loss fidelity.
          // For example, a caller was executed 8 times, but after
          // normalization, it may be 0 time. At that time, if the
          // normalized_count of this block (it may be a loop body) is a
          // positive number, we could think normalized_count is bigger than the
          // execution count of caller, hence we set it as 100, otherwise it's
          // smaller than execution count of caller, we could set it as 0.
          outgoing_prob = normalized_count ? 100 : 0;
        }

        if (builtin_density_map_.count(callee_id)) {
          callee_density = builtin_density_map_.at(callee_id);
          if (callee_density != 0) {
            incoming_prob = normalized_count * 100 / callee_density;
          } else {
            // Same as caller prob when callee density exists but is 0.
            incoming_prob = normalized_count ? 100 : 0;
          }

        } else {
          // If callee_density does not exist, it means the callee was not
          // compiled by TurboFan or execution count is too small (0 after
          // normalization), we couldn't get the callee count, so we set it as
          // -1. In that case we could avoid merging this callee builtin into
          // any other cluster.
          incoming_prob = -1;
        }

        CallProbability probs = CallProbability(incoming_prob, outgoing_prob);
        if (call_graph_.count(caller_id) == 0) {
          call_graph_.emplace(caller_id, CallProbabilities());
        }
        CallProbabilities& call_probs = call_graph_.at(caller_id);
        call_probs.emplace(callee_id, probs);
      }
    }
  }
  CHECK(line_stream.eof());
}

void BuiltinsSorter::ProcessBuiltinDensityLineInfo(
    std::istringstream& line_stream,
    std::unordered_map<std::string, Builtin>& name2id) {
  // Any line starting with kBuiltinDensityMarker is normalized execution count
  // for block 0 of a builtin, we take it as density of this builtin. The format
  // is:
  //   literal kBuiltinDensityMarker , builtin_name , density
  std::string token;
  std::string builtin_name;
  CHECK(std::getline(line_stream, builtin_name, ','));
  std::getline(line_stream, token, ',');
  CHECK(line_stream.eof());
  char* end = nullptr;
  errno = 0;
  int density = static_cast<int>(strtol(token.c_str(), &end, 0));
  CHECK(errno == 0 && end != token.c_str());

  Builtin builtin_id = name2id[builtin_name];
  builtin_density_map_.emplace(builtin_id, density);
}

void BuiltinsSorter::InitializeCallGraph(const char* profiling_file,
                                         const std::vector<uint32_t>& size) {
  std::ifstream file(profiling_file);
  CHECK_WITH_MSG(file.good(), "Can't read log file");

  std::unordered_map<std::string, Builtin> name2id;
  for (Builtin i = Builtins::kFirst; i <= Builtins::kLast; ++i) {
    std::string name = Builtins::name(i);
    name2id.emplace(name, i);
    builtin_size_.push_back(size.at(static_cast<uint32_t>(i)));
  }

  for (std::string line; std::getline(file, line);) {
    std::string token;
    std::istringstream line_stream(line);
    // We must put lines start with kBuiltinDensityMarker before lines start
    // with kBuiltinCallBlockDensityMarker, because we have to density to
    // calculate call prob.
    if (!std::getline(line_stream, token, ',')) continue;
    if (token == kBuiltinCallBlockDensityMarker) {
      ProcessBlockCountLineInfo(line_stream, name2id);
    } else if (token == kBuiltinDensityMarker) {
      ProcessBuiltinDensityLineInfo(line_stream, name2id);
    }
  }
}

std::vector<Builtin> BuiltinsSorter::SortBuiltins(
    const char* profiling_file, const std::vector<uint32_t>& builtin_size) {
  InitializeCallGraph(profiling_file, builtin_size);

  // Step 1: initialization.
  InitializeClusters();

  // Step 2: Merge best predecessors.
  MergeBestPredecessors();

  // Step 3: Sort clusters again.
  SortClusters();

  std::unordered_set<Builtin> processed_builtins;
  std::vector<Builtin> builtin_order;

  // For functions in the sorted cluster from step 3.
  for (size_t i = 0; i < clusters_.size(); i++) {
    Cluster* cls = clusters_.at(i);
    for (size_t j = 0; j < cls->targets_.size(); j++) {
      Builtin builtin = cls->targets_[j];
      CHECK(
          AddBuiltinIfNotProcessed(builtin, builtin_order, processed_builtins));
    }
  }

  // For the remaining builtins.
  for (Builtin i = Builtins::kFirst; i <= Builtins::kLast; ++i) {
    AddBuiltinIfNotProcessed(i, builtin_order, processed_builtins);
  }

  return builtin_order;
}

}  // namespace internal
}  // namespace v8
