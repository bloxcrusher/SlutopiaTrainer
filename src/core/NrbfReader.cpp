#include "NrbfReader.hpp"
#include <cstring>
#include <limits>
#include <new>
#include <unordered_set>

namespace trainer::nrbf {
namespace {

constexpr std::size_t kMaxBufferBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxStringBytes = 1 * 1024 * 1024;
constexpr std::size_t kMaxGraphMemoryBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxRecords = 262144;
constexpr std::size_t kMaxArrayElements = 262144;
constexpr std::size_t kMaxTotalElements = 262144;
constexpr std::size_t kMaxMembers = 4096;
constexpr std::size_t kMaxDepth = 64;
constexpr std::size_t kMaxRank = 32;

enum class RecordType : std::uint8_t {
    SerializedStreamHeader = 0,
    ClassWithId = 1,
    SystemClassWithMembers = 2,
    ClassWithMembers = 3,
    SystemClassWithMembersAndTypes = 4,
    ClassWithMembersAndTypes = 5,
    BinaryObjectString = 6,
    BinaryArray = 7,
    MemberPrimitiveTyped = 8,
    MemberReference = 9,
    ObjectNull = 10,
    MessageEnd = 11,
    BinaryLibrary = 12,
    ObjectNullMultiple256 = 13,
    ObjectNullMultiple = 14,
    ArraySinglePrimitive = 15,
    ArraySingleObject = 16,
    ArraySingleString = 17
};

struct MemberTypeDesc {
    std::string name;
    BinaryType binaryType = BinaryType::Object;
    PrimitiveType primitiveType = PrimitiveType::Null;
    std::string className;
    std::int32_t libraryId = 0;
};

struct ClassMetadata {
    std::int32_t metadataId = 0;
    std::int32_t libraryId = 0;
    std::string className;
    std::vector<MemberTypeDesc> members;
};

struct SlotConstraint {
    BinaryType binaryType = BinaryType::Object;
    PrimitiveType primitiveType = PrimitiveType::Null;
    std::string className;
    bool allowNullRuns = false;
};

struct PendingRef {
    std::int32_t targetId = 0;
    SlotConstraint constraint;
};

bool IsValidPrimitiveType(PrimitiveType pt) noexcept {
    switch (pt) {
    case PrimitiveType::Boolean:
    case PrimitiveType::Byte:
    case PrimitiveType::Char:
    case PrimitiveType::Decimal:
    case PrimitiveType::Double:
    case PrimitiveType::Int16:
    case PrimitiveType::Int32:
    case PrimitiveType::Int64:
    case PrimitiveType::SByte:
    case PrimitiveType::Single:
    case PrimitiveType::TimeSpan:
    case PrimitiveType::DateTime:
    case PrimitiveType::UInt16:
    case PrimitiveType::UInt32:
    case PrimitiveType::UInt64:
        return true;
    default:
        return false;
    }
}

bool IsArrayClassName(std::string_view name) noexcept {
    if (name == "System.Array") return true;
    if (name.size() < 2 || name.back() != ']') return false;
    std::size_t i = name.size() - 1;
    while (i > 0 && name[i - 1] == ',') --i;
    return i > 0 && name[i - 1] == '[';
}

bool IsJaggedCompatibleElementType(BinaryType bt, std::string_view clsName) noexcept {
    switch (bt) {
    case BinaryType::Object:
    case BinaryType::ObjectArray:
    case BinaryType::StringArray:
    case BinaryType::PrimitiveArray:
        return true;
    case BinaryType::SystemClass:
    case BinaryType::Class:
        return IsArrayClassName(clsName);
    default:
        return false;
    }
}

bool DecodeUtf8CodePoint(const std::uint8_t* data, std::size_t avail, std::size_t& consumed, std::uint32_t& codePoint) noexcept {
    if (avail == 0 || data == nullptr) return false;
    const std::uint8_t b0 = data[0];
    if (b0 < 0x80u) {
        consumed = 1;
        codePoint = b0;
        return true;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
        if (avail < 2) return false;
        const std::uint8_t b1 = data[1];
        if ((b1 & 0xC0u) != 0x80u) return false;
        const std::uint32_t cp = (static_cast<std::uint32_t>(b0 & 0x1Fu) << 6) | static_cast<std::uint32_t>(b1 & 0x3Fu);
        if (cp < 0x80u) return false; // overlong
        consumed = 2;
        codePoint = cp;
        return true;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
        if (avail < 3) return false;
        const std::uint8_t b1 = data[1];
        const std::uint8_t b2 = data[2];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) return false;
        const std::uint32_t cp =
            (static_cast<std::uint32_t>(b0 & 0x0Fu) << 12) |
            (static_cast<std::uint32_t>(b1 & 0x3Fu) << 6) |
            static_cast<std::uint32_t>(b2 & 0x3Fu);
        if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) return false; // overlong or UTF-16 surrogate
        consumed = 3;
        codePoint = cp;
        return true;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
        if (avail < 4) return false;
        const std::uint8_t b1 = data[1];
        const std::uint8_t b2 = data[2];
        const std::uint8_t b3 = data[3];
        if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u || (b3 & 0xC0u) != 0x80u) return false;
        const std::uint32_t cp =
            (static_cast<std::uint32_t>(b0 & 0x07u) << 18) |
            (static_cast<std::uint32_t>(b1 & 0x3Fu) << 12) |
            (static_cast<std::uint32_t>(b2 & 0x3Fu) << 6) |
            static_cast<std::uint32_t>(b3 & 0x3Fu);
        if (cp < 0x10000u || cp > 0x10FFFFu) return false;
        consumed = 4;
        codePoint = cp;
        return true;
    }
    return false;
}

