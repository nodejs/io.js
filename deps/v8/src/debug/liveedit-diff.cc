// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/liveedit-diff.h"

#include <cmath>
#include <map>
#include <optional>
#include <vector>

#include "src/base/logging.h"

namespace v8 {
namespace internal {

namespace {

// Implements Myer's Algorithm from
// "An O(ND) Difference Algorithm and Its Variations", particularly the
// linear space refinement mentioned in section 4b.
//
// The differ is input agnostic.
//
// The algorithm works by finding the shortest edit string (SES) in the edit
// graph. The SES describes how to get from a string A of length N to a string
// B of length M via deleting from A and inserting from B.
//
// Example: A = "abbaa", B = "abab"
//
//                  A
//
//          a   b   b   a    a
//        o---o---o---o---o---o
//      a | \ |   |   | \ | \ |
//        o---o---o---o---o---o
//      b |   | \ | \ |   |   |
//  B     o---o---o---o---o---o
//      a | \ |   |   | \ | \ |
//        o---o---o---o---o---o
//      b |   | \ | \ |   |   |
//        o---o---o---o---o---o
//
// The edit graph is constructed with the characters from string A on the x-axis
// and the characters from string B on the y-axis. Starting from (0, 0) we can:
//
//     - Move right, which is equivalent to deleting from A
//     - Move downwards, which is equivalent to inserting from B
//     - Move diagonally if the characters from string A and B match, which
//       means no insertion or deletion.
//
// Any path from (0, 0) to (N, M) describes a valid edit string, but we try to
// find the path with the most diagonals, conversely that is the path with the
// least insertions or deletions.
// Note that a path with "D" insertions/deletions is called a D-path.
class MyersDiffer {
 private:
  // A point in the edit graph.
  struct Point {
    int x, y;

    // Less-than for a point in the edit graph is defined as less than in both
    // components (i.e. at least one diagonal away).
    bool operator<(const Point& other) const {
      return x < other.x && y < other.y;
    }
  };

  // Describes a rectangle in the edit graph.
  struct EditGraphArea {
    Point top_left, bottom_right;

    int width() const { return bottom_right.x - top_left.x; }
    int height() const { return bottom_right.y - top_left.y; }
    int size() const { return width() + height(); }
    int delta() const { return width() - height(); }
  };

  // A path or path-segment through the edit graph. Not all points along
  // the path are necessarily listed since it is trivial to figure out all
  // the concrete points along a snake.
  struct Path {
    std::vector<Point> points;

    void Add(const Point& p) { points.push_back(p); }
    void Add(const Path& p) {
      points.insert(points.end(), p.points.begin(), p.points.end());
    }
  };

  // A snake is a path between two points that is either:
  //
  //     - A single right or down move followed by a (possibly empty) list of
  //       diagonals (in the normal case).
  //     - A (possibly empty) list of diagonals followed by a single right or
  //       or down move (in the reverse case).
  struct Snake {
    Point from, to;
  };

  // A thin wrapper around std::vector<int> that allows negative indexing.
  //
  // This class stores the x-value of the furthest reaching path
  // for each k-diagonal. k-diagonals are numbered from -M to N and defined
  // by y(x) = x - k.
  //
  // We only store the x-value instead of the full point since we can
  // calculate y via y = x - k.
  class FurthestReaching {
   public:
    explicit FurthestReaching(std::vector<int>::size_type size) : v_(size) {}

    int& operator[](int index) {
      const size_t idx = index >= 0 ? index : v_.size() + index;
      return v_[idx];
    }

    const int& operator[](int index) const {
      const size_t idx = index >= 0 ? index : v_.size() + index;
      return v_[idx];
    }

   private:
    std::vector<int> v_;
  };

  class ResultWriter;

  Comparator::Input* input_;
  Comparator::Output* output_;

  // Stores the x-value of the furthest reaching path for each k-diagonal.
  // k-diagonals are numbered from '-height' to 'width', centered on (0,0) and
  // are defined by y(x) = x - k.
  FurthestReaching fr_forward_;

  // Stores the x-value of the furthest reaching reverse path for each
  // l-diagonal. l-diagonals are numbered from '-width' to 'height' and centered
  // on 'bottom_right' of the edit graph area.
  // k-diagonals and l-diagonals represent the same diagonals. While we refer to
  // the diagonals as k-diagonals when calculating SES from (0,0), we refer to
  // the diagonals as l-diagonals when calculating SES from (M,N).
  // The corresponding k-diagonal name of an l-diagonal is: k = l + delta
  // where delta = width -height.
  FurthestReaching fr_reverse_;

  MyersDiffer(Comparator::Input* input, Comparator::Output* output)
      : input_(input),
        output_(output),
        fr_forward_(input->GetLength1() + input->GetLength2() + 1),
        fr_reverse_(input->GetLength1() + input->GetLength2() + 1) {
    // Length1 + Length2 + 1 is the upper bound for our work arrays.
    // We allocate the work arrays once and reuse them for all invocations of
    // `FindMiddleSnake`.
  }

