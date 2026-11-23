#include "core/NrbfReader.hpp"
#include "core/SaveManager.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace trainer;
using Bytes = std::vector<std::uint8_t>;

static_assert(noexcept(nrbf::Parse(std::declval<const Bytes&>())), "nrbf::Parse must be noexcept");
static_assert(noexcept(nrbf::PatchInt32Verified(
    std::declval<const Bytes&>(),
    std::declval<const nrbf::PatchRequest&>(),
    std::declval<std::string&>())), "nrbf::PatchInt32Verified must be noexcept");

namespace {

void Check(bool ok, const char* msg) {
    if (!ok) throw std::runtime_error(msg);
}

void PushU8(Bytes& out, std::uint8_t v) { out.push_back(v); }

void PushI16(Bytes& out, std::int16_t v) {
    const auto base = out.size();
    out.resize(base + 2);
    std::memcpy(&out[base], &v, 2);
}

void PushI32(Bytes& out, std::int32_t v) {
    const auto base = out.size();
    out.resize(base + 4);
    std::memcpy(&out[base], &v, 4);
}

void PushI64(Bytes& out, std::int64_t v) {
    const auto base = out.size();
    out.resize(base + 8);
    std::memcpy(&out[base], &v, 8);
}

void PushStr(Bytes& out, std::string_view s) {
    std::size_t len = s.size();
    do {
        std::uint8_t b = static_cast<std::uint8_t>(len & 0x7Fu);
        len >>= 7;
        if (len != 0) b |= 0x80u;
        out.push_back(b);
    } while (len != 0);
    out.insert(out.end(), s.begin(), s.end());
}

Bytes Header(std::int32_t rootId = 1, std::int32_t headerId = -1) {
    Bytes b;
    PushU8(b, 0);
    PushI32(b, rootId);
    PushI32(b, headerId);
    PushI32(b, 1);
    PushI32(b, 0);
    return b;
}

void StringRec(Bytes& b, std::int32_t id, std::string_view s) {
    PushU8(b, 6);
    PushI32(b, id);
    PushStr(b, s);
}

Bytes ScalarObj(std::uint8_t pt, std::int32_t rawBytes) {
    auto b = Header(1);
    PushU8(b, 4);
    PushI32(b, 1);
    PushStr(b, "T");
    PushI32(b, 1);
    PushStr(b, "V");
    PushU8(b, 0);
    PushU8(b, pt);
    PushI32(b, rawBytes);
    PushU8(b, 11);
    return b;
}

Bytes BuildValidSavedData(std::int32_t softVal, std::int32_t hardVal, std::int32_t levelVal) {
    auto b = Header(1);
    PushU8(b, 12); PushI32(b, 2); PushStr(b, "Assembly-CSharp, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");

    PushU8(b, 5); PushI32(b, 1); PushStr(b, "Saves.SavedData"); PushI32(b, 2);
    PushStr(b, "UpdatedAt"); PushStr(b, "Data");
    PushU8(b, 0); PushU8(b, 3);
    PushU8(b, 9); PushStr(b, "System.Collections.Generic.List`1[[Data.Memento, Assembly-CSharp]]");
    PushI32(b, 2);
    PushI64(b, 1795400000LL);
    PushU8(b, 9); PushI32(b, 3);

    PushU8(b, 4); PushI32(b, 3); PushStr(b, "System.Collections.Generic.List`1[[Data.Memento, Assembly-CSharp]]");
    PushI32(b, 3);
    PushStr(b, "_items"); PushStr(b, "_size"); PushStr(b, "_version");
    PushU8(b, 4); PushU8(b, 0); PushU8(b, 0);
    PushStr(b, "Data.Memento[]"); PushI32(b, 2); PushU8(b, 8); PushU8(b, 8);
    PushU8(b, 9); PushI32(b, 4);
    PushI32(b, 3);
    PushI32(b, 3);

    PushU8(b, 7); PushI32(b, 4); PushU8(b, 0); PushI32(b, 1); PushI32(b, 4);
    PushU8(b, 4); PushStr(b, "Data.Memento"); PushI32(b, 2);
    PushU8(b, 9); PushI32(b, 10);
    PushU8(b, 9); PushI32(b, 11);
    PushU8(b, 9); PushI32(b, 12);
    PushU8(b, 10);

    PushU8(b, 5); PushI32(b, 10); PushStr(b, "StripClub.Model.Data.SimpleCurrency+CurrencyMemento");
    PushI32(b, 3);
    PushStr(b, "Count"); PushStr(b, "Memento+<UniqueKey>k__BackingField"); PushStr(b, "Memento+<Version>k__BackingField");
    PushU8(b, 0); PushU8(b, 1); PushU8(b, 1);
    PushU8(b, 8);
    PushI32(b, 2);
    PushI32(b, softVal);
    PushU8(b, 6); PushI32(b, 100); PushStr(b, "currency_Soft[0];");
    PushU8(b, 6); PushI32(b, 101); PushStr(b, "6.3");

    PushU8(b, 1); PushI32(b, 11); PushI32(b, 10);
    PushI32(b, hardVal);
    PushU8(b, 6); PushI32(b, 102); PushStr(b, "currency_Hard[0];");
    PushU8(b, 9); PushI32(b, 101);

    PushU8(b, 5); PushI32(b, 12); PushStr(b, "_Scripts.PlayerLeveling.PlayerLevelerMemento");
    PushI32(b, 3);
    PushStr(b, "rewardedLevel"); PushStr(b, "Memento+<UniqueKey>k__BackingField"); PushStr(b, "Memento+<Version>k__BackingField");
    PushU8(b, 0); PushU8(b, 1); PushU8(b, 1);
    PushU8(b, 8);
    PushI32(b, 2);
    PushI32(b, levelVal);
    PushU8(b, 6); PushI32(b, 103); PushStr(b, "PlayerLeveler");
    PushU8(b, 9); PushI32(b, 101);

    PushU8(b, 11);
    return b;
}

void Reject(const Bytes& b, const char* reason) {
    const auto r = nrbf::Parse(b);
    Check(!r.ok && !r.error.empty(), reason);
    Check(r.document.objects.empty() && r.document.arrays.empty() &&
          r.document.strings.empty() && r.document.libraries.empty(),
          "Failed Parse must leave Document completely empty");
}

} // namespace