bool ValidateUtf8(const std::uint8_t* data, std::size_t len) noexcept {
    std::size_t i = 0;
    while (i < len) {
        std::size_t consumed = 0;
        std::uint32_t cp = 0;
        if (!DecodeUtf8CodePoint(data + i, len - i, consumed, cp)) return false;
        i += consumed;
    }
    return true;
}

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    ParseResult Run() {
        ParseResult res;
        if (bytes_.empty() || bytes_.size() > kMaxBufferBytes) {
            res.error = "NRBF buffer is empty or exceeds 16 MiB limit";
            return res;
        }

        std::uint8_t firstType = 0;
        if (!ReadU8(firstType) || firstType != static_cast<std::uint8_t>(RecordType::SerializedStreamHeader)) {
            res.error = "Missing NRBF SerializedStreamHeader";
            return res;
        }
        std::int32_t major = 0, minor = 0;
        if (!ReadI32(doc_.rootId) || !ReadI32(doc_.headerId) || !ReadI32(major) || !ReadI32(minor)) {
            res.error = "Truncated NRBF SerializedStreamHeader";
            return res;
        }
        if (major != 1 || minor != 0) {
            res.error = "Unsupported NRBF version";
            return res;
        }
        ++doc_.recordCount;

        bool sawMessageEnd = false;
        while (pos_ < bytes_.size()) {
            if (!CountRecord()) break;
            std::uint8_t rawType = 0;
            if (!ReadU8(rawType)) break;
            const auto recType = static_cast<RecordType>(rawType);
            if (recType == RecordType::MessageEnd) {
                sawMessageEnd = true;
                break;
            }
            if (!ReadTopLevelRecord(recType, 0)) break;
        }

        if (!error_.empty()) {
            res.error = std::move(error_);
            return res;
        }
        if (!sawMessageEnd) {
            res.error = "NRBF stream terminated without MessageEnd";
            return res;
        }
        if (pos_ != bytes_.size()) {
            res.error = "Unexpected trailing bytes after NRBF MessageEnd";
            return res;
        }
        if (!ValidateGraph()) {
            res.error = std::move(error_);
            return res;
        }

        doc_.bytesConsumed = pos_;
        res.ok = true;
        res.document = std::move(doc_);
        return res;
    }