  std::optional<Path> FindEditPath() {
    return FindEditPath(Point{0, 0},
                        Point{input_->GetLength1(), input_->GetLength2()});
  }

  // Returns the path of the SES between `from` and `to`.
  std::optional<Path> FindEditPath(Point from, Point to) {
    // Divide the area described by `from` and `to` by finding the
    // middle snake ...
    std::optional<Snake> snake = FindMiddleSnake(from, to);

    if (!snake) return std::nullopt;

    // ... and then conquer the two resulting sub-areas.
    std::optional<Path> head = FindEditPath(from, snake->from);
    std::optional<Path> tail = FindEditPath(snake->to, to);

    // Combine `head` and `tail` or use the snake start/end points for
    // zero-size areas.
    Path result;
    if (head) {
      result.Add(*head);
    } else {
      result.Add(snake->from);
    }

    if (tail) {
      result.Add(*tail);
    } else {
      result.Add(snake->to);
    }
    return result;
  }

  // Returns the snake in the middle of the area described by `from` and `to`.
  //
  // Incrementally calculates the D-paths (starting from 'from') and the
  // "reverse" D-paths (starting from 'to') until we find a "normal" and a
  // "reverse" path that overlap. That is we first calculate the normal
  // and reverse 0-path, then the normal and reverse 1-path and so on.
  //
  // If a step from a (d-1)-path to a d-path overlaps with a reverse path on
  // the same diagonal (or the other way around), then we consider that step
  // our middle snake and return it immediately.
  std::optional<Snake> FindMiddleSnake(Point from, Point to) {
    EditGraphArea area{from, to};
    if (area.size() == 0) return std::nullopt;

    // Initialise the furthest reaching vectors with an "artificial" edge
    // from (0, -1) -> (0, 0) and (N, -M) -> (N, M) to serve as the initial
    // snake when d = 0.
    fr_forward_[1] = area.top_left.x;
    fr_reverse_[-1] = area.bottom_right.x;

    for (int d = 0; d <= std::ceil(area.size() / 2.0f); ++d) {
      if (auto snake = ShortestEditForward(area, d)) return snake;
      if (auto snake = ShortestEditReverse(area, d)) return snake;
    }

    return std::nullopt;
  }

  // Greedily calculates the furthest reaching `d`-paths for each k-diagonal
  // where k is in [-d, d].  For each k-diagonal we look at the furthest
  // reaching `d-1`-path on the `k-1` and `k+1` depending on which is further
  // along the x-axis we either add an insertion from the `k+1`-diagonal or
  // a deletion from the `k-1`-diagonal. Then we follow all possible diagonal
  // moves and finally record the result as the furthest reaching path on the
  // k-diagonal.
  std::optional<Snake> ShortestEditForward(const EditGraphArea& area, int d) {
    Point from, to;
    // We alternate between looking at odd and even k-diagonals. That is
    // because when we extend a `d-path` by a single move we can at most move
    // one diagonal over. That is either move from `k-1` to `k` or from `k+1` to
    // `k`. That is if `d` is even (odd) then we require only the odd (even)
    // k-diagonals calculated in step `d-1`.
    for (int k = -d; k <= d; k += 2) {
      if (k == -d || (k != d && fr_forward_[k - 1] < fr_forward_[k + 1])) {
        // Move downwards, i.e. add an insertion, because either we are at the
        // edge and downwards is the only way we can move, or because the
        // `d-1`-path along the `k+1` diagonal reaches further on the x-axis
        // than the `d-1`-path along the `k-1` diagonal.
        from.x = to.x = fr_forward_[k + 1];
      } else {
        // Move right, i.e. add a deletion.
        from.x = fr_forward_[k - 1];
        to.x = from.x + 1;
      }

      // Calculate y via y = x - k. We need to adjust k though since the k=0
      // diagonal is centered on `area.top_left` and not (0, 0).
      to.y = area.top_left.y + (to.x - area.top_left.x) - k;
      from.y = (d == 0 || from.x != to.x) ? to.y : to.y - 1;

      // Extend the snake diagonally as long as we can.
      while (to < area.bottom_right && input_->Equals(to.x, to.y)) {
        ++to.x;
        ++to.y;
      }

      fr_forward_[k] = to.x;

      // Check whether there is a reverse path on this k-diagonal which we
      // are overlapping with. If yes, that is our snake.
      const bool odd = area.delta() % 2 != 0;
      const int l = k - area.delta();
      if (odd && l >= (-d + 1) && l <= d - 1 && to.x >= fr_reverse_[l]) {
        return Snake{from, to};
      }
    }
    return std::nullopt;
  }

