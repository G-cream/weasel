#pragma once
#include <windows.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <commctrl.h>
#include <set>
#pragma comment(lib, "comctl32.lib")
#include "DotaNativePosition.h"

// Local TSF-only rules: no changes to the server's IPC serialization format.
struct GameCompatibility {
  bool enabled = false;
  bool force_own_ui = false;
  bool window_position = false;
  bool detected_position = false;
  bool native_position = false;
  bool hide_system_ime_ui = false;
  bool debug = false;
  ULONGLONG detected_not_before = 0;
  int anchor_x = 250;  // thousandths of the client width
  int anchor_y = 780;  // thousandths of the client height
  int offset_x = 0;   // DIPs at 96 DPI
  int offset_y = 0;
};

inline int ClampGameValue(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

inline GameCompatibility LoadGameCompatibilityForApp(const wchar_t* path,
                                                     const wchar_t* app) {
  GameCompatibility result;
  if (!path || !app || !*app) return result;
  result.enabled = GetPrivateProfileIntW(app, L"enabled", 0, path) == 1;
  if (!result.enabled) return result;
  result.force_own_ui = GetPrivateProfileIntW(app, L"force_own_ui", 0, path) == 1;
  result.debug = GetPrivateProfileIntW(app, L"debug", 0, path) == 1;
  wchar_t position[32] = {};
  GetPrivateProfileStringW(app, L"position", L"caret", position, 32, path);
  result.window_position = _wcsicmp(position, L"window") == 0;
  result.detected_position = _wcsicmp(position, L"detected") == 0;
  result.native_position = _wcsicmp(position, L"native") == 0;
  result.hide_system_ime_ui = GetPrivateProfileIntW(app, L"hide_system_ime_ui", 0, path) == 1;
  result.anchor_x = ClampGameValue(static_cast<int>(GetPrivateProfileIntW(app, L"anchor_x", 250, path)), 0, 1000);
  result.anchor_y = ClampGameValue(static_cast<int>(GetPrivateProfileIntW(app, L"anchor_y", 780, path)), 0, 1000);
  result.offset_x = ClampGameValue(static_cast<int>(GetPrivateProfileIntW(app, L"offset_x", 0, path)), -4096, 4096);
  result.offset_y = ClampGameValue(static_cast<int>(GetPrivateProfileIntW(app, L"offset_y", 0, path)), -4096, 4096);
  return result;
}

inline std::wstring GameCompatibilityUserDirectory() {
  wchar_t directory[32768] = {};
  DWORD bytes = sizeof(directory);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Rime\\Weasel", L"RimeUserDir",
                   RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, directory, &bytes) != ERROR_SUCCESS || !*directory) {
    ExpandEnvironmentStringsW(L"%APPDATA%\\Rime", directory, 32768);
  }
  return std::wstring(directory);
}

inline void GameCompatibilityLog(const GameCompatibility& rule, const char* format, ...) {
  if (!rule.debug) return;
  std::wstring path = GameCompatibilityUserDirectory() + L"\\game_compat." + std::to_wstring(GetCurrentProcessId()) + L".log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  char buffer[1024] = {};
  int prefix = sprintf_s(buffer, "tick=%llu tid=%lu ", GetTickCount64(), GetCurrentThreadId());
  va_list args; va_start(args, format);
  vsnprintf_s(buffer+prefix, sizeof(buffer)-prefix-3, _TRUNCATE, format, args);
  va_end(args);
  size_t length = strlen(buffer); buffer[length++]='\r'; buffer[length++]='\n';
  DWORD written; WriteFile(file,buffer,static_cast<DWORD>(length),&written,nullptr);
  CloseHandle(file);
}

inline GameCompatibility LoadGameCompatibility() {
  std::wstring path = GameCompatibilityUserDirectory();
  if (path.empty()) return {};
  if (path.back() != L'\\' && path.back() != L'/') path += L'\\';
  path += L"game_compat.ini";
  wchar_t executable[32768] = {};
  DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
  if (!length || length >= 32768) return {};
  const wchar_t* app = wcsrchr(executable, L'\\');
  return LoadGameCompatibilityForApp(path.c_str(), app ? app + 1 : executable);
}

inline RECT GameCompatibilityAnchor(const GameCompatibility& rule, const RECT& client, UINT dpi) {
  const LONG width = client.right - client.left;
  const LONG height = client.bottom - client.top;
  LONG x = client.left + MulDiv(width, rule.anchor_x, 1000) + MulDiv(rule.offset_x, dpi, 96);
  LONG y = client.top + MulDiv(height, rule.anchor_y, 1000) + MulDiv(rule.offset_y, dpi, 96);
  x = ClampGameValue(x, client.left, client.right > client.left ? client.right - 1 : client.left);
  y = ClampGameValue(y, client.top, client.bottom > client.top ? client.bottom - 1 : client.top);
  return {x, y, x, y};
}

