// Pizza local-search improver
//
// Usage:
//   pizza_ls <input_file> <input_solution_file> [output_file] [time_limit_seconds]
//
// Reads a Hash Code 2017 "Pizza" instance and an existing solution,
// then runs a local-search loop that tries several move types to
// increase the total number of covered cells (= the score).
//
// Moves used:
//   - GROW  : try to extend each slice on each side into uncovered cells.
//   - FILL  : scan uncovered cells and try to drop in a brand-new slice.
//   - REMOVE_AND_REFILL : remove a small "patch" of slices (1 slice + its
//                        neighborhood), then greedily re-pack the freed
//                        rectangle. Accept if total covered cells improved.
//
// We never accept worsening moves (pure hill climber + random restarts of
// patches), but we keep going for the full time budget. Output is rewritten
// only when a strictly better solution is found.
//
//  Compile:
//  g++ -o pizza_ls pizza_ls.cpp

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using Clock = chrono::steady_clock;

// ---------- problem & solution data ----------

struct Problem {
    int R, C, L, H;
    vector<vector<int>> g; // 0 = T, 1 = M
};

struct Slice {
    int r1, c1, r2, c2; // inclusive
    int area() const { return (r2 - r1 + 1) * (c2 - c1 + 1); }
};

struct Solution {
    vector<Slice> slices;
    // owner[r][c] = index into slices, or -1 if uncovered
    vector<vector<int>> owner;
};

// ---------- I/O ----------

static Problem readProblem(const string& path) {
    ifstream in(path);
    if (!in) { cerr << "cannot open input: " << path << "\n"; exit(1); }
    Problem p;
    in >> p.R >> p.C >> p.L >> p.H;
    p.g.assign(p.R, vector<int>(p.C, 0));
    string line;
    getline(in, line); // consume EOL
    for (int r = 0; r < p.R; r++) {
        getline(in, line);
        for (int c = 0; c < p.C; c++) {
            char ch = line[c];
            p.g[r][c] = (ch == 'M') ? 1 : 0;
        }
    }
    return p;
}

static Solution readSolution(const string& path, const Problem& p) {
    ifstream in(path);
    if (!in) { cerr << "cannot open solution: " << path << "\n"; exit(1); }
    int n; in >> n;
    Solution s;
    s.slices.reserve(n);
    s.owner.assign(p.R, vector<int>(p.C, -1));
    for (int i = 0; i < n; i++) {
        int a, b, c, d;
        in >> a >> b >> c >> d;
        Slice sl;
        sl.r1 = min(a, c); sl.r2 = max(a, c);
        sl.c1 = min(b, d); sl.c2 = max(b, d);
        int idx = (int)s.slices.size();
        s.slices.push_back(sl);
        for (int r = sl.r1; r <= sl.r2; r++)
            for (int cc = sl.c1; cc <= sl.c2; cc++) {
                if (s.owner[r][cc] != -1) {
                    cerr << "WARNING: input solution has overlap at "
                         << r << "," << cc << "\n";
                }
                s.owner[r][cc] = idx;
            }
    }
    return s;
}

static int totalCovered(const Solution& s) {
    int t = 0;
    for (const auto& sl : s.slices) t += sl.area();
    return t;
}

static void writeSolution(const string& path, const Solution& s) {
    ofstream out(path);
    out << s.slices.size() << "\n";
    for (const auto& sl : s.slices) {
        out << sl.r1 << " " << sl.c1 << " " << sl.r2 << " " << sl.c2 << "\n";
    }
}

// ---------- slice validity ----------

// 2D prefix sums of 'M' count, so slice ingredient counts are O(1).
struct PrefixSum {
    int R, C;
    vector<vector<int>> ps; // ps[r][c] = count of M in rect (0..r-1,0..c-1)

    void build(const Problem& p) {
        R = p.R; C = p.C;
        ps.assign(R + 1, vector<int>(C + 1, 0));
        for (int r = 0; r < R; r++)
            for (int c = 0; c < C; c++)
                ps[r+1][c+1] = ps[r][c+1] + ps[r+1][c] - ps[r][c] + p.g[r][c];
    }
    // count of M in inclusive rect
    inline int countM(int r1, int c1, int r2, int c2) const {
        return ps[r2+1][c2+1] - ps[r1][c2+1] - ps[r2+1][c1] + ps[r1][c1];
    }
};

