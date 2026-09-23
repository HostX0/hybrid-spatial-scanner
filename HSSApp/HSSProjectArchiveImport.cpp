#include "HSSProjectArchiveImport.hpp"

#include "hss/project_import_validation.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <zlib.h>

namespace hss::native_import {
namespace {

constexpr uint32_t kEndOfCentralDirectory = 0x06054b50U;
constexpr uint32_t kCentralDirectoryEntry = 0x02014b50U;
constexpr uint32_t kLocalFileHeader = 0x04034b50U;
constexpr uint16_t kHssSha256ExtraField = 0x4853U;
constexpr uint16_t kZipEncryptionFlag = 1U;
constexpr uint16_t kZipDataDescriptorFlag = 8U;
constexpr uint16_t kZipUTF8Flag = 1U << 11U;
constexpr uint16_t kSupportedFlags = kZipUTF8Flag;
constexpr uint32_t kUnixFileTypeMask = 0170000U;
constexpr uint32_t kUnixSymbolicLink = 0120000U;
constexpr uint64_t kMaximumArchiveBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr size_t kCopyBufferBytes = 64U * 1024U;

uint16_t le16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         static_cast<uint16_t>(value[1]) << 8U;
}

uint32_t le32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         static_cast<uint32_t>(value[1]) << 8U |
         static_cast<uint32_t>(value[2]) << 16U |
         static_cast<uint32_t>(value[3]) << 24U;
}

bool add_fits(uint64_t left, uint64_t right, uint64_t* result) {
  if (right > std::numeric_limits<uint64_t>::max() - left) return false;
  *result = left + right;
  return true;
}

bool read_exact(std::ifstream* stream, uint64_t offset, uint8_t* destination,
                size_t count) {
  if (stream == nullptr || offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return false;
  }
  stream->clear();
  stream->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!*stream) return false;
  stream->read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(count));
  return static_cast<size_t>(stream->gcount()) == count;
}

bool archive_size(const std::string& path, uint64_t* result) {
  std::error_code error;
  const uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size == 0U || size > kMaximumArchiveBytes) return false;
  *result = static_cast<uint64_t>(size);
  return true;
}

struct ZipEntry {
  std::string path;
  bool directory{false};
  uint16_t flags{0};
  uint16_t method{0};
  uint64_t compressed_bytes{0};
  uint64_t uncompressed_bytes{0};
  uint64_t local_header_offset{0};
  uint64_t data_offset{0};
  hss::project_import::Sha256 sha256{};
};

bool sha256_from_extra(const std::vector<uint8_t>& extra,
                       hss::project_import::Sha256* sha256) {
  size_t position = 0;
  bool found = false;
  while (position < extra.size()) {
    if (extra.size() - position < 4U) return false;
    const uint16_t identifier = le16(extra.data() + position);
    const uint16_t size = le16(extra.data() + position + 2U);
    position += 4U;
    if (size > extra.size() - position) return false;
    if (identifier == kHssSha256ExtraField) {
      if (found || size != sha256->size()) return false;
      std::copy_n(extra.data() + position, sha256->size(), sha256->begin());
      found = true;
    }
    position += size;
  }
  return found;
}

