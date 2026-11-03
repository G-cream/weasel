#pragma once
#include "DotaNativeInterfaces.h"

namespace aoe2_native {
using Image = dota_native::Image;

struct Layout {
  Image image;
  const BYTE* textTable = nullptr;
  const BYTE* typeGetter = nullptr;
  const BYTE* managerSlot = nullptr;
  const BYTE* focusSetter = nullptr;
  DWORD collections = 0;
  DWORD focus = 0;
  DWORD flags = 0;
  DWORD owner = 0;
  DWORD rect = 0;
  DWORD transform = 0;
  uint64_t focusMask = 0;
};

#if defined(_M_X64)
inline DWORD U32(const BYTE* p) { DWORD value; memcpy(&value,p,4); return value; }
inline const BYTE* Rip(const BYTE* instructionEnd, const BYTE* displacement) {
  int32_t value; memcpy(&value,displacement,4);
  return reinterpret_cast<const BYTE*>(reinterpret_cast<uintptr_t>(instructionEnd)+value);
}
inline bool Field(DWORD offset, DWORD alignment) {
  return offset >= 8 && offset <= 0x10000 && offset % alignment == 0;
}
inline const BYTE* TableMethod(const Image& image, const BYTE* table, size_t slot,
                           size_t bytes) {
  if (!image.ReadOnly(table,(slot+1)*sizeof(void*))) return nullptr;
  uintptr_t raw=0; memcpy(&raw,table+slot*sizeof(void*),sizeof(raw));
  auto code=reinterpret_cast<const BYTE*>(raw);
  return image.Code(code,bytes) ? code : nullptr;
}

inline bool FunctionRange(const Image& image, const BYTE* code,
                            const BYTE** begin, const BYTE** end) {
  if (!image.Code(code,1)) return false;
  auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
  auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.base+dos->e_lfanew);
  const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
  if (directory.VirtualAddress >= image.size || !directory.Size ||
      directory.Size % sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY)) return false;
  auto entries=reinterpret_cast<const IMAGE_RUNTIME_FUNCTION_ENTRY*>(image.base+directory.VirtualAddress);
  if (!image.ReadOnly(entries,directory.Size)) return false;
  size_t low=0,high=directory.Size/sizeof(*entries);
  DWORD rva=static_cast<DWORD>(code-image.base);
  while (low<high) {
    size_t mid=low+(high-low)/2;
    if (entries[mid].BeginAddress<=rva) low=mid+1;
    else high=mid;
  }
  if (!low) return false;
  const auto& entry=entries[low-1];
  if (rva>=entry.EndAddress || entry.EndAddress<=entry.BeginAddress ||
      entry.EndAddress>image.size ||
      !image.Code(image.base+entry.BeginAddress,entry.EndAddress-entry.BeginAddress)) return false;
  *begin=image.base+entry.BeginAddress; *end=image.base+entry.EndAddress;
  return true;
}

// Walk readable module sections only, on the resolver worker. Never touch the
// game's heap, execute game functions, decrypt code, or scan on a key callback.
template<class Visitor>
inline void ScanSections(const Image& image, bool executable, Visitor visitor) {
  for (WORD sectionIndex=0;sectionIndex<image.count;++sectionIndex) {
    const auto& section=image.sections[sectionIndex];
    DWORD flags=section.Characteristics;
    if (!(flags&IMAGE_SCN_MEM_READ) ||
        !!(flags&IMAGE_SCN_MEM_EXECUTE)!=executable ||
        (!executable && (flags&IMAGE_SCN_MEM_WRITE)) ||
        section.VirtualAddress>image.size ||
        section.Misc.VirtualSize>image.size-section.VirtualAddress) continue;
    uintptr_t cursor=reinterpret_cast<uintptr_t>(image.base+section.VirtualAddress);
    uintptr_t limit=cursor+section.Misc.VirtualSize;
    while (cursor<limit) {
      MEMORY_BASIC_INFORMATION memory={};
      if (!VirtualQuery(reinterpret_cast<const void*>(cursor),&memory,sizeof(memory))) break;
      uintptr_t next=reinterpret_cast<uintptr_t>(memory.BaseAddress)+memory.RegionSize;
      if (next<=cursor) break;
      if (next>limit) next=limit;
      DWORD access=memory.Protect&0xff;
      bool readable=access==PAGE_READONLY || access==PAGE_READWRITE ||
        access==PAGE_WRITECOPY || access==PAGE_EXECUTE_READ ||
        access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
      if (memory.State==MEM_COMMIT && readable &&
          !(memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)))
        visitor(reinterpret_cast<const BYTE*>(cursor),static_cast<size_t>(next-cursor));
      cursor=next;
    }
  }
}

