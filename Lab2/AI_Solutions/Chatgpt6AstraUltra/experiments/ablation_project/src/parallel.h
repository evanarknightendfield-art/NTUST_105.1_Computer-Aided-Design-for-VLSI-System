// parallel.h — 兩條執行緒「分工」最佳化同一棵樹（大測資用）
//
// 1. makeCut：從 SRC 往下，一直把「sink 最多的節點」展開成它的 children，
//    直到每棵子樹的 sink 數都 <= N * maxFrac。展開過的節點凍結（這一段時間不能改），
//    剩下的子樹根（區域根）依座標沿 x 或 y 排序，按 sink 數對半分成兩組。
// 2. 兩條執行緒各拿一份樹的副本，只能改「自己那組子樹裡」的結構；區域根的位置與 parent 不能動，
//    所以凍結節點的線長永遠不會同時被兩邊改到。
// 3. mergeCut：以執行緒 0 的副本為底，把第 1 組子樹整個換成執行緒 1 的版本（新 buffer 重新編號）。
#pragma once
#include <vector>

#include "tree.h"

struct Cut {
    std::vector<signed char> region;   // 每個節點屬於哪一組（0 / 1），凍結節點為 -1
    std::vector<char> isRoot;          // 區域根
    std::vector<int> roots[2];         // 兩組的區域根
};

// axis = 0 依 x 分組、1 依 y；失敗（樹太小、分不成兩組）回傳 false
bool makeCut(const Tree& T, int axis, double maxFrac, Cut& cut);

// 把 A（第 0 組）與 B（第 1 組）合併到 M；座標衝突等原因無法合併時回傳 false
bool mergeCut(const Tree& A, const Tree& B, const Cut& cut, Tree& M);
