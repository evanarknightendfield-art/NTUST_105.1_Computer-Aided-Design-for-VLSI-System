# Clock Buffer Insertion

## Requirements

- CMake 3.10 or later
- A compiler supporting C++17
- C++ standard library only

## Build

```bash
cmake -S . -B build
cmake --build build
```

The executable is `build/cbi`. The default build type is Release.

## Run

```bash
./build/cbi INPUT_FILE OUTPUT_FILE
```

Example:

```bash
./build/cbi inputs/input001.cbi results/output001.cbi
```

The output directory must already exist.

Standard output reports `T_max`, `T_min`, and `Score`.
Construction and optimization statistics are printed to standard error.

## Algorithm

The initial tree is constructed bottom-up. Unconnected nodes are
sorted by Morton order and greedily grouped. A buffer position is
searched near the coordinate-wise median of each group, subject to
fanout, total outgoing length, boundary, and coordinate constraints.

When grouping cannot progress, relay buffers are inserted toward
the clock source. Construction finishes when the remaining nodes
can all be connected to the source within its driving limits.

Optimization uses randomized hill climbing. Buffer positions are
perturbed with several movement radii. Each candidate is checked
by a full tree evaluation and retained only if it is legal and its
Score does not increase.

For each buffer, the cheapest library type satisfying its current
fanout and total outgoing length is selected.

Score = total buffer cost + w_skew * (T_max - T_min).