struct Findings {
  const BYTE* type = nullptr;
  const BYTE* vectorCode = nullptr;
  const BYTE* focusCode = nullptr;
  const BYTE* transformFunction = nullptr;
  DWORD collections=0,focus=0,flags=0,transform=0;
  uint64_t focusMask=0;
  bool ambiguous=false;
};
inline void Unique(const BYTE*& destination, const BYTE* value, Findings& found) {
  if (destination && destination!=value) found.ambiguous=true;
  else destination=value;
}

inline void InspectCode(const Image& image, const BYTE* span, size_t size,
                          Findings& found) {
  const BYTE* end=span+size;
  for (const BYTE* p=span;p<end;++p) {
    size_t left=static_cast<size_t>(end-p);
    if (*p==0x48 && left>=8 && !memcmp(p,"\x48\x8d\x05",3) && p[7]==0xc3) {
      auto name=Rip(p+7,p+3);
      if (image.ReadOnly(name,13) && !memcmp(name,"TextBoxInput",13))
        Unique(found.type,p,found);
    }
    if (*p==0x48 && left>=19 && !memcmp(p,"\x48\x8b\x9e",3) &&
        !memcmp(p+7,"\x48\x3b\x9e",3) && p[14]==0x74 &&
        !memcmp(p+16,"\x45\x33\xed",3)) {
      DWORD finish=U32(p+3),begin=U32(p+10);
      if (Field(begin,8) && finish==begin+8) {
        Unique(found.vectorCode,p,found); found.collections=begin;
      }
    }
    if (*p==0x53 && left>=50 &&
        !memcmp(p,"\x53\x56\x48\x83\xec\x28\x48\x8b\xf1\x48\x8b\xda\x48\x8b\x89",15) &&
        !memcmp(p+19,"\x48\x85\xc9\x74",4) &&
        !memcmp(p+24,"\x48\x8b\x91",3) &&
        !memcmp(p+31,"\x48\x8b\x01\x48\x0f\xba\xf2",7) && p[38]<64 &&
        !memcmp(p+39,"\x90\xff\x50\x78\x48\x89\x9e",7)) {
      DWORD focus=U32(p+15),flags=U32(p+27);
      if (Field(focus,8) && Field(flags,8) && U32(p+46)==focus) {
        // MSVC may use a redundant REX prefix to make the entry instruction
        // two bytes long. The vtable points to that prefix, not to PUSH RBX.
        auto entry=(p>span && p[-1]==0x40) ? p-1 : p;
        Unique(found.focusCode,entry,found);
        found.focus=focus; found.flags=flags; found.focusMask=uint64_t(1)<<p[38];
      }
    }
    if (*p==0xf3 && left>=25 && !memcmp(p,"\xf3\x0f\x5c\xb7",4) &&
        !memcmp(p+8,"\xf3\x0f\x59\xb7",4) &&
        !memcmp(p+16,"\x48\x8b\x07\x48\x8b\xcf\xff\x50\x50",9)) {
      DWORD offset=U32(p+4),scale=U32(p+12);
      const BYTE *function=nullptr,*finish=nullptr;
      if (!Field(offset,4) || offset<16 || scale!=offset+16 ||
          !FunctionRange(image,p,&function,&finish)) continue;
      if (finish>p+128) finish=p+128;
      unsigned matches=0;
      for (auto q=p+25;q+16<=finish && q+16<=end;++q) {
        if (!memcmp(q,"\xf3\x0f\x5c\x8f",4) && U32(q+4)==offset+4 &&
            !memcmp(q+8,"\xf3\x0f\x59\x8f",4) && U32(q+12)==scale+4) ++matches;
      }
      if (matches==1) {
        if (found.transformFunction && found.transform!=offset-8)
          found.ambiguous=true;
        Unique(found.transformFunction,function,found); found.transform=offset-8;
      }
    }
  }
}

