#pragma once
#include "../../WeaselTSF/AoE2NativePosition.h"
#include <cassert>
#include <vector>

#if defined(_M_X64)
// Data-only fixture: discovery decodes these methods but never executes them.
struct AoE2Fixture {
  BYTE* base;
  DWORD code,ro;
  std::vector<IMAGE_RUNTIME_FUNCTION_ENTRY> functions;
  aoe2_native::Layout layout;
  BYTE *textTable,*collectionTable,*typeCode,*vectorCode,*global;
  DWORD collections,focus,flags,owner,rect,transform;
  uint64_t mask;
  alignas(8) BYTE holder[8]={},manager[0x800]={},collection[0x400]={},control[0x800]={};
  const BYTE* list[1];

  static void Pointer(BYTE* destination,const BYTE* value) {memcpy(destination,&value,8);}
  static void Number(BYTE* destination,DWORD value) {memcpy(destination,&value,4);}
  static void Relative(BYTE* operand,const BYTE* target) {
    int32_t distance=static_cast<int32_t>(target-(operand+4)); memcpy(operand,&distance,4);
  }
  BYTE* Data(size_t size) {auto result=base+ro;ro+=static_cast<DWORD>((size+7)&~size_t(7));return result;}
  BYTE* Function(const std::vector<BYTE>& value,bool unwind=false) {
    auto result=base+code;
    if(unwind) functions.push_back({code,code+static_cast<DWORD>(value.size()),0});
    memcpy(result,value.data(),value.size());code+=static_cast<DWORD>((value.size()+15)&~size_t(15));
    return result;
  }
  static void Slot(BYTE* table,size_t slot,const BYTE* function) {Pointer(table+slot*8,function);}
  void PublishFunctions() {
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress=0x20000;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size=
        static_cast<DWORD>(functions.size()*sizeof(functions[0]));
    memcpy(base+0x20000,functions.data(),functions.size()*sizeof(functions[0]));
  }
  explicit AoE2Fixture(bool moved) : code(moved?0x1800:0x1000),ro(moved?0x10800:0x10000) {
    base=static_cast<BYTE*>(VirtualAlloc(nullptr,0x60000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(base);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+0x80);
    nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections=4;
    nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.TimeDateStamp=moved?0x76543210:0x12345678;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfHeaders=0x1000;
    nt->OptionalHeader.SizeOfImage=moved?0x60000:0x50000;
    auto sections=IMAGE_FIRST_SECTION(nt);
    const DWORD starts[]={0x1000,0x10000,0x20000,0x30000};
    const DWORD sizes[]={0x8000,0x10000,0x1000,0x10000};
    for(int i=0;i<4;++i){
      sections[i].VirtualAddress=starts[i];sections[i].Misc.VirtualSize=sizes[i];
      sections[i].Characteristics=IMAGE_SCN_MEM_READ;
    }
    sections[0].Characteristics|=IMAGE_SCN_MEM_EXECUTE;
    sections[3].Characteristics|=IMAGE_SCN_MEM_WRITE;
    collections=moved?0x380:0x300;focus=moved?0x1a0:0x128;
    flags=moved?0x450:0x3d8;owner=moved?0xf0:0xd0;
    rect=moved?0x180:0x100;transform=moved?0x200:0x138;
    BYTE bit=moved?9:7;mask=uint64_t(1)<<bit;
    global=base+(moved?0x30800:0x30000);
    textTable=Data(19*8);collectionTable=Data(26*8);
    auto name=Data(13);memcpy(name,"TextBoxInput",13);
    typeCode=Function({0x48,0x8d,5,0,0,0,0,0xc3});Relative(typeCode+3,name);
    Slot(textTable,14,typeCode);
    std::vector<BYTE> flagsMethod={0x48,0x8b,0x81,0,0,0,0,0x90,0x48,0x3b,0xd0,0x74,0,
      0x48,0x87,0x91,0,0,0,0,0xb8,1,0,0,0,0x86,0x81,0,0,0,0,0xc3};
    Number(flagsMethod.data()+3,flags);Number(flagsMethod.data()+16,flags);
    Slot(textTable,15,Function(flagsMethod));
    std::vector<BYTE> vector={0x48,0x8b,0x9e,0,0,0,0,0x48,0x3b,0x9e,0,0,0,0,0x74,0,0x45,0x33,0xed};
    Number(vector.data()+3,collections+8);Number(vector.data()+10,collections);
    vectorCode=Function(vector);
    std::vector<BYTE> focusMethod={0x53,0x56,0x48,0x83,0xec,0x28,0x48,0x8b,0xf1,
      0x48,0x8b,0xda,0x48,0x8b,0x89,0,0,0,0,0x48,0x85,0xc9,0x74,0,
      0x48,0x8b,0x91,0,0,0,0,0x48,0x8b,1,0x48,0x0f,0xba,0xf2,bit,0x90,
      0xff,0x50,0x78,0x48,0x89,0x9e,0,0,0,0};
    Number(focusMethod.data()+15,focus);Number(focusMethod.data()+27,flags);
    Number(focusMethod.data()+46,focus);
    if(moved) focusMethod.insert(focusMethod.begin(),0x40);
    Slot(collectionTable,24,Function(focusMethod));
    std::vector<BYTE> transformMethod(80,0x90);
    const BYTE x[]={0xf3,0x0f,0x5c,0xb7,0,0,0,0,0xf3,0x0f,0x59,0xb7,0,0,0,0,
      0x48,0x8b,7,0x48,0x8b,0xcf,0xff,0x50,0x50};
    memcpy(transformMethod.data(),x,sizeof(x));
    Number(transformMethod.data()+4,transform+8);Number(transformMethod.data()+12,transform+24);
    const BYTE y[]={0xf3,0x0f,0x5c,0x8f,0,0,0,0,0xf3,0x0f,0x59,0x8f,0,0,0,0};
    memcpy(transformMethod.data()+48,y,sizeof(y));
    Number(transformMethod.data()+52,transform+12);Number(transformMethod.data()+60,transform+28);
    Slot(collectionTable,20,Function(transformMethod,true));
    std::vector<BYTE> accessor={0x48,0x8b,5,0,0,0,0,0x48,0x8b,0x38,0x48,0x89,0x18,
      0x90,0x90,0x48,0x8b,5,0,0,0,0,0x48,0x8b,0x5c,0x24,0x38,0x48,0x83,0xc4,0x20,0x5f,0xc3};
    auto accessorCode=Function(accessor,true);Relative(accessorCode+3,global);Relative(accessorCode+18,global);
    std::vector<BYTE> font={0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,
      0x48,0x8b,0xf9,0x89,0x91,0,0,0,0,0xe8,0,0,0,0,0x8b,0x97,0,0,0,0,
      0x48,0x8b,0xc8,0xe8,0,0,0,0};
    Number(font.data()+15,0x31c);Number(font.data()+26,0x31c);
    auto fontCode=Function(font,true);Relative(fontCode+20,accessorCode);Slot(textTable,18,fontCode);
    std::vector<BYTE> rectMethod={0x80,0xbf,0,0,0,0,0,0x48,0x8d,0x9f,0,0,0,0,
      0xf3,0x0f,0x10,0x44,0x24,0x20,0x8b,0x47,0x40,0xf3,0x0f,0x11,3};
    Number(rectMethod.data()+10,rect);Slot(textTable,8,Function(rectMethod,true));
    auto collectionPosition=Function({0xf3,0x0f,0x10,0x59,0x38,0x4c,0x8b,0xc1,0xf3,0x0f,0x10,0x51,0x34});
    std::vector<BYTE> position={0x48,0x8b,0x8a,0,0,0,0,0x48,0x85,0xc9,0x74,0,
      0x48,0x8d,0x54,0x24,0x20,0xe8,0,0,0,0};
    Number(position.data()+3,owner);
    auto positionCode=Function(position,true);Relative(positionCode+18,collectionPosition);Slot(textTable,10,positionCode);
    PublishFunctions();
    int stage=0;assert(aoe2_native::ResolveSafe(base,&layout,&stage) && stage==0);
    assert(layout.collections==collections && layout.focus==focus && layout.flags==flags &&
      layout.owner==owner && layout.rect==rect && layout.transform==transform && layout.focusMask==mask);
    Pointer(global,holder);Pointer(holder,manager);
    list[0]=collection;
    Pointer(manager+collections,reinterpret_cast<const BYTE*>(list));
    Pointer(manager+collections+8,reinterpret_cast<const BYTE*>(list+1));
    Pointer(collection,collectionTable);Pointer(collection+focus,control);
    Pointer(control,textTable);Pointer(control+owner,collection);
    memcpy(control+flags,&mask,8);
    const float rectangle[]={660,395,630,50};
    const float mapping[]={1366,768,0.333312988f,0,1365.333f,768,2.8125f,2.8125f};
    memcpy(control+rect,rectangle,sizeof(rectangle));memcpy(collection+transform,mapping,sizeof(mapping));
  }
  ~AoE2Fixture(){VirtualFree(base,0,MEM_RELEASE);}
};
#endif