private:
    bool Fail(std::string msg) {
        if (error_.empty()) error_ = std::move(msg);
        return false;
    }

    bool ChargeMemory(std::size_t bytes) {
        if (bytes > kMaxGraphMemoryBytes - graphMemoryBytes_) {
            return Fail("NRBF parsed graph exceeds 16 MiB memory budget");
        }
        graphMemoryBytes_ += bytes;
        return true;
    }

    bool ChargeElements(std::size_t count) {
        if (count > kMaxTotalElements - totalElements_) {
            return Fail("NRBF cumulative element count exceeds safety budget");
        }
        totalElements_ += count;
        return true;
    }

    bool CountRecord() {
        if (++doc_.recordCount > kMaxRecords) return Fail("NRBF record count exceeds safety limit");
        return true;
    }

    bool CanRead(std::size_t n) const noexcept {
        return pos_ <= bytes_.size() && n <= bytes_.size() - pos_;
    }

    bool ReadU8(std::uint8_t& out) {
        if (!CanRead(1)) return Fail("Unexpected end of NRBF stream (uint8)");
        out = bytes_[pos_++];
        return true;
    }

    template <typename T>
    bool ReadPod(T& out) {
        if (!CanRead(sizeof(T))) return Fail("Unexpected end of NRBF stream (POD)");
        std::memcpy(&out, bytes_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    bool ReadI32(std::int32_t& out) { return ReadPod(out); }

    bool RegisterObjectId(std::int32_t id) {
        if (id == 0) return Fail("NRBF object/string/array ID 0 is invalid");
        if (!usedObjectIds_.insert(id).second) {
            return Fail("Duplicate NRBF graph ID: " + std::to_string(id));
        }
        return true;
    }

    bool ReadLengthPrefixedString(std::string& out) {
        std::uint32_t len = 0;
        std::uint32_t shift = 0;
        for (int i = 0; i < 5; ++i) {
            std::uint8_t b = 0;
            if (!ReadU8(b)) return false;
            if (i == 4 && (b & 0xF0u) != 0) return Fail("Invalid 7-bit string length overflow");
            len |= static_cast<std::uint32_t>(b & 0x7Fu) << shift;
            if ((b & 0x80u) == 0) break;
            shift += 7;
            if (i == 4) return Fail("Unterminated 7-bit string length");
        }
        if (len > kMaxStringBytes) return Fail("NRBF string exceeds maximum allowed length");
        if (len == 0) {
            out.clear();
            return true;
        }
        if (!CanRead(len)) return Fail("Truncated NRBF string");
        if (!ChargeMemory(len)) return false;
        const std::uint8_t* ptr = bytes_.data() + pos_;
        if (!ValidateUtf8(ptr, len)) return Fail("Invalid UTF-8 sequence in NRBF string");
        out.assign(reinterpret_cast<const char*>(ptr), len);
        pos_ += len;
        return true;
    }

    bool ReadUtf8Char(std::int64_t& out) {
        if (pos_ >= bytes_.size()) return Fail("Unexpected end of NRBF stream (Char)");
        std::size_t consumed = 0;
        std::uint32_t cp = 0;
        if (!DecodeUtf8CodePoint(bytes_.data() + pos_, bytes_.size() - pos_, consumed, cp)) {
            return Fail("Invalid UTF-8 Char sequence in NRBF");
        }
        if (cp > 0xFFFFu) {
            return Fail("NRBF Char code point exceeds 16-bit BMP range");
        }
        pos_ += consumed;
        out = static_cast<std::int64_t>(cp);
        return true;
    }

    bool ReadPrimitive(PrimitiveType pt, FieldValue& out) {
        out.primitiveType = pt;
        out.byteOffset = pos_;
        switch (pt) {
        case PrimitiveType::Boolean: {
            std::uint8_t v = 0;
            if (!ReadU8(v)) return false;
            if (v > 1) return Fail("Invalid NRBF Boolean byte value (must be 0 or 1)");
            out.byteSize = 1;
            out.value = (v != 0);
            return true;
        }
        case PrimitiveType::Byte: {
            std::uint8_t v = 0;
            if (!ReadU8(v)) return false;
            out.byteSize = 1;
            out.value = static_cast<std::uint64_t>(v);
            return true;
        }
        case PrimitiveType::SByte: {
            std::int8_t v = 0;
            if (!ReadPod(v)) return false;
            out.byteSize = 1;
            out.value = static_cast<std::int64_t>(v);
            return true;
        }
        case PrimitiveType::Char: {
            std::int64_t v = 0;
            if (!ReadUtf8Char(v)) return false;
            out.byteSize = pos_ - out.byteOffset;
            out.value = v;
            return true;
        }
        case PrimitiveType::Int16: {
            std::int16_t v = 0;
            if (!ReadPod(v)) return false;
            out.byteSize = 2;
            out.value = static_cast<std::int64_t>(v);
            return true;
        }
        case PrimitiveType::UInt16: {
            std::uint16_t v = 0;
            if (!ReadPod(v)) return false;
            out.byteSize = 2;
            out.value = static_cast<std::uint64_t>(v);
            return true;
        }
        case PrimitiveType::Int32: {
            std::int32_t v = 0;
            if (!ReadPod(v)) return false;
            out.byteSize = 4;
            out.value = static_cast<std::int64_t>(v);
            return true;
        }
        case PrimitiveType::UInt32: {
            std::uint32_t v = 0;
            if (!ReadPod(v)) return false;
            out.byteSize = 4;
            out.value = static_cast<std::uint64_t>(v);
            return true;
        }
        case PrimitiveType::Int64:
        case PrimitiveType::TimeSpan: {
            std::int64_t v = 0;
            if (!ReadPod(v)) return false;
            out.byteSize = 8;
            out.value = v;
            return true;
        }
        case PrimitiveType::UInt64:
        case PrimitiveType::DateTime: {
            std::uint64_t v = 0;
            if (!ReadPod(v)) return false;
            out.byteSize = 8;
            out.value = v;
            return true;
        }
        case PrimitiveType::Single: {
            float v = 0.0f;
            if (!ReadPod(v)) return false;
            out.byteSize = 4;
            out.value = static_cast<double>(v);
            return true;
        }
        case PrimitiveType::Double: {
            double v = 0.0;
            if (!ReadPod(v)) return false;
            out.byteSize = 8;
            out.value = v;
            return true;
        }
        case PrimitiveType::Decimal: {
            std::string s;
            if (!ReadLengthPrefixedString(s)) return false;
            out.byteSize = pos_ - out.byteOffset;
            out.value = std::move(s);
            return true;
        }
        default:
            return Fail("Unsupported NRBF PrimitiveTypeEnumeration: " + std::to_string(static_cast<int>(pt)));
        }
    }

    bool ReadPrimitiveArrayElements(PrimitiveType pt, std::size_t count, ArrayRecord& arr) {
        if (!IsValidPrimitiveType(pt)) {
            return Fail("Invalid PrimitiveType in primitive array: " + std::to_string(static_cast<int>(pt)));
        }
        if (!ChargeElements(count)) return false;
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(ArrayElement)) {
            return Fail("ArrayElement byte allocation overflow");
        }
        if (!ChargeMemory(count * sizeof(ArrayElement))) return false;

        arr.elements.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            FieldValue fv;
            if (!ReadPrimitive(pt, fv)) return false;
            arr.elements.push_back(ArrayElement{fv.primitiveType, fv.byteOffset, fv.byteSize, std::move(fv.value)});
        }
        return true;
    }

    bool ReadBinaryLibrary() {
        std::int32_t libId = 0;
        std::string libName;
        if (!ReadI32(libId) || !ReadLengthPrefixedString(libName)) return false;
        if (libId <= 0) return Fail("NRBF BinaryLibrary ID must be positive");
        if (!ChargeMemory(sizeof(std::string) + 48)) return false;
        if (!doc_.libraries.emplace(libId, std::move(libName)).second) {
            return Fail("Duplicate NRBF BinaryLibrary ID: " + std::to_string(libId));
        }
        return true;
    }

    bool ReadBinaryObjectString(std::int32_t& outObjId, std::string& outStr) {
        if (!ReadI32(outObjId) || !RegisterObjectId(outObjId) || !ReadLengthPrefixedString(outStr)) return false;
        if (!ChargeMemory(sizeof(std::string) + 48)) return false;
        doc_.strings.emplace(outObjId, outStr);
        return true;
    }

    bool RecordUsedLibrary(std::int32_t libId) {
        if (libId <= 0) return Fail("Referenced NRBF libraryId must be positive");
        if (referencedLibraryIds_.insert(libId).second) {
            if (!ChargeMemory(16)) return false;
        }
        return true;
    }

    bool CheckArrayCompatibility(const ArrayRecord& arr, const SlotConstraint& constraint) {
        switch (constraint.binaryType) {
        case BinaryType::Object:
            return true;
        case BinaryType::ObjectArray:
            return arr.elementBinaryType != BinaryType::Primitive;
        case BinaryType::StringArray:
            return arr.elementBinaryType == BinaryType::String;
        case BinaryType::PrimitiveArray:
            return arr.elementBinaryType == BinaryType::Primitive &&
                   arr.elementPrimitiveType == constraint.primitiveType;
        case BinaryType::SystemClass:
        case BinaryType::Class:
            return IsArrayClassName(constraint.className);
        default:
            return false;
        }
    }

    bool CheckRefCompatibility(std::int32_t refId, const SlotConstraint& constraint) {
        if (refId == 0 || !usedObjectIds_.contains(refId)) {
            return Fail("Unresolved NRBF MemberReference target ID: " + std::to_string(refId));
        }
        if (doc_.strings.contains(refId)) {
            return constraint.binaryType == BinaryType::String || constraint.binaryType == BinaryType::Object;
        }
        if (const auto it = doc_.arrays.find(refId); it != doc_.arrays.end()) {
            return CheckArrayCompatibility(it->second, constraint);
        }
        if (doc_.objects.contains(refId)) {
            if (constraint.binaryType == BinaryType::Object) return true;
            if (constraint.binaryType == BinaryType::SystemClass || constraint.binaryType == BinaryType::Class) {
                return !IsArrayClassName(constraint.className);
            }
            return false;
        }
        return false;
    }

    bool ReadClassWithMembersAndTypes(bool hasLibrary, std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        ClassMetadata meta;
        std::int32_t memberCount = 0;
        if (!ReadI32(meta.metadataId) || !ReadLengthPrefixedString(meta.className) || !ReadI32(memberCount)) {
            return false;
        }
        if (meta.metadataId == 0) return Fail("NRBF class metadataId 0 is invalid");
        if (metadata_.contains(meta.metadataId)) {
            return Fail("Duplicate NRBF metadataId: " + std::to_string(meta.metadataId));
        }
        if (memberCount < 0 || static_cast<std::size_t>(memberCount) > kMaxMembers) {
            return Fail("NRBF class member count out of bounds");
        }
        const auto mCount = static_cast<std::size_t>(memberCount);
        if (!ChargeMemory(sizeof(ClassMetadata) + mCount * (sizeof(MemberTypeDesc) + 48))) return false;
        meta.members.resize(mCount);

        std::unordered_set<std::string> seenMembers;
        seenMembers.reserve(mCount);
        for (auto& m : meta.members) {
            if (!ReadLengthPrefixedString(m.name)) return false;
            if (!seenMembers.insert(m.name).second) {
                return Fail("Duplicate member name '" + m.name + "' in NRBF class '" + meta.className + "'");
            }
        }
        for (auto& m : meta.members) {
            std::uint8_t bt = 0;
            if (!ReadU8(bt)) return false;
            if (bt > static_cast<std::uint8_t>(BinaryType::PrimitiveArray)) {
                return Fail("Invalid NRBF BinaryTypeEnumeration");
            }
            m.binaryType = static_cast<BinaryType>(bt);
        }
        for (auto& m : meta.members) {
            if (!ReadAdditionalTypeInfo(m.binaryType, m.primitiveType, m.className, m.libraryId)) return false;
        }
        if (hasLibrary) {
            if (!ReadI32(meta.libraryId) || !RecordUsedLibrary(meta.libraryId)) return false;
        }

        const auto metaId = meta.metadataId;
        metadata_.emplace(metaId, std::move(meta));
        outObjId = metaId;
        return ReadClassMemberValues(metaId, metaId, depth + 1);
    }

    bool ReadClassWithId(std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        std::int32_t objId = 0, metaId = 0;
        if (!ReadI32(objId) || !ReadI32(metaId)) return false;
        outObjId = objId;
        return ReadClassMemberValues(objId, metaId, depth + 1);
    }

    bool ReadAdditionalTypeInfo(BinaryType bt, PrimitiveType& outPt, std::string& outCls, std::int32_t& outLibId) {
        switch (bt) {
        case BinaryType::Primitive:
        case BinaryType::PrimitiveArray: {
            std::uint8_t pt = 0;
            if (!ReadU8(pt)) return false;
            outPt = static_cast<PrimitiveType>(pt);
            if (!IsValidPrimitiveType(outPt)) {
                return Fail("Invalid PrimitiveType in AdditionalTypeInfo: " + std::to_string(pt));
            }
            return true;
        }
        case BinaryType::SystemClass:
            return ReadLengthPrefixedString(outCls);
        case BinaryType::Class:
            return ReadLengthPrefixedString(outCls) && ReadI32(outLibId) && RecordUsedLibrary(outLibId);
        case BinaryType::String:
        case BinaryType::Object:
        case BinaryType::ObjectArray:
        case BinaryType::StringArray:
            return true;
        default:
            return Fail("Invalid BinaryType in AdditionalTypeInfo");
        }
    }

    bool ReadClassMemberValues(std::int32_t objId, std::int32_t metaId, std::size_t depth) {
        if (!RegisterObjectId(objId)) return false;
        const auto it = metadata_.find(metaId);
        if (it == metadata_.end()) return Fail("NRBF ClassWithId references unknown metadataId");
        const ClassMetadata meta = it->second;

        std::size_t copiedNamesBytes = meta.className.size();
        for (const auto& m : meta.members) copiedNamesBytes += m.name.size();
        if (!ChargeMemory(sizeof(ObjectRecord) + copiedNamesBytes + meta.members.size() * (sizeof(FieldValue) + 16))) {
            return false;
        }

        ObjectRecord obj;
        obj.objectId = objId;
        obj.metadataId = metaId;
        obj.libraryId = meta.libraryId;
        obj.className = meta.className;
        obj.fields.reserve(meta.members.size());

        for (const auto& m : meta.members) {
            FieldValue fv;
            fv.name = m.name;
            if (m.binaryType == BinaryType::Primitive) {
                if (!ReadPrimitive(m.primitiveType, fv)) return false;
            } else {
                const SlotConstraint constraint{m.binaryType, m.primitiveType, m.className, false};
                std::size_t nullRun = 0;
                if (!ReadMemberRecord(fv, depth, constraint, nullRun)) return false;
                if (nullRun != 1) return Fail("Null-run record is invalid in a single class member");
            }
            obj.fields.push_back(std::move(fv));
        }

        doc_.objects.emplace(objId, std::move(obj));
        return true;
    }

    bool ReadMemberRecord(FieldValue& out, std::size_t depth, const SlotConstraint& constraint, std::size_t& nullRun) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        nullRun = 1;
        while (true) {
            if (!CountRecord()) return false;
            std::uint8_t rawType = 0;
            if (!ReadU8(rawType)) return false;
            const auto recType = static_cast<RecordType>(rawType);
            if (recType == RecordType::BinaryLibrary) {
                if (!ReadBinaryLibrary()) return false;
                continue;
            }
            out.byteOffset = pos_;
            switch (recType) {
            case RecordType::ObjectNull:
                out.value = NullValue{};
                return true;
            case RecordType::ObjectNullMultiple256: {
                if (!constraint.allowNullRuns) {
                    return Fail("ObjectNullMultiple256 is not valid in a non-array slot");
                }
                std::uint8_t c = 0;
                if (!ReadU8(c) || c == 0) return Fail("Invalid zero-count ObjectNullMultiple256");
                nullRun = c;
                out.value = NullValue{};
                return true;
            }
            case RecordType::ObjectNullMultiple: {
                if (!constraint.allowNullRuns) {
                    return Fail("ObjectNullMultiple is not valid in a non-array slot");
                }
                std::int32_t c = 0;
                if (!ReadI32(c) || c <= 0 || static_cast<std::size_t>(c) > kMaxArrayElements) {
                    return Fail("Invalid ObjectNullMultiple count");
                }
                nullRun = static_cast<std::size_t>(c);
                out.value = NullValue{};
                return true;
            }
            case RecordType::MemberReference: {
                std::int32_t idRef = 0;
                if (!ReadI32(idRef) || idRef == 0) return Fail("Invalid NRBF MemberReference target ID");
                if (!ChargeMemory(sizeof(PendingRef) + constraint.className.size())) return false;
                pendingRefs_.push_back(PendingRef{idRef, constraint});
                out.byteSize = 4;
                out.value = ObjectRef{idRef};
                return true;
            }
            case RecordType::BinaryObjectString: {
                if (constraint.binaryType != BinaryType::String && constraint.binaryType != BinaryType::Object) {
                    return Fail("BinaryObjectString is incompatible with declared slot BinaryType");
                }
                std::int32_t strId = 0;
                std::string s;
                if (!ReadBinaryObjectString(strId, s)) return false;
                out.value = ObjectRef{strId};
                return true;
            }
            case RecordType::MemberPrimitiveTyped: {
                if (constraint.binaryType != BinaryType::Object) {
                    return Fail("MemberPrimitiveTyped is only valid in an Object slot");
                }
                std::uint8_t pt = 0;
                if (!ReadU8(pt)) return false;
                return ReadPrimitive(static_cast<PrimitiveType>(pt), out);
            }
            case RecordType::ClassWithId: {
                if (constraint.binaryType != BinaryType::Object &&
                    constraint.binaryType != BinaryType::SystemClass &&
                    constraint.binaryType != BinaryType::Class) {
                    return Fail("ClassWithId is incompatible with declared slot BinaryType");
                }
                if ((constraint.binaryType == BinaryType::SystemClass || constraint.binaryType == BinaryType::Class) &&
                    IsArrayClassName(constraint.className)) {
                    return Fail("ClassWithId cannot populate an array-typed class member");
                }
                std::int32_t nestedId = 0;
                if (!ReadClassWithId(depth + 1, nestedId)) return false;
                out.value = ObjectRef{nestedId};
                return true;
            }
            case RecordType::SystemClassWithMembersAndTypes: {
                if (constraint.binaryType != BinaryType::Object &&
                    constraint.binaryType != BinaryType::SystemClass &&
                    constraint.binaryType != BinaryType::Class) {
                    return Fail("SystemClassWithMembersAndTypes is incompatible with declared slot BinaryType");
                }
                if ((constraint.binaryType == BinaryType::SystemClass || constraint.binaryType == BinaryType::Class) &&
                    IsArrayClassName(constraint.className)) {
                    return Fail("SystemClassWithMembersAndTypes cannot populate an array-typed class member");
                }
                std::int32_t nestedId = 0;
                if (!ReadClassWithMembersAndTypes(false, depth + 1, nestedId)) return false;
                out.value = ObjectRef{nestedId};
                return true;
            }
            case RecordType::ClassWithMembersAndTypes: {
                if (constraint.binaryType != BinaryType::Object &&
                    constraint.binaryType != BinaryType::SystemClass &&
                    constraint.binaryType != BinaryType::Class) {
                    return Fail("ClassWithMembersAndTypes is incompatible with declared slot BinaryType");
                }
                if ((constraint.binaryType == BinaryType::SystemClass || constraint.binaryType == BinaryType::Class) &&
                    IsArrayClassName(constraint.className)) {
                    return Fail("ClassWithMembersAndTypes cannot populate an array-typed class member");
                }
                std::int32_t nestedId = 0;
                if (!ReadClassWithMembersAndTypes(true, depth + 1, nestedId)) return false;
                out.value = ObjectRef{nestedId};
                return true;
            }
            case RecordType::BinaryArray: {
                std::int32_t arrId = 0;
                if (!ReadBinaryArray(depth + 1, arrId)) return false;
                if (!CheckArrayCompatibility(doc_.arrays.at(arrId), constraint)) {
                    return Fail("BinaryArray is incompatible with declared slot type");
                }
                out.value = ObjectRef{arrId};
                return true;
            }
            case RecordType::ArraySinglePrimitive: {
                std::int32_t arrId = 0;
                if (!ReadArraySinglePrimitive(arrId)) return false;
                if (!CheckArrayCompatibility(doc_.arrays.at(arrId), constraint)) {
                    return Fail("ArraySinglePrimitive is incompatible with declared slot type");
                }
                out.value = ObjectRef{arrId};
                return true;
            }
            case RecordType::ArraySingleObject: {
                std::int32_t arrId = 0;
                if (!ReadArraySingleObject(depth + 1, arrId)) return false;
                if (!CheckArrayCompatibility(doc_.arrays.at(arrId), constraint)) {
                    return Fail("ArraySingleObject is incompatible with declared slot type");
                }
                out.value = ObjectRef{arrId};
                return true;
            }
            case RecordType::ArraySingleString: {
                std::int32_t arrId = 0;
                if (!ReadArraySingleString(depth + 1, arrId)) return false;
                if (!CheckArrayCompatibility(doc_.arrays.at(arrId), constraint)) {
                    return Fail("ArraySingleString is incompatible with declared slot type");
                }
                out.value = ObjectRef{arrId};
                return true;
            }
            default:
                return Fail("Unexpected NRBF record type in member slot: " + std::to_string(static_cast<int>(recType)));
            }
        }
    }

    bool ReadArrayElements(std::size_t totalElements, std::size_t depth, const SlotConstraint& elemConstraint, ArrayRecord& arr) {
        if (!ChargeElements(totalElements)) return false;
        if (totalElements > std::numeric_limits<std::size_t>::max() / sizeof(ArrayElement)) {
            return Fail("ArrayElement allocation overflow");
        }
        if (!ChargeMemory(totalElements * sizeof(ArrayElement))) return false;

        arr.elements.reserve(totalElements);
        std::size_t idx = 0;
        while (idx < totalElements) {
            FieldValue elem;
            std::size_t nullRun = 0;
            if (!ReadMemberRecord(elem, depth + 1, elemConstraint, nullRun)) return false;
            if (nullRun == 0 || nullRun > totalElements - idx) {
                return Fail("ObjectNullMultiple exceeds remaining array elements");
            }
            if (std::holds_alternative<NullValue>(elem.value)) {
                for (std::size_t k = 0; k < nullRun; ++k) {
                    arr.elements.push_back(ArrayElement{PrimitiveType::Null, elem.byteOffset, 0, NullValue{}});
                }
            } else {
                arr.elements.push_back(ArrayElement{elem.primitiveType, elem.byteOffset, elem.byteSize, std::move(elem.value)});
            }
            idx += nullRun;
        }
        return true;
    }

    bool ReadBinaryArray(std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        std::int32_t objId = 0, rank = 0;
        std::uint8_t rawArrayType = 0;
        if (!ReadI32(objId) || !RegisterObjectId(objId) || !ReadU8(rawArrayType) || !ReadI32(rank)) return false;
        if (rawArrayType > static_cast<std::uint8_t>(BinaryArrayType::RectangularOffset)) {
            return Fail("Invalid NRBF BinaryArrayTypeEnumeration");
        }
        const auto arrayType = static_cast<BinaryArrayType>(rawArrayType);
        const bool isSingle = (arrayType == BinaryArrayType::Single || arrayType == BinaryArrayType::SingleOffset);
        const bool isJagged = (arrayType == BinaryArrayType::Jagged || arrayType == BinaryArrayType::JaggedOffset);
        const bool isRectangular = (arrayType == BinaryArrayType::Rectangular || arrayType == BinaryArrayType::RectangularOffset);
        const bool hasOffsets = (arrayType == BinaryArrayType::SingleOffset ||
                                 arrayType == BinaryArrayType::JaggedOffset ||
                                 arrayType == BinaryArrayType::RectangularOffset);

        if ((isSingle || isJagged) && rank != 1) {
            return Fail("NRBF Single/Jagged BinaryArray must have rank 1");
        }
        if (isRectangular && (rank < 2 || static_cast<std::size_t>(rank) > kMaxRank)) {
            return Fail("NRBF Rectangular BinaryArray rank out of range (2..32)");
        }

        const auto uRank = static_cast<std::size_t>(rank);
        if (!ChargeMemory(sizeof(ArrayRecord) + uRank * (hasOffsets ? 8u : 4u) + 32)) return false;

        ArrayRecord arr;
        arr.objectId = objId;
        arr.arrayType = arrayType;
        arr.lengths.resize(uRank);
        std::size_t total = 1;
        for (std::size_t i = 0; i < uRank; ++i) {
            std::int32_t len = 0;
            if (!ReadI32(len) || len < 0 || static_cast<std::size_t>(len) > kMaxArrayElements) {
                return Fail("Invalid NRBF BinaryArray dimension length");
            }
            arr.lengths[i] = len;
            const auto ulen = static_cast<std::size_t>(len);
            if (ulen != 0 && total > kMaxArrayElements / ulen) {
                return Fail("NRBF BinaryArray element count overflow");
            }
            total *= ulen;
        }
        if (total > kMaxArrayElements) return Fail("NRBF BinaryArray element count exceeds limit");

        if (hasOffsets) {
            arr.lowerBounds.resize(uRank);
            for (std::size_t i = 0; i < uRank; ++i) {
                std::int32_t lb = 0;
                if (!ReadI32(lb)) return false;
                const std::int64_t upperExclusive = static_cast<std::int64_t>(lb) + static_cast<std::int64_t>(arr.lengths[i]);
                if (upperExclusive > std::numeric_limits<std::int32_t>::max()) {
                    return Fail("NRBF BinaryArray lowerBound + length overflows Int32");
                }
                arr.lowerBounds[i] = lb;
            }
        }

        std::uint8_t rawBt = 0;
        if (!ReadU8(rawBt) || rawBt > static_cast<std::uint8_t>(BinaryType::PrimitiveArray)) {
            return Fail("Invalid BinaryArray element BinaryType");
        }
        const auto bt = static_cast<BinaryType>(rawBt);
        PrimitiveType pt = PrimitiveType::Null;
        std::string elemCls;
        std::int32_t elemLibId = 0;
        if (!ReadAdditionalTypeInfo(bt, pt, elemCls, elemLibId)) return false;

        if (isJagged && !IsJaggedCompatibleElementType(bt, elemCls)) {
            return Fail("NRBF Jagged BinaryArray requires array-compatible element BinaryType");
        }

        arr.elementBinaryType = bt;
        arr.elementPrimitiveType = pt;

        if (bt == BinaryType::Primitive) {
            if (!ReadPrimitiveArrayElements(pt, total, arr)) return false;
        } else {
            const SlotConstraint elemConstraint{bt, pt, elemCls, true};
            if (!ReadArrayElements(total, depth, elemConstraint, arr)) return false;
        }

        doc_.arrays.emplace(objId, std::move(arr));
        outObjId = objId;
        return true;
    }

    bool ReadArraySinglePrimitive(std::int32_t& outObjId) {
        std::int32_t objId = 0, len = 0;
        std::uint8_t rawPt = 0;
        if (!ReadI32(objId) || !RegisterObjectId(objId) || !ReadI32(len) || !ReadU8(rawPt)) return false;
        if (len < 0 || static_cast<std::size_t>(len) > kMaxArrayElements) {
            return Fail("Invalid ArraySinglePrimitive length");
        }
        const auto pt = static_cast<PrimitiveType>(rawPt);
        if (!IsValidPrimitiveType(pt)) {
            return Fail("Invalid ArraySinglePrimitive element type: " + std::to_string(rawPt));
        }
        if (!ChargeMemory(sizeof(ArrayRecord) + 36)) return false;

        ArrayRecord arr;
        arr.objectId = objId;
        arr.arrayType = BinaryArrayType::Single;
        arr.elementBinaryType = BinaryType::Primitive;
        arr.elementPrimitiveType = pt;
        arr.lengths.push_back(len);
        if (!ReadPrimitiveArrayElements(pt, static_cast<std::size_t>(len), arr)) return false;

        doc_.arrays.emplace(objId, std::move(arr));
        outObjId = objId;
        return true;
    }

    bool ReadArraySingleObject(std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        std::int32_t objId = 0, len = 0;
        if (!ReadI32(objId) || !RegisterObjectId(objId) || !ReadI32(len)) return false;
        if (len < 0 || static_cast<std::size_t>(len) > kMaxArrayElements) {
            return Fail("Invalid ArraySingleObject length");
        }
        if (!ChargeMemory(sizeof(ArrayRecord) + 36)) return false;

        ArrayRecord arr;
        arr.objectId = objId;
        arr.arrayType = BinaryArrayType::Single;
        arr.elementBinaryType = BinaryType::Object;
        arr.lengths.push_back(len);
        const SlotConstraint elemConstraint{BinaryType::Object, PrimitiveType::Null, {}, true};
        if (!ReadArrayElements(static_cast<std::size_t>(len), depth, elemConstraint, arr)) return false;

        doc_.arrays.emplace(objId, std::move(arr));
        outObjId = objId;
        return true;
    }

    bool ReadArraySingleString(std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        std::int32_t objId = 0, len = 0;
        if (!ReadI32(objId) || !RegisterObjectId(objId) || !ReadI32(len)) return false;
        if (len < 0 || static_cast<std::size_t>(len) > kMaxArrayElements) {
            return Fail("Invalid ArraySingleString length");
        }
        if (!ChargeMemory(sizeof(ArrayRecord) + 36)) return false;

        ArrayRecord arr;
        arr.objectId = objId;
        arr.arrayType = BinaryArrayType::Single;
        arr.elementBinaryType = BinaryType::String;
        arr.lengths.push_back(len);
        const SlotConstraint elemConstraint{BinaryType::String, PrimitiveType::Null, {}, true};
        if (!ReadArrayElements(static_cast<std::size_t>(len), depth, elemConstraint, arr)) return false;

        doc_.arrays.emplace(objId, std::move(arr));
        outObjId = objId;
        return true;
    }

    bool ReadTopLevelRecord(RecordType recType, std::size_t depth) {
        std::int32_t id = 0;
        switch (recType) {
        case RecordType::BinaryLibrary:
            return ReadBinaryLibrary();
        case RecordType::BinaryObjectString: {
            std::string s;
            return ReadBinaryObjectString(id, s);
        }
        case RecordType::ClassWithId:
            return ReadClassWithId(depth, id);
        case RecordType::SystemClassWithMembersAndTypes:
            return ReadClassWithMembersAndTypes(false, depth, id);
        case RecordType::ClassWithMembersAndTypes:
            return ReadClassWithMembersAndTypes(true, depth, id);
        case RecordType::BinaryArray:
            return ReadBinaryArray(depth, id);
        case RecordType::ArraySinglePrimitive:
            return ReadArraySinglePrimitive(id);
        case RecordType::ArraySingleObject:
            return ReadArraySingleObject(depth, id);
        case RecordType::ArraySingleString:
            return ReadArraySingleString(depth, id);
        default:
            return Fail("Unsupported top-level NRBF record type: " + std::to_string(static_cast<int>(recType)));
        }
    }

    bool ValidateGraph() {
        if (doc_.rootId == 0 || !usedObjectIds_.contains(doc_.rootId)) {
            return Fail("NRBF rootId does not resolve to any parsed object, string, or array");
        }
        if (doc_.headerId != -1 && (doc_.headerId == 0 || !usedObjectIds_.contains(doc_.headerId))) {
            return Fail("NRBF headerId does not resolve to any parsed record");
        }
        for (const std::int32_t libId : referencedLibraryIds_) {
            if (!doc_.libraries.contains(libId)) {
                return Fail("NRBF references undefined BinaryLibrary ID: " + std::to_string(libId));
            }
        }
        for (const auto& pr : pendingRefs_) {
            if (!CheckRefCompatibility(pr.targetId, pr.constraint)) {
                return Fail("NRBF MemberReference target is missing or incompatible with declared type: " +
                            std::to_string(pr.targetId));
            }
        }
        return true;
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t pos_ = 0;
    std::size_t graphMemoryBytes_ = 0;
    std::size_t totalElements_ = 0;
    std::string error_;
    Document doc_;
    std::unordered_set<std::int32_t> usedObjectIds_;
    std::unordered_set<std::int32_t> referencedLibraryIds_;
    std::unordered_map<std::int32_t, ClassMetadata> metadata_;
    std::vector<PendingRef> pendingRefs_;
};

} // namespace

