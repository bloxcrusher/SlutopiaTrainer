#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

enum class BinaryType : std::uint8_t {
    Primitive = 0,
    String = 1,
    Object = 2,
    SystemClass = 3,
    Class = 4,
    ObjectArray = 5,
    StringArray = 6,
    PrimitiveArray = 7
};

enum class BinaryArrayType : std::uint8_t {
    Single = 0,
    Jagged = 1,
    Rectangular = 2,
    SingleOffset = 3,
    JaggedOffset = 4,
    RectangularOffset = 5
};

struct ObjectRef {
    std::int32_t id = 0;
};

struct NullValue {};

using ValueVariant = std::variant<NullValue, bool, std::int64_t, std::uint64_t, double, std::string, ObjectRef>;

struct FieldValue {
    std::string name;
    PrimitiveType primitiveType = PrimitiveType::Null;
    std::size_t byteOffset = 0;
    std::size_t byteSize = 0;
    ValueVariant value;
};

struct ObjectRecord {
    std::int32_t objectId = 0;
    std::int32_t metadataId = 0;
    std::int32_t libraryId = 0;
    std::string className;
    std::vector<FieldValue> fields;

    [[nodiscard]] const FieldValue* FindField(std::string_view name) const;
};

struct ArrayElement {
    PrimitiveType primitiveType = PrimitiveType::Null;
    std::size_t byteOffset = 0;
    std::size_t byteSize = 0;
    ValueVariant value;
};

struct ArrayRecord {
    std::int32_t objectId = 0;
    BinaryArrayType arrayType = BinaryArrayType::Single;
    BinaryType elementBinaryType = BinaryType::Object;
    PrimitiveType elementPrimitiveType = PrimitiveType::Null;
    std::vector<std::int32_t> lengths;
    std::vector<std::int32_t> lowerBounds;
    std::vector<ArrayElement> elements;
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

// Bounded, non-executing MS-NRBF parser. Guaranteed noexcept; clears Document on failure.
[[nodiscard]] ParseResult Parse(const std::vector<std::uint8_t>& bytes) noexcept;

// Pure integrity-checked Int32 patcher. Guaranteed noexcept; re-parses before and after patching,
// verifying expected old value, PrimitiveType::Int32, bounds, and complete NRBF stream validity.
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
    std::string& error) noexcept;

} // namespace trainer::nrbf
