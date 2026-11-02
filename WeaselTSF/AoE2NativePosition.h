#pragma once
#include "DotaNativePosition.h"
#include <cstdint>

// AoE2 DE 101.103.57257.0. Only read existing Widget state on its GUI thread.
// No game functions, synchronous window messages, scanning, or target writes.
inline const BYTE* AoE2NativePointer(const void* address) {
  uintptr_t value = 0;
  memcpy(&value, address, sizeof(value));
  if (value < 0x10000 || value > 0x00007fffffffffffULL || value % sizeof(void*))
    return nullptr;
  return reinterpret_cast<const BYTE*>(value);
}

inline bool AoE2NativeLayout(const float* rect, const float* transform,
                             const RECT& client, RECT* result) {
  if (!rect || !transform || !result) return false;
  for (int i = 0; i < 4; ++i)
    if (!std::isfinite(rect[i])) return false;
  for (int i = 0; i < 8; ++i)
    if (!std::isfinite(transform[i])) return false;
  const float width = static_cast<float>(client.right - client.left);
  const float height = static_cast<float>(client.bottom - client.top);
  // Refuse stale layout or a mismatched DPI coordinate space.
  if (width <= 0 || height <= 0 ||
      std::fabs(transform[0] - width) > 1 ||
      std::fabs(transform[1] - height) > 1 ||
      transform[4] <= 0 || transform[5] <= 0 ||
      transform[6] < 0.01f || transform[7] < 0.01f ||
      transform[6] > 100 || transform[7] > 100) return false;
  return DotaNativeArea(rect[0] / transform[6] + transform[2],
                        rect[1] / transform[7] + transform[3],
                        rect[2] / transform[6], rect[3] / transform[7],
                        client, result);
}

inline bool AoE2NativeRead(const BYTE* base, const RECT& client, RECT* result,
                           int* reason) {
#if defined(_WIN64)
  *reason = 22;
  if (!DotaNativeBuild(reinterpret_cast<HMODULE>(const_cast<BYTE*>(base)),
                       0x6acf93a9, 0x4f24000)) return false;
  const BYTE typeCode[] = {0x48,0x8d,0x05,0xc9,0xce,0x6f,0x02,0xc3};
  // Lazy-decrypted executable code must already be available; never call it.
  if (memcmp(base + 0xb1ce00, typeCode, sizeof(typeCode)) ||
      memcmp(base + 0x3219cd0, "TextBoxInput", 13) ||
      AoE2NativePointer(base + 0x321d998 + 0x70) != base + 0xb1ce00)
    return false;
  *reason = 23;
  const BYTE* holder = AoE2NativePointer(base + 0x4293138);
  if (!holder) return false;
  const BYTE* manager = AoE2NativePointer(holder);
  if (!manager) return false;
  const BYTE* begin = AoE2NativePointer(manager + 0x300);
  const BYTE* end = AoE2NativePointer(manager + 0x308);
  const uintptr_t first = reinterpret_cast<uintptr_t>(begin);
  const uintptr_t last = reinterpret_cast<uintptr_t>(end);
  if (!begin || !end || last < first || last - first > 128 * sizeof(void*) ||
      (last - first) % sizeof(void*)) return false;
  const size_t count = (last - first) / sizeof(void*);
  const BYTE* collections[128] = {};
  memcpy(collections, begin, count * sizeof(void*));
  *reason = 24;
  // Active collections are ordered back-to-front; a modal takes precedence.
  for (size_t i = count; i; --i) {
    const BYTE* collection = collections[i - 1];
    if (reinterpret_cast<uintptr_t>(collection) < 0x10000 ||
        reinterpret_cast<uintptr_t>(collection) % sizeof(void*)) continue;
    const BYTE* vtable = AoE2NativePointer(collection);
    if (!vtable || reinterpret_cast<uintptr_t>(vtable) <
                       reinterpret_cast<uintptr_t>(base) + 0x3000000 ||
        reinterpret_cast<uintptr_t>(vtable) >=
                       reinterpret_cast<uintptr_t>(base) + 0x4000000) continue;
    const BYTE* control = AoE2NativePointer(collection + 0x128);
    if (!control || AoE2NativePointer(control) != base + 0x321d998 ||
        AoE2NativePointer(control + 0xd0) != collection) continue;
    uint64_t flags = 0;
    memcpy(&flags, control + 0x3d8, sizeof(flags));
    if (!(flags & 0x80)) continue;
    float rect[4] = {}, transform[8] = {};
    memcpy(rect, control + 0x100, sizeof(rect));
    memcpy(transform, collection + 0x138, sizeof(transform));
    *reason = 25;
    if (!AoE2NativeLayout(rect, transform, client, result)) return false;
    // Recheck ownership/focus before publishing the sampled geometry.
    if (AoE2NativePointer(collection + 0x128) != control ||
        AoE2NativePointer(control + 0xd0) != collection) return false;
    *reason = 0;
    return true;
  }
  return false;
#else
  *reason = 26;
  return false;
#endif
}

inline bool AoE2NativeReadSafe(const BYTE* base, const RECT& client,
                               RECT* result, int* reason) {
  __try { return AoE2NativeRead(base, client, result, reason); }
  __except (EXCEPTION_EXECUTE_HANDLER) { *reason = 27; return false; }
}

inline bool GetAoE2NativePosition(HWND window, const RECT& client, RECT* result,
                                  int* reason) {
  *reason = 20;
  wchar_t executable[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, executable, MAX_PATH);
  const wchar_t* name = wcsrchr(executable, L'\\');
  if (_wcsicmp(name ? name + 1 : executable, L"AoE2DE_s.exe") != 0)
    return false;
  *reason = 21;
  DWORD pid = 0;
  if (GetWindowThreadProcessId(window, &pid) != GetCurrentThreadId() ||
      pid != GetCurrentProcessId()) return false;
  return AoE2NativeReadSafe(reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr)),
                            client, result, reason);
}
