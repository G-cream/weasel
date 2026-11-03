#include "../../WeaselTSF/DotaNativePosition.h"
#include <cassert>
#include <cstdio>
#include <vector>

#if defined(_M_X64)
// Synthetic module images with executable getter stubs. No game is loaded or
// modified. A second fixture relocates every table/function and changes fields.
struct Module {
  BYTE* base;
  DWORD code, ro, rw;
  dota_native::Image image;
  explicit Module(bool moved) : code(moved ? 0x1800 : 0x1000),
      ro(moved ? 0x10800 : 0x10000), rw(moved ? 0x20800 : 0x20000) {
    base = static_cast<BYTE*>(VirtualAlloc(nullptr,0x40000,
                         MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    assert(base);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x80;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base+0x80);
    nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections=3;
    nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.TimeDateStamp=moved ? 0x76543210 : 0x12345678;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage=0x40000;
    nt->OptionalHeader.SizeOfHeaders=0x1000;
    auto sections=IMAGE_FIRST_SECTION(nt);
    sections[0].VirtualAddress=0x1000; sections[0].Misc.VirtualSize=0x8000;
    sections[0].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE;
    sections[1].VirtualAddress=0x10000; sections[1].Misc.VirtualSize=0x8000;
    sections[1].Characteristics=IMAGE_SCN_MEM_READ;
    sections[2].VirtualAddress=0x20000; sections[2].Misc.VirtualSize=0x8000;
    sections[2].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;
    assert(image.Init(reinterpret_cast<HMODULE>(base)));
  }
  ~Module() { VirtualFree(base,0,MEM_RELEASE); }
  BYTE* Table(const char* name, DWORD adjustment, size_t slots) {
    DWORD type=rw; rw+=256;
    memcpy(base+type+16,name,strlen(name)+1);
    DWORD locator=ro; ro+=32;
    DWORD fields[]={1,adjustment,0,type,0,locator};
    memcpy(base+locator,fields,sizeof(fields));
    BYTE* storage=base+ro; ro+=static_cast<DWORD>((slots+1)*8);
    ro=(ro+7)&~7u;
    const BYTE* pointer=base+locator; memcpy(storage,&pointer,8);
    return storage+8;
  }
  BYTE* Function(const std::vector<BYTE>& bytes) {
    BYTE* result=base+code; code+=static_cast<DWORD>((bytes.size()+15)&~15u);
    memcpy(result,bytes.data(),bytes.size()); return result;
  }
};

static void Pointer(BYTE* address, const BYTE* value) { memcpy(address,&value,8); }
static void Slot(BYTE* table, size_t slot, BYTE* function) { Pointer(table+slot*8,function); }
static void Append(std::vector<BYTE>& bytes, float value) {
  BYTE raw[4]; memcpy(raw,&value,4); bytes.insert(bytes.end(),raw,raw+4);
}

struct Game {
  Module ime, client, panorama;
  alignas(8) BYTE manager[128]={}, owner[64]={}, view[160]={};
  alignas(8) BYTE entry[160]={}, panel[1024]={};
  BYTE *activeCode, *positionCode, *kindCode, *controlTable;
  DWORD adjustment, enabledField;
  explicit Game(bool moved) : ime(moved),client(moved),panorama(moved) {
    BYTE ownerField=moved ? 0x50 : 0x58;
    BYTE viewField=moved ? 0x18 : 8;
    BYTE focusField=moved ? 0x68 : 0x70;
    BYTE panelField=moved ? 16 : 8;
    DWORD widthField=moved ? 0x1d8 : 0x1b8, heightField=widthField+4;
    adjustment=moved ? 0x38 : 0x28; enabledField=moved ? 0x62 : 0x60;
    DWORD viewSlot=moved ? 24 : 23;
    auto managerTable=ime.Table(".?AVCIMEManager@@",0,21);
    Pointer(manager,managerTable); manager[enabledField]=1;
    Pointer(manager+ownerField,owner);
    auto ownerTable=ime.Table(".?AVGFxIMEManagerWin32@@",0,viewSlot+1);
    Pointer(owner,ownerTable); Pointer(owner+viewField,view);
    Slot(ownerTable,viewSlot,ime.Function({0x48,0x8b,0x41,viewField,0xc3}));
    std::vector<BYTE> active={0x80,0x79,static_cast<BYTE>(enabledField),0,0x75,3,
      0x33,0xc0,0xc3,0x48,0x8b,0x49,ownerField,0x48,0x8b,1,0x48,0xff,0xa0};
    DWORD slotBytes=viewSlot*8;
    const BYTE* raw=reinterpret_cast<const BYTE*>(&slotBytes);
    active.insert(active.end(),raw,raw+4);
    activeCode=ime.Function(active); Slot(managerTable,20,activeCode);
    auto viewTable=panorama.Table(".?AVCTopLevelWindowSource2@panorama@@",320,1);
    Pointer(view,viewTable); Pointer(view+focusField,entry+adjustment);
    std::vector<BYTE> focus={0x48,0x89,0x5c,0x24,8,0x48,0x89,0x74,0x24,0x10,
      0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,focusField,0x33,0xf6,0x48,0x8b,0xf9,
      0x48,0x8b,0xc3,0x48,0x8b,0x5c,0x24,0x30,0x48,0x8b,0x74,0x24,0x38,
      0x48,0x83,0xc4,0x20,0x5f,0xc3};
    Slot(viewTable,0,panorama.Function(focus));
    controlTable=client.Table(".?AVCTextEntry@panorama@@",adjustment,1);
    Pointer(entry+adjustment,controlTable);
    kindCode=client.Function({0xb8,1,0,0,0,0xc3}); Slot(controlTable,0,kindCode);
    auto primary=client.Table(".?AVCTextEntry@panorama@@",0,46);
    Pointer(entry,primary); Pointer(entry+panelField,panel);
    Slot(primary,0,client.Function({0x48,0x8b,0x41,panelField,0xc3}));
    std::vector<BYTE> position={0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x40,
      0x49,0x8b,0xf9,0x48,0xc7,0x44,0x24,0x30,0,0,0,0,0x49,0x8b,0xd8,0xc7,3};
    Append(position,320); position.insert(position.end(),{0xc7,7}); Append(position,180);
    position.insert(position.end(),{0x48,0x8b,0x5c,0x24,0x50,0x48,0x83,0xc4,0x40,0x5f,0xc3});
    positionCode=client.Function(position); Slot(primary,45,positionCode);
    auto panelTable=panorama.Table(".?AVCUIPanel@panorama@@",0,102);
    Pointer(panel,panelTable);
    for (auto field : {widthField,heightField}) {
      std::vector<BYTE> getter={0xf3,0x0f,0x10,0x81};
      raw=reinterpret_cast<const BYTE*>(&field); getter.insert(getter.end(),raw,raw+4);
      getter.push_back(0xc3);
      Slot(panelTable,field==widthField ? 100 : 101,panorama.Function(getter));
    }
    float width=600,height=40;
    memcpy(panel+widthField,&width,4); memcpy(panel+heightField,&height,4);
  }
  bool Read(dota_native::Geometry* value, int* reason) {
    return dota_native::GeometryForManager(ime.image,client.image,panorama.image,
                                            manager,value,reason);
  }
};
#endif

int main() {
#if defined(_M_X64)
  for (bool moved : {false,true}) {
    Game game(moved); dota_native::Geometry geometry={}; int reason=0;
    assert(game.Read(&geometry,&reason));
    assert(geometry.x==320 && geometry.y==180 && geometry.width==600 && geometry.height==40);
    RECT client={100,200,2020,1280},area={};
    assert(DotaNativeArea(geometry.x,geometry.y,geometry.width,geometry.height,client,&area));
    assert(area.left==420 && area.top==380 && area.bottom==420);
    game.manager[game.enabledField]=0;
    assert(!game.Read(&geometry,&reason) && reason==5);
    game.manager[game.enabledField]=1;
    game.activeCode[4]=0x74;
    assert(!game.Read(&geometry,&reason) && reason==5);
    game.activeCode[4]=0x75;
    game.kindCode[1]=2;
    assert(!game.Read(&geometry,&reason) && reason==6);
    game.kindCode[1]=1;
    auto locator=const_cast<BYTE*>(dota_native::Pointer(game.controlTable-8));
    locator[20]^=1;
    assert(!game.Read(&geometry,&reason) && reason==6);
    locator[20]^=1;
    game.positionCode[0]=0x90;
    assert(!game.Read(&geometry,&reason) && reason==8);
    game.positionCode[0]=0x48;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(game.panorama.base+0x80);
    IMAGE_FIRST_SECTION(nt)[1].Characteristics|=IMAGE_SCN_MEM_WRITE;
    assert(!game.Read(&geometry,&reason));
    IMAGE_FIRST_SECTION(nt)[1].Characteristics&=~IMAGE_SCN_MEM_WRITE;
    assert(game.Read(&geometry,&reason));
    assert(!game.client.image.Contains(game.client.base+0x3ffff,16,IMAGE_SCN_MEM_READ));
    // Simulate the focused control disappearing during the geometry query.
    auto focus=dota_native::Method(game.panorama.image,game.view,0,43);
    std::vector<BYTE> transition(focus,focus+24);
    transition.insert(transition.end(),{0x48,0xc7,0x47,focus[18],0,0,0,0});
    transition.insert(transition.end(),focus+24,focus+43);
    auto viewTable=const_cast<BYTE*>(dota_native::Pointer(game.view));
    Slot(viewTable,0,game.panorama.Function(transition));
    assert(!game.Read(&geometry,&reason) && reason==8);
  }
  puts("PASS: relocated modules, changed timestamps/RVAs/fields, native interface geometry, inactive focus and malformed ABI/RTTI/section rejection.");
#else
  puts("PASS: x64 native interface calls are excluded on this architecture.");
#endif
}
