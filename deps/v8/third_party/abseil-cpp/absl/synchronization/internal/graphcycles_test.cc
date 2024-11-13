// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/synchronization/internal/graphcycles.h"

#include <climits>
#include <map>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/log/log.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

// We emulate a GraphCycles object with a node vector and an edge vector.
// We then compare the two implementations.

using Nodes = std::vector<int>;
struct Edge {
  int from;
  int to;
};
using Edges = std::vector<Edge>;
using RandomEngine = std::mt19937_64;

// Mapping from integer index to GraphId.
typedef std::map<int, GraphId> IdMap;
static GraphId Get(const IdMap& id, int num) {
  auto iter = id.find(num);
  return (iter == id.end()) ? InvalidGraphId() : iter->second;
}

// Return whether "to" is reachable from "from".
static bool IsReachable(Edges *edges, int from, int to,
                        std::unordered_set<int> *seen) {
  seen->insert(from);     // we are investigating "from"; don't do it again
  if (from == to) return true;
  for (const auto &edge : *edges) {
    if (edge.from == from) {
      if (edge.to == to) {  // success via edge directly
        return true;
      } else if (seen->find(edge.to) == seen->end() &&  // success via edge
                 IsReachable(edges, edge.to, to, seen)) {
        return true;
      }
    }
  }
  return false;
}

static void PrintEdges(Edges *edges) {
  LOG(INFO) << "EDGES (" << edges->size() << ")";
  for (const auto &edge : *edges) {
    int a = edge.from;
    int b = edge.to;
    LOG(INFO) << a << " " << b;
  }
  LOG(INFO) << "---";
}

static void PrintGCEdges(Nodes *nodes, const IdMap &id, GraphCycles *gc) {
  LOG(INFO) << "GC EDGES";
  for (int a : *nodes) {
    for (int b : *nodes) {
      if (gc->HasEdge(Get(id, a), Get(id, b))) {
        LOG(INFO) << a << " " << b;
      }
    }
  }
  LOG(INFO) << "---";
}

static void PrintTransitiveClosure(Nodes *nodes, Edges *edges) {
  LOG(INFO) << "Transitive closure";
  for (int a : *nodes) {
    for (int b : *nodes) {
      std::unordered_set<int> seen;
      if (IsReachable(edges, a, b, &seen)) {
        LOG(INFO) << a << " " << b;
      }
    }
  }
  LOG(INFO) << "---";
}

static void PrintGCTransitiveClosure(Nodes *nodes, const IdMap &id,
                                     GraphCycles *gc) {
  LOG(INFO) << "GC Transitive closure";
  for (int a : *nodes) {
    for (int b : *nodes) {
      if (gc->IsReachable(Get(id, a), Get(id, b))) {
        LOG(INFO) << a << " " << b;
      }
    }
  }
  LOG(INFO) << "---";
}

static void CheckTransitiveClosure(Nodes *nodes, Edges *edges, const IdMap &id,
                                   GraphCycles *gc) {
  std::unordered_set<int> seen;
  for (const auto &a : *nodes) {
    for (const auto &b : *nodes) {
      seen.clear();
      bool gc_reachable = gc->IsReachable(Get(id, a), Get(id, b));
      bool reachable = IsReachable(edges, a, b, &seen);
      if (gc_reachable != reachable) {
        PrintEdges(edges);
        PrintGCEdges(nodes, id, gc);
        PrintTransitiveClosure(nodes, edges);
        PrintGCTransitiveClosure(nodes, id, gc);
        LOG(FATAL) << "gc_reachable " << gc_reachable << " reachable "
                   << reachable << " a " << a << " b " << b;
      }
    }
  }
}

