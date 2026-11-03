#pragma once
#include "DotaNativePosition.h"
#include "AoE2NativeDiscovery.h"
#include <cstdint>

// Read existing Widget state on its GUI thread using a discovered layout.
// Discovery scans module sections once on a worker; no game functions or writes.
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

inline bool AoE2NativeReadLayout(const aoe2_native::Layout& layout,
                                  const RECT& client, RECT* result, int* reason) {
#if defined(_M_X64)
  *reason = 23;
  const BYTE* holder = AoE2NativePointer(layout.managerSlot);
  if (!holder) return false;
  const BYTE* manager = AoE2NativePointer(holder);
  if (!manager) return false;
  const BYTE* begin = AoE2NativePointer(manager + layout.collections);
  const BYTE* end = AoE2NativePointer(manager + layout.collections + sizeof(void*));
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
    if (!vtable || aoe2_native::TableMethod(layout.image,vtable,24,1)!=layout.focusSetter)
      continue;
    const BYTE* control = AoE2NativePointer(collection + layout.focus);
    if (!control || AoE2NativePointer(control) != layout.textTable ||
        AoE2NativePointer(control + layout.owner) != collection) continue;
    auto type=aoe2_native::TableMethod(layout.image,layout.textTable,14,8);
    if (type!=layout.typeGetter || memcmp(type,"\x48\x8d\x05",3) || type[7]!=0xc3)
      return false;
    auto name=aoe2_native::Rip(type+7,type+3);
    if (!layout.image.ReadOnly(name,13) || memcmp(name,"TextBoxInput",13)) return false;
    uint64_t flags = 0;
    memcpy(&flags, control + layout.flags, sizeof(flags));
    if (!(flags & layout.focusMask)) continue;
    float rect[4] = {}, transform[8] = {};
    memcpy(rect, control + layout.rect, sizeof(rect));
    memcpy(transform, collection + layout.transform, sizeof(transform));
    *reason = 25;
    RECT area={};
    if (!AoE2NativeLayout(rect, transform, client, &area)) return false;
    // Recheck ownership/focus before publishing the sampled geometry.
    if (AoE2NativePointer(collection + layout.focus) != control ||
        AoE2NativePointer(control + layout.owner) != collection) return false;
    memcpy(&flags,control+layout.flags,sizeof(flags));
    if (!(flags&layout.focusMask)) return false;
    *result=area;
    *reason = 0;
    return true;
  }
  return false;
#else
  (void)layout; (void)client; (void)result;
  *reason = 26;
  return false;
#endif
}

inline bool AoE2NativeReadLayoutSafe(const aoe2_native::Layout& layout,
                                      const RECT& client, RECT* result, int* reason) {
  __try { return AoE2NativeReadLayout(layout,client,result,reason); }
  __except (EXCEPTION_EXECUTE_HANDLER) { *reason=27; return false; }
}

inline bool AoE2NativeRead(const BYTE* base, const RECT& client, RECT* result,
                           int* reason) {
#if defined(_M_X64)
  *reason=22;
  aoe2_native::Layout layout;
  if (!aoe2_native::GetLayout(base,&layout,reason)) return false;
  return AoE2NativeReadLayout(layout,client,result,reason);
#else
  (void)base; (void)client; (void)result;
  *reason=26;
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
