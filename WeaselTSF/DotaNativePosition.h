#pragma once
#include <windows.h>
#include <cmath>
#include <cstring>

// Only the locally inspected game build is supported. This adapter reads the
// game's existing focused-control geometry; it installs no hooks or timers.
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
#if defined(_WIN64)
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
  if (!DotaNativeBuild(ime, 0x6ac449d4, 0x3e000) ||
      !DotaNativeBuild(client, 0x6ae0f906, 0x6e3a000) ||
      !DotaNativeBuild(panorama, 0x6ac5739e, 0x62d000)) return false;
  auto ib = reinterpret_cast<BYTE*>(ime);
  auto cb = reinterpret_cast<BYTE*>(client);
  auto pb = reinterpret_cast<BYTE*>(panorama);
  const BYTE factoryCode[] = {0x48,0x8d,0x05,0x29,0xb1,0x02,0x00,0xc3};
  const BYTE positionCode[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x4c,0x8d,0x4a,0x04,0x48,0x8b,0xda};
  if (memcmp(ib+0xc970,factoryCode,sizeof(factoryCode)) ||
      memcmp(cb+0x38b4090,positionCode,sizeof(positionCode))) return false;
  using Factory = void* (__cdecl*)(const char*, int*);
  auto factory = reinterpret_cast<Factory>(GetProcAddress(ime,"CreateInterface"));
  *reason = 4;
  if (!factory) return false;
  BYTE* manager = static_cast<BYTE*>(factory("IMEManager001",nullptr));
  // The checked factory returns this already constructed singleton.
  if (manager != ib+0x37aa0) return false;
  BYTE* owner = *reinterpret_cast<BYTE**>(manager+0x58);
  *reason = 5;
  if (!owner || *reinterpret_cast<BYTE**>(owner) != ib+0x2f5b8) return false;
  BYTE* view = *reinterpret_cast<BYTE**>(owner+8);
  *reason = 6;
  if (!view || *reinterpret_cast<BYTE**>(view) != pb+0x4c2448) return false;
  using FocusedControl = void* (__fastcall*)(void*);
  auto getControl = reinterpret_cast<FocusedControl>(pb+0x9c420);
  BYTE* control = static_cast<BYTE*>(getControl(view));
  *reason = 7;
  if (!control || *reinterpret_cast<BYTE**>(control) != cb+0x506b450) return false;
  BYTE* panel = *reinterpret_cast<BYTE**>(control-0x20);
  *reason = 8;
  if (!panel || *reinterpret_cast<BYTE**>(panel) != pb+0x4cb1a8) return false;
  using Position = void* (__fastcall*)(void*, float*);
  auto getPosition = reinterpret_cast<Position>(cb+0x38b4090);
  float position[2] = {};
  getPosition(control-0x28,position);
  float width = *reinterpret_cast<float*>(panel+0x1b8);
  float height = *reinterpret_cast<float*>(panel+0x1bc);
  *reason = 9;
  if (!DotaNativeArea(position[0],position[1],width,height,clientArea,result)) return false;
  *reason = 0;
  return true;
#else
  *reason = 10;
  return false;
#endif
}

inline bool GetDotaNativePosition(HWND window, const RECT& client, RECT* result, int* reason) {
  // No C++ objects needing unwinding in this SEH boundary.
  __try { return DotaNativePositionImpl(window,client,result,reason); }
  __except(EXCEPTION_EXECUTE_HANDLER) { *reason = 11; return false; }
}
