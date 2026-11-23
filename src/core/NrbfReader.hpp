#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace trainer::nrbf {

enum class PrimitiveType : std::uint8_t {
    Boolean = 1,
    Byte = 2,
    Char = 3,
    Decimal = 5,
    Double = 6,
    Int16 = 7,
    Int32 = 8,
    Int64 = 9,
    SByte = 10,
    Single = 11,
    TimeSpan = 12,
    DateTime = 13,
    UInt16 = 14,
    UInt32 = 15,
    UInt64 = 16,
    Null = 17,
    String = 18
};

struct ObjectRef {
    std::int32_t id = 0;
};

struct NullValue {};

struct FieldValue {
    std::string name;
    std::size_t byteOffset = 0;
    std::size_t byteSize = 0;
    std::variant<NullValue, bool, std::int64_t, std::uint64_t, double, std::string, ObjectRef> value;
};

struct ObjectRecord {
    std::int32_t objectId = 0;
    std::int32_t metadataId = 0;
    std::int32_t libraryId = 0;
    std::string className;
    std::vector<FieldValue> fields;

    [[nodiscard]] const FieldValue* FindField(std::string_view name) const;
};

struct ArrayRecord {
    std::int32_t objectId = 0;
    std::vector<std::int32_t> lengths;
    // Populated for object/string arrays (element is ObjectRef, string, or NullValue).
    // Primitive arrays are validated and skipped without inflating memory.
    std::vector<std::variant<NullValue, std::string, ObjectRef>> elements;
};

struct Document {
    std::int32_t rootId = 0;
    std::int32_t headerId = 0;
    std::size_t bytesConsumed = 0;
    std::size_t recordCount = 0;
    std::unordered_map<std::int32_t, std::string> libraries;
    std::unordered_map<std::int32_t, std::string> strings;
    std::unordered_map<std::int32_t, ObjectRecord> objects;
    std::unordered_map<std::int32_t, ArrayRecord> arrays;

    [[nodiscard]] std::optional<std::string> ResolveString(const FieldValue& field) const;
    [[nodiscard]] const ObjectRecord* ResolveObject(const FieldValue& field) const;
    [[nodiscard]] const ArrayRecord* ResolveArray(const FieldValue& field) const;
};

struct ParseResult {
    bool ok = false;
    std::string error;
    Document document;
};

// Bounded, non-executing MS-NRBF parser. Never throws on malformed input.
[[nodiscard]] ParseResult Parse(const std::vector<std::uint8_t>& bytes);

// Pure integrity-checked Int32 patcher: re-parses before and after patching,
// verifying expected old value, bounds, and complete NRBF stream validity.
struct PatchRequest {
    std::size_t byteOffset = 0;
    std::int32_t expectedOldValue = 0;
    std::int32_t newValue = 0;
    std::int32_t minAllowed = 0;
    std::int32_t maxAllowed = 1000000000;
};

[[nodiscard]] std::optional<std::vector<std::uint8_t>> PatchInt32Verified(
    const std::vector<std::uint8_t>& originalBytes,
    const PatchRequest& request,
    std::string& error);

} // namespace trainer::nrbf