static inline bool sliceIsValid(const Problem& p, const PrefixSum& ps,
                                int r1, int c1, int r2, int c2) {
    if (r1 < 0 || c1 < 0 || r2 >= p.R || c2 >= p.C) return false;
    if (r1 > r2 || c1 > c2) return false;
    int area = (r2 - r1 + 1) * (c2 - c1 + 1);
    if (area > p.H) return false;
    int m = ps.countM(r1, c1, r2, c2);
    int t = area - m;
    return m >= p.L && t >= p.L;
}

// Check that a candidate slice's cells are all currently owned by 'allowed'
// (a set of slice indices, typically: -1 meaning uncovered, plus a few
// "to be removed" slice indices). Returns true if every cell is allowed.
static inline bool cellsAllUncovered(const Solution& s, int r1, int c1,
                                     int r2, int c2) {
    for (int r = r1; r <= r2; r++)
        for (int c = c1; c <= c2; c++)
            if (s.owner[r][c] != -1) return false;
    return true;
}

// ---------- ownership helpers ----------

static void paintSlice(Solution& s, int idx) {
    const Slice& sl = s.slices[idx];
    for (int r = sl.r1; r <= sl.r2; r++)
        for (int c = sl.c1; c <= sl.c2; c++)
            s.owner[r][c] = idx;
}

static void unpaintSlice(Solution& s, int idx) {
    const Slice& sl = s.slices[idx];
    for (int r = sl.r1; r <= sl.r2; r++)
        for (int c = sl.c1; c <= sl.c2; c++)
            s.owner[r][c] = -1;
}

// Remove a slice by swap-and-pop. Updates owner indices.
static void removeSlice(Solution& s, int idx) {
    unpaintSlice(s, idx);
    int last = (int)s.slices.size() - 1;
    if (idx != last) {
        s.slices[idx] = s.slices[last];
        // repaint moved slice with its new index
        const Slice& moved = s.slices[idx];
        for (int r = moved.r1; r <= moved.r2; r++)
            for (int c = moved.c1; c <= moved.c2; c++)
                s.owner[r][c] = idx;
    }
    s.slices.pop_back();
}

// ---------- moves ----------

// Try to grow each slice on each side into uncovered cells.
// Returns number of cells gained.
static int doGrow(Solution& s, const Problem& p, const PrefixSum& ps) {
    int gained = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < (int)s.slices.size(); i++) {
            Slice& sl = s.slices[i];
            // try each of 4 directions, possibly multiple cells
            // up
            while (sl.r1 > 0) {
                int nr = sl.r1 - 1;
                int newArea = (sl.r2 - nr + 1) * (sl.c2 - sl.c1 + 1);
                if (newArea > p.H) break;
                bool ok = true;
                for (int c = sl.c1; c <= sl.c2; c++)
                    if (s.owner[nr][c] != -1) { ok = false; break; }
                if (!ok) break;
                // paint
                for (int c = sl.c1; c <= sl.c2; c++) s.owner[nr][c] = i;
                sl.r1 = nr;
                gained += (sl.c2 - sl.c1 + 1);
                changed = true;
            }
            // down
            while (sl.r2 < p.R - 1) {
                int nr = sl.r2 + 1;
                int newArea = (nr - sl.r1 + 1) * (sl.c2 - sl.c1 + 1);
                if (newArea > p.H) break;
                bool ok = true;
                for (int c = sl.c1; c <= sl.c2; c++)
                    if (s.owner[nr][c] != -1) { ok = false; break; }
                if (!ok) break;
                for (int c = sl.c1; c <= sl.c2; c++) s.owner[nr][c] = i;
                sl.r2 = nr;
                gained += (sl.c2 - sl.c1 + 1);
                changed = true;
            }
            // left
            while (sl.c1 > 0) {
                int nc = sl.c1 - 1;
                int newArea = (sl.r2 - sl.r1 + 1) * (sl.c2 - nc + 1);
                if (newArea > p.H) break;
                bool ok = true;
                for (int r = sl.r1; r <= sl.r2; r++)
                    if (s.owner[r][nc] != -1) { ok = false; break; }
                if (!ok) break;
                for (int r = sl.r1; r <= sl.r2; r++) s.owner[r][nc] = i;
                sl.c1 = nc;
                gained += (sl.r2 - sl.r1 + 1);
                changed = true;
            }
            // right
            while (sl.c2 < p.C - 1) {
                int nc = sl.c2 + 1;
                int newArea = (sl.r2 - sl.r1 + 1) * (nc - sl.c1 + 1);
                if (newArea > p.H) break;
                bool ok = true;
                for (int r = sl.r1; r <= sl.r2; r++)
                    if (s.owner[r][nc] != -1) { ok = false; break; }
                if (!ok) break;
                for (int r = sl.r1; r <= sl.r2; r++) s.owner[r][nc] = i;
                sl.c2 = nc;
                gained += (sl.r2 - sl.r1 + 1);
                changed = true;
            }
        }
    }
    return gained;
}

