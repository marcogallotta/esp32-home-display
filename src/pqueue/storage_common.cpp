#include "storage_common.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

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

bool validIndex(const IndexRecord& record) {
    return record.magic == kFileStoreIndexMagic &&
           record.version == kFormatVersion &&
           record.crc == indexCrc(record) &&
           record.tail >= record.head &&
           record.count == record.tail - record.head;
}

IndexRecord toRecord(const FileStoreIndex& index, std::uint32_t generation) {
    IndexRecord out;
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

bool makeRecordName(std::uint32_t sequence, char* out, std::size_t outSize) {
    const int n = std::snprintf(out, outSize, "pqueue_rec_%08lu.bin", static_cast<unsigned long>(sequence));
    return n > 0 && static_cast<std::size_t>(n) < outSize;
}

bool parseRecordSequence(const std::string& name, std::uint32_t& out) {
    unsigned long value = 0;
    char suffix = '\0';
    if (std::sscanf(name.c_str(), "pqueue_rec_%08lu.bin%c", &value, &suffix) != 1) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

FileStoreIndex rebuildIndexFromSequences(std::vector<std::uint32_t>& sequences) {
    FileStoreIndex out;
    if (sequences.empty()) {
        return out;
    }
    std::sort(sequences.begin(), sequences.end());
    out.head = sequences.front();
    out.tail = sequences.back() + 1;
    out.count = out.tail - out.head;
    return out;
}

} // namespace pqueue::storage_detail
