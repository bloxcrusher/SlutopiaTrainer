#include "NrbfReader.hpp"
#include <cstring>
#include <limits>

namespace trainer::nrbf {
namespace {

constexpr std::size_t kMaxBufferBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxStringBytes = 1 * 1024 * 1024;
constexpr std::size_t kMaxRecords = 262144;
constexpr std::size_t kMaxArrayElements = 262144;
constexpr std::size_t kMaxMembers = 4096;
constexpr std::size_t kMaxDepth = 64;

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

struct MemberTypeDesc {
    std::string name;
    BinaryType binaryType = BinaryType::Object;
    PrimitiveType primitiveType = PrimitiveType::Null;
};

struct ClassMetadata {
    std::int32_t metadataId = 0;
    std::int32_t libraryId = 0;
    std::string className;
    std::vector<MemberTypeDesc> members;
};

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
            res.error = error_;
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

    bool CountRecord() {
        if (++doc_.recordCount > kMaxRecords) return Fail("NRBF record count exceeds safety limit");
        return true;
    }

    bool CanRead(std::size_t n) const {
        return pos_ <= bytes_.size() && n <= bytes_.size() - pos_;
    }

    bool Skip(std::size_t n) {
        if (!CanRead(n)) return Fail("Unexpected end of NRBF stream");
        pos_ += n;
        return true;
    }

    bool ReadU8(std::uint8_t& out) {
        if (!CanRead(1)) return Fail("Unexpected end of NRBF stream (uint8)");
        out = bytes_[pos_++];
        return true;
    }