inline bool TextLayout(const Image& image, const BYTE* table,
                         const Findings& found, Layout* output) {
  if (TableMethod(image,table,14,8)!=found.type) return false;
  // The flags setter is decoded but never called. Cross-check its field with
  // the collection's focus setter, which clears/sets this same flag word.
  auto flags=TableMethod(image,table,15,32);
  if (!flags || memcmp(flags,"\x48\x8b\x81",3) || U32(flags+3)!=found.flags ||
      memcmp(flags+7,"\x90\x48\x3b\xd0\x74",5) ||
      memcmp(flags+13,"\x48\x87\x91",3) || U32(flags+16)!=found.flags ||
      memcmp(flags+20,"\xb8\x01\x00\x00\x00",5) || flags[31]!=0xc3) return false;
  auto font=TableMethod(image,table,18,38);
  const BYTE fontPrefix[]={0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9};
  if (!font || memcmp(font,fontPrefix,sizeof(fontPrefix)) ||
      memcmp(font+13,"\x89\x91",2) || font[19]!=0xe8 ||
      memcmp(font+24,"\x8b\x97",2) || U32(font+15)!=U32(font+26)) return false;
  auto accessor=Rip(font+24,font+20);
  const BYTE *begin=nullptr,*end=nullptr;
  if (!FunctionRange(image,accessor,&begin,&end) || begin!=accessor ||
      end-begin<32 || end-begin>0x1000) return false;
  auto tail=end-18;
  if (memcmp(tail,"\x48\x8b\x05",3) ||
      memcmp(tail+7,"\x48\x8b\x5c\x24\x38\x48\x83\xc4\x20\x5f\xc3",11)) return false;
  auto global=Rip(tail+7,tail+3);
  if (!image.Contains(global,8,IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE,
                       IMAGE_SCN_MEM_EXECUTE) || reinterpret_cast<uintptr_t>(global)%8) return false;
  unsigned rootMatches=0;
  for (auto p=begin;p+13<=end;++p)
    if (!memcmp(p,"\x48\x8b\x05",3) && Rip(p+7,p+3)==global &&
        !memcmp(p+7,"\x48\x8b\x38\x48\x89\x18",6)) ++rootMatches;
  if (rootMatches!=1) return false;

  auto rectMethod=TableMethod(image,table,8,1);
  if (!FunctionRange(image,rectMethod,&begin,&end) || begin!=rectMethod || end-begin>0x1000)
    return false;
  DWORD rect=0; unsigned rectMatches=0;
  for (auto p=begin+7;p+20<=end;++p) {
    if (!memcmp(p,"\x48\x8d\x9f",3) &&
        p[-7]==0x80 && p[-6]==0xbf && p[-1]==0 &&
        !memcmp(p+7,"\xf3\x0f\x10\x44\x24\x20\x8b\x47",8) &&
        !memcmp(p+16,"\xf3\x0f\x11\x03",4) && Field(U32(p+3),4)) {
      rect=U32(p+3); ++rectMatches;
    }
  }
  if (rectMatches!=1) return false;
  auto position=TableMethod(image,table,10,1);
  if (!FunctionRange(image,position,&begin,&end) || begin!=position || end-begin>0x1000)
    return false;
  DWORD owner=0; unsigned ownerMatches=0;
  for (auto p=begin;p+22<=end;++p) {
    if (memcmp(p,"\x48\x8b\x8a",3) || memcmp(p+7,"\x48\x85\xc9\x74",4) ||
        memcmp(p+12,"\x48\x8d\x54\x24\x20\xe8",6) || !Field(U32(p+3),8)) continue;
    auto collectionPosition=Rip(p+22,p+18);
    if (image.Code(collectionPosition,13) &&
        !memcmp(collectionPosition,"\xf3\x0f\x10\x59",4) &&
        !memcmp(collectionPosition+5,"\x4c\x8b\xc1\xf3\x0f\x10\x51",7)) {
      owner=U32(p+3); ++ownerMatches;
    }
  }
  if (ownerMatches!=1) return false;
  output->image=image; output->textTable=table; output->typeGetter=found.type;
  output->managerSlot=global; output->focusSetter=found.focusCode;
  output->collections=found.collections; output->focus=found.focus;
  output->flags=found.flags; output->focusMask=found.focusMask;
  output->rect=rect; output->owner=owner; output->transform=found.transform;
  return true;
}

