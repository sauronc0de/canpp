#include "canpp/trace/binary_trace.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <type_traits>

namespace canpp::trace {
namespace {

constexpr std::array<char, 8> magic{'C', 'O', 'M', 'T', 'R', 'C', '1', '\0'};
constexpr std::uint32_t version = 1;

#pragma pack(push, 1)
struct FileHeader {
    char magic_bytes[8];
    std::uint32_t format_version;
    std::uint32_t header_size;
    std::uint64_t record_count;
};

struct RecordHeader {
    std::uint64_t timestamp_ns;
    std::uint32_t stream_id;
    std::uint32_t metadata_size;
    std::uint32_t payload_size;
    std::uint16_t protocol;
    std::uint16_t flags;
    std::uint8_t direction;
    std::uint8_t reserved[3];
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 24);
static_assert(sizeof(RecordHeader) == 28);
static_assert(std::is_trivially_copyable_v<FileHeader>);
static_assert(std::is_trivially_copyable_v<RecordHeader>);

bool valid_size(std::size_t value) {
    return value <= std::numeric_limits<std::uint32_t>::max();
}

} // namespace

BinaryTraceWriter::BinaryTraceWriter(const std::filesystem::path& path)
    : stream_(path, std::ios::binary | std::ios::trunc) {
    if (!stream_) {
        error_ = "Cannot create trace file: " + path.string();
        return;
    }
    FileHeader header{};
    std::memcpy(header.magic_bytes, magic.data(), magic.size());
    header.format_version = version;
    header.header_size = sizeof(FileHeader);
    stream_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!stream_) {
        error_ = "Cannot write trace header";
    }
}

BinaryTraceWriter::~BinaryTraceWriter() {
    if (!finalized_) {
        static_cast<void>(finalize());
    }
}

bool BinaryTraceWriter::good() const { return stream_.good() && error_.empty(); }
const std::string& BinaryTraceWriter::error() const { return error_; }

bool BinaryTraceWriter::append(const Record& record) {
    if (!good()) {
        return false;
    }
    if (!valid_size(record.metadata.size()) || !valid_size(record.payload.size())) {
        error_ = "Record metadata or payload exceeds 4 GiB";
        return false;
    }

    RecordHeader header{};
    header.timestamp_ns = record.timestamp_ns;
    header.stream_id = record.stream_id;
    header.metadata_size = static_cast<std::uint32_t>(record.metadata.size());
    header.payload_size = static_cast<std::uint32_t>(record.payload.size());
    header.protocol = static_cast<std::uint16_t>(record.protocol);
    header.flags = record.flags;
    header.direction = static_cast<std::uint8_t>(record.direction);

    stream_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    stream_.write(reinterpret_cast<const char*>(record.metadata.data()),
                  static_cast<std::streamsize>(record.metadata.size()));
    stream_.write(reinterpret_cast<const char*>(record.payload.data()),
                  static_cast<std::streamsize>(record.payload.size()));
    if (!stream_) {
        error_ = "Cannot append trace record";
        return false;
    }
    ++record_count_;
    return true;
}

bool BinaryTraceWriter::finalize() {
    if (finalized_) {
        return error_.empty();
    }
    finalized_ = true;
    if (!stream_) {
        return false;
    }
    stream_.seekp(offsetof(FileHeader, record_count), std::ios::beg);
    stream_.write(reinterpret_cast<const char*>(&record_count_), sizeof(record_count_));
    stream_.flush();
    if (!stream_) {
        error_ = "Cannot finalize trace file";
        return false;
    }
    return true;
}

bool BinaryTraceReader::open(const std::filesystem::path& path, std::string& error) {
    close();
    stream_.open(path, std::ios::binary);
    if (!stream_) {
        error = "Cannot open trace file: " + path.string();
        return false;
    }

    FileHeader file_header{};
    stream_.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
    if (!stream_ || std::memcmp(file_header.magic_bytes, magic.data(), magic.size()) != 0 ||
        file_header.format_version != version || file_header.header_size != sizeof(FileHeader)) {
        error = "Unsupported or corrupt communication trace";
        close();
        return false;
    }

    record_offsets_.reserve(static_cast<std::size_t>(file_header.record_count));
    for (std::uint64_t index = 0; index < file_header.record_count; ++index) {
        const auto offset = stream_.tellg();
        if (offset < 0) {
            error = "Corrupt record offset";
            close();
            return false;
        }
        RecordHeader record_header{};
        stream_.read(reinterpret_cast<char*>(&record_header), sizeof(record_header));
        if (!stream_) {
            error = "Unexpected end of trace";
            close();
            return false;
        }
        record_offsets_.push_back(static_cast<std::uint64_t>(offset));
        const auto skip = static_cast<std::uint64_t>(record_header.metadata_size) +
                          static_cast<std::uint64_t>(record_header.payload_size);
        stream_.seekg(static_cast<std::streamoff>(skip), std::ios::cur);
        if (!stream_) {
            error = "Corrupt record size";
            close();
            return false;
        }
    }
    path_ = path;
    return true;
}

void BinaryTraceReader::close() {
    stream_.close();
    stream_.clear();
    path_.clear();
    record_offsets_.clear();
}

bool BinaryTraceReader::is_open() const { return stream_.is_open(); }
std::uint64_t BinaryTraceReader::size() const { return record_offsets_.size(); }
const std::filesystem::path& BinaryTraceReader::path() const { return path_; }

std::optional<Record> BinaryTraceReader::read(std::uint64_t index) const {
    if (index >= record_offsets_.size()) {
        return std::nullopt;
    }
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(record_offsets_[static_cast<std::size_t>(index)]),
                  std::ios::beg);
    RecordHeader header{};
    stream_.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!stream_) {
        return std::nullopt;
    }

    Record record;
    record.timestamp_ns = header.timestamp_ns;
    record.stream_id = header.stream_id;
    record.protocol = static_cast<ProtocolId>(header.protocol);
    record.direction = static_cast<Direction>(header.direction);
    record.flags = header.flags;
    record.metadata.resize(header.metadata_size);
    record.payload.resize(header.payload_size);
    stream_.read(reinterpret_cast<char*>(record.metadata.data()),
                 static_cast<std::streamsize>(record.metadata.size()));
    stream_.read(reinterpret_cast<char*>(record.payload.data()),
                 static_cast<std::streamsize>(record.payload.size()));
    if (!stream_) {
        return std::nullopt;
    }
    return record;
}

bool write_selection(const BinaryTraceReader& source,
                     const std::vector<std::uint64_t>& indices,
                     const std::filesystem::path& output,
                     std::string& error) {
    BinaryTraceWriter writer(output);
    if (!writer.good()) {
        error = writer.error();
        return false;
    }
    for (const auto index : indices) {
        const auto record = source.read(index);
        if (!record) {
            error = "Cannot read selected record " + std::to_string(index);
            return false;
        }
        if (!writer.append(*record)) {
            error = writer.error();
            return false;
        }
    }
    if (!writer.finalize()) {
        error = writer.error();
        return false;
    }
    return true;
}

} // namespace canpp::trace