static void CheckEdges(Nodes *nodes, Edges *edges, const IdMap &id,
                       GraphCycles *gc) {
  int count = 0;
  for (const auto &edge : *edges) {
    int a = edge.from;
    int b = edge.to;
    if (!gc->HasEdge(Get(id, a), Get(id, b))) {
      PrintEdges(edges);
      PrintGCEdges(nodes, id, gc);
      LOG(FATAL) << "!gc->HasEdge(" << a << ", " << b << ")";
    }
  }
  for (const auto &a : *nodes) {
    for (const auto &b : *nodes) {
      if (gc->HasEdge(Get(id, a), Get(id, b))) {
        count++;
      }
    }
  }
  if (count != edges->size()) {
    PrintEdges(edges);
    PrintGCEdges(nodes, id, gc);
    LOG(FATAL) << "edges->size() " << edges->size() << "  count " << count;
  }
}

static void CheckInvariants(const GraphCycles &gc) {
  CHECK(gc.CheckInvariants()) << "CheckInvariants";
}

// Returns the index of a randomly chosen node in *nodes.
// Requires *nodes be non-empty.
static int RandomNode(RandomEngine* rng, Nodes *nodes) {
  std::uniform_int_distribution<int> uniform(0, nodes->size()-1);
  return uniform(*rng);
}

// Returns the index of a randomly chosen edge in *edges.
// Requires *edges be non-empty.
static int RandomEdge(RandomEngine* rng, Edges *edges) {
  std::uniform_int_distribution<int> uniform(0, edges->size()-1);
  return uniform(*rng);
}

// Returns the index of edge (from, to) in *edges or -1 if it is not in *edges.
static int EdgeIndex(Edges *edges, int from, int to) {
  int i = 0;
  while (i != edges->size() &&
         ((*edges)[i].from != from || (*edges)[i].to != to)) {
    i++;
  }
  return i == edges->size()? -1 : i;
}