bool parse_archive(const std::string& path, std::vector<ZipEntry>* entries,
                   std::string* message) {
  uint64_t size = 0;
  if (!archive_size(path, &size)) {
    *message = "The selected file is unavailable or exceeds the archive limit.";
    return false;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    *message = "The selected archive could not be read.";
    return false;
  }
  const size_t trailer_size = static_cast<size_t>(std::min<uint64_t>(size, 65557U));
  std::vector<uint8_t> trailer(trailer_size);
  if (!read_exact(&stream, size - trailer_size, trailer.data(), trailer.size())) {
    *message = "The archive trailer is incomplete.";
    return false;
  }
  size_t eocd = trailer.size();
  if (trailer.size() >= 22U) {
    for (size_t position = trailer.size() - 22U + 1U; position-- > 0U;) {
      if (le32(trailer.data() + position) == kEndOfCentralDirectory) {
        eocd = position;
        break;
      }
    }
  }
  if (eocd == trailer.size() || eocd + 22U > trailer.size()) {
    *message = "The selected file is not a supported HSSCAN ZIP archive.";
    return false;
  }
  const uint8_t* end = trailer.data() + eocd;
  const uint16_t disk = le16(end + 4U);
  const uint16_t central_disk = le16(end + 6U);
  const uint16_t disk_entries = le16(end + 8U);
  const uint16_t entry_count = le16(end + 10U);
  const uint32_t central_size = le32(end + 12U);
  const uint32_t central_offset = le32(end + 16U);
  const uint16_t comment_length = le16(end + 20U);
  if (disk != 0U || central_disk != 0U || disk_entries != entry_count ||
      entry_count == 0xffffU || central_size == 0xffffffffU ||
      central_offset == 0xffffffffU || eocd + 22U + comment_length != trailer.size()) {
    *message = "Split, ZIP64, or malformed HSSCAN archives are unsupported.";
    return false;
  }
  uint64_t central_end = 0;
  if (!add_fits(central_offset, central_size, &central_end) || central_end > size) {
    *message = "The archive central directory is outside the selected file.";
    return false;
  }
  uint64_t position = central_offset;
  entries->clear();
  entries->reserve(entry_count);
  for (uint16_t index = 0; index < entry_count; ++index) {
    std::array<uint8_t, 46U> header{};
    if (!read_exact(&stream, position, header.data(), header.size()) ||
        le32(header.data()) != kCentralDirectoryEntry) {
      *message = "The archive central directory has an invalid entry.";
      return false;
    }
    const uint16_t made_by = le16(header.data() + 4U);
    const uint16_t flags = le16(header.data() + 8U);
    const uint16_t method = le16(header.data() + 10U);
    const uint32_t compressed = le32(header.data() + 20U);
    const uint32_t uncompressed = le32(header.data() + 24U);
    const uint16_t name_length = le16(header.data() + 28U);
    const uint16_t extra_length = le16(header.data() + 30U);
    const uint16_t comment_size = le16(header.data() + 32U);
    const uint32_t external_attributes = le32(header.data() + 38U);
    const uint32_t local_offset = le32(header.data() + 42U);
    uint64_t record_size = 46U;
    if (!add_fits(record_size, name_length, &record_size) ||
        !add_fits(record_size, extra_length, &record_size) ||
        !add_fits(record_size, comment_size, &record_size) ||
        record_size > central_end - position ||
        (flags & (kZipEncryptionFlag | kZipDataDescriptorFlag)) != 0U ||
        (flags & ~kSupportedFlags) != 0U || (method != 0U && method != 8U)) {
      *message = "The archive contains an encrypted, streamed, or unsupported entry.";
      return false;
    }
    const uint32_t unix_mode = (made_by >> 8U) == 3U ? external_attributes >> 16U : 0U;
    if ((unix_mode & kUnixFileTypeMask) == kUnixSymbolicLink) {
      *message = "The archive contains a symbolic link, which is not supported.";
      return false;
    }
    std::vector<uint8_t> name(name_length);
    std::vector<uint8_t> extra(extra_length);
    if ((name_length != 0U && !read_exact(&stream, position + 46U, name.data(), name.size())) ||
        (extra_length != 0U && !read_exact(&stream, position + 46U + name_length, extra.data(), extra.size()))) {
      *message = "The archive entry name or metadata is incomplete.";
      return false;
    }
    ZipEntry entry;
    entry.path.assign(reinterpret_cast<const char*>(name.data()), name.size());
    entry.directory = !entry.path.empty() && entry.path.back() == '/';
    entry.flags = flags;
    entry.method = method;
    entry.compressed_bytes = compressed;
    entry.uncompressed_bytes = uncompressed;
    entry.local_header_offset = local_offset;
    if (entry.directory) {
      if (compressed != 0U || uncompressed != 0U || !extra.empty()) {
        *message = "The archive contains a nonempty directory entry.";
        return false;
      }
    } else if (!sha256_from_extra(extra, &entry.sha256)) {
      *message = "Every HSSCAN file must include a SHA-256 archive entry digest.";
      return false;
    }
    std::array<uint8_t, 30U> local{};
    if (!read_exact(&stream, entry.local_header_offset, local.data(), local.size()) ||
        le32(local.data()) != kLocalFileHeader || le16(local.data() + 6U) != flags ||
        le16(local.data() + 8U) != method || le32(local.data() + 18U) != compressed ||
        le32(local.data() + 22U) != uncompressed) {
      *message = "The archive local entry does not match its central directory.";
      return false;
    }
    uint64_t local_data = 0;
    uint64_t data_end = 0;
    if (!add_fits(entry.local_header_offset, 30U, &local_data) ||
        !add_fits(local_data, le16(local.data() + 26U), &local_data) ||
        !add_fits(local_data, le16(local.data() + 28U), &local_data) ||
        !add_fits(local_data, entry.compressed_bytes, &data_end) || data_end > size) {
      *message = "The archive data is outside the selected file.";
      return false;
    }
    entry.data_offset = local_data;
    entries->push_back(std::move(entry));
    position += record_size;
  }
  if (position != central_offset + central_size) {
    *message = "The archive central directory has trailing records.";
    return false;
  }
  return true;
}