int main() {
    try {
        std::string error;

        // D01: Global memory & cumulative element budgets
        {
            auto b = Header(1);
            for (int id = 1; id <= 4; ++id) {
                PushU8(b, 16); PushI32(b, id); PushI32(b, 200000);
                PushU8(b, 14); PushI32(b, 200000);
            }
            PushU8(b, 11);
            Reject(b, "D01a: cumulative null-run across arrays must hit element budget");
        }
        {
            auto b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, std::string(800000, 'T')); PushI32(b, 0);
            for (int id = 2; id < 60; ++id) { PushU8(b, 1); PushI32(b, id); PushI32(b, 1); }
            PushU8(b, 11);
            Reject(b, "D01b: copied metadata class names must hit 16 MiB graph memory budget");
        }

        // D02: Empty string at physical EOF (no MessageEnd) and valid empty string
        {
            auto b = Header(1);
            StringRec(b, 1, "");
            Reject(b, "D02a: empty string at EOF without MessageEnd must fail safely");

            auto okEmpty = Header(1);
            StringRec(okEmpty, 1, "");
            PushU8(okEmpty, 11);
            const auto r = nrbf::Parse(okEmpty);
            Check(r.ok && r.document.strings.at(1).empty(), "D02b: valid empty UTF-8 string must parse");
        }

        // D03: Duplicate IDs, ID 0, ID namespaces, duplicate member names, valid negative IDs
        {
            auto b = Header(1); StringRec(b, 1, "a"); StringRec(b, 1, "b"); PushU8(b, 11);
            Reject(b, "D03a: duplicate BinaryObjectString ID rejected");

            b = Header(1); StringRec(b, 1, "a"); PushU8(b, 16); PushI32(b, 1); PushI32(b, 0); PushU8(b, 11);
            Reject(b, "D03b: cross-kind ID collision (string vs array) rejected");

            b = Header(0); StringRec(b, 0, "a"); PushU8(b, 11);
            Reject(b, "D03c: zero graph ID rejected");

            b = Header(1);
            PushU8(b, 12); PushI32(b, 2); PushStr(b, "A");
            PushU8(b, 12); PushI32(b, 2); PushStr(b, "B");
            StringRec(b, 1, "s"); PushU8(b, 11);
            Reject(b, "D03d: duplicate BinaryLibrary ID rejected");

            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 2);
            PushStr(b, "x"); PushStr(b, "x"); PushU8(b, 0); PushU8(b, 0); PushU8(b, 8); PushU8(b, 8);
            PushI32(b, 1); PushI32(b, 2); PushU8(b, 11);
            Reject(b, "D03e: duplicate member name in class rejected");

            // Negative inline struct IDs (used by Mono BinaryFormatter) remain valid when unique
            b = Header(-1);
            PushU8(b, 4); PushI32(b, -1); PushStr(b, "Box"); PushI32(b, 1);
            PushStr(b, "V"); PushU8(b, 0); PushU8(b, 8); PushI32(b, 9);
            PushU8(b, 1); PushI32(b, -2); PushI32(b, -1); PushI32(b, 10);
            PushU8(b, 11);
            Check(nrbf::Parse(b).ok, "D03f: valid negative object IDs must parse");
        }

        // D04: Missing root, dangling MemberReference, missing BinaryLibrary, valid cycle
        {
            auto b = Header(1); PushU8(b, 11);
            Reject(b, "D04a: missing rootId rejected");

            b = Header(1); StringRec(b, 2, "s"); PushU8(b, 11);
            Reject(b, "D04b: rootId not present in graph rejected");

            b = Header(1); PushU8(b, 16); PushI32(b, 1); PushI32(b, 1); PushU8(b, 9); PushI32(b, 99); PushU8(b, 11);
            Reject(b, "D04c: dangling MemberReference rejected");

            b = Header(1); PushU8(b, 5); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 0); PushI32(b, 2); PushU8(b, 11);
            Reject(b, "D04d: undefined ClassWithMembersAndTypes libraryId rejected");

            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1);
            PushStr(b, "f"); PushU8(b, 4); PushStr(b, "Other"); PushI32(b, 99);
            PushU8(b, 10); PushU8(b, 11);
            Reject(b, "D04e: undefined AdditionalTypeInfo BinaryType::Class libraryId rejected");

            b = Header(1); PushU8(b, 16); PushI32(b, 1); PushI32(b, 1); PushU8(b, 9); PushI32(b, 1); PushU8(b, 11);
            Check(nrbf::Parse(b).ok, "D04f: valid self-referencing cycle accepted");
        }

        // D05: Type consistency (inline + resolved reference + Boolean + empty array)
        {
            // ArraySingleString containing ArraySingleObject
            auto b = Header(1);
            PushU8(b, 17); PushI32(b, 1); PushI32(b, 1);
            PushU8(b, 16); PushI32(b, 2); PushI32(b, 0); PushU8(b, 11);
            Reject(b, "D05a: ArraySingleString containing ArraySingleObject rejected");

            // ArraySingleString referencing an ObjectRecord
            b = Header(2);
            PushU8(b, 4); PushI32(b, 2); PushStr(b, "T"); PushI32(b, 0);
            PushU8(b, 17); PushI32(b, 1); PushI32(b, 1); PushU8(b, 9); PushI32(b, 2); PushU8(b, 11);
            Reject(b, "D05b: ArraySingleString MemberReference to ObjectRecord rejected");

            // Class member PrimitiveArray(Int32) receiving ArraySinglePrimitive(Byte)
            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1);
            PushStr(b, "arr"); PushU8(b, 7); PushU8(b, 8);
            PushU8(b, 15); PushI32(b, 2); PushI32(b, 1); PushU8(b, 2); PushU8(b, 5);
            PushU8(b, 11);
            Reject(b, "D05c: PrimitiveArray(Int32) slot populated by ArraySinglePrimitive(Byte) rejected");

            // ArraySinglePrimitive(Boolean) with invalid byte 2
            b = Header(1);
            PushU8(b, 15); PushI32(b, 1); PushI32(b, 1); PushU8(b, 1); PushU8(b, 2); PushU8(b, 11);
            Reject(b, "D05d: invalid Boolean byte value 2 rejected");

            // Empty BinaryArray with invalid PrimitiveType 255
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 0); PushI32(b, 1); PushI32(b, 0);
            PushU8(b, 0); PushU8(b, 255); PushU8(b, 11);
            Reject(b, "D05e: empty BinaryArray with invalid PrimitiveType rejected");

            // Empty BinaryArray with PrimitiveType String (18)
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 0); PushI32(b, 1); PushI32(b, 0);
            PushU8(b, 0); PushU8(b, 18); PushU8(b, 11);
            Reject(b, "D05f: empty BinaryArray with PrimitiveType::String rejected");

            // Class member BinaryType::SystemClass("MyClass") receiving BinaryObjectString
            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1);
            PushStr(b, "obj"); PushU8(b, 3); PushStr(b, "MyClass");
            StringRec(b, 2, "bad"); PushU8(b, 11);
            Reject(b, "D05g: SystemClass slot receiving BinaryObjectString rejected");

            // Positive: SystemClass("System.Decimal[]") receiving ArraySinglePrimitive(Decimal)
            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1);
            PushStr(b, "decs"); PushU8(b, 3); PushStr(b, "System.Decimal[]");
            PushU8(b, 15); PushI32(b, 2); PushI32(b, 1); PushU8(b, 5); PushStr(b, "12.34");
            PushU8(b, 11);
            Check(nrbf::Parse(b).ok, "D05h: SystemClass array type referencing ArraySinglePrimitive(Decimal) accepted");
        }

        // D06: Primitive array elements & boxed MemberPrimitiveTyped in ArraySingleObject preserved
        {
            auto b = Header(1);
            PushU8(b, 15); PushI32(b, 1); PushI32(b, 3); PushU8(b, 8);
            PushI32(b, 10); PushI32(b, -20); PushI32(b, 30); PushU8(b, 11);
            const auto r = nrbf::Parse(b);
            Check(r.ok, "D06a: ArraySinglePrimitive(Int32) parse ok");
            const auto& arr = r.document.arrays.at(1);
            Check(arr.elementBinaryType == nrbf::BinaryType::Primitive &&
                  arr.elementPrimitiveType == nrbf::PrimitiveType::Int32 &&
                  arr.elements.size() == 3, "D06a: 3 Int32 elements preserved");
            Check(std::get<std::int64_t>(arr.elements[0].value) == 10 &&
                  std::get<std::int64_t>(arr.elements[1].value) == -20 &&
                  std::get<std::int64_t>(arr.elements[2].value) == 30, "D06a: exact Int32 element values preserved");

            auto b2 = Header(1);
            PushU8(b2, 16); PushI32(b2, 1); PushI32(b2, 1);
            PushU8(b2, 8); PushU8(b2, 8); PushI32(b2, 42); PushU8(b2, 11);
            const auto r2 = nrbf::Parse(b2);
            Check(r2.ok && r2.document.arrays.at(1).elements.size() == 1, "D06b: boxed MemberPrimitiveTyped in array parsed");
            const auto& elem = r2.document.arrays.at(1).elements[0];
            Check(elem.primitiveType == nrbf::PrimitiveType::Int32 &&
                  std::get<std::int64_t>(elem.value) == 42, "D06b: boxed primitive value 42 preserved");
        }

        // D07: Null-run records disallowed outside arrays / zero counts / top-level ObjectNull
        {
            auto b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1); PushStr(b, "s"); PushU8(b, 1);
            PushU8(b, 13); PushU8(b, 1); PushU8(b, 11);
            Reject(b, "D07a: ObjectNullMultiple256(1) in class member slot rejected");

            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1); PushStr(b, "s"); PushU8(b, 1);
            PushU8(b, 14); PushI32(b, 1); PushU8(b, 11);
            Reject(b, "D07b: ObjectNullMultiple(1) in class member slot rejected");

            b = Header(1);
            PushU8(b, 16); PushI32(b, 1); PushI32(b, 1); PushU8(b, 13); PushU8(b, 0); PushU8(b, 11);
            Reject(b, "D07c: zero-count ObjectNullMultiple256 in array rejected");

            b = Header(1);
            PushU8(b, 16); PushI32(b, 1); PushI32(b, 1); PushU8(b, 14); PushI32(b, 2); PushU8(b, 11);
            Reject(b, "D07d: ObjectNullMultiple exceeding array bounds rejected");

            b = Header(1);
            StringRec(b, 1, "s"); PushU8(b, 10); PushU8(b, 11);
            Reject(b, "D07e: top-level ObjectNull rejected");
        }

        // D08: Strict UTF-8 validation and full Unicode Char decoding
        {
            auto b = Header(1); StringRec(b, 1, std::string("\xC0\x80", 2)); PushU8(b, 11);
            Reject(b, "D08a: overlong UTF-8 string rejected");

            b = Header(1); StringRec(b, 1, std::string("\xED\xA0\x80", 3)); PushU8(b, 11);
            Reject(b, "D08b: UTF-16 surrogate in UTF-8 string rejected");

            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1); PushStr(b, "c"); PushU8(b, 0); PushU8(b, 3);
            PushU8(b, 0xC2); PushU8(b, 0x20); PushU8(b, 11);
            Reject(b, "D08c: invalid UTF-8 Char continuation byte rejected");

            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 1); PushStr(b, "c"); PushU8(b, 0); PushU8(b, 3);
            PushU8(b, 0xF0); PushU8(b, 0x9F); PushU8(b, 0x98); PushU8(b, 0x80); PushU8(b, 11);
            Reject(b, "D08d: non-BMP 4-byte Char rejected");

            // Valid 2-byte UTF-8 Cyrillic 'Д' (U+0414 = D0 94) and 3-byte '€' (U+20AC = E2 82 AC)
            b = Header(1);
            PushU8(b, 4); PushI32(b, 1); PushStr(b, "T"); PushI32(b, 2);
            PushStr(b, "c1"); PushStr(b, "c2");
            PushU8(b, 0); PushU8(b, 0); PushU8(b, 3); PushU8(b, 3);
            PushU8(b, 0xD0); PushU8(b, 0x94);
            PushU8(b, 0xE2); PushU8(b, 0x82); PushU8(b, 0xAC);
            PushU8(b, 11);
            const auto r = nrbf::Parse(b);
            Check(r.ok, "D08e: valid multi-byte BMP Chars parsed");
            const auto* f1 = r.document.objects.at(1).FindField("c1");
            const auto* f2 = r.document.objects.at(1).FindField("c2");
            Check(f1 && f1->primitiveType == nrbf::PrimitiveType::Char && f1->byteSize == 2 &&
                  std::get<std::int64_t>(f1->value) == 0x0414, "D08e: Char U+0414 exact code point");
            Check(f2 && f2->primitiveType == nrbf::PrimitiveType::Char && f2->byteSize == 3 &&
                  std::get<std::int64_t>(f2->value) == 0x20AC, "D08e: Char U+20AC exact code point");
        }

        // D09: Preserve PrimitiveType & enforce Int32 in PatchInt32Verified and SaveManager
        {
            const auto valid = BuildValidSavedData(72, 88, 2);
            const auto parsedValid = nrbf::Parse(valid);
            Check(parsedValid.ok, "D09 baseline parse");
            const auto* countField = parsedValid.document.objects.at(10).FindField("Count");
            Check(countField && countField->primitiveType == nrbf::PrimitiveType::Int32, "D09a: Int32 PrimitiveType recorded");

            // Mutate metadata member type from Int32 (8) to Single (11)
            auto wrongCountType = valid;
            wrongCountType[countField->byteOffset - 5] = 11;
            Check(nrbf::Parse(wrongCountType).ok && SaveManager::ParseParameters(wrongCountType).empty(),
                  "D09b: SaveManager must reject non-Int32 CurrencyMemento.Count");

            // Mutate library of CurrencyMemento to non-Assembly-CSharp
            auto wrongLib = valid;
            wrongLib[countField->byteOffset - 4] = 3;
            wrongLib.pop_back();
            PushU8(wrongLib, 12); PushI32(wrongLib, 3); PushStr(wrongLib, "OtherAssembly"); PushU8(wrongLib, 11);
            Check(nrbf::Parse(wrongLib).ok && SaveManager::ParseParameters(wrongLib).empty(),
                  "D09c: SaveManager must reject CurrencyMemento from untrusted library");

            // Duplicate UniqueKey inside Saves.SavedData
            auto dupKeys = valid;
            const std::string fromKey = "currency_Hard[0];", toKey = "currency_Soft[0];";
            auto loc = std::search(dupKeys.begin(), dupKeys.end(), fromKey.begin(), fromKey.end());
            Check(loc != dupKeys.end(), "D09 dupKey fixture lookup");
            std::copy(toKey.begin(), toKey.end(), loc);
            Check(nrbf::Parse(dupKeys).ok && SaveManager::ParseParameters(dupKeys).empty(),
                  "D09i: SaveManager must reject duplicate parameter UniqueKeys");

            const auto s = ScalarObj(11, 72);
            const auto off = nrbf::Parse(s).document.objects.at(1).fields.at(0).byteOffset;
            Check(!nrbf::PatchInt32Verified(s, {off, 72, 500, 0, 1000}, error).has_value(),
                  "D09d: PatchInt32Verified rejects Single");

            const auto u = ScalarObj(15, 72);
            Check(!nrbf::PatchInt32Verified(u, {off, 72, 500, 0, 1000}, error).has_value(),
                  "D09e: PatchInt32Verified rejects UInt32");

            auto i32 = ScalarObj(8, 72);
            Check(nrbf::PatchInt32Verified(i32, {off, 72, 500, 0, 1000}, error).has_value(),
                  "D09f: PatchInt32Verified succeeds for Int32");
            Check(!nrbf::PatchInt32Verified(i32, {off, 72, 500, 100, 0}, error).has_value(),
                  "D09g: PatchInt32Verified rejects inverted range");
            Check(!nrbf::PatchInt32Verified(i32, {std::numeric_limits<std::size_t>::max(), 72, 500, 0, 1000}, error).has_value(),
                  "D09h: PatchInt32Verified rejects SIZE_MAX offset");
        }

        // D10: BinaryArray forms, rank, lower bounds, and valid multi-dimensional arrays
        {
            // Single (0) with rank=2 -> fail
            auto b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 0); PushI32(b, 2); PushI32(b, 1); PushI32(b, 1);
            PushU8(b, 2); PushU8(b, 10); PushU8(b, 11);
            Reject(b, "D10a: BinaryArray Single(0) with rank=2 rejected");

            // Jagged (1) with rank=2 -> fail
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 1); PushI32(b, 2); PushI32(b, 1); PushI32(b, 1);
            PushU8(b, 5); PushU8(b, 10); PushU8(b, 11);
            Reject(b, "D10b: BinaryArray Jagged(1) with rank=2 rejected");

            // Jagged (1) with Primitive element type -> fail
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 1); PushI32(b, 1); PushI32(b, 0);
            PushU8(b, 0); PushU8(b, 8); PushU8(b, 11);
            Reject(b, "D10c: BinaryArray Jagged(1) with Primitive element type rejected");

            // Rectangular (2) with rank=1 -> fail
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 2); PushI32(b, 1); PushI32(b, 1);
            PushU8(b, 2); PushU8(b, 10); PushU8(b, 11);
            Reject(b, "D10d: BinaryArray Rectangular(2) with rank=1 rejected");

            // Rectangular (2) dimension overflow -> fail
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 2); PushI32(b, 2); PushI32(b, 262144); PushI32(b, 262144);
            PushU8(b, 0); PushU8(b, 8); PushU8(b, 11);
            Reject(b, "D10e: Rectangular BinaryArray product overflow rejected");

            // SingleOffset (3) lowerBound + length > INT32_MAX -> fail
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 3); PushI32(b, 1); PushI32(b, 1);
            PushI32(b, std::numeric_limits<std::int32_t>::max());
            PushU8(b, 2); PushU8(b, 10); PushU8(b, 11);
            Reject(b, "D10f: SingleOffset lowerBound+len overflow rejected");

            // Positive: Rectangular(2) 2x2 of Int16
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 2); PushI32(b, 2); PushI32(b, 2); PushI32(b, 2);
            PushU8(b, 0); PushU8(b, 7);
            PushI16(b, 1); PushI16(b, 2); PushI16(b, 3); PushI16(b, -4); PushU8(b, 11);
            const auto rect = nrbf::Parse(b);
            Check(rect.ok && rect.document.arrays.at(1).lengths.size() == 2 &&
                  rect.document.arrays.at(1).elements.size() == 4 &&
                  std::get<std::int64_t>(rect.document.arrays.at(1).elements[3].value) == -4,
                  "D10g: valid Rectangular BinaryArray parsed with 4 Int16 elements");

            // Positive: SingleOffset(3) len=2, lowerBound=-5, Int32
            b = Header(1);
            PushU8(b, 7); PushI32(b, 1); PushU8(b, 3); PushI32(b, 1); PushI32(b, 2); PushI32(b, -5);
            PushU8(b, 0); PushU8(b, 8);
            PushI32(b, 100); PushI32(b, 200); PushU8(b, 11);
            const auto offArr = nrbf::Parse(b);
            Check(offArr.ok && offArr.document.arrays.at(1).lowerBounds.size() == 1 &&
                  offArr.document.arrays.at(1).lowerBounds[0] == -5 &&
                  offArr.document.arrays.at(1).elements.size() == 2 &&
                  std::get<std::int64_t>(offArr.document.arrays.at(1).elements[1].value) == 200,
                  "D10h: valid SingleOffset BinaryArray parsed with lowerBound=-5");
        }

        // D12: Truncations and deterministic single-byte mutation resilience on baseline fixture
        {
            const auto valid = BuildValidSavedData(72, 88, 2);
            Check(nrbf::Parse(valid).ok, "D12a: baseline SavedData fixture ok");
            for (std::size_t i = 0; i < valid.size(); ++i) {
                Reject(Bytes(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(i)),
                       "D12b: truncated fixture must fail cleanly");
            }
            std::mt19937 rng(12345);
            for (int i = 0; i < 2000; ++i) {
                auto mut = valid;
                mut[rng() % mut.size()] ^= static_cast<std::uint8_t>(1 + (rng() % 255));
                (void)nrbf::Parse(mut);
            }
        }

        std::cout << "NRBF audit regression tests (D01-D12): PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << "\n";
        return 1;
    }
}
