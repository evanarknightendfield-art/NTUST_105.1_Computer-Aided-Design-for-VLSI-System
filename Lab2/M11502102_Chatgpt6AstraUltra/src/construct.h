// construct.h — Stage 1：建構合法的初始 clock tree
#pragma once
#include "tree.h"

// 由下而上分群建樹。成功回傳 true（T 為一棵合法的樹）。
// singleRoot = false：frontier 一旦能直接接上 SRC 就接（buffer 最少）
// singleRoot = true ：一路合併到只剩一個根 buffer 再接 SRC（根 buffer 可以對齊各子樹的延遲）
bool buildInitial(Tree& T, bool singleRoot);

// FLUTE-inspired split portfolio; experimental unless benchmarked beneficial.
bool buildInitialSpatial(Tree& T, bool singleRoot = false);