class ZipReader final : public hss::project_import::StreamingReader {
 public:
  ZipReader(std::string path, const ZipEntry& entry) : path_(std::move(path)), entry_(entry) {}

  ~ZipReader() override {
    if (zstream_initialized_) inflateEnd(&zstream_);
  }

  hss::project_import::ReadResult read(uint8_t* destination, size_t capacity) override {
    if (destination == nullptr || capacity == 0U || failed_) return {hss::project_import::ReadState::kFailure, 0U};
    if (finished_) return {hss::project_import::ReadState::kEnd, 0U};
    if (!started_ && !start()) return {hss::project_import::ReadState::kFailure, 0U};
    if (entry_.method == 0U) return read_stored(destination, capacity);
    return read_deflated(destination, capacity);
  }

 private:
  bool start() {
    stream_.open(path_, std::ios::binary);
    if (!stream_) return false;
    stream_.seekg(static_cast<std::streamoff>(entry_.data_offset), std::ios::beg);
    if (!stream_) return false;
    started_ = true;
    if (entry_.method == 8U) {
      std::memset(&zstream_, 0, sizeof(zstream_));
      if (inflateInit2(&zstream_, -MAX_WBITS) != Z_OK) return false;
      zstream_initialized_ = true;
    }
    return true;
  }