// For each uncovered cell, try to place a new valid slice covering it.
// We try all rectangles that contain (r,c) with size <= H, but cap the
// search by max dim sqrt(H)*2 etc. Returns cells gained.
static int doFill(Solution& s, const Problem& p, const PrefixSum& ps) {
    int gained = 0;
    int maxDim = p.H; // worst case, but most slices are small
    // Iterate cells in random-ish order to spread placements
    for (int r = 0; r < p.R; r++) {
        for (int c = 0; c < p.C; c++) {
            if (s.owner[r][c] != -1) continue;
            // find best slice covering (r,c)
            int bestArea = 0;
            int bR1=0,bC1=0,bR2=0,bC2=0;
            // determine extents we can grow uncovered around (r,c)
            // up
            int up = r;
            while (up - 1 >= 0 && s.owner[up - 1][c] == -1) up--;
            int down = r;
            while (down + 1 < p.R && s.owner[down + 1][c] == -1) down++;
            int left = c;
            while (left - 1 >= 0 && s.owner[r][left - 1] == -1) left--;
            int right = c;
            while (right + 1 < p.C && s.owner[r][right + 1] == -1) right++;

            for (int r1 = up; r1 <= r; r1++) {
                for (int r2 = r; r2 <= down; r2++) {
                    int rows = r2 - r1 + 1;
                    if (rows > p.H) break;
                    // for these rows, check uncovered horizontal extent
                    // restricted to columns where ALL rows r1..r2 are uncovered
                    int lo = left, hi = right;
                    // shrink lo
                    int curLo = c;
                    while (curLo - 1 >= lo) {
                        bool ok = true;
                        for (int rr = r1; rr <= r2; rr++)
                            if (s.owner[rr][curLo - 1] != -1) { ok = false; break; }
                        if (!ok) break;
                        curLo--;
                    }
                    int curHi = c;
                    while (curHi + 1 <= hi) {
                        bool ok = true;
                        for (int rr = r1; rr <= r2; rr++)
                            if (s.owner[rr][curHi + 1] != -1) { ok = false; break; }
                        if (!ok) break;
                        curHi++;
                    }
                    // try widths from largest down; first valid is best for these rows
                    int maxCols = p.H / rows;
                    for (int c1 = curLo; c1 <= c; c1++) {
                        for (int c2 = c; c2 <= curHi; c2++) {
                            int cols = c2 - c1 + 1;
                            if (cols > maxCols) break;
                            int area = rows * cols;
                            if (area > p.H) break;
                            if (area <= bestArea) continue;
                            if (sliceIsValid(p, ps, r1, c1, r2, c2)) {
                                bestArea = area;
                                bR1 = r1; bC1 = c1; bR2 = r2; bC2 = c2;
                            }
                        }
                    }
                }
            }
            if (bestArea > 0) {
                Slice sl{bR1, bC1, bR2, bC2};
                int idx = (int)s.slices.size();
                s.slices.push_back(sl);
                paintSlice(s, idx);
                gained += bestArea;
            }
        }
    }
    return gained;
}

