#include "../src/guard.hpp"
#include "../src/pool.hpp"
#include <cassert>
#include <stdexcept>

int main() {
    Placement p; p.seg = {0, 1}; p.site = {2, 3};
    const auto before = p;
    auto same = [&] { return p.seg == before.seg && p.site == before.site; };
    assert(!guardedStep("bad invariant", p, [&] { p.site[0] = 9; }, [] { return false; }));
    assert(same());
    assert(!guardedStep("exception", p, [&] { p.seg[0] = 99; throw std::runtime_error("injected"); }, [] { return true; }));
    assert(same());
    assert(!guardedStep("non-standard exception", p, [&] { p.site.clear(); throw 42; }, [] { return true; }));
    assert(same());
    Pool pool(2);
    assert(!guardedStep("worker failure", p, [&] {
        pool.run(2, [&](int i) { p.site[i] = 8; if (i == 1) throw std::runtime_error("worker"); });
    }, [] { return true; }));
    assert(same());
    assert(guardedStep("next step", p, [&] { p.site[0] = 4; }, [&] { return p.site[0] == 4; }));
    assert(p.site[0] == 4);
}