  hss::project_import::ReadResult read_stored(uint8_t* destination, size_t capacity) {
    const uint64_t remaining = entry_.compressed_bytes - compressed_read_;
    if (remaining == 0U) {
      finished_ = true;
      return {hss::project_import::ReadState::kEnd, 0U};
    }
    const size_t wanted = static_cast<size_t>(std::min<uint64_t>(remaining, capacity));
    stream_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(wanted));
    if (stream_.gcount() != static_cast<std::streamsize>(wanted)) return fail();
    compressed_read_ += wanted;
    return {hss::project_import::ReadState::kData, wanted};
  }

  hss::project_import::ReadResult read_deflated(uint8_t* destination, size_t capacity) {
    zstream_.next_out = destination;
    zstream_.avail_out = static_cast<uInt>(std::min<size_t>(capacity, std::numeric_limits<uInt>::max()));
    const uInt output_capacity = zstream_.avail_out;
    while (zstream_.avail_out != 0U) {
      if (zstream_.avail_in == 0U && compressed_read_ < entry_.compressed_bytes) {
        const size_t wanted = static_cast<size_t>(std::min<uint64_t>(entry_.compressed_bytes - compressed_read_, input_.size()));
        stream_.read(reinterpret_cast<char*>(input_.data()), static_cast<std::streamsize>(wanted));
        if (stream_.gcount() != static_cast<std::streamsize>(wanted)) return fail();
        compressed_read_ += wanted;
        zstream_.next_in = input_.data();
        zstream_.avail_in = static_cast<uInt>(wanted);
      }
      const int status = inflate(&zstream_, Z_NO_FLUSH);
      if (status == Z_STREAM_END) {
        if (zstream_.avail_in != 0U || compressed_read_ != entry_.compressed_bytes) return fail();
        finished_ = true;
        break;
      }
      if (status != Z_OK || (zstream_.avail_in == 0U && compressed_read_ == entry_.compressed_bytes && zstream_.avail_out == output_capacity)) {
        return fail();
      }
      if (zstream_.avail_out != output_capacity) break;
    }
    const size_t produced = output_capacity - zstream_.avail_out;
    return produced == 0U ? (finished_ ? hss::project_import::ReadResult{hss::project_import::ReadState::kEnd, 0U} : fail())
                          : hss::project_import::ReadResult{hss::project_import::ReadState::kData, produced};
  }

  hss::project_import::ReadResult fail() {
    failed_ = true;
    return {hss::project_import::ReadState::kFailure, 0U};
  }

  std::string path_;
  ZipEntry entry_;
  std::ifstream stream_;
  std::array<uint8_t, kCopyBufferBytes> input_{};
  z_stream zstream_{};
  uint64_t compressed_read_{0};
  bool started_{false};
  bool finished_{false};
  bool failed_{false};
  bool zstream_initialized_{false};
};

class FileReader final : public hss::project_import::StreamingReader {
 public:
  explicit FileReader(std::filesystem::path path) : path_(std::move(path)) {}

  hss::project_import::ReadResult read(uint8_t* destination, size_t capacity) override {
    if (destination == nullptr || capacity == 0U || failed_) return fail();
    if (!opened_) {
      stream_.open(path_, std::ios::binary);
      opened_ = true;
      if (!stream_) return fail();
    }
    stream_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(capacity));
    const std::streamsize count = stream_.gcount();
    if (count > 0) return {hss::project_import::ReadState::kData, static_cast<size_t>(count)};
    if (stream_.eof()) return {hss::project_import::ReadState::kEnd, 0U};
    return fail();
  }

 private:
  hss::project_import::ReadResult fail() {
    failed_ = true;
    return {hss::project_import::ReadState::kFailure, 0U};
  }

  std::filesystem::path path_;
  std::ifstream stream_;
  bool opened_{false};
  bool failed_{false};
};

bool copy_file(const std::string& archive_path, const ZipEntry& entry,
               const std::filesystem::path& destination) {
  ZipReader reader(archive_path, entry);
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  std::array<uint8_t, kCopyBufferBytes> buffer{};
  uint64_t copied = 0;
  while (copied < entry.uncompressed_bytes) {
    const size_t requested = static_cast<size_t>(std::min<uint64_t>(buffer.size(), entry.uncompressed_bytes - copied));
    const auto read = reader.read(buffer.data(), requested);
    if (read.state != hss::project_import::ReadState::kData || read.bytes == 0U || read.bytes > requested) return false;
    output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(read.bytes));
    if (!output) return false;
    copied += read.bytes;
  }
  const auto trailing = reader.read(buffer.data(), 1U);
  return trailing.state == hss::project_import::ReadState::kEnd && trailing.bytes == 0U && output.good();
}

}  // namespace

