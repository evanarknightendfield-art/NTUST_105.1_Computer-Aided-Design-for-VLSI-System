// improve.hpp — v4：Stage 1 最後的兩個步驟（都不會讓最大位移變大）
//
//   matchSameSize：同樣大小的 cells 互換位置不會影響合法性。把附近一群同大小的 cells 和它們
//                  佔的位置做最小成本匹配（Hungarian），總位移最小。[講義 L07 的 matching 合法化]
//   neutralSwap：  同樣大小的兩個 cell 交換後總位移沒有變多、HPWL 變少就換。位移的目標在很多方向是平的，
//                  匹配只挑了其中一個解，不看 net；這一步從平的那些解裡挑 HPWL 小的。
#pragma once
#include <chrono>

#include "placedb.hpp"

using Clock = std::chrono::steady_clock;

// 回傳總位移少了多少
double matchSameSize(const PlaceDB& db, Placement& P, double cap, Clock::time_point until, bool verbose);
// 回傳 HPWL 少了多少
double neutralSwap(const PlaceDB& db, Placement& P, double cap, Clock::time_point until, bool verbose);