    template <typename T>
    bool ReadPod(T& out) {
        if (!CanRead(sizeof(T))) return Fail("Unexpected end of NRBF stream (POD)");
        std::memcpy(&out, &bytes_[pos_], sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    bool ReadI32(std::int32_t& out) { return ReadPod(out); }

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
        if (!CanRead(len)) return Fail("Truncated NRBF string");
        out.assign(reinterpret_cast<const char*>(&bytes_[pos_]), len);
        pos_ += len;
        return true;
    }

    bool ReadUtf8Char(std::int64_t& out) {
        std::uint8_t b0 = 0;
        if (!ReadU8(b0)) return false;
        std::size_t extra = 0;
        if ((b0 & 0x80u) == 0) extra = 0;
        else if ((b0 & 0xE0u) == 0xC0u) extra = 1;
        else if ((b0 & 0xF0u) == 0xE0u) extra = 2;
        else if ((b0 & 0xF8u) == 0xF0u) extra = 3;
        else return Fail("Invalid UTF-8 Char lead byte in NRBF");
        if (!Skip(extra)) return false;
        out = static_cast<std::int64_t>(b0);
        return true;
    }

    bool ReadPrimitive(PrimitiveType pt, FieldValue& out) {
        out.byteOffset = pos_;
        switch (pt) {
        case PrimitiveType::Boolean: {
            std::uint8_t v = 0;
            if (!ReadU8(v)) return false;
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
        case PrimitiveType::Decimal:
        case PrimitiveType::String: {
            std::string s;
            if (!ReadLengthPrefixedString(s)) return false;
            out.byteSize = pos_ - out.byteOffset;
            out.value = std::move(s);
            return true;
        }
        case PrimitiveType::Null:
            out.byteSize = 0;
            out.value = NullValue{};
            return true;
        default:
            return Fail("Unknown NRBF PrimitiveTypeEnumeration: " + std::to_string(static_cast<int>(pt)));
        }
    }

    bool SkipPrimitiveArrayElements(PrimitiveType pt, std::size_t count) {
        std::size_t elemSize = 0;
        switch (pt) {
        case PrimitiveType::Boolean:
        case PrimitiveType::Byte:
        case PrimitiveType::SByte: elemSize = 1; break;
        case PrimitiveType::Int16:
        case PrimitiveType::UInt16: elemSize = 2; break;
        case PrimitiveType::Int32:
        case PrimitiveType::UInt32:
        case PrimitiveType::Single: elemSize = 4; break;
        case PrimitiveType::Int64:
        case PrimitiveType::UInt64:
        case PrimitiveType::Double:
        case PrimitiveType::TimeSpan:
        case PrimitiveType::DateTime: elemSize = 8; break;
        case PrimitiveType::Char:
        case PrimitiveType::Decimal:
        case PrimitiveType::String: {
            for (std::size_t i = 0; i < count; ++i) {
                FieldValue dummy;
                if (!ReadPrimitive(pt, dummy)) return false;
            }
            return true;
        }
        default:
            return Fail("Unsupported primitive array element type");
        }
        if (count > bytes_.size() / elemSize) return Fail("Primitive array byte size overflow");
        return Skip(count * elemSize);
    }

    bool ReadBinaryLibrary() {
        std::int32_t libId = 0;
        std::string libName;
        if (!ReadI32(libId) || !ReadLengthPrefixedString(libName)) return false;
        doc_.libraries[libId] = std::move(libName);
        return true;
    }

    bool ReadBinaryObjectString(std::int32_t& outObjId, std::string& outStr) {
        if (!ReadI32(outObjId) || !ReadLengthPrefixedString(outStr)) return false;
        doc_.strings[outObjId] = outStr;
        return true;
    }

    bool ReadClassWithMembersAndTypes(bool hasLibrary, std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        ClassMetadata meta;
        std::int32_t memberCount = 0;
        if (!ReadI32(meta.metadataId) || !ReadLengthPrefixedString(meta.className) || !ReadI32(memberCount)) {
            return false;
        }
        if (memberCount < 0 || static_cast<std::size_t>(memberCount) > kMaxMembers) {
            return Fail("NRBF class member count out of bounds");
        }
        meta.members.resize(static_cast<std::size_t>(memberCount));
        for (auto& m : meta.members) {
            if (!ReadLengthPrefixedString(m.name)) return false;
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
            if (!ReadAdditionalTypeInfo(m.binaryType, m.primitiveType)) return false;
        }
        if (hasLibrary) {
            if (!ReadI32(meta.libraryId)) return false;
        }

        const auto metaId = meta.metadataId;
        metadata_[metaId] = meta;
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

    bool ReadAdditionalTypeInfo(BinaryType bt, PrimitiveType& outPt) {
        switch (bt) {
        case BinaryType::Primitive:
        case BinaryType::PrimitiveArray: {
            std::uint8_t pt = 0;
            if (!ReadU8(pt)) return false;
            outPt = static_cast<PrimitiveType>(pt);
            return true;
        }
        case BinaryType::SystemClass: {
            std::string cls;
            return ReadLengthPrefixedString(cls);
        }
        case BinaryType::Class: {
            std::string cls;
            std::int32_t libId = 0;
            return ReadLengthPrefixedString(cls) && ReadI32(libId);
        }
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
        const auto it = metadata_.find(metaId);
        if (it == metadata_.end()) return Fail("NRBF ClassWithId references unknown metadataId");
        // Copy metadata to avoid iterator invalidation if nested records register new metadata.
        const ClassMetadata meta = it->second;

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
                std::size_t nullRun = 0;
                if (!ReadMemberRecord(fv, depth, nullRun)) return false;
                if (nullRun > 1) return Fail("ObjectNullMultiple is not valid for a single class member");
            }
            obj.fields.push_back(std::move(fv));
        }

        doc_.objects[objId] = std::move(obj);
        return true;
    }

    bool ReadMemberRecord(FieldValue& out, std::size_t depth, std::size_t& nullRun) {
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
                std::uint8_t c = 0;
                if (!ReadU8(c)) return false;
                nullRun = c;
                out.value = NullValue{};
                return true;
            }
            case RecordType::ObjectNullMultiple: {
                std::int32_t c = 0;
                if (!ReadI32(c) || c < 0 || static_cast<std::size_t>(c) > kMaxArrayElements) {
                    return Fail("Invalid ObjectNullMultiple count");
                }
                nullRun = static_cast<std::size_t>(c);
                out.value = NullValue{};
                return true;
            }
            case RecordType::MemberReference: {
                std::int32_t idRef = 0;
                if (!ReadI32(idRef)) return false;
                out.byteSize = 4;
                out.value = ObjectRef{idRef};
                return true;
            }
            case RecordType::BinaryObjectString: {
                std::int32_t strId = 0;
                std::string s;
                if (!ReadBinaryObjectString(strId, s)) return false;
                out.value = ObjectRef{strId};
                return true;
            }
            case RecordType::MemberPrimitiveTyped: {
                std::uint8_t pt = 0;
                if (!ReadU8(pt)) return false;
                return ReadPrimitive(static_cast<PrimitiveType>(pt), out);
            }
            case RecordType::ClassWithId: {
                std::int32_t nestedId = 0;
                if (!ReadClassWithId(depth + 1, nestedId)) return false;
                out.value = ObjectRef{nestedId};
                return true;
            }
            case RecordType::SystemClassWithMembersAndTypes: {
                std::int32_t nestedId = 0;
                if (!ReadClassWithMembersAndTypes(false, depth + 1, nestedId)) return false;
                out.value = ObjectRef{nestedId};
                return true;
            }
            case RecordType::ClassWithMembersAndTypes: {
                std::int32_t nestedId = 0;
                if (!ReadClassWithMembersAndTypes(true, depth + 1, nestedId)) return false;
                out.value = ObjectRef{nestedId};
                return true;
            }
            case RecordType::BinaryArray: {
                std::int32_t arrId = 0;
                if (!ReadBinaryArray(depth + 1, arrId)) return false;
                out.value = ObjectRef{arrId};
                return true;
            }
            case RecordType::ArraySinglePrimitive: {
                std::int32_t arrId = 0;
                if (!ReadArraySinglePrimitive(arrId)) return false;
                out.value = ObjectRef{arrId};
                return true;
            }
            case RecordType::ArraySingleObject: {
                std::int32_t arrId = 0;
                if (!ReadArraySingleObject(depth + 1, arrId)) return false;
                out.value = ObjectRef{arrId};
                return true;
            }
            case RecordType::ArraySingleString: {
                std::int32_t arrId = 0;
                if (!ReadArraySingleString(depth + 1, arrId)) return false;
                out.value = ObjectRef{arrId};
                return true;
            }
            default:
                return Fail("Unexpected NRBF record type in member slot: " + std::to_string(static_cast<int>(recType)));
            }
        }
    }

