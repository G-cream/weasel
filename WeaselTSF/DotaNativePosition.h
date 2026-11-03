#pragma once
#include <windows.h>
#include <cmath>
#include <cstring>
#include "DotaNativeInterfaces.h"

// Exact-build helper retained for the separate AoE2 adapter.
inline bool DotaNativeBuild(HMODULE module, DWORD stamp, DWORD size) {
  if (!module) return false;
  auto base = reinterpret_cast<const BYTE*>(module);
  auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == stamp &&
      nt->OptionalHeader.SizeOfImage == size;
}

inline bool DotaNativeArea(float x, float y, float width, float height,
                          const RECT& client, RECT* area) {
  if (!area || !std::isfinite(x) || !std::isfinite(y) ||
      !std::isfinite(width) || !std::isfinite(height) || x < 0 || y < 0 ||
      width <= 0 || height <= 0 || x + width > client.right-client.left + 1 ||
      y + height > client.bottom-client.top + 1) return false;
  LONG left = client.left + static_cast<LONG>(std::lround(x));
  LONG top = client.top + static_cast<LONG>(std::lround(y));
  // Retain the input area's top/bottom so upward placement avoids its text.
  *area = {left, top, left, client.top + static_cast<LONG>(std::lround(y+height))};
  return true;
}

inline bool DotaNativePositionImpl(HWND window, const RECT& clientArea, RECT* result,
                                   int* reason) {
#if defined(_M_X64)
  *reason = 1;
  wchar_t executable[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, executable, MAX_PATH);
  const wchar_t* name = wcsrchr(executable, L'\\');
  if (_wcsicmp(name ? name+1 : executable, L"dota2.exe") != 0) return false;
  *reason = 2;
  if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
  HMODULE ime = GetModuleHandleW(L"imemanager.dll");
  HMODULE client = GetModuleHandleW(L"client.dll");
  HMODULE panorama = GetModuleHandleW(L"panorama.dll");
  *reason = 3;
  dota_native::Image imeImage, clientImage, panoramaImage;
  if (!imeImage.Init(ime) || !clientImage.Init(client) || !panoramaImage.Init(panorama))
    return false;
  using Factory = void* (__cdecl*)(const char*, int*);
  auto factory = reinterpret_cast<Factory>(GetProcAddress(ime,"CreateInterface"));
  *reason = 4;
  if (!factory || !imeImage.Code(reinterpret_cast<const void*>(factory), 1)) return false;
  BYTE* manager = static_cast<BYTE*>(factory("IMEManager001",nullptr));
  dota_native::Geometry geometry = {};
  if (!dota_native::GeometryForManager(imeImage, clientImage, panoramaImage,
                                       manager, &geometry, reason)) return false;
  *reason = 9;
  if (!DotaNativeArea(geometry.x,geometry.y,geometry.width,geometry.height,
                       clientArea,result)) return false;
  *reason = 0;
  return true;
#else
  (void)window; (void)clientArea; (void)result;
  *reason = 10;
  return false;
#endif
}

inline bool GetDotaNativePosition(HWND window, const RECT& client, RECT* result, int* reason) {
  // No C++ objects needing unwinding in this SEH boundary.
  __try { return DotaNativePositionImpl(window,client,result,reason); }
  __except(EXCEPTION_EXECUTE_HANDLER) { *reason = 11; return false; }
}