// Greedy refill within a bounding box [R1..R2] x [C1..C2]:
// Cells of removedSlices are uncovered; place new slices only inside the box,
// covering only currently-uncovered cells. Returns total cells covered by
// the new slices.
static int greedyRefillBox(Solution& s, const Problem& p, const PrefixSum& ps,
                           int R1, int C1, int R2, int C2,
                           vector<int>& newSliceIdx) {
    int covered = 0;
    for (int r = R1; r <= R2; r++) {
        for (int c = C1; c <= C2; c++) {
            if (s.owner[r][c] != -1) continue;
            // find best slice with top-left at... no, slice covering (r,c)
            // restricted to box.
            int bestArea = 0;
            int bR1=0,bC1=0,bR2=0,bC2=0;
            int up = r;
            while (up - 1 >= R1 && s.owner[up - 1][c] == -1) up--;
            int down = r;
            while (down + 1 <= R2 && s.owner[down + 1][c] == -1) down++;
            int left = c;
            while (left - 1 >= C1 && s.owner[r][left - 1] == -1) left--;
            int right = c;
            while (right + 1 <= C2 && s.owner[r][right + 1] == -1) right++;

            for (int r1 = up; r1 <= r; r1++) {
                for (int r2 = r; r2 <= down; r2++) {
                    int rows = r2 - r1 + 1;
                    if (rows > p.H) break;
                    int curLo = c;
                    while (curLo - 1 >= left) {
                        bool ok = true;
                        for (int rr = r1; rr <= r2; rr++)
                            if (s.owner[rr][curLo - 1] != -1) { ok = false; break; }
                        if (!ok) break;
                        curLo--;
                    }
                    int curHi = c;
                    while (curHi + 1 <= right) {
                        bool ok = true;
                        for (int rr = r1; rr <= r2; rr++)
                            if (s.owner[rr][curHi + 1] != -1) { ok = false; break; }
                        if (!ok) break;
                        curHi++;
                    }
                    int maxCols = p.H / rows;
                    for (int c1 = curLo; c1 <= c; c1++) {
                        for (int c2 = c; c2 <= curHi; c2++) {
                            int cols = c2 - c1 + 1;
                            if (cols > maxCols) break;
                            int area = rows * cols;
                            if (area > p.H) break;
                            if (area <= bestArea) continue;
                            if (sliceIsValid(p, ps, r1, c1, r2, c2)) {
                                bestArea = area;
                                bR1 = r1; bC1 = c1; bR2 = r2; bC2 = c2;
                            }
                        }
                    }
                }
            }
            if (bestArea > 0) {
                Slice sl{bR1, bC1, bR2, bC2};
                int idx = (int)s.slices.size();
                s.slices.push_back(sl);
                paintSlice(s, idx);
                newSliceIdx.push_back(idx);
                covered += bestArea;
            }
        }
    }
    return covered;
}

// Remove a target slice and any slice that is even partially within an
// expanded bounding box, then greedily refill the box. Accept iff strictly
// better total coverage in the affected region.
//
// Returns cells gained (>=0 means improvement actually applied; 0 if none).
static int doRemoveAndRefill(Solution& s, const Problem& p, const PrefixSum& ps,
                             int targetIdx, int pad, mt19937& rng) {
    if (targetIdx < 0 || targetIdx >= (int)s.slices.size()) return 0;
    Slice t = s.slices[targetIdx];
    int R1 = max(0, t.r1 - pad);
    int C1 = max(0, t.c1 - pad);
    int R2 = min(p.R - 1, t.r2 + pad);
    int C2 = min(p.C - 1, t.c2 + pad);

    // collect indices of slices fully inside box (we only remove fully-contained
    // slices to keep accounting simple)
    vector<int> toRemove;
    for (int i = 0; i < (int)s.slices.size(); i++) {
        const Slice& sl = s.slices[i];
        if (sl.r1 >= R1 && sl.r2 <= R2 && sl.c1 >= C1 && sl.c2 <= C2)
            toRemove.push_back(i);
    }
    if (toRemove.empty()) return 0;

    // compute current coverage in box from these slices
    int oldCovered = 0;
    for (int i : toRemove) oldCovered += s.slices[i].area();

    // snapshot: remember slices we are about to remove (so we can restore)
    vector<Slice> backup;
    backup.reserve(toRemove.size());
    for (int i : toRemove) backup.push_back(s.slices[i]);

    // remove them. We do swap-pop; do it from highest index down so swap target
    // doesn't move under us.
    sort(toRemove.begin(), toRemove.end(), greater<int>());
    for (int i : toRemove) removeSlice(s, i);

    // greedy refill
    vector<int> newIdx;
    int newCovered = greedyRefillBox(s, p, ps, R1, C1, R2, C2, newIdx);

    if (newCovered > oldCovered) {
        return newCovered - oldCovered;
    }
    // revert: pop the new slices, then re-add the backups
    // pop new slices (highest index first)
    sort(newIdx.begin(), newIdx.end(), greater<int>());
    for (int i : newIdx) removeSlice(s, i);
    for (const Slice& sl : backup) {
        int idx = (int)s.slices.size();
        s.slices.push_back(sl);
        paintSlice(s, idx);
    }
    return 0;
}

// ---------- validation ----------