inline bool Resolve(const BYTE* base, Layout* output, int* stage) {
  *stage=1;
  Image image;
  if (!image.Init(reinterpret_cast<HMODULE>(const_cast<BYTE*>(base)))) return false;
  Findings found;
  ScanSections(image,true,[&](const BYTE* span,size_t size){InspectCode(image,span,size,found);});
  *stage=2;
  if (found.ambiguous || !found.type || !found.vectorCode || !found.focusCode ||
      !found.transformFunction) return false;
  unsigned textMatches=0,collectionMatches=0;
  Layout value;
  ScanSections(image,false,[&](const BYTE* span,size_t size){
    uintptr_t start=(reinterpret_cast<uintptr_t>(span)+7)&~uintptr_t(7);
    uintptr_t finish=reinterpret_cast<uintptr_t>(span)+size;
    for (;start+8<=finish;start+=8) {
      auto slot=reinterpret_cast<const BYTE*>(start);
      uint64_t target=0; memcpy(&target,slot,8);
      if (target==reinterpret_cast<uintptr_t>(found.type) && start>=14*8) {
        auto table=slot-14*8;
        Layout candidate;
        if (TextLayout(image,table,found,&candidate)) {value=candidate; ++textMatches;}
      }
      if (target==reinterpret_cast<uintptr_t>(found.focusCode) && start>=24*8) {
        auto table=slot-24*8;
        if (TableMethod(image,table,20,1)==found.transformFunction) ++collectionMatches;
      }
    }
  });
  *stage=3;
  if (textMatches!=1 || !collectionMatches) return false;
  *output=value; *stage=0;
  return true;
}
inline bool ResolveSafe(const BYTE* base, Layout* output, int* stage) {
  __try { return Resolve(base,output,stage); }
  __except(EXCEPTION_EXECUTE_HANDLER) { *stage=4; return false; }
}

struct Cache {
  SRWLOCK lock=SRWLOCK_INIT;
  Layout layout;
  const BYTE* base=nullptr;
  bool ready=false,busy=false;
  unsigned attempts=0;
  int failure=0;
  ULONGLONG nextAttempt=0;
};
inline Cache cache;
struct Work { const BYTE* base; HMODULE self; };

inline DWORD WINAPI Discover(void* argument) {
  auto work=static_cast<Work*>(argument);
  Layout value; int stage=0;
  bool ok=ResolveSafe(work->base,&value,&stage);
  AcquireSRWLockExclusive(&cache.lock);
  if (cache.base==work->base) {
    cache.layout=value; cache.ready=ok; cache.failure=stage; cache.busy=false;
  }
  ReleaseSRWLockExclusive(&cache.lock);
  HMODULE self=work->self;
  HeapFree(GetProcessHeap(),0,work);
  // Release the worker's own module reference only after it can no longer run
  // any DLL code. No worker survives completion, no global hooks are installed.
  FreeLibraryAndExitThread(self,0);
}

inline bool GetLayout(const BYTE* base, Layout* output, int* reason) {
  if (!base) return false;
  AcquireSRWLockExclusive(&cache.lock);
  if (cache.ready && cache.base==base) {
    *output=cache.layout; ReleaseSRWLockExclusive(&cache.lock); return true;
  }
  if (!cache.busy && cache.base!=base) {
    cache.base=base; cache.ready=false; cache.failure=0;
    cache.attempts=0; cache.nextAttempt=0;
  }
  *reason=cache.failure ? 220+cache.failure : 22;
  ULONGLONG now=GetTickCount64();
  if (cache.busy || now<cache.nextAttempt) {
    ReleaseSRWLockExclusive(&cache.lock); return false;
  }
  cache.base=base; cache.ready=false; cache.busy=true;
  cache.nextAttempt=now+(++cache.attempts>3 ? 30000 : 2000);
  ReleaseSRWLockExclusive(&cache.lock);
  HMODULE self=nullptr;
  auto work=static_cast<Work*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Work)));
  if (work && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(&Discover),&self)) {
    work->base=base; work->self=self;
    HANDLE thread=CreateThread(nullptr,0,Discover,work,0,nullptr);
    if (thread) { CloseHandle(thread); return false; }
    FreeLibrary(self);
  }
  if (work) HeapFree(GetProcessHeap(),0,work);
  AcquireSRWLockExclusive(&cache.lock); cache.busy=false;
  ReleaseSRWLockExclusive(&cache.lock);
  return false;
}
#endif
}  // namespace aoe2_native