TEST(GraphCycles, RandomizedTest) {
  int next_node = 0;
  Nodes nodes;
  Edges edges;   // from, to
  IdMap id;
  GraphCycles graph_cycles;
  static const int kMaxNodes = 7;  // use <= 7 nodes to keep test short
  static const int kDataOffset = 17;  // an offset to the node-specific data
  int n = 100000;
  int op = 0;
  RandomEngine rng(testing::UnitTest::GetInstance()->random_seed());
  std::uniform_int_distribution<int> uniform(0, 5);

  auto ptr = [](intptr_t i) {
    return reinterpret_cast<void*>(i + kDataOffset);
  };

  for (int iter = 0; iter != n; iter++) {
    for (const auto &node : nodes) {
      ASSERT_EQ(graph_cycles.Ptr(Get(id, node)), ptr(node)) << " node " << node;
    }
    CheckEdges(&nodes, &edges, id, &graph_cycles);
    CheckTransitiveClosure(&nodes, &edges, id, &graph_cycles);
    op = uniform(rng);
    switch (op) {
    case 0:     // Add a node
      if (nodes.size() < kMaxNodes) {
        int new_node = next_node++;
        GraphId new_gnode = graph_cycles.GetId(ptr(new_node));
        ASSERT_NE(new_gnode, InvalidGraphId());
        id[new_node] = new_gnode;
        ASSERT_EQ(ptr(new_node), graph_cycles.Ptr(new_gnode));
        nodes.push_back(new_node);
      }
      break;

    case 1:    // Remove a node
      if (nodes.size() > 0) {
        int node_index = RandomNode(&rng, &nodes);
        int node = nodes[node_index];
        nodes[node_index] = nodes.back();
        nodes.pop_back();
        graph_cycles.RemoveNode(ptr(node));
        ASSERT_EQ(graph_cycles.Ptr(Get(id, node)), nullptr);
        id.erase(node);
        int i = 0;
        while (i != edges.size()) {
          if (edges[i].from == node || edges[i].to == node) {
            edges[i] = edges.back();
            edges.pop_back();
          } else {
            i++;
          }
        }
      }
      break;

    case 2:   // Add an edge
      if (nodes.size() > 0) {
        int from = RandomNode(&rng, &nodes);
        int to = RandomNode(&rng, &nodes);
        if (EdgeIndex(&edges, nodes[from], nodes[to]) == -1) {
          if (graph_cycles.InsertEdge(id[nodes[from]], id[nodes[to]])) {
            Edge new_edge;
            new_edge.from = nodes[from];
            new_edge.to = nodes[to];
            edges.push_back(new_edge);
          } else {
            std::unordered_set<int> seen;
            ASSERT_TRUE(IsReachable(&edges, nodes[to], nodes[from], &seen))
                << "Edge " << nodes[to] << "->" << nodes[from];
          }
        }
      }
      break;

    case 3:    // Remove an edge
      if (edges.size() > 0) {
        int i = RandomEdge(&rng, &edges);
        int from = edges[i].from;
        int to = edges[i].to;
        ASSERT_EQ(i, EdgeIndex(&edges, from, to));
        edges[i] = edges.back();
        edges.pop_back();
        ASSERT_EQ(-1, EdgeIndex(&edges, from, to));
        graph_cycles.RemoveEdge(id[from], id[to]);
      }
      break;

    case 4:   // Check a path
      if (nodes.size() > 0) {
        int from = RandomNode(&rng, &nodes);
        int to = RandomNode(&rng, &nodes);
        GraphId path[2*kMaxNodes];
        int path_len = graph_cycles.FindPath(id[nodes[from]], id[nodes[to]],
                                             ABSL_ARRAYSIZE(path), path);
        std::unordered_set<int> seen;
        bool reachable = IsReachable(&edges, nodes[from], nodes[to], &seen);
        bool gc_reachable =
            graph_cycles.IsReachable(Get(id, nodes[from]), Get(id, nodes[to]));
        ASSERT_EQ(path_len != 0, reachable);
        ASSERT_EQ(path_len != 0, gc_reachable);
        // In the following line, we add one because a node can appear
        // twice, if the path is from that node to itself, perhaps via
        // every other node.
        ASSERT_LE(path_len, kMaxNodes + 1);
        if (path_len != 0) {
          ASSERT_EQ(id[nodes[from]], path[0]);
          ASSERT_EQ(id[nodes[to]], path[path_len-1]);
          for (int i = 1; i < path_len; i++) {
            ASSERT_TRUE(graph_cycles.HasEdge(path[i-1], path[i]));
          }
        }
      }
      break;

    case 5:  // Check invariants
      CheckInvariants(graph_cycles);
      break;

    default:
      LOG(FATAL) << "op " << op;
    }

    // Very rarely, test graph expansion by adding then removing many nodes.
    std::bernoulli_distribution one_in_1024(1.0 / 1024);
    if (one_in_1024(rng)) {
      CheckEdges(&nodes, &edges, id, &graph_cycles);
      CheckTransitiveClosure(&nodes, &edges, id, &graph_cycles);
      for (int i = 0; i != 256; i++) {
        int new_node = next_node++;
        GraphId new_gnode = graph_cycles.GetId(ptr(new_node));
        ASSERT_NE(InvalidGraphId(), new_gnode);
        id[new_node] = new_gnode;
        ASSERT_EQ(ptr(new_node), graph_cycles.Ptr(new_gnode));
        for (const auto &node : nodes) {
          ASSERT_NE(node, new_node);
        }
        nodes.push_back(new_node);
      }
      for (int i = 0; i != 256; i++) {
        ASSERT_GT(nodes.size(), 0);
        int node_index = RandomNode(&rng, &nodes);
        int node = nodes[node_index];
        nodes[node_index] = nodes.back();
        nodes.pop_back();
        graph_cycles.RemoveNode(ptr(node));
        id.erase(node);
        int j = 0;
        while (j != edges.size()) {
          if (edges[j].from == node || edges[j].to == node) {
            edges[j] = edges.back();
            edges.pop_back();
          } else {
            j++;
          }
        }
      }
      CheckInvariants(graph_cycles);
    }
  }
}

class GraphCyclesTest : public ::testing::Test {
 public:
  IdMap id_;
  GraphCycles g_;

