#pragma once
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(__clang__)
#include <intrin.h>
// QuickJS-NG 0.17 的 ctz64 在 x86 MSVC 下调用了仅 x64 提供的 intrinsic。
// 从低到高查找首个置位位，将 0..63 的位置写入 index；全零返回 0 且不保证 index。
static __forceinline unsigned char nsq_BitScanForward64(unsigned long* index, unsigned __int64 mask) {
    if (_BitScanForward(index, (unsigned long)mask)) return 1;
    if (_BitScanForward(index, (unsigned long)(mask >> 32))) { *index += 32; return 1; }
    return 0;
}
#define _BitScanForward64 nsq_BitScanForward64
#endif
