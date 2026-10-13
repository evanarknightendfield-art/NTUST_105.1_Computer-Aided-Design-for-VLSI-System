// Standalone regression/property test for sparse structural checkpoints.
// Compile from the project root with tree.cpp and problem.cpp, using -Isrc.
#include "tree.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

template <typename T>
void equalArray(const std::vector<T>& actual, const std::vector<T>& expected,
                const std::string& name, const std::string& context) {
    require(actual.size() == expected.size(), context + ": " + name + " size");
    for (size_t i = 0; i < actual.size(); ++i)
        require(actual[i] == expected[i], context + ": " + name + "[" + std::to_string(i) + "]");
}

void equalSnapshots(const Snapshot& actual, const Snapshot& expected, const std::string& context) {
    equalArray(actual.x, expected.x, "x", context);
    equalArray(actual.y, expected.y, "y", context);
    equalArray(actual.parent, expected.parent, "parent", context);
    equalArray(actual.kind, expected.kind, "kind", context);
    equalArray(actual.alive, expected.alive, "alive", context);
}

void verifyCheckpoint(Tree& tree, Snapshot& sparse, const std::string& context) {
    Snapshot full;
    tree.save(full);
    tree.saveCheckpoint(sparse);
    equalSnapshots(sparse, full, context);
}

struct Fixture {
    Problem problem;
    TypeTable table;
    Tree tree;
    Snapshot sparse;
    std::mt19937 rng;

    explicit Fixture(unsigned seed) : rng(seed) {
        problem.dimx = problem.dimy = 1024;
        problem.srcX = problem.srcY = 1;
        problem.srcF = 128;
        problem.srcL = 1000000000;
        problem.wSkew = 1;
        problem.lib.push_back({"TEST", 128, 1000000000, 7});
        for (int i = 0; i < 32; ++i) {
            problem.sinkX.push_back(20 + i * 3);
            problem.sinkY.push_back(30 + i * 5);
        }
        table.build(problem.lib);
        tree.init(problem, table);
        for (int i = 0; i < 4; ++i) {
            int b = tree.addBuffer(200 + i * 10, 300 + i * 10);
            tree.link(b, 0);
            for (int j = 0; j < 8; ++j) tree.link(i * 8 + j + 1, b);
        }
        tree.recomputeAll();
        tree.startCheckpoints(sparse);
        Snapshot full;
        tree.save(full);
        equalSnapshots(sparse, full, "start");
    }

    std::pair<int, int> freePoint() {
        while (true) {
            int x = 1 + rng() % 1022, y = 1 + rng() % 1022;
            if (tree.usable(x, y)) return {x, y};
        }
    }

    std::vector<int> buffers() const {
        std::vector<int> ids;
        for (size_t i = tree.N + 1; i < tree.nd.size(); ++i)
            if (tree.nd[i].alive) ids.push_back(static_cast<int>(i));
        return ids;
    }

    int insertAbove(int child) {
        int parent = tree.nd[child].parent;
        auto point = freePoint();
        int b = tree.addBuffer(point.first, point.second);
        tree.unlink(child);
        tree.link(b, parent);
        tree.link(child, b);
        return b;
    }

    void dissolve(int b) {
        int parent = tree.nd[b].parent;
        auto children = tree.nd[b].ch;
        for (int child : children) {
            tree.unlink(child);
            tree.link(child, parent);
        }
        tree.unlink(b);
        tree.removeBuffer(b);
    }