#pragma pack(push, 1)
struct GameDetectedPosition {
  DWORD magic;
  DWORD pid;
  ULONGLONG timestamp;
  RECT area;
  DWORD width;
  DWORD height;
  ULONGLONG window;
};
#pragma pack(pop)
static_assert(sizeof(GameDetectedPosition) == 48, "Position protocol must match provider");

inline bool IsGameDetectedPositionValid(const GameDetectedPosition& data, DWORD pid, HWND window,
                                        const RECT& client, ULONGLONG now) {
  return data.magic == 0x57475031 && data.pid == pid && data.window == reinterpret_cast<ULONGLONG>(window) &&
    data.timestamp <= now && now - data.timestamp <= 4000 &&
    data.width == static_cast<DWORD>(client.right-client.left) &&
    data.height == static_cast<DWORD>(client.bottom-client.top) &&
    data.area.left >= 0 && data.area.right <= client.right-client.left &&
    data.area.top >= 0 && data.area.bottom <= client.bottom-client.top &&
    data.area.right > data.area.left && data.area.bottom > data.area.top;
}

inline bool GetGameCompatibilityAnchor(const GameCompatibility& rule, HWND window, RECT* result) {
  if (!rule.enabled || (!rule.window_position && !rule.detected_position && !rule.native_position) || !window || !result) return false;
  HWND root = GetAncestor(window, GA_ROOT);
  if (!root) root = window;
  RECT client = {};
  if (!GetClientRect(root, &client) || client.right <= 0 || client.bottom <= 0) return false;
  POINT origin = {};
  if (!ClientToScreen(root, &origin)) return false;
  OffsetRect(&client, origin.x, origin.y);
  if (rule.native_position) {
    int reason = 0;
    bool ok = GetDotaNativePosition(root, client, result, &reason);
    GameCompatibilityLog(rule, "native valid=%d reason=%d area=%ld,%ld,%ld,%ld", ok,reason,
      result->left,result->top,result->right,result->bottom);
    return ok;
  }
  if (rule.detected_position) {
    std::wstring path = GameCompatibilityUserDirectory() + L"\\game_position." + std::to_wstring(GetCurrentProcessId()) + L".bin";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      GameCompatibilityLog(rule, "position open failed error=%lu", GetLastError());
      return false;
    }
    GameDetectedPosition data = {}; DWORD read = 0;
    BOOL ok = ReadFile(file, &data, sizeof(data), &read, nullptr);
    CloseHandle(file);
    bool valid = ok && read == sizeof(data) && data.timestamp >= rule.detected_not_before &&
        IsGameDetectedPositionValid(data, GetCurrentProcessId(), root, client, GetTickCount64());
    GameCompatibilityLog(rule, "position valid=%d read=%lu magic=%lx pid=%lu window=%llu expectedWindow=%llu time=%llu notBefore=%llu size=%lu,%lu client=%ld,%ld,%ld,%ld area=%ld,%ld,%ld,%ld", valid, read, data.magic, data.pid, data.window, reinterpret_cast<ULONGLONG>(root), data.timestamp, rule.detected_not_before, data.width, data.height, client.left,client.top,client.right,client.bottom,data.area.left,data.area.top,data.area.right,data.area.bottom);
    if (!valid) return false;
    *result = {client.left + data.area.left, client.top + data.area.bottom,
               client.left + data.area.left, client.top + data.area.bottom};
    return true;
  }
  using GetWindowDpi = UINT (WINAPI*)(HWND);
  static auto getWindowDpi = reinterpret_cast<GetWindowDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  UINT dpi = getWindowDpi ? getWindowDpi(root) : 96;
  *result = GameCompatibilityAnchor(rule, client, dpi ? dpi : 96);
  return true;
}

inline bool IsGameCompatibilityForeground(HWND window) {
  DWORD owner = 0, foreground = 0;
  GetWindowThreadProcessId(window, &owner);
  GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
  return owner == GetCurrentProcessId() && owner == foreground && !IsIconic(GetAncestor(window, GA_ROOT));
}

// Match only the current GUI thread's MSCTF compatibility frame.
inline bool IsDotaSystemImeFrame(HWND window) {
  DWORD pid = 0;
  if (GetWindowThreadProcessId(window, &pid) != GetCurrentThreadId() ||
      pid != GetCurrentProcessId()) return false;
  wchar_t name[64] = {};
  GetClassNameW(window,name,64);
  if (wcscmp(name,L"CiceroUIWndFrame") != 0) return false;
  HWND owner = GetWindow(window,GW_OWNER);
  DWORD ownerPid = 0;
  if (!owner || GetWindowThreadProcessId(owner,&ownerPid) != GetCurrentThreadId() ||
      ownerPid != GetCurrentProcessId()) return false;
  GetClassNameW(owner,name,64);
  return wcscmp(name,L"MSCTFIME UI") == 0;
}

