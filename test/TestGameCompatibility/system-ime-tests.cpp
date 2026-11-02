#include "../../WeaselTSF/GameCompatibility.h"
#include <cassert>
#include <cstdio>
static unsigned visibleTransitions = 0;
static LRESULT CALLBACK WindowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
  if (m == WM_WINDOWPOSCHANGED && (reinterpret_cast<WINDOWPOS*>(b)->flags & SWP_SHOWWINDOW))
    ++visibleTransitions;
  return DefWindowProcW(w,m,a,b);
}
int main() {
  WNDCLASSW wc = {}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc=WindowProc;
  wc.lpszClassName=L"MSCTFIME UI"; assert(RegisterClassW(&wc));
  wc.lpszClassName=L"CiceroUIWndFrame"; assert(RegisterClassW(&wc));
  wc.lpszClassName=L"WeaselTestCandidate"; assert(RegisterClassW(&wc));
  HWND owner=CreateWindowW(L"MSCTFIME UI",L"",WS_POPUP,0,0,1,1,nullptr,nullptr,wc.hInstance,nullptr);
  assert(owner);
  GameCompatibility rule; rule.enabled=true; rule.hide_system_ime_ui=true;
  GameSystemImeGuard guard; guard.Configure(rule);
  visibleTransitions=0;
  for (int i=0;i<20;++i) {
    HWND frame=CreateWindowW(L"CiceroUIWndFrame",L"",WS_POPUP|WS_VISIBLE,0,0,38,27,owner,nullptr,wc.hInstance,nullptr);
    assert(frame && !IsWindowVisible(frame));
    for (int j=0;j<10;++j) {
      ShowWindow(frame,SW_SHOWNOACTIVATE);
      assert(!IsWindowVisible(frame));
      SetWindowPos(frame,nullptr,0,0,38,27,SWP_SHOWWINDOW|SWP_NOACTIVATE);
      assert(!IsWindowVisible(frame));
    }
    DestroyWindow(frame);
  }
  assert(visibleTransitions==0);
  HWND candidate=CreateWindowW(L"WeaselTestCandidate",L"",WS_POPUP|WS_VISIBLE,0,0,38,27,owner,nullptr,wc.hInstance,nullptr);
  assert(IsWindowVisible(candidate));
  HWND wrongOwner=CreateWindowW(L"CiceroUIWndFrame",L"",WS_POPUP|WS_VISIBLE,0,0,38,27,candidate,nullptr,wc.hInstance,nullptr);
  assert(IsWindowVisible(wrongOwner));
  HWND large=CreateWindowW(L"CiceroUIWndFrame",L"",WS_POPUP|WS_VISIBLE,0,0,600,200,owner,nullptr,wc.hInstance,nullptr);
  assert(IsWindowVisible(large));
  HWND shifted=CreateWindowW(L"CiceroUIWndFrame",L"",WS_POPUP|WS_VISIBLE,300,300,38,27,owner,nullptr,wc.hInstance,nullptr);
  assert(IsWindowVisible(shifted));
  HWND retained=CreateWindowW(L"CiceroUIWndFrame",L"",WS_POPUP,0,0,38,27,owner,nullptr,wc.hInstance,nullptr);
  { GameSystemImeGuard second; second.Configure(rule); guard.Stop(); ShowWindow(retained,SW_SHOW); assert(!IsWindowVisible(retained)); }
  ShowWindow(retained,SW_SHOW); assert(IsWindowVisible(retained));
  ShowWindow(retained,SW_HIDE); guard.Configure(rule); ShowWindow(retained,SW_SHOW); assert(!IsWindowVisible(retained));
  guard.Stop(); ShowWindow(retained,SW_SHOW); assert(IsWindowVisible(retained));
  DestroyWindow(retained); DestroyWindow(shifted); DestroyWindow(large); DestroyWindow(wrongOwner); DestroyWindow(candidate); DestroyWindow(owner);
  puts("PASS: first creation, 400 repeated show attempts, zero visible transitions; candidate/owner/size/position exclusions; shared lifetime, reattach and cleanup.");
}