const FieldValue* ObjectRecord::FindField(std::string_view name) const {
    for (const auto& f : fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

std::optional<std::string> Document::ResolveString(const FieldValue& field) const {
    if (const auto* s = std::get_if<std::string>(&field.value)) return *s;
    if (const auto* ref = std::get_if<ObjectRef>(&field.value)) {
        const auto it = strings.find(ref->id);
        if (it != strings.end()) return it->second;
    }
    return std::nullopt;
}

const ObjectRecord* Document::ResolveObject(const FieldValue& field) const {
    if (const auto* ref = std::get_if<ObjectRef>(&field.value)) {
        const auto it = objects.find(ref->id);
        if (it != objects.end()) return &it->second;
    }
    return nullptr;
}

const ArrayRecord* Document::ResolveArray(const FieldValue& field) const {
    if (const auto* ref = std::get_if<ObjectRef>(&field.value)) {
        const auto it = arrays.find(ref->id);
        if (it != arrays.end()) return &it->second;
    }
    return nullptr;
}

ParseResult Parse(const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        Reader reader(bytes);
        return reader.Run();
    } catch (const std::bad_alloc&) {
        ParseResult fail;
        try { fail.error = "NRBF parser memory allocation limit reached"; } catch (...) {}
        return fail;
    } catch (const std::exception& ex) {
        ParseResult fail;
        try { fail.error = std::string("NRBF parser failure: ") + ex.what(); } catch (...) {}
        return fail;
    } catch (...) {
        ParseResult fail;
        try { fail.error = "NRBF parser unexpected failure"; } catch (...) {}
        return fail;
    }
}

std::optional<std::vector<std::uint8_t>> PatchInt32Verified(
    const std::vector<std::uint8_t>& originalBytes,
    const PatchRequest& request,
    std::string& error) noexcept {
    try {
        error.clear();
        if (request.minAllowed > request.maxAllowed) {
            error = "Invalid patch bounds (minAllowed > maxAllowed)";
            return std::nullopt;
        }
        if (request.newValue < request.minAllowed || request.newValue > request.maxAllowed) {
            error = "Requested value is outside allowed bounds";
            return std::nullopt;
        }
        if (originalBytes.size() < sizeof(std::int32_t) ||
            request.byteOffset > originalBytes.size() - sizeof(std::int32_t)) {
            error = "Patch offset is outside buffer bounds";
            return std::nullopt;
        }

        const auto before = Parse(originalBytes);
        if (!before.ok) {
            error = "Original NRBF stream failed validation: " + before.error;
            return std::nullopt;
        }

        bool matchedField = false;
        std::int32_t targetObjId = 0;
        std::string targetFieldName;
        for (const auto& [objId, obj] : before.document.objects) {
            for (const auto& field : obj.fields) {
                if (field.byteOffset == request.byteOffset) {
                    if (field.primitiveType != PrimitiveType::Int32 || field.byteSize != sizeof(std::int32_t)) {
                        error = "Target offset field is not PrimitiveType::Int32";
                        return std::nullopt;
                    }
                    const auto* iv = std::get_if<std::int64_t>(&field.value);
                    if (!iv || *iv != static_cast<std::int64_t>(request.expectedOldValue)) {
                        error = "Field value mismatch at target offset (concurrent modification or stale snapshot)";
                        return std::nullopt;
                    }
                    matchedField = true;
                    targetObjId = objId;
                    targetFieldName = field.name;
                    break;
                }
            }
            if (matchedField) break;
        }
        if (!matchedField) {
            error = "Target offset does not match any validated Int32 field in NRBF graph";
            return std::nullopt;
        }

        std::vector<std::uint8_t> patched = originalBytes;
        std::memcpy(patched.data() + request.byteOffset, &request.newValue, sizeof(request.newValue));

        const auto after = Parse(patched);
        if (!after.ok || after.document.bytesConsumed != patched.size() ||
            after.document.recordCount != before.document.recordCount) {
            error = "Patched NRBF stream failed post-write integrity validation";
            return std::nullopt;
        }

        const auto afterObjIt = after.document.objects.find(targetObjId);
        if (afterObjIt == after.document.objects.end()) {
            error = "Target object missing after patch re-parse";
            return std::nullopt;
        }
        const auto* afterField = afterObjIt->second.FindField(targetFieldName);
        if (!afterField || afterField->byteOffset != request.byteOffset ||
            afterField->primitiveType != PrimitiveType::Int32 || afterField->byteSize != sizeof(std::int32_t)) {
            error = "Target field type or layout changed after patch";
            return std::nullopt;
        }
        const auto* newIv = std::get_if<std::int64_t>(&afterField->value);
        if (!newIv || *newIv != static_cast<std::int64_t>(request.newValue)) {
            error = "Post-patch readback verification failed";
            return std::nullopt;
        }

        return patched;
    } catch (const std::bad_alloc&) {
        try { error = "Patch memory allocation failed"; } catch (...) {}
        return std::nullopt;
    } catch (const std::exception& ex) {
        try { error = std::string("Patch failure: ") + ex.what(); } catch (...) {}
        return std::nullopt;
    } catch (...) {
        try { error = "Patch unexpected failure"; } catch (...) {}
        return std::nullopt;
    }
}

} // namespace trainer::nrbf
