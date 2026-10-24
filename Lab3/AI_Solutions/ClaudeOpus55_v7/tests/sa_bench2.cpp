// sa_bench2 (measurement tool, added after the algorithm was frozen) — like tests/sa_bench.cpp (a common Stage-2
// driver compiled against one version's sources), plus SA options, so that
// SA settings can be compared from the same legal start.
//   sa_bench2 ORIGINAL LEGAL_PREFIX OUT [--seed N] [--time S] [--polish S] [--threads N]
//             [--sync M] [--p0 P] [--moves N] [--effort K] [--starved A]
// prints: hpwl,attempts,sa_seconds,stage2_seconds
#include "annealer.hpp"
#include "optimizer.hpp"
#include "window.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error("sa_bench2 original legal_prefix out [options]");
        PlaceDB db, loc;
        std::string err;
        if (!db.read(argv[1], err) || !loc.read(argv[2], err)) throw std::runtime_error(err);
        Placement p;
        p.seg.assign(db.nodes.size(), -1);
        p.site.assign(db.nodes.size(), 0);
        for (int v : db.movable)
            for (size_t s = 0; s < db.segs.size(); ++s) {
                const auto& sg = db.segs[s];
                double x = (loc.nodes[v].gx - sg.x0) / sg.sp;
                if (std::abs(loc.nodes[v].gy - sg.y) < 1e-6 && x >= -1e-6 && x + db.sitesOf(v, sg) <= sg.numSites + 1e-6 &&
                    std::abs(x - std::round(x)) < 1e-6) {
                    p.seg[v] = int(s);
                    p.site[v] = int(std::round(x));
                    break;
                }
            }
        if (!checkLegal(db, p).empty()) throw std::runtime_error(checkLegal(db, p));
        const Placement legal = p;
        SAOptions opt;
        double polishSec = 30;
        opt.timeLimit = 60;
        opt.threads = 0;
        for (int i = 4; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&] { if (i + 1 >= argc) throw std::runtime_error("missing value"); return std::string(argv[++i]); };
            if (a == "--seed") opt.seed = std::stoull(next());
            else if (a == "--time") opt.timeLimit = std::stod(next());
            else if (a == "--polish") polishSec = std::stod(next());
            else if (a == "--threads") opt.threads = std::stoi(next());
            else if (a == "--sync") opt.syncMoves = std::stod(next());
            else if (a == "--p0") opt.p0 = std::stod(next());
            else if (a == "--moves") { opt.minMoves = opt.maxMoves = std::stod(next()); opt.movesPerCell = 0; }
            else if (a == "--effort") { opt.movesPerCell = std::stod(next()); opt.maxMoves = 1e18; }
            else if (a == "--starved") opt.starvedAttemptsPerCell = std::stod(next());   // v7 (0 = SA keeps --time)
            else throw std::runtime_error("unknown option " + a);
        }
        double start = wallSeconds();
        auto stats = anneal(db, p, opt);
        if (polishSec > 0) {
            OptimizeOptions polish;
            polish.until = wallSeconds() + polishSec;
            polishHPWL(db, p, polish);
            WindowOptions w;
            w.timeLimit = std::max(0.0, polish.until - wallSeconds());
            optimizeWindowsHPWL(db, p, w);
            reorderWindows(db, p, 2, false, polish.until);
        }
        if (!checkLegal(db, p).empty() || totalHPWL(db, p) > totalHPWL(db, legal) + 1e-5)
            throw std::runtime_error("Stage 2 invariant");
        writePl(db, p, std::string(argv[3]) + ".pl");
        std::printf("hpwl,attempts,sa_seconds,stage2_seconds\n%.12f,%lld,%.6f,%.6f\n", totalHPWL(db, p), stats.attempts,
                    stats.seconds, wallSeconds() - start);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
