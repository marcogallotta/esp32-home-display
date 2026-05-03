#include "storage_common.h"

#include <cstddef>

namespace pqueue::storage_detail {
namespace {

void writeLe16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
}

void writeLe32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

std::uint16_t readLe16(const std::string& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 8U));
}

std::uint32_t readLe32(const std::string& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset])) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 1])) << 8U) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 2])) << 16U) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[offset + 3])) << 24U);
}

std::string serializeIndexPrefix(const IndexRecord& record) {
    std::string out;
    out.reserve(kIndexRecordBytes);
    writeLe32(out, record.magic);
    writeLe16(out, record.version);
    writeLe16(out, record.headerBytes);
    writeLe32(out, record.capacityRecords);
    writeLe32(out, record.recordSizeBytes);
    writeLe32(out, record.reservedBytes);
    writeLe32(out, record.head);
    writeLe32(out, record.tail);
    writeLe32(out, record.count);
    writeLe32(out, record.generation);
    return out;
}

std::string serializeRecordHeaderPrefix(const RecordHeader& header) {
    std::string out;
    out.reserve(kRecordHeaderBytes);
    writeLe32(out, header.magic);
    writeLe16(out, header.version);
    writeLe16(out, header.headerBytes);
    writeLe32(out, header.sequence);
    writeLe32(out, header.recordBytes);
    return out;
}

} // namespace

std::uint32_t crc32Update(std::uint32_t crc, const void* data, std::size_t len) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

std::uint32_t indexCrc(const IndexRecord& record) {
    const std::string prefix = serializeIndexPrefix(record);
    return crc32Update(0, prefix.data(), prefix.size());
}

std::uint32_t recordCrc(const RecordHeader& header, const std::string& record) {
    const std::string prefix = serializeRecordHeaderPrefix(header);
    std::uint32_t crc = crc32Update(0, prefix.data(), prefix.size());
    return crc32Update(crc, record.data(), record.size());
}

std::string serializeIndexRecord(const IndexRecord& record) {
    std::string out = serializeIndexPrefix(record);
    writeLe32(out, record.crc);
    return out;
}

bool parseIndexRecord(const std::string& bytes, IndexRecord& out) {
    if (bytes.size() != kIndexRecordBytes) {
        return false;
    }
    out.magic = readLe32(bytes, 0);
    out.version = readLe16(bytes, 4);
    out.headerBytes = readLe16(bytes, 6);
    out.capacityRecords = readLe32(bytes, 8);
    out.recordSizeBytes = readLe32(bytes, 12);
    out.reservedBytes = readLe32(bytes, 16);
    out.head = readLe32(bytes, 20);
    out.tail = readLe32(bytes, 24);
    out.count = readLe32(bytes, 28);
    out.generation = readLe32(bytes, 32);
    out.crc = readLe32(bytes, 36);
    return true;
}

std::string serializeRecordHeader(const RecordHeader& header) {
    std::string out = serializeRecordHeaderPrefix(header);
    writeLe32(out, header.crc);
    return out;
}

bool parseRecordHeader(const std::string& bytes, RecordHeader& out) {
    if (bytes.size() < kRecordHeaderBytes) {
        return false;
    }
    out.magic = readLe32(bytes, 0);
    out.version = readLe16(bytes, 4);
    out.headerBytes = readLe16(bytes, 6);
    out.sequence = readLe32(bytes, 8);
    out.recordBytes = readLe32(bytes, 12);
    out.crc = readLe32(bytes, 16);
    return true;
}

bool validIndexShape(const IndexRecord& record) {
    return record.magic == kFileStoreIndexMagic &&
           record.version == kFormatVersion &&
           record.headerBytes == kIndexRecordBytes &&
           record.crc == indexCrc(record) &&
           record.capacityRecords > 0 &&
           record.recordSizeBytes > 0 &&
           record.reservedBytes > 0 &&
           record.count <= record.capacityRecords &&
           record.tail >= record.head &&
           record.count == record.tail - record.head;
}

bool validIndexForConfig(const IndexRecord& record, std::uint32_t capacityRecords, std::uint32_t recordSizeBytes, std::uint32_t reservedBytes) {
    return validIndexShape(record) &&
           record.capacityRecords == capacityRecords &&
           record.recordSizeBytes == recordSizeBytes &&
           record.reservedBytes == reservedBytes;
}

IndexRecord toRecord(const FileStoreIndex& index, std::uint32_t generation, std::uint32_t capacityRecords, std::uint32_t recordSizeBytes, std::uint32_t reservedBytes) {
    IndexRecord out;
    out.capacityRecords = capacityRecords;
    out.recordSizeBytes = recordSizeBytes;
    out.reservedBytes = reservedBytes;
    out.head = index.head;
    out.tail = index.tail;
    out.count = index.count;
    out.generation = generation;
    out.crc = indexCrc(out);
    return out;
}

FileStoreIndex fromRecord(const IndexRecord& record) {
    FileStoreIndex out;
    out.head = record.head;
    out.tail = record.tail;
    out.count = record.count;
    return out;
}

} // namespace pqueue::storage_detail