Result extract_hsscan_archive(const std::string& archive_path,
                              const std::string& staging_directory) {
  if (archive_path.empty() || staging_directory.empty()) {
    return {Status::kInvalidArgument, "An archive and a private staging directory are required."};
  }
  std::vector<ZipEntry> entries;
  std::string message;
  if (!parse_archive(archive_path, &entries, &message)) return {Status::kUnsupportedArchive, std::move(message)};

  std::vector<std::unique_ptr<ZipReader>> readers;
  std::vector<hss::project_import::ArchiveEntry> validation_entries;
  readers.reserve(entries.size());
  validation_entries.reserve(entries.size());
  for (const ZipEntry& entry : entries) {
    hss::project_import::ArchiveEntry candidate;
    candidate.path = entry.path;
    candidate.kind = entry.directory ? hss::project_import::EntryKind::kDirectory
                                     : hss::project_import::EntryKind::kRegularFile;
    candidate.content_kind = entry.path.ends_with(".json") ? hss::project_import::ContentKind::kJson
                                                            : hss::project_import::ContentKind::kBinary;
    candidate.declared_uncompressed_bytes = entry.uncompressed_bytes;
    if (!entry.directory) {
      readers.push_back(std::make_unique<ZipReader>(archive_path, entry));
      candidate.has_sha256 = true;
      candidate.sha256 = entry.sha256;
      candidate.reader = readers.back().get();
    }
    validation_entries.push_back(std::move(candidate));
  }
  const auto validation = hss::project_import::validate_archive(validation_entries, {});
  if (validation.status != hss::project_import::Status::kOk) {
    return {Status::kValidationFailed, "The archive failed the secure project validation gate."};
  }

  std::error_code error;
  const std::filesystem::path staging(staging_directory);
  if (std::filesystem::exists(staging, error) || error ||
      !std::filesystem::create_directories(staging, error) || error) {
    return {Status::kIoError, "The private import staging directory could not be created."};
  }
  for (const ZipEntry& entry : entries) {
    const std::filesystem::path destination = staging / std::filesystem::path(entry.path);
    if (entry.directory) {
      std::filesystem::create_directories(destination, error);
      if (error || !std::filesystem::is_directory(destination, error) || error) {
        std::filesystem::remove_all(staging, error);
        return {Status::kExtractionFailed, "A validated directory could not be staged."};
      }
      continue;
    }
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error || !std::filesystem::is_directory(destination.parent_path(), error) || error ||
        !copy_file(archive_path, entry, destination)) {
      std::filesystem::remove_all(staging, error);
      return {Status::kExtractionFailed, "A validated archive entry could not be staged."};
    }
  }
  // A provider can alter a security-scoped file between the validation pass
  // and extraction. Re-read the private staging files against the same B-039
  // digests before they become candidates for project-store replay.
  std::vector<std::unique_ptr<FileReader>> staged_readers;
  std::vector<hss::project_import::ArchiveEntry> staged_entries;
  staged_readers.reserve(entries.size());
  staged_entries.reserve(entries.size());
  for (const ZipEntry& entry : entries) {
    hss::project_import::ArchiveEntry candidate;
    candidate.path = entry.path;
    candidate.kind = entry.directory ? hss::project_import::EntryKind::kDirectory
                                     : hss::project_import::EntryKind::kRegularFile;
    candidate.content_kind = entry.path.ends_with(".json") ? hss::project_import::ContentKind::kJson
                                                            : hss::project_import::ContentKind::kBinary;
    candidate.declared_uncompressed_bytes = entry.uncompressed_bytes;
    if (!entry.directory) {
      staged_readers.push_back(std::make_unique<FileReader>(staging / std::filesystem::path(entry.path)));
      candidate.has_sha256 = true;
      candidate.sha256 = entry.sha256;
      candidate.reader = staged_readers.back().get();
    }
    staged_entries.push_back(std::move(candidate));
  }
  if (hss::project_import::validate_archive(staged_entries, {}).status != hss::project_import::Status::kOk) {
    std::filesystem::remove_all(staging, error);
    return {Status::kExtractionFailed, "The staged project no longer matches the validated archive."};
  }
  return {Status::kOk, {}};
}

}  // namespace hss::native_import
