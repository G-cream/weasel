// Link against the Release x64 TSF objects to exercise the real candidate code
// without registering an input method, opening a server session or a game.
#include "../../WeaselTSF/stdafx.h"
#include <WeaselUI.h>
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
#include "../../WeaselTSF/GameCompatibility.h"
#include "../../WeaselTSF/WeaselTSF.h"
#include "../../WeaselTSF/CandidateList.h"
#include <cstdio>

class FakeThreadMgr : public ITfThreadMgr, public ITfUIElementMgr {
 public:
  bool exposeUI = true;
  bool failBegin = false;
  unsigned begins = 0, ends = 0, updates = 0;
  STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
    *out = nullptr;
    if (iid == IID_IUnknown || iid == IID_ITfThreadMgr)
      *out = static_cast<ITfThreadMgr*>(this);
    else if (iid == IID_ITfUIElementMgr && exposeUI)
      *out = static_cast<ITfUIElementMgr*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP Activate(TfClientId*) override { return E_NOTIMPL; }
  STDMETHODIMP Deactivate() override { return E_NOTIMPL; }
  STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs**) override { return E_NOTIMPL; }
  STDMETHODIMP GetFocus(ITfDocumentMgr** out) override { *out = nullptr; return S_FALSE; }
  STDMETHODIMP SetFocus(ITfDocumentMgr*) override { return E_NOTIMPL; }
  STDMETHODIMP AssociateFocus(HWND, ITfDocumentMgr*, ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP IsThreadFocus(BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetFunctionProvider(REFCLSID, ITfFunctionProvider**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders**) override { return E_NOTIMPL; }
  STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP BeginUIElement(ITfUIElement*, BOOL* show, DWORD* id) override {
    ++begins;
    if (failBegin) return E_FAIL;
    *show = FALSE; *id = 42; return S_OK;
  }
  STDMETHODIMP UpdateUIElement(DWORD id) override { assert(id == 42); ++updates; return S_OK; }
  STDMETHODIMP EndUIElement(DWORD id) override { assert(id == 42); ++ends; return S_OK; }
  STDMETHODIMP GetUIElement(DWORD, ITfUIElement**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumUIElements(IEnumTfUIElements**) override { return E_NOTIMPL; }
};

struct CandidateListLifecycleTest {
static void Run() {
  FakeThreadMgr manager;
  auto service = new WeaselTSF;
  service->_pThreadMgr = &manager;
  auto candidate = service->_cand.p;

  // GetFocus succeeds with S_FALSE and a null document: do not dereference it.
  assert(candidate->_GetActiveWnd() == GetFocus());
  candidate->EndUI();
  candidate->_UpdateUIElement();
  assert(manager.ends == 0 && manager.updates == 0);

  manager.failBegin = true;
  candidate->StartUI();
  assert(!candidate->_uiElementRegistered && !candidate->_gameUiActive);
  assert(candidate->_gamePositionTimer == 0);
  candidate->EndUI();
  assert(manager.ends == 0);

  manager.failBegin = false;
  candidate->StartUI();
  assert(candidate->_uiElementRegistered && candidate->_gameUiActive);
  candidate->_UpdateUIElement();
  assert(manager.updates == 1);
  candidate->_gameCompatibility.enabled = true;
  candidate->_gameCompatibility.native_position = true;
  candidate->_StartGamePositionTimer();
  assert(candidate->_gamePositionTimer != 0);
  service->_HideUI();
  assert(!candidate->_gameDocumentFocused && candidate->_gamePositionTimer == 0);
  service->_ShowUI();
  assert(candidate->_gameDocumentFocused && candidate->_gamePositionTimer != 0);
  candidate->HideGameUI();
  assert(!candidate->_gameUiActive && candidate->_gamePositionTimer == 0);
  service->_ShowUI();
  assert(candidate->_gamePositionTimer == 0);
  candidate->EndUI();
  candidate->EndUI();
  assert(manager.ends == 1);

  candidate->StartUI();
  candidate->_gameCompatibility.native_position = true;
  candidate->_StartGamePositionTimer();
  manager.exposeUI = false;
  candidate->EndUI();
  assert(!candidate->_uiElementRegistered && !candidate->_gameUiActive);
  assert(candidate->_gamePositionTimer == 0);
  // QueryInterface failure must not prevent local cleanup or a later restart.
  manager.exposeUI = true;
  candidate->StartUI();
  assert(candidate->_uiElementRegistered);
  candidate->EndUI();
  assert(manager.ends == 2);

  // Break the existing service/candidate ownership cycle explicitly.
  candidate->_tsf.Release();
  service->_cand.Release();
  candidate->Release();
  service->_pThreadMgr.Release();
  service->Release();
  puts("PASS: null TSF focus, failed UI registration, registered-only notifications, document-focus suspension, timer cleanup, duplicate end and restart after interface failure.");
}
};

int main() { CandidateListLifecycleTest::Run(); }