    void randomEdit() {
        auto ids = buffers();
        unsigned action = rng() % 7;
        if (ids.empty() || (action == 0 && ids.size() < 96)) {
            insertAbove(1 + rng() % tree.N);
        } else if (action == 1) {
            dissolve(ids[rng() % ids.size()]);
        } else if (action == 2) {
            int b = ids[rng() % ids.size()];
            auto point = freePoint();
            tree.moveTo(b, point.first, point.second);
        } else if (action == 3) {
            int a = 1 + rng() % tree.N, b = 1 + rng() % tree.N;
            if (a != b) {
                int pa = tree.nd[a].parent, pb = tree.nd[b].parent;
                tree.unlink(a); tree.unlink(b);
                tree.link(a, pb); tree.link(b, pa);
            }
        } else if (action == 4) {
            // Only child order changes; snapshots should remain unchanged.
            int child = 1 + rng() % tree.N;
            int parent = tree.nd[child].parent;
            tree.unlink(child);
            tree.link(child, parent);
        } else if (action == 5) {
            // Exercise an allocated-then-deleted slot within one transaction.
            auto point = freePoint();
            int b = tree.addBuffer(point.first, point.second);
            tree.removeBuffer(b);
        }
        // action 6 touches derived statistics only.
        tree.recomputeAll();
    }
};

void edgeCases() {
    Fixture f(1);
    Tree& t = f.tree;
    Snapshot& s = f.sparse;

    size_t beforeSize = t.nd.size();
    t.begin();
    int fresh = f.insertAbove(1);
    t.recomputeAll();
    t.rollback();
    require(t.nd.size() == beforeSize + 1 && !t.nd[fresh].alive, "rejected allocation leaves dead slot");
    verifyCheckpoint(t, s, "rejected new dead slot");

    t.begin();
    int reused = f.insertAbove(2);
    require(reused == fresh, "reuse rejected new slot");
    t.recomputeAll();
    t.commit();
    verifyCheckpoint(t, s, "reused slot committed");

    t.begin();
    f.dissolve(reused);
    t.recomputeAll();
    t.commit();
    verifyCheckpoint(t, s, "deleted buffer");

    t.begin();
    int reusedAgain = f.insertAbove(3);
    require(reusedAgain == reused, "reuse deleted buffer");
    t.recomputeAll();
    verifyCheckpoint(t, s, "checkpoint inside trial");
    Snapshot trial = s;
    t.rollback();
    equalSnapshots(s, trial, "rollback preserves recorded trial");
    verifyCheckpoint(t, s, "checkpoint after saved trial rollback");

    t.begin();
    f.insertAbove(4);
    t.recomputeAll();
    verifyCheckpoint(t, s, "second saved trial");
    t.rollback();
    t.begin();
    f.insertAbove(5);
    t.recomputeAll();
    t.commit();
    verifyCheckpoint(t, s, "saved trial rollback followed by commit");

    t.stopCheckpoints();
    t.startCheckpoints(s);
    verifyCheckpoint(t, s, "restart checkpoints");
    t.load(s);
    t.startCheckpoints(s);
    verifyCheckpoint(t, s, "load and restart checkpoints");
}

void randomCases() {
    unsigned long checkpoints = 0;
    for (unsigned seed = 1; seed <= 32; ++seed) {
        Fixture f(seed);
        for (unsigned step = 0; step < 4000; ++step) {
            std::string context = "seed=" + std::to_string(seed) + " step=" + std::to_string(step);
            f.tree.begin();
            for (unsigned edits = 1 + f.rng() % 4; edits; --edits) f.randomEdit();
            bool saveTrial = f.rng() % 4 == 0;
            if (saveTrial) {
                verifyCheckpoint(f.tree, f.sparse, context + " trial");
                ++checkpoints;
            }
            Snapshot priorRecord = f.sparse;
            if (f.rng() % 2) f.tree.commit();
            else f.tree.rollback();
            equalSnapshots(f.sparse, priorRecord, context + " record immutable");
            if (f.rng() % 5 == 0) {
                verifyCheckpoint(f.tree, f.sparse, context + " settled");
                ++checkpoints;
            }
            if (step % 251 == 0) {
                verifyCheckpoint(f.tree, f.sparse, context + " before load");
                f.tree.load(f.sparse);
                f.tree.startCheckpoints(f.sparse);
                ++checkpoints;
            }
        }
        verifyCheckpoint(f.tree, f.sparse, "final seed=" + std::to_string(seed));
        ++checkpoints;
    }
    std::cout << "PASS: 128000 randomized transactions, " << checkpoints
              << " checkpoint comparisons, all five Snapshot arrays exact\n";
}
}  // namespace

int main() {
    edgeCases();
    randomCases();
}