static bool validateSolution(const Solution& s, const Problem& p, const PrefixSum& ps,
                             string& err) {
    vector<vector<int>> own(p.R, vector<int>(p.C, -1));
    for (int i = 0; i < (int)s.slices.size(); i++) {
        const Slice& sl = s.slices[i];
        if (!sliceIsValid(p, ps, sl.r1, sl.c1, sl.r2, sl.c2)) {
            ostringstream o;
            o << "slice " << i << " (" << sl.r1 << "," << sl.c1 << ","
              << sl.r2 << "," << sl.c2 << ") invalid (size or ingredients)";
            err = o.str();
            return false;
        }
        for (int r = sl.r1; r <= sl.r2; r++)
            for (int c = sl.c1; c <= sl.c2; c++) {
                if (own[r][c] != -1) {
                    ostringstream o;
                    o << "overlap at (" << r << "," << c << ") between slices "
                      << own[r][c] << " and " << i;
                    err = o.str();
                    return false;
                }
                own[r][c] = i;
            }
    }
    return true;
}

// ---------- main ----------

int main(int argc, char** argv) {
    if (argc < 3) {
        cerr << "usage: " << argv[0]
             << " <input_file> <input_solution_file>"
             << " [output_file] [time_limit_seconds]\n";
        return 1;
    }
    string inputPath = argv[1];
    string solPath   = argv[2];
    string outPath   = (argc >= 4) ? argv[3] : (solPath + ".improved");
    double timeLimit = (argc >= 5) ? atof(argv[4]) : 300.0;

    Problem p = readProblem(inputPath);
    PrefixSum ps; ps.build(p);
    Solution s = readSolution(solPath, p);

    string err;
    if (!validateSolution(s, p, ps, err)) {
        cerr << "input solution invalid: " << err << "\n";
        return 1;
    }

    int initial = totalCovered(s);
    int maxScore = p.R * p.C;
    cerr << "Pizza " << p.R << "x" << p.C << " L=" << p.L << " H=" << p.H << "\n";
    cerr << "Initial score: " << initial << " / " << maxScore
         << " (" << (100.0 * initial / maxScore) << "%)\n";

    auto t0 = Clock::now();
    auto elapsed = [&]() {
        return chrono::duration<double>(Clock::now() - t0).count();
    };

    // initial cheap improvements
    int gained = 0;
    int g1 = doGrow(s, p, ps);
    int g2 = doFill(s, p, ps);
    gained += g1 + g2;
    if (gained > 0) {
        cerr << "  GROW gained " << g1 << ", FILL gained " << g2
             << " (score now " << totalCovered(s) << ")\n";
        // write improvement immediately
        writeSolution(outPath, s);
    }

    // local search loop: pick random slice, try remove-and-refill
    mt19937 rng(123456789u);
    int bestScore = totalCovered(s);
    long long iters = 0;
    long long acceptedIters = 0;
    double lastReportTime = elapsed();
    int lastReportScore = bestScore;

    int padCycle = 0;
    int pads[] = {1, 2, 3, 1, 2, 4, 1, 2, 3, 5};
    int padN = sizeof(pads) / sizeof(pads[0]);

    while (elapsed() < timeLimit && bestScore < maxScore) {
        if (s.slices.empty()) break;
        int idx = (int)(rng() % s.slices.size());
        int pad = pads[padCycle % padN];
        padCycle++;
        int g = doRemoveAndRefill(s, p, ps, idx, pad, rng);
        iters++;
        if (g > 0) {
            acceptedIters++;
            // a successful refill may have created room for cheap grows/fills
            doGrow(s, p, ps);
            int curr = totalCovered(s);
            if (curr > bestScore) {
                bestScore = curr;
                writeSolution(outPath, s);
            }
        }
        // periodic try a global FILL / GROW pass too (cheap)
        if ((iters % 5000) == 0) {
            int gg = doGrow(s, p, ps);
            int gf = doFill(s, p, ps);
            int curr = totalCovered(s);
            if (curr > bestScore) {
                bestScore = curr;
                writeSolution(outPath, s);
            }
            (void)gg; (void)gf;
        }
        // progress log every ~5s
        double now = elapsed();
        if (now - lastReportTime >= 5.0) {
            cerr << "  t=" << (int)now << "s  iters=" << iters
                 << "  accepted=" << acceptedIters
                 << "  score=" << bestScore
                 << "  (+ " << (bestScore - lastReportScore) << " in last "
                 << (int)(now - lastReportTime) << "s)\n";
            lastReportTime = now;
            lastReportScore = bestScore;
        }
    }

    // final validation
    if (!validateSolution(s, p, ps, err)) {
        cerr << "FINAL solution invalid: " << err << "\n";
        return 1;
    }
    int finalScore = totalCovered(s);
    cerr << "Final score: " << finalScore << " / " << maxScore
         << " (improved by " << (finalScore - initial) << ")\n";
    cerr << "Total iters: " << iters << "  accepted: " << acceptedIters << "\n";
    cerr << "Output written to: " << outPath << "\n";
    return 0;
}