    bool ReadArrayElements(std::size_t totalElements, std::size_t depth, ArrayRecord& arr) {
        arr.elements.reserve(totalElements);
        std::size_t idx = 0;
        while (idx < totalElements) {
            FieldValue elem;
            std::size_t nullRun = 0;
            if (!ReadMemberRecord(elem, depth + 1, nullRun)) return false;
            if (nullRun == 0) return Fail("Zero-length ObjectNullMultiple in array");
            if (nullRun > totalElements - idx) return Fail("ObjectNullMultiple exceeds remaining array elements");
            if (std::holds_alternative<NullValue>(elem.value)) {
                for (std::size_t k = 0; k < nullRun; ++k) arr.elements.emplace_back(NullValue{});
            } else if (const auto* ref = std::get_if<ObjectRef>(&elem.value)) {
                arr.elements.emplace_back(*ref);
            } else if (const auto* str = std::get_if<std::string>(&elem.value)) {
                arr.elements.emplace_back(*str);
            } else {
                arr.elements.emplace_back(NullValue{});
            }
            idx += nullRun;
        }
        return true;
    }

    bool ReadBinaryArray(std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        std::int32_t objId = 0, rank = 0;
        std::uint8_t arrayType = 0;
        if (!ReadI32(objId) || !ReadU8(arrayType) || !ReadI32(rank)) return false;
        if (arrayType > 5 || rank <= 0 || rank > 32) return Fail("Invalid NRBF BinaryArray type or rank");

        ArrayRecord arr;
        arr.objectId = objId;
        arr.lengths.resize(static_cast<std::size_t>(rank));
        std::size_t total = 1;
        for (std::int32_t i = 0; i < rank; ++i) {
            std::int32_t len = 0;
            if (!ReadI32(len) || len < 0) return Fail("Invalid NRBF BinaryArray length");
            arr.lengths[static_cast<std::size_t>(i)] = len;
            const auto ulen = static_cast<std::size_t>(len);
            if (ulen != 0 && total > kMaxArrayElements / ulen) return Fail("NRBF BinaryArray element count overflow");
            total *= ulen;
        }
        if (arrayType >= 3) {
            for (std::int32_t i = 0; i < rank; ++i) {
                std::int32_t lb = 0;
                if (!ReadI32(lb)) return false;
            }
        }

        std::uint8_t rawBt = 0;
        if (!ReadU8(rawBt) || rawBt > static_cast<std::uint8_t>(BinaryType::PrimitiveArray)) {
            return Fail("Invalid BinaryArray element BinaryType");
        }
        const auto bt = static_cast<BinaryType>(rawBt);
        PrimitiveType pt = PrimitiveType::Null;
        if (!ReadAdditionalTypeInfo(bt, pt)) return false;

        if (bt == BinaryType::Primitive) {
            if (!SkipPrimitiveArrayElements(pt, total)) return false;
        } else {
            if (!ReadArrayElements(total, depth, arr)) return false;
        }

        doc_.arrays[objId] = std::move(arr);
        outObjId = objId;
        return true;
    }

    bool ReadArraySinglePrimitive(std::int32_t& outObjId) {
        std::int32_t objId = 0, len = 0;
        std::uint8_t pt = 0;
        if (!ReadI32(objId) || !ReadI32(len) || !ReadU8(pt)) return false;
        if (len < 0 || static_cast<std::size_t>(len) > kMaxArrayElements) {
            return Fail("Invalid ArraySinglePrimitive length");
        }
        if (!SkipPrimitiveArrayElements(static_cast<PrimitiveType>(pt), static_cast<std::size_t>(len))) {
            return false;
        }
        ArrayRecord arr;
        arr.objectId = objId;
        arr.lengths.push_back(len);
        doc_.arrays[objId] = std::move(arr);
        outObjId = objId;
        return true;
    }

