#pragma once
#include <vector>
#include <string_view>
#include <cstdint>
#include <cstring>
namespace {

void PushU8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }
void PushI32(std::vector<std::uint8_t>& out, std::int32_t v) {
    const auto base = out.size();
    out.resize(base + 4);
    std::memcpy(&out[base], &v, 4);
}
void PushI64(std::vector<std::uint8_t>& out, std::int64_t v) {
    const auto base = out.size();
    out.resize(base + 8);
    std::memcpy(&out[base], &v, 8);
}
void PushStr(std::vector<std::uint8_t>& out, std::string_view s) {
    std::size_t len = s.size();
    do {
        std::uint8_t b = static_cast<std::uint8_t>(len & 0x7Fu);
        len >>= 7;
        if (len != 0) b |= 0x80u;
        out.push_back(b);
    } while (len != 0);
    out.insert(out.end(), s.begin(), s.end());
}

// Builds a minimal, valid MS-NRBF stream matching Slutopia's Saves.SavedData schema.
std::vector<std::uint8_t> BuildValidSavedDataNrbf(std::int32_t softVal, std::int32_t hardVal, std::int32_t levelVal) {
    std::vector<std::uint8_t> b;
    // Record 0: SerializedStreamHeader (rootId=1, headerId=-1, major=1, minor=0)
    PushU8(b, 0); PushI32(b, 1); PushI32(b, -1); PushI32(b, 1); PushI32(b, 0);
    // Record 12: BinaryLibrary (id=2, Assembly-CSharp)
    PushU8(b, 12); PushI32(b, 2); PushStr(b, "Assembly-CSharp, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");

    // Record 5: ClassWithMembersAndTypes (id=1, Saves.SavedData, lib=2)
    PushU8(b, 5); PushI32(b, 1); PushStr(b, "Saves.SavedData"); PushI32(b, 2);
    PushStr(b, "UpdatedAt"); PushStr(b, "Data");
    PushU8(b, 0); PushU8(b, 3); // Primitive(Int64), SystemClass
    PushU8(b, 9); PushStr(b, "System.Collections.Generic.List`1[[Data.Memento, Assembly-CSharp]]");
    PushI32(b, 2); // libraryId
    PushI64(b, 1795400000LL); // UpdatedAt
    PushU8(b, 9); PushI32(b, 3); // MemberReference -> List (id=3)

    // Record 4: SystemClassWithMembersAndTypes (id=3, List<Memento>)
    PushU8(b, 4); PushI32(b, 3); PushStr(b, "System.Collections.Generic.List`1[[Data.Memento, Assembly-CSharp]]");
    PushI32(b, 3);
    PushStr(b, "_items"); PushStr(b, "_size"); PushStr(b, "_version");
    PushU8(b, 4); PushU8(b, 0); PushU8(b, 0); // Class(array), Int32, Int32
    PushStr(b, "Data.Memento[]"); PushI32(b, 2); PushU8(b, 8); PushU8(b, 8);
    PushU8(b, 9); PushI32(b, 4); // _items -> BinaryArray (id=4)
    PushI32(b, 3); // _size = 3
    PushI32(b, 3); // _version = 3

    // Record 7: BinaryArray (id=4, Single, rank=1, len=4, Class Data.Memento)
    PushU8(b, 7); PushI32(b, 4); PushU8(b, 0); PushI32(b, 1); PushI32(b, 4);
    PushU8(b, 4); PushStr(b, "Data.Memento"); PushI32(b, 2);
    PushU8(b, 9); PushI32(b, 10); // [0] -> CurrencyMemento (Soft)
    PushU8(b, 9); PushI32(b, 11); // [1] -> CurrencyMemento (Hard)
    PushU8(b, 9); PushI32(b, 12); // [2] -> PlayerLevelerMemento
    PushU8(b, 10);                // [3] -> ObjectNull (unused capacity)

    // Record 5: ClassWithMembersAndTypes (id=10, CurrencyMemento - Soft)
    PushU8(b, 5); PushI32(b, 10); PushStr(b, "StripClub.Model.Data.SimpleCurrency+CurrencyMemento");
    PushI32(b, 3);
    PushStr(b, "Count"); PushStr(b, "Memento+<UniqueKey>k__BackingField"); PushStr(b, "Memento+<Version>k__BackingField");
    PushU8(b, 0); PushU8(b, 1); PushU8(b, 1); // Int32, String, String
    PushU8(b, 8);
    PushI32(b, 2); // libId
    PushI32(b, softVal);
    PushU8(b, 6); PushI32(b, 100); PushStr(b, "currency_Soft[0];");
    PushU8(b, 6); PushI32(b, 101); PushStr(b, "6.3");

    // Record 1: ClassWithId (id=11, metadataId=10, CurrencyMemento - Hard)
    PushU8(b, 1); PushI32(b, 11); PushI32(b, 10);
    PushI32(b, hardVal);
    PushU8(b, 6); PushI32(b, 102); PushStr(b, "currency_Hard[0];");
    PushU8(b, 9); PushI32(b, 101); // reuse version string "6.3"

    // Record 5: ClassWithMembersAndTypes (id=12, PlayerLevelerMemento)
    PushU8(b, 5); PushI32(b, 12); PushStr(b, "_Scripts.PlayerLeveling.PlayerLevelerMemento");
    PushI32(b, 3);
    PushStr(b, "rewardedLevel"); PushStr(b, "Memento+<UniqueKey>k__BackingField"); PushStr(b, "Memento+<Version>k__BackingField");
    PushU8(b, 0); PushU8(b, 1); PushU8(b, 1);
    PushU8(b, 8);
    PushI32(b, 2);
    PushI32(b, levelVal);
    PushU8(b, 6); PushI32(b, 103); PushStr(b, "PlayerLeveler");
    PushU8(b, 9); PushI32(b, 101);

    // Record 11: MessageEnd
    PushU8(b, 11);
    return b;
}

} // namespace

