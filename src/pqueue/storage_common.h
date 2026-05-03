#pragma once

#include "file_store.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace pqueue::storage_detail {

constexpr std::uint32_t kFileStoreIndexMagic = 0x50514958;
constexpr std::uint32_t kFileStoreRecordMagic = 0x50515243;
// On-disk pqueue storage format.
// 0 = unreleased/unstable development format.
// 1+ = public stable formats.
// Only exact-version reads are supported; future versions may add upgrade paths,
// but never automatic downgrades.
constexpr std::uint16_t kFormatVersion = 0;
constexpr std::size_t kMaxPathBytes = 160;

constexpr std::uint16_t kIndexRecordBytes = 40;
constexpr std::uint16_t kRecordHeaderBytes = 20;

struct IndexRecord {
    std::uint32_t magic = kFileStoreIndexMagic;
    std::uint16_t version = kFormatVersion;
    std::uint16_t headerBytes = kIndexRecordBytes;
    std::uint32_t capacityRecords = 0;
    std::uint32_t recordSizeBytes = 0;
    std::uint32_t reservedBytes = 0;
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    std::uint32_t generation = 0;
    std::uint32_t crc = 0;
};

struct RecordHeader {
    std::uint32_t magic = kFileStoreRecordMagic;
    std::uint16_t version = kFormatVersion;
    std::uint16_t headerBytes = kRecordHeaderBytes;
    std::uint32_t sequence = 0;
    std::uint32_t recordBytes = 0;
    std::uint32_t crc = 0;
};

std::uint32_t crc32Update(std::uint32_t crc, const void* data, std::size_t len);
std::uint32_t indexCrc(const IndexRecord& record);
std::uint32_t recordCrc(const RecordHeader& header, const std::string& record);

std::string serializeIndexRecord(const IndexRecord& record);
bool parseIndexRecord(const std::string& bytes, IndexRecord& out);
std::string serializeRecordHeader(const RecordHeader& header);
bool parseRecordHeader(const std::string& bytes, RecordHeader& out);

bool validIndexShape(const IndexRecord& record);
bool validIndexForConfig(const IndexRecord& record, std::uint32_t capacityRecords, std::uint32_t recordSizeBytes, std::uint32_t reservedBytes);
IndexRecord toRecord(const FileStoreIndex& index, std::uint32_t generation, std::uint32_t capacityRecords, std::uint32_t recordSizeBytes, std::uint32_t reservedBytes);
FileStoreIndex fromRecord(const IndexRecord& record);

} // namespace pqueue::storage_detail
