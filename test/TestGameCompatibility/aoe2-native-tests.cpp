#include "../../WeaselTSF/AoE2NativePosition.h"
#include <cassert>
#include <cstdio>
#include "aoe2-fixture.h"

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

  int reason=0;
#if defined(_M_X64)
  for (bool moved : {false,true}) {
    AoE2Fixture game(moved);
    auto read=[&]() {return AoE2NativeReadLayoutSafe(game.layout,window,&area,&reason);};
    assert(read() && reason==0);
    assert(area.left==474 && area.top==413 && area.bottom==431);
    uint64_t noFlags=0;
    memcpy(game.control+game.flags,&noFlags,8);
    assert(!read() && reason==24);
    memcpy(game.control+game.flags,&game.mask,8);
    AoE2Fixture::Pointer(game.control+game.owner,game.manager);
    assert(!read() && reason==24);
    AoE2Fixture::Pointer(game.control+game.owner,game.collection);
    float stale=1920;memcpy(game.collection+game.transform,&stale,4);
    RECT before=area;
    assert(!read() && reason==25 && EqualRect(&before,&area));
    float width=1366;memcpy(game.collection+game.transform,&width,4);
    AoE2Fixture::Pointer(game.manager+game.collections+8,
        reinterpret_cast<const BYTE*>(game.list)+129*sizeof(void*));
    assert(!read() && reason==23);
    AoE2Fixture::Pointer(game.manager+game.collections+8,reinterpret_cast<const BYTE*>(game.list+1));
    game.typeCode[0]=0x90;
    assert(!read() && reason==24);
    aoe2_native::Layout ignored;int stage=0;
    assert(!aoe2_native::ResolveSafe(game.base,&ignored,&stage) && stage==2);
    game.typeCode[0]=0x48;
    BYTE* blocked=static_cast<BYTE*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));
    assert(blocked);AoE2Fixture::Pointer(game.global,blocked);
    assert(!read() && reason==27);
    AoE2Fixture::Pointer(game.global,game.holder);VirtualFree(blocked,0,MEM_RELEASE);
    assert(read());
    std::vector<BYTE> duplicateVector(game.vectorCode,game.vectorCode+19);
    auto duplicate=game.Function(duplicateVector);
    assert(!aoe2_native::ResolveSafe(game.base,&ignored,&stage) && stage==2);
    memset(duplicate,0,19);
    assert(aoe2_native::ResolveSafe(game.base,&ignored,&stage));
    auto duplicateTable=game.Data(19*8);memcpy(duplicateTable,game.textTable,19*8);
    assert(!aoe2_native::ResolveSafe(game.base,&ignored,&stage) && stage==3);
    auto mapping=const_cast<BYTE*>(aoe2_native::TableMethod(game.layout.image,
                                                           game.collectionTable,20,80));
    aoe2_native::Findings seen;
    aoe2_native::InspectCode(game.layout.image,mapping,80,seen);
    assert(seen.transformFunction && !seen.ambiguous);
    for(auto operand : {4,12,52,60})
      AoE2Fixture::Number(mapping+operand,aoe2_native::U32(mapping+operand)+4);
    aoe2_native::InspectCode(game.layout.image,mapping,80,seen);
    assert(seen.ambiguous);
  }
  assert(!GetAoE2NativePosition(nullptr,window,&area,&reason) && reason==20);
  puts("PASS: changed module bases/build IDs/RVAs/fields/focus bits; discovered layout, ownership, bounds, stale viewport, invalid memory and ambiguous-signature rejection.");
#else
  assert(!AoE2NativeReadSafe(nullptr,window,&area,&reason) && reason==26);
  assert(!GetAoE2NativePosition(nullptr,window,&area,&reason) && reason==20);
  puts("PASS: layout conversion and safe rejection of x64-only native adapters on x86.");
#endif
}
