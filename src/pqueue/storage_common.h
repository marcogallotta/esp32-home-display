#pragma once

#include "file_store.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pqueue::storage_detail {

constexpr std::uint32_t kFileStoreIndexMagic = 0x50514958;
constexpr std::uint32_t kFileStoreRecordMagic = 0x50515243;
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::size_t kMaxPathBytes = 160;
constexpr std::size_t kMaxRecordBytes = 4096;

struct IndexRecord {
    std::uint32_t magic = kFileStoreIndexMagic;
    std::uint16_t version = kFormatVersion;
    std::uint16_t reserved = 0;
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    std::uint32_t generation = 0;
    std::uint32_t crc = 0;
};

struct RecordHeader {
    std::uint32_t magic = kFileStoreRecordMagic;
    std::uint16_t version = kFormatVersion;
    std::uint16_t headerBytes = sizeof(RecordHeader);
    std::uint32_t recordBytes = 0;
    std::uint32_t crc = 0;
};

std::uint32_t crc32Update(std::uint32_t crc, const void* data, std::size_t len);
std::uint32_t indexCrc(const IndexRecord& record);
std::uint32_t recordCrc(const RecordHeader& header, const std::string& record);

bool validIndex(const IndexRecord& record);
IndexRecord toRecord(const FileStoreIndex& index, std::uint32_t generation);
FileStoreIndex fromRecord(const IndexRecord& record);

bool makeRecordName(std::uint32_t sequence, char* out, std::size_t outSize);
bool parseRecordSequence(const std::string& name, std::uint32_t& out);
FileStoreIndex rebuildIndexFromSequences(std::vector<std::uint32_t>& sequences);

} // namespace pqueue::storage_detail
