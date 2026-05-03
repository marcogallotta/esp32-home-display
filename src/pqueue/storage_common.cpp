#include "storage_common.h"

#include <cstddef>

namespace pqueue::storage_detail {

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
    return crc32Update(0, &record, offsetof(IndexRecord, crc));
}

std::uint32_t recordCrc(const RecordHeader& header, const std::string& record) {
    std::uint32_t crc = crc32Update(0, &header, offsetof(RecordHeader, crc));
    return crc32Update(crc, record.data(), record.size());
}

bool validIndexShape(const IndexRecord& record) {
    return record.magic == kFileStoreIndexMagic &&
           record.version == kFormatVersion &&
           record.headerBytes == sizeof(IndexRecord) &&
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