  static void* Ptr(int i) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(i));
  }

  static int Num(void* ptr) {
    return static_cast<int>(reinterpret_cast<uintptr_t>(ptr));
  }

  // Test relies on ith NewNode() call returning Node numbered i
  GraphCyclesTest() {
    for (int i = 0; i < 100; i++) {
      id_[i] = g_.GetId(Ptr(i));
    }
    CheckInvariants(g_);
  }

  bool AddEdge(int x, int y) {
    return g_.InsertEdge(Get(id_, x), Get(id_, y));
  }

  void AddMultiples() {
    // For every node x > 0: add edge to 2*x, 3*x
    for (int x = 1; x < 25; x++) {
      EXPECT_TRUE(AddEdge(x, 2*x)) << x;
      EXPECT_TRUE(AddEdge(x, 3*x)) << x;
    }
    CheckInvariants(g_);
  }

  std::string Path(int x, int y) {
    GraphId path[5];
    int np = g_.FindPath(Get(id_, x), Get(id_, y), ABSL_ARRAYSIZE(path), path);
    std::string result;
    for (int i = 0; i < np; i++) {
      if (i >= ABSL_ARRAYSIZE(path)) {
        result += " ...";
        break;
      }
      if (!result.empty()) result.push_back(' ');
      char buf[20];
      snprintf(buf, sizeof(buf), "%d", Num(g_.Ptr(path[i])));
      result += buf;
    }
    return result;
  }
};

TEST_F(GraphCyclesTest, NoCycle) {
  AddMultiples();
  CheckInvariants(g_);
}

TEST_F(GraphCyclesTest, SimpleCycle) {
  AddMultiples();
  EXPECT_FALSE(AddEdge(8, 4));
  EXPECT_EQ("4 8", Path(4, 8));
  CheckInvariants(g_);
}

TEST_F(GraphCyclesTest, IndirectCycle) {
  AddMultiples();
  EXPECT_TRUE(AddEdge(16, 9));
  CheckInvariants(g_);
  EXPECT_FALSE(AddEdge(9, 2));
  EXPECT_EQ("2 4 8 16 9", Path(2, 9));
  CheckInvariants(g_);
}

TEST_F(GraphCyclesTest, LongPath) {
  ASSERT_TRUE(AddEdge(2, 4));
  ASSERT_TRUE(AddEdge(4, 6));
  ASSERT_TRUE(AddEdge(6, 8));
  ASSERT_TRUE(AddEdge(8, 10));
  ASSERT_TRUE(AddEdge(10, 12));
  ASSERT_FALSE(AddEdge(12, 2));
  EXPECT_EQ("2 4 6 8 10 ...", Path(2, 12));
  CheckInvariants(g_);
}

TEST_F(GraphCyclesTest, RemoveNode) {
  ASSERT_TRUE(AddEdge(1, 2));
  ASSERT_TRUE(AddEdge(2, 3));
  ASSERT_TRUE(AddEdge(3, 4));
  ASSERT_TRUE(AddEdge(4, 5));
  g_.RemoveNode(g_.Ptr(id_[3]));
  id_.erase(3);
  ASSERT_TRUE(AddEdge(5, 1));
}

TEST_F(GraphCyclesTest, ManyEdges) {
  const int N = 50;
  for (int i = 0; i < N; i++) {
    for (int j = 1; j < N; j++) {
      ASSERT_TRUE(AddEdge(i, i+j));
    }
  }
  CheckInvariants(g_);
  ASSERT_TRUE(AddEdge(2*N-1, 0));
  CheckInvariants(g_);
  ASSERT_FALSE(AddEdge(10, 9));
  CheckInvariants(g_);
}

TEST(GraphCycles, IntegerOverflow) {
  GraphCycles graph_cycles;
  char *buf = (char *)nullptr;
  GraphId prev_id = graph_cycles.GetId(buf);
  buf += 1;
  GraphId id = graph_cycles.GetId(buf);
  ASSERT_TRUE(graph_cycles.InsertEdge(prev_id, id));

  // INT_MAX / 40 is enough to cause an overflow when multiplied by 41.
  graph_cycles.TestOnlyAddNodes(INT_MAX / 40);

  buf += 1;
  GraphId newid = graph_cycles.GetId(buf);
  graph_cycles.HasEdge(prev_id, newid);

  graph_cycles.RemoveNode(buf);
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl
