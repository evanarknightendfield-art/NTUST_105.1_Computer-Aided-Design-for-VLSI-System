#pragma once
#include "tree.h"

// Globally best legal integer coordinate on one horizontal/vertical line,
// for fixed topology and the other coordinate held at its current value.
// Primary objective: actual Score. Ties minimize arrival variance and then
// local wire length. The current buffer location remains an allowed candidate.
// Returns false if the current position wins. Runtime depends on fanout,
// library size, ancestor siblings, and occupied points on the line, not die size.
bool exactCoordinateSpot(const Tree& T, int b, bool xAxis, int& bx, int& by);
