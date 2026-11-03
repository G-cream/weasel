#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dota_native {

// Module identity is established by its loader handle. Validate section bounds
// and interface ABI instead of accepting only one timestamp/image size/RVA.
struct Image {
  const BYTE* base = nullptr;
  DWORD size = 0;
  const IMAGE_SECTION_HEADER* sections = nullptr;
  WORD count = 0;

  bool Init(HMODULE module) {
    if (!module) return false;
    base = reinterpret_cast<const BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(*dos) ||
        dos->e_lfanew > 0x100000) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        !nt->FileHeader.NumberOfSections || nt->FileHeader.NumberOfSections > 96)
      return false;
    size = nt->OptionalHeader.SizeOfImage;
    if (size < 4096 || size > 0x40000000) return false;
    sections = IMAGE_FIRST_SECTION(nt);
    count = nt->FileHeader.NumberOfSections;
    auto end = reinterpret_cast<const BYTE*>(sections + count);
    return end >= base && static_cast<size_t>(end - base) <=
                              nt->OptionalHeader.SizeOfHeaders &&
           nt->OptionalHeader.SizeOfHeaders <= size;
  }

  bool Contains(const void* address, size_t bytes, DWORD required,
                DWORD forbidden = 0) const {
    auto value = reinterpret_cast<uintptr_t>(address);
    auto start = reinterpret_cast<uintptr_t>(base);
    if (!base || value < start || bytes > size || value - start > size - bytes)
      return false;
    size_t offset = value - start;
    for (WORD i = 0; i < count; ++i) {
      const auto& section = sections[i];
      size_t length = section.Misc.VirtualSize;
      if (section.VirtualAddress > size || length > size - section.VirtualAddress)
        continue;
      if (offset >= section.VirtualAddress && bytes <= length &&
          offset - section.VirtualAddress <= length - bytes &&
          (section.Characteristics & required) == required &&
          !(section.Characteristics & forbidden)) return true;
    }
    return false;
  }

  bool ReadOnly(const void* address, size_t bytes) const {
    return Contains(address, bytes, IMAGE_SCN_MEM_READ,
                    IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE);
  }
  bool Code(const void* address, size_t bytes) const {
    return Contains(address, bytes, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE,
                    IMAGE_SCN_MEM_WRITE);
  }
};

inline const BYTE* Pointer(const void* address) {
  uintptr_t value = 0;
  memcpy(&value, address, sizeof(value));
  if (value < 0x10000) return nullptr;
  return reinterpret_cast<const BYTE*>(value);
}

inline const BYTE* Method(const Image& image, const BYTE* object, size_t slot,
                           size_t bytes) {
  if (!object || slot > 128) return nullptr;
  auto table = Pointer(object);
  if (!table || reinterpret_cast<uintptr_t>(table) % sizeof(void*) ||
      !image.ReadOnly(table, (slot + 1) * sizeof(void*))) return nullptr;
  // Function entry points need not be pointer-aligned.
  uintptr_t raw = 0;
  memcpy(&raw, table + slot * sizeof(void*), sizeof(raw));
  auto function = reinterpret_cast<const BYTE*>(raw);
  return image.Code(function, bytes) ? function : nullptr;
}

inline bool Type(const Image& image, const BYTE* object, const char* name,
                   DWORD* offset = nullptr) {
  if (!object) return false;
  auto table = Pointer(object);
  if (!table || reinterpret_cast<uintptr_t>(table) % sizeof(void*) ||
      !image.ReadOnly(table - sizeof(void*), 2 * sizeof(void*))) return false;
  auto locator = Pointer(table - sizeof(void*));
  if (!locator || !image.ReadOnly(locator, 24)) return false;
  DWORD fields[6] = {};
  memcpy(fields, locator, sizeof(fields));
  if (fields[0] != 1 || fields[1] > 0x100000 || fields[2] != 0 ||
      fields[5] >= image.size || image.base + fields[5] != locator ||
      fields[3] >= image.size || image.size - fields[3] < 16) return false;
  auto typeName = image.base + fields[3] + 16;
  size_t length = strlen(name) + 1;
  if (!image.Contains(typeName, length, IMAGE_SCN_MEM_READ) ||
      memcmp(typeName, name, length)) return false;
  if (offset) *offset = fields[1];
  return true;
}

// Decode only complete, side-effect-free leaf getters. Field offsets are
// obtained from the current implementation, not remembered across game builds.
inline bool PointerField(const BYTE* code, DWORD* offset) {
  if (code[0] != 0x48 || code[1] != 0x8b) return false;
  if (code[2] == 0x41 && code[4] == 0xc3 && code[3] < 0x80)
    *offset = code[3];
  else if (code[2] == 0x81 && code[7] == 0xc3)
    memcpy(offset, code + 3, sizeof(*offset));
  else return false;
  return *offset <= 0x10000 && *offset % sizeof(void*) == 0;
}

