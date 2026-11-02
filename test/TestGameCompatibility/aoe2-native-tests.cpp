#include "../../WeaselTSF/AoE2NativePosition.h"
#include <cassert>
#include <cstdio>

int main() {
  RECT window = {239,273,1605,1041}, area = {};
  float rect[4] = {660,395,630,50};
  float transform[8] = {1366,768,0.333312988f,0,1365.333f,768,2.8125f,2.8125f};
  assert(AoE2NativeLayout(rect,transform,window,&area));
  assert(area.left==474 && area.right==474 && area.top==413 && area.bottom==431);
  transform[0]=1920;
  assert(!AoE2NativeLayout(rect,transform,window,&area));
  transform[0]=1366; transform[6]=0;
  assert(!AoE2NativeLayout(rect,transform,window,&area));
  transform[6]=NAN;
  assert(!AoE2NativeLayout(rect,transform,window,&area));
  transform[6]=2.8125f; rect[0]=-100;
  assert(!AoE2NativeLayout(rect,transform,window,&area));
  rect[0]=660;

  // Sparse fake module: validate production PE/type/focus/ownership checks
  // without loading or calling any game code or modifying another process.
  BYTE* base=static_cast<BYTE*>(VirtualAlloc(nullptr,0x4f24000,MEM_RESERVE,PAGE_NOACCESS));
  assert(base);
  const size_t pages[]={0,0xb1c000,0x3219000,0x321d000,0x4293000};
  for (size_t page:pages) assert(VirtualAlloc(base+page,4096,MEM_COMMIT,PAGE_READWRITE));
  auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x80;
  auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+0x80);
  nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.TimeDateStamp=0x6acf93a9;
  nt->OptionalHeader.SizeOfImage=0x4f24000;
  const BYTE code[]={0x48,0x8d,0x05,0xc9,0xce,0x6f,0x02,0xc3};
  memcpy(base+0xb1ce00,code,sizeof(code));
  memcpy(base+0x3219cd0,"TextBoxInput",13);
  auto pointer=[](BYTE* a,const BYTE* value) {memcpy(a,&value,sizeof(value));};
  pointer(base+0x321d998+0x70,base+0xb1ce00);
  alignas(8) BYTE manager[0x320]={}, collection[0x200]={}, control[0x400]={};
  const BYTE* holder=manager;
  const BYTE* list[]={collection};
  pointer(base+0x4293138,reinterpret_cast<BYTE*>(&holder));
  pointer(manager+0x300,reinterpret_cast<BYTE*>(list));
  pointer(manager+0x308,reinterpret_cast<BYTE*>(list+1));
  pointer(collection,base+0x333df60);pointer(collection+0x128,control);
  pointer(control,base+0x321d998);pointer(control+0xd0,collection);
  memcpy(control+0x100,rect,sizeof(rect));memcpy(collection+0x138,transform,sizeof(transform));
  control[0x3d8]=0x80;
  int reason=0;
  assert(AoE2NativeReadSafe(base,window,&area,&reason) && reason==0);
  assert(area.left==474 && area.top==413 && area.bottom==431);
  control[0x3d8]=0;
  assert(!AoE2NativeReadSafe(base,window,&area,&reason) && reason==24);
  control[0x3d8]=0x80;pointer(control+0xd0,manager);
  assert(!AoE2NativeReadSafe(base,window,&area,&reason) && reason==24);
  pointer(control+0xd0,collection);
  float stale=1920;memcpy(collection+0x138,&stale,sizeof(stale));
  assert(!AoE2NativeReadSafe(base,window,&area,&reason) && reason==25);
  memcpy(collection+0x138,transform,sizeof(transform));
  pointer(manager+0x308,reinterpret_cast<BYTE*>(list)+129*sizeof(void*));
  assert(!AoE2NativeReadSafe(base,window,&area,&reason) && reason==23);
  pointer(manager+0x308,reinterpret_cast<BYTE*>(list+1));
  nt->FileHeader.TimeDateStamp++;
  assert(!AoE2NativeReadSafe(base,window,&area,&reason) && reason==22);
  nt->FileHeader.TimeDateStamp--;
  base[0xb1ce00]=0;
  assert(!AoE2NativeReadSafe(base,window,&area,&reason) && reason==22);
  base[0xb1ce00]=code[0];
  holder=reinterpret_cast<const BYTE*>(0x10000);
  assert(!AoE2NativeReadSafe(base,window,&area,&reason) && reason==27);
  assert(!GetAoE2NativePosition(nullptr,window,&area,&reason) && reason==20);
  VirtualFree(base,0,MEM_RELEASE);
  puts("PASS: real window transform, focus, ownership, stale viewport, bounded traversal, build/type guards and invalid-memory containment.");
}