  // Greedily calculates the furthest reaching reverse `d`-paths for each
  // l-diagonal where l is in [-d, d].
  // Works the same as `ShortestEditForward` but we move upwards and left
  // instead.
  std::optional<Snake> ShortestEditReverse(const EditGraphArea& area, int d) {
    Point from, to;
    // We alternate between looking at odd and even l-diagonals. That is
    // because when we extend a `d-path` by a single move we can at most move
    // one diagonal over. That is either move from `l-1` to `l` or from `l+1` to
    // `l`. That is if `d` is even (odd) then we require only the odd (even)
    // l-diagonals calculated in step `d-1`.
    for (int l = d; l >= -d; l -= 2) {
      if (l == d || (l != -d && fr_reverse_[l - 1] > fr_reverse_[l + 1])) {
        // Move upwards, i.e. add an insertion, because either we are at the
        // edge and upwards is the only way we can move, or because the
        // `d-1`-path along the `l-1` diagonal reaches further on the x-axis
        // than the `d-1`-path along the `l+1` diagonal.
        from.x = to.x = fr_reverse_[l - 1];
      } else {
        // Move left, i.e. add a deletion.
        from.x = fr_reverse_[l + 1];
        to.x = from.x - 1;
      }

      // Calculate y via y = x - k. We need to adjust k though since the k=0
      // diagonal is centered on `area.top_left` and not (0, 0).
      const int k = l + area.delta();
      to.y = area.top_left.y + (to.x - area.top_left.x) - k;
      from.y = (d == 0 || from.x != to.x) ? to.y : to.y + 1;

      // Extend the snake diagonally as long as we can.
      while (area.top_left < to && input_->Equals(to.x - 1, to.y - 1)) {
        --to.x;
        --to.y;
      }

      fr_reverse_[l] = to.x;

      // Check whether there is a path on this k-diagonal which we
      // are overlapping with. If yes, that is our snake.
      const bool even = area.delta() % 2 == 0;
      if (even && k >= -d && k <= d && to.x <= fr_forward_[k]) {
        // Invert the points so the snake goes left to right, top to bottom.
        return Snake{to, from};
      }
    }
    return std::nullopt;
  }

  // Small helper class that converts a "shortest edit script" path into a
  // source mapping. The result is a list of "chunks" where each "chunk"
  // describes a range in the input string and where it can now be found
  // in the output string.
  //
  // The list of chunks can be calculated in a simple pass over all the points
  // of the edit path:
  //
  //     - For any diagonal we close and report the current chunk if there is
  //       one open at the moment.
  //     - For an insertion or deletion we open a new chunk if none is ongoing.
  class ResultWriter {
   public:
    explicit ResultWriter(Comparator::Output* output) : output_(output) {}

    void RecordNoModification(const Point& from) {
      if (!change_is_ongoing_) return;

      // We close the current chunk, going from `change_start_` to `from`.
      CHECK(change_start_);
      output_->AddChunk(change_start_->x, change_start_->y,
                        from.x - change_start_->x, from.y - change_start_->y);
      change_is_ongoing_ = false;
    }

    void RecordInsertionOrDeletion(const Point& from) {
      if (change_is_ongoing_) return;

      // We start a new chunk beginning at `from`.
      change_start_ = from;
      change_is_ongoing_ = true;
    }

   private:
    Comparator::Output* output_;
    bool change_is_ongoing_ = false;
    std::optional<Point> change_start_;
  };

  // Takes an edit path and "fills in the blanks". That is we notify the
  // `ResultWriter` after each single downwards, left or diagonal move.
  void WriteResult(const Path& path) {
    ResultWriter writer(output_);

    for (size_t i = 1; i < path.points.size(); ++i) {
      Point p1 = path.points[i - 1];
      Point p2 = path.points[i];

      p1 = WalkDiagonal(writer, p1, p2);
      const int cmp = (p2.x - p1.x) - (p2.y - p1.y);
      if (cmp == -1) {
        writer.RecordInsertionOrDeletion(p1);
        p1.y++;
      } else if (cmp == 1) {
        writer.RecordInsertionOrDeletion(p1);
        p1.x++;
      }

      p1 = WalkDiagonal(writer, p1, p2);
      DCHECK(p1.x == p2.x && p1.y == p2.y);
    }

    // Write one diagonal in the end to flush out any open chunk.
    writer.RecordNoModification(path.points.back());
  }

  Point WalkDiagonal(ResultWriter& writer, Point p1, Point p2) {
    while (p1.x < p2.x && p1.y < p2.y && input_->Equals(p1.x, p1.y)) {
      writer.RecordNoModification(p1);
      p1.x++;
      p1.y++;
    }
    return p1;
  }

 public:
  static void MyersDiff(Comparator::Input* input, Comparator::Output* output) {
    MyersDiffer differ(input, output);
    auto result = differ.FindEditPath();
    if (!result) return;  // Empty input doesn't produce a path.

    differ.WriteResult(*result);
  }
};

}  // namespace

void Comparator::CalculateDifference(Comparator::Input* input,
                                     Comparator::Output* result_writer) {
  MyersDiffer::MyersDiff(input, result_writer);
}

}  // namespace internal
}  // namespace v8