    bool ReadArraySingleObject(std::size_t depth, std::int32_t& outObjId) {
        if (depth >= kMaxDepth) return Fail("NRBF nesting depth exceeded");
        std::int32_t objId = 0, len = 0;
        if (!ReadI32(objId) || !ReadI32(len)) return false;
        if (len < 0 || static_cast<std::size_t>(len) > kMaxArrayElements) {
            return Fail("Invalid ArraySingleObject length");
        }
        ArrayRecord arr;
        arr.objectId = objId;
        arr.lengths.push_back(len);
        if (!ReadArrayElements(static_cast<std::size_t>(len), depth, arr)) return false;
        doc_.arrays[objId] = std::move(arr);
        outObjId = objId;
        return true;
    }

    bool ReadArraySingleString(std::size_t depth, std::int32_t& outObjId) {
        return ReadArraySingleObject(depth, outObjId);
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
        case RecordType::ObjectNull:
            return true;
        default:
            return Fail("Unsupported top-level NRBF record type: " + std::to_string(static_cast<int>(recType)));
        }
    }

    const std::vector<std::uint8_t>& bytes_;
    std::size_t pos_ = 0;
    std::string error_;
    Document doc_;
    std::unordered_map<std::int32_t, ClassMetadata> metadata_;
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

ParseResult Parse(const std::vector<std::uint8_t>& bytes) {
    Reader reader(bytes);
    return reader.Run();
}

std::optional<std::vector<std::uint8_t>> PatchInt32Verified(
    const std::vector<std::uint8_t>& originalBytes,
    const PatchRequest& request,
    std::string& error) {
    error.clear();
    if (request.newValue < request.minAllowed || request.newValue > request.maxAllowed) {
        error = "Requested value is outside allowed bounds";
        return std::nullopt;
    }
    if (originalBytes.size() < sizeof(std::int32_t) ||
        request.byteOffset > originalBytes.size() - sizeof(std::int32_t)) {
        error = "Patch offset is outside buffer bounds";
        return std::nullopt;
    }

    // Validate original stream integrity first.
    const auto before = Parse(originalBytes);
    if (!before.ok) {
        error = "Original NRBF stream failed validation: " + before.error;
        return std::nullopt;
    }

    // Confirm the offset corresponds to an actual parsed Int32 field matching expectedOldValue.
    bool matchedField = false;
    for (const auto& [objId, obj] : before.document.objects) {
        for (const auto& field : obj.fields) {
            if (field.byteOffset == request.byteOffset && field.byteSize == sizeof(std::int32_t)) {
                if (const auto* iv = std::get_if<std::int64_t>(&field.value)) {
                    if (*iv != static_cast<std::int64_t>(request.expectedOldValue)) {
                        error = "Field value mismatch at target offset (concurrent modification or stale snapshot)";
                        return std::nullopt;
                    }
                    matchedField = true;
                    break;
                }
            }
        }
        if (matchedField) break;
    }
    if (!matchedField) {
        error = "Target offset does not match any validated Int32 field in NRBF graph";
        return std::nullopt;
    }

    std::vector<std::uint8_t> patched = originalBytes;
    std::memcpy(&patched[request.byteOffset], &request.newValue, sizeof(request.newValue));

    // Re-parse the entire patched stream from byte 0 to MessageEnd to verify structural integrity.
    const auto after = Parse(patched);
    if (!after.ok || after.document.bytesConsumed != patched.size() ||
        after.document.recordCount != before.document.recordCount) {
        error = "Patched NRBF stream failed post-write integrity validation";
        return std::nullopt;
    }

    // Confirm target field now reads back newValue.
    bool verifiedNewValue = false;
    for (const auto& [objId, obj] : after.document.objects) {
        for (const auto& field : obj.fields) {
            if (field.byteOffset == request.byteOffset && field.byteSize == sizeof(std::int32_t)) {
                if (const auto* iv = std::get_if<std::int64_t>(&field.value)) {
                    verifiedNewValue = (*iv == static_cast<std::int64_t>(request.newValue));
                }
                break;
            }
        }
        if (verifiedNewValue) break;
    }
    if (!verifiedNewValue) {
        error = "Post-patch readback verification failed";
        return std::nullopt;
    }

    return patched;
}

} // namespace trainer::nrbf