// A thread-local creation observer attaches a normal window subclass BEFORE
// first show. The subclass removes SWP_SHOWWINDOW synchronously; no timer hide.
// Never patch game code, replace a class procedure, or hook another thread.
class GameSystemImeGuard {
  struct State {
    HHOOK hook = nullptr;
    unsigned users = 0;
    std::set<HWND> windows;
    GameCompatibility rule;
  };
  static State& Current() { static thread_local State state; return state; }
  static constexpr UINT_PTR subclassId = 0x57494d45;
  bool attached = false;
  static LRESULT CALLBACK Subclass(HWND window, UINT message, WPARAM wp,
                                   LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
      RemoveWindowSubclass(window,Subclass,subclassId);
      Current().windows.erase(window);
      return DefSubclassProc(window,message,wp,lp);
    }
    LRESULT result = DefSubclassProc(window,message,wp,lp);
    if (message == WM_WINDOWPOSCHANGING && lp && Current().users &&
        IsDotaSystemImeFrame(window)) {
      auto pos = reinterpret_cast<WINDOWPOS*>(lp);
      RECT rect = {};
      if (GetWindowRect(window,&rect)) {
        LONG x = (pos->flags & SWP_NOMOVE) ? rect.left : pos->x;
        LONG y = (pos->flags & SWP_NOMOVE) ? rect.top : pos->y;
        LONG width = (pos->flags & SWP_NOSIZE) ? rect.right-rect.left : pos->cx;
        LONG height = (pos->flags & SWP_NOSIZE) ? rect.bottom-rect.top : pos->cy;
        if ((pos->flags & SWP_SHOWWINDOW) && x == 0 && y == 0 &&
            width > 0 && height > 0 && width <= 160 && height <= 96) {
          pos->flags &= ~SWP_SHOWWINDOW;
          GameCompatibilityLog(Current().rule,"system-ime show blocked hwnd=%llu",reinterpret_cast<ULONGLONG>(window));
        }
      }
    }
    return result;
  }
  static BOOL CALLBACK Attach(HWND window, LPARAM) {
    DWORD pid = 0;
    if (GetWindowThreadProcessId(window,&pid) != GetCurrentThreadId() ||
        pid != GetCurrentProcessId()) return TRUE;
    wchar_t name[64] = {};
    GetClassNameW(window,name,64);
    // At WM_NCCREATE the owner may not yet be fully initialized. Check it at show.
    if (wcscmp(name,L"CiceroUIWndFrame") == 0 &&
        SetWindowSubclass(window,Subclass,subclassId,0))
      Current().windows.insert(window);
    return TRUE;
  }
  static LRESULT CALLBACK Observe(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && lp && Current().users) {
      auto message = reinterpret_cast<CWPSTRUCT*>(lp);
      if (message->message == WM_NCCREATE || message->message == WM_CREATE)
        Attach(message->hwnd,0);
    }
    return CallNextHookEx(Current().hook,code,wp,lp);
  }
 public:
  GameSystemImeGuard() = default;
  GameSystemImeGuard(const GameSystemImeGuard&) = delete;
  GameSystemImeGuard& operator=(const GameSystemImeGuard&) = delete;
  ~GameSystemImeGuard() { Stop(); }
  void Configure(const GameCompatibility& rule) {
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr,executable,MAX_PATH);
    const wchar_t* name = wcsrchr(executable,L'\\');
    if (!rule.enabled || !rule.hide_system_ime_ui ||
        _wcsicmp(name ? name+1 : executable,L"dota2.exe") != 0) {
      Stop(); return;
    }
    auto& state = Current();
    state.rule = rule;
    if (attached) return;
    if (!state.users) {
      state.hook = SetWindowsHookExW(WH_CALLWNDPROC,Observe,nullptr,GetCurrentThreadId());
      if (!state.hook) return;
    }
    ++state.users;
    attached = true;
    EnumThreadWindows(GetCurrentThreadId(),Attach,0);
  }
  void Stop() {
    if (!attached) return;
    attached = false;
    auto& state = Current();
    if (--state.users) return;
    UnhookWindowsHookEx(state.hook);
    state.hook = nullptr;
    for (HWND window : state.windows)
      RemoveWindowSubclass(window,Subclass,subclassId);
    state.windows.clear();
  }
};