inline bool FloatField(const BYTE* code, DWORD* offset) {
  if (memcmp(code, "\xf3\x0f\x10", 3)) return false;
  if (code[3] == 0x41 && code[5] == 0xc3 && code[4] < 0x80)
    *offset = code[4];
  else if (code[3] == 0x81 && code[8] == 0xc3)
    memcpy(offset, code + 4, sizeof(*offset));
  else return false;
  return *offset <= 0x10000 && *offset % sizeof(float) == 0;
}

inline const BYTE* ActiveView(const Image& ime, const BYTE* manager) {
  if (!Type(ime, manager, ".?AVCIMEManager@@")) return nullptr;
  auto code = Method(ime, manager, 20, 23);
  if (!code || memcmp(code, "\x80\x79", 2) || code[2] >= 0x80 ||
      memcmp(code + 3, "\x00\x75\x03\x33\xc0\xc3\x48\x8b\x49", 9) ||
      code[12] >= 0x80 || memcmp(code + 13, "\x48\x8b\x01\x48\xff\xa0", 6))
    return nullptr;
  DWORD slotBytes = 0;
  memcpy(&slotBytes, code + 19, sizeof(slotBytes));
  if (slotBytes % sizeof(void*) || slotBytes / sizeof(void*) > 128 ||
      !manager[code[2]]) return nullptr;
  auto owner = Pointer(manager + code[12]);
  if (!Type(ime, owner, ".?AVGFxIMEManagerWin32@@")) return nullptr;
  auto getter = Method(ime, owner, slotBytes / sizeof(void*), 8);
  DWORD field = 0;
  return getter && PointerField(getter, &field) ? Pointer(owner + field) : nullptr;
}

struct Geometry { float x, y, width, height; };

inline bool GeometryForManager(const Image& ime, const Image& client,
                                 const Image& panorama, const BYTE* manager,
                                 Geometry* geometry, int* reason) {
#if defined(_M_X64)
  *reason = 5;
  auto view = ActiveView(ime, manager);
  if (!Type(panorama, view, ".?AVCTopLevelWindowSource2@panorama@@")) return false;
  auto focus = Method(panorama, view, 0, 24);
  const BYTE focusPrefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,
                              0x24,0x10,0x57,0x48,0x83,0xec,0x20};
  if (!focus || memcmp(focus, focusPrefix, sizeof(focusPrefix)) ||
      memcmp(focus + 15, "\x48\x8b\x59", 3) || focus[18] >= 0x80 ||
      memcmp(focus + 19, "\x33\xf6\x48\x8b\xf9", 5)) return false;
  using FocusedControl = const BYTE* (__fastcall*)(const BYTE*);
  auto getControl = reinterpret_cast<FocusedControl>(const_cast<BYTE*>(focus));
  auto control = getControl(view);
  *reason = 6;
  DWORD adjustment = 0;
  if (!Type(client, control, ".?AVCTextEntry@panorama@@", &adjustment)) return false;
  auto kind = Method(client, control, 0, 6);
  if (!kind || memcmp(kind, "\xb8\x01\x00\x00\x00\xc3", 6)) return false;
  auto entry = reinterpret_cast<const BYTE*>(reinterpret_cast<uintptr_t>(control) - adjustment);
  DWORD primaryAdjustment = 0;
  if (!Type(client, entry, ".?AVCTextEntry@panorama@@", &primaryAdjustment) || primaryAdjustment)
    return false;
  *reason = 7;
  auto panelGetter = Method(client, entry, 0, 8);
  DWORD panelField = 0;
  if (!panelGetter || !PointerField(panelGetter, &panelField)) return false;
  auto panel = Pointer(entry + panelField);
  if (!Type(panorama, panel, ".?AVCUIPanel@panorama@@")) return false;
  auto widthGetter = Method(panorama, panel, 100, 9);
  auto heightGetter = Method(panorama, panel, 101, 9);
  DWORD widthField = 0, heightField = 0;
  if (!widthGetter || !heightGetter || !FloatField(widthGetter, &widthField) ||
      !FloatField(heightGetter, &heightField) || heightField != widthField + sizeof(float))
    return false;
  *reason = 8;
  auto position = Method(client, entry, 45, 25);
  const BYTE positionPrefix[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,
    0xec,0x40,0x49,0x8b,0xf9,0x48,0xc7,0x44,0x24,0x30,0,0,0,0,0x49,0x8b,0xd8};
  if (!position || memcmp(position, positionPrefix, sizeof(positionPrefix))) return false;
  using Position = void (__fastcall*)(const BYTE*, int, float*, float*);
  Geometry value = {std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::quiet_NaN(), 0, 0};
  reinterpret_cast<Position>(const_cast<BYTE*>(position))(entry, 0, &value.x, &value.y);
  memcpy(&value.width, panel + widthField, sizeof(float));
  memcpy(&value.height, panel + heightField, sizeof(float));
  // A focus transition during a native query must not publish the old rectangle.
  if (ActiveView(ime, manager) != view || getControl(view) != control) return false;
  *geometry = value;
  return true;
#else
  (void)ime; (void)client; (void)panorama; (void)manager; (void)geometry;
  *reason = 10;
  return false;
#endif
}
}  // namespace dota_native
