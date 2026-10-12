#include "HSSProjectArchiveExport.hpp"
#include "hss/durable_source_store.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>
#include <zlib.h>

namespace hss::native_export {
namespace {

namespace fs = std::filesystem;

constexpr uint16_t kHssSha256ExtraField = 0x4853U;
constexpr uint16_t kZipVersion = 20U;
constexpr uint16_t kZipStoreMethod = 0U;
constexpr uint16_t kZipFlags = 0U;
constexpr uint64_t kMaximumProjectBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaximumChunkBytes = 256ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaximumManifestBytes = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaximumFiles = 20'000U;
constexpr size_t kCopyBufferBytes = 64U * 1024U;

class Sha256 {
 public:
  void update(const uint8_t* data, size_t size) {
    total_bytes_ += static_cast<uint64_t>(size);
    while (size != 0U) {
      const size_t copied = std::min(block_.size() - block_size_, size);
      std::copy_n(data, copied, block_.data() + block_size_);
      block_size_ += copied;
      data += copied;
      size -= copied;
      if (block_size_ == block_.size()) {
        transform(block_.data());
        block_size_ = 0U;
      }
    }
  }

  std::array<uint8_t, 32> finish() {
    const uint64_t bit_count = total_bytes_ * 8U;
    block_[block_size_++] = 0x80U;
    if (block_size_ > 56U) {
      std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0U);
      transform(block_.data());
      block_size_ = 0U;
    }
    std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56, 0U);
    for (size_t index = 0; index < 8U; ++index) {
      block_[63U - index] = static_cast<uint8_t>(bit_count >> (8U * index));
    }
    transform(block_.data());
    std::array<uint8_t, 32> digest{};
    for (size_t index = 0; index < 8U; ++index) {
      for (size_t byte = 0; byte < 4U; ++byte) {
        digest[index * 4U + byte] = static_cast<uint8_t>(state_[index] >> (24U - 8U * byte));
      }
    }
    return digest;
  }

 private:
  static uint32_t rotate_right(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32U - shift));
  }

  void transform(const uint8_t* block) {
    static constexpr std::array<uint32_t, 64> kConstants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16U; ++index) {
      words[index] = (static_cast<uint32_t>(block[index * 4U]) << 24U) |
                     (static_cast<uint32_t>(block[index * 4U + 1U]) << 16U) |
                     (static_cast<uint32_t>(block[index * 4U + 2U]) << 8U) |
                     static_cast<uint32_t>(block[index * 4U + 3U]);
    }
    for (size_t index = 16U; index < words.size(); ++index) {
      const uint32_t small0 = rotate_right(words[index - 15U], 7U) ^ rotate_right(words[index - 15U], 18U) ^
                              (words[index - 15U] >> 3U);
      const uint32_t small1 = rotate_right(words[index - 2U], 17U) ^ rotate_right(words[index - 2U], 19U) ^
                              (words[index - 2U] >> 10U);
      words[index] = words[index - 16U] + small0 + words[index - 7U] + small1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (size_t index = 0; index < words.size(); ++index) {
      const uint32_t big1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
      const uint32_t choose = (e & f) ^ (~e & g);
      const uint32_t temp1 = h + big1 + choose + kConstants[index] + words[index];
      const uint32_t big0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = big0 + majority;
      h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  std::array<uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                  0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<uint8_t, 64> block_{};
  size_t block_size_{0U};
  uint64_t total_bytes_{0U};
};

struct Entry {
  std::string relative_path;
  fs::path source_path;
  uint32_t crc32{0U};
  uint32_t bytes{0U};
  std::array<uint8_t, 32> sha256{};
  uint32_t local_offset{0U};
};

bool add_fits(uint64_t left, uint64_t right, uint64_t* result) {
  if (right > std::numeric_limits<uint64_t>::max() - left) return false;
  *result = left + right;
  return true;
}

bool safe_relative_path(const std::string& path) {
  if (path.empty() || path.size() > 4096U || path.front() == '/' || path.find('\\') != std::string::npos ||
      path.find('\0') != std::string::npos) return false;
  size_t position = 0U;
  while (position < path.size()) {
    const size_t slash = path.find('/', position);
    const size_t end = slash == std::string::npos ? path.size() : slash;
    const std::string_view component(path.data() + position, end - position);
    if (component.empty() || component == "." || component == "..") return false;
    if (slash == std::string::npos) return true;
    position = slash + 1U;
  }
  return false;
}

bool parse_unsigned(std::string_view text, uint64_t* value) {
  if (text.empty() || value == nullptr) return false;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, *value);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool regular_file(const fs::path& path, uint64_t* size) {
  std::error_code error;
  const fs::file_status status = fs::symlink_status(path, error);
  if (error || fs::is_symlink(status) || !fs::is_regular_file(status)) return false;
  const uintmax_t bytes = fs::file_size(path, error);
  if (error || bytes > static_cast<uintmax_t>(std::numeric_limits<uint32_t>::max())) return false;
  *size = static_cast<uint64_t>(bytes);
  return true;
}

bool has_no_symlink_components(const fs::path& root, const std::string& relative_path) {
  fs::path current = root;
  for (const fs::path& component : fs::path(relative_path)) {
    current /= component;
    std::error_code error;
    const fs::file_status status = fs::symlink_status(current, error);
    if (error || fs::is_symlink(status)) return false;
  }
  return true;
}

bool calculate_entry(Entry* entry, uint64_t maximum_bytes) {
  uint64_t bytes = 0U;
  if (entry == nullptr || !regular_file(entry->source_path, &bytes) || bytes > maximum_bytes) return false;
  std::ifstream source(entry->source_path, std::ios::binary);
  if (!source) return false;
  std::array<uint8_t, kCopyBufferBytes> buffer{};
  Sha256 hasher;
  uLong crc = crc32(0L, Z_NULL, 0U);
  uint64_t counted = 0U;
  while (source) {
    source.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = source.gcount();
    if (read <= 0) break;
    const size_t count = static_cast<size_t>(read);
    hasher.update(buffer.data(), count);
    crc = crc32(crc, buffer.data(), static_cast<uInt>(count));
    if (!add_fits(counted, count, &counted)) return false;
  }
  if (!source.eof() || counted != bytes) return false;
  entry->bytes = static_cast<uint32_t>(bytes);
  entry->crc32 = static_cast<uint32_t>(crc);
  entry->sha256 = hasher.finish();
  return true;
}

bool write_bytes(std::ofstream* output, const void* bytes, size_t count) {
  output->write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(count));
  return static_cast<bool>(*output);
}

bool write_u16(std::ofstream* output, uint16_t value) {
  const std::array<uint8_t, 2> bytes{static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8U)};
  return write_bytes(output, bytes.data(), bytes.size());
}

bool write_u32(std::ofstream* output, uint32_t value) {
  return write_u16(output, static_cast<uint16_t>(value)) && write_u16(output, static_cast<uint16_t>(value >> 16U));
}

bool write_extra(std::ofstream* output, const Entry& entry) {
  return write_u16(output, kHssSha256ExtraField) && write_u16(output, entry.sha256.size()) &&
         write_bytes(output, entry.sha256.data(), entry.sha256.size());
}

bool write_local_header(std::ofstream* output, const Entry& entry) {
  return write_u32(output, 0x04034b50U) && write_u16(output, kZipVersion) && write_u16(output, kZipFlags) &&
         write_u16(output, kZipStoreMethod) && write_u16(output, 0U) && write_u16(output, 0U) &&
         write_u32(output, entry.crc32) && write_u32(output, entry.bytes) && write_u32(output, entry.bytes) &&
         write_u16(output, static_cast<uint16_t>(entry.relative_path.size())) && write_u16(output, 36U) &&
         write_bytes(output, entry.relative_path.data(), entry.relative_path.size()) && write_extra(output, entry);
}

bool copy_entry(std::ofstream* output, const Entry& entry) {
  std::ifstream source(entry.source_path, std::ios::binary);
  if (!source) return false;
  std::array<uint8_t, kCopyBufferBytes> buffer{};
  Sha256 hasher;
  uLong crc = crc32(0L, Z_NULL, 0U);
  uint64_t counted = 0U;
  while (source) {
    source.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = source.gcount();
    if (read <= 0) break;
    const size_t count = static_cast<size_t>(read);
    if (!write_bytes(output, buffer.data(), count)) return false;
    hasher.update(buffer.data(), count);
    crc = crc32(crc, buffer.data(), static_cast<uInt>(count));
    if (!add_fits(counted, count, &counted)) return false;
  }
  return source.eof() && counted == entry.bytes && static_cast<uint32_t>(crc) == entry.crc32 &&
         hasher.finish() == entry.sha256;
}

bool write_central_header(std::ofstream* output, const Entry& entry) {
  return write_u32(output, 0x02014b50U) && write_u16(output, kZipVersion) && write_u16(output, kZipVersion) &&
         write_u16(output, kZipFlags) && write_u16(output, kZipStoreMethod) && write_u16(output, 0U) &&
         write_u16(output, 0U) && write_u32(output, entry.crc32) && write_u32(output, entry.bytes) &&
         write_u32(output, entry.bytes) && write_u16(output, static_cast<uint16_t>(entry.relative_path.size())) &&
         write_u16(output, 36U) && write_u16(output, 0U) && write_u16(output, 0U) && write_u16(output, 0U) &&
         write_u32(output, 0U) && write_u32(output, entry.local_offset) &&
         write_bytes(output, entry.relative_path.data(), entry.relative_path.size()) && write_extra(output, entry);
}

bool parse_active_manifest(const fs::path& manifest_path, std::set<std::string>* chunks) {
  std::ifstream input(manifest_path, std::ios::binary);
  if (!input) return false;
  input.seekg(0, std::ios::end);
  const std::streamoff length = input.tellg();
  if (length < 0 || static_cast<uint64_t>(length) > kMaximumManifestBytes) return false;
  input.seekg(0, std::ios::beg);
  std::string line;
  if (!std::getline(input, line) || line != "HSS_SOURCE_MANIFEST 1") return false;
  if (!std::getline(input, line) || line.rfind("revision ", 0U) != 0U) return false;
  uint64_t revision = 0U;
  if (!parse_unsigned(std::string_view(line).substr(9U), &revision)) return false;
  if (!std::getline(input, line) || line.rfind("records ", 0U) != 0U) return false;
  uint64_t record_count = 0U;
  if (!parse_unsigned(std::string_view(line).substr(8U), &record_count) || record_count > kMaximumFiles - 2U) return false;
  for (uint64_t index = 0U; index < record_count; ++index) {
    if (!std::getline(input, line)) return false;
    std::istringstream fields(line);
    std::array<std::string, 13> values{};
    for (std::string& value : values) if (!(fields >> value)) return false;
    std::string unexpected;
    if (fields >> unexpected || values[0] != "record" || !safe_relative_path(values[12]) ||
        values[12].rfind("chunks/", 0U) != 0U) return false;
    chunks->insert(values[12]);
  }
  if (!std::getline(input, line) || line.rfind("manifest_sha256 ", 0U) != 0U || line.size() != 80U) return false;
  return !std::getline(input, line);
}

Result invalid_project(std::string message) { return {Status::kInvalidProject, std::move(message)}; }

}  // namespace

Result write_hsscan_archive(const std::string& project_directory, const std::string& destination_path) {
  const fs::path root(project_directory);
  const fs::path destination(destination_path);
  std::error_code error;
  const fs::file_status root_status = fs::symlink_status(root, error);
  if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status)) {
    return invalid_project("The saved project directory is unavailable.");
  }
  if (destination.empty() || destination.filename().empty() || fs::exists(destination, error)) {
    return {Status::kIoError, "The local HSSCAN destination already exists or is unavailable."};
  }
  const fs::path parent = destination.parent_path();
  fs::create_directories(parent, error);
  const fs::file_status parent_status = fs::symlink_status(parent, error);
  if (error || fs::is_symlink(parent_status) || !fs::is_directory(parent_status)) {
    return {Status::kIoError, "The local HSSCAN destination directory is unavailable."};
  }
  const fs::path canonical_root = fs::weakly_canonical(root, error);
  const fs::path canonical_destination = fs::weakly_canonical(destination, error);
  if (error || canonical_destination.string().rfind(canonical_root.string() + "/", 0U) == 0U) {
    return invalid_project("A project archive cannot be written inside its source project.");
  }

  std::set<std::string> selected{"project.json", "active.manifest"};
  if (!parse_active_manifest(root / "active.manifest", &selected)) {
    return invalid_project("The saved project does not have a valid committed active manifest.");
  }
  std::vector<Entry> entries;
  entries.reserve(selected.size());
  uint64_t total_bytes = 0U;
  for (const std::string& relative : selected) {
    const uint64_t limit = relative == "project.json" || relative == "active.manifest"
        ? kMaximumManifestBytes : kMaximumChunkBytes;
    Entry entry{.relative_path = relative, .source_path = root / relative};
    if (!has_no_symlink_components(root, relative) || !calculate_entry(&entry, limit)) {
      return invalid_project("A committed project file is unavailable, linked, or exceeds the export limit.");
    }
    if (!add_fits(total_bytes, entry.bytes, &total_bytes) || total_bytes > kMaximumProjectBytes) {
      return {Status::kLimitExceeded, "The committed project exceeds the HSSCAN export size limit."};
    }
    entries.push_back(std::move(entry));
  }
  if (entries.empty() || entries.size() > std::numeric_limits<uint16_t>::max()) {
    return {Status::kLimitExceeded, "The committed project has too many HSSCAN files."};
  }
  uint64_t archive_bytes = 22U;
  for (const Entry& entry : entries) {
    uint64_t next = 0U;
    if (!add_fits(archive_bytes, 30U + 36U + entry.relative_path.size() + entry.bytes, &next)) {
      return {Status::kLimitExceeded, "The HSSCAN archive size overflowed."};
    }
    archive_bytes = next;
    if (!add_fits(archive_bytes, 46U + 36U + entry.relative_path.size(), &next)) {
      return {Status::kLimitExceeded, "The HSSCAN archive size overflowed."};
    }
    archive_bytes = next;
  }
  if (archive_bytes > std::numeric_limits<uint32_t>::max()) {
    return {Status::kLimitExceeded, "HSSCAN exports are limited to 4 GiB because ZIP64 imports are unsupported."};
  }

  fs::path staging;
  for (uint32_t attempt = 0U; attempt < 8U; ++attempt) {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    staging = parent / ("." + destination.filename().string() + ".pending-" + std::to_string(getpid()) + "-" +
                        std::to_string(static_cast<unsigned long long>(tick) + attempt));
    if (!fs::exists(staging, error)) break;
  }
  if (staging.empty() || fs::exists(staging, error)) {
    return {Status::kIoError, "A private HSSCAN staging file could not be created."};
  }
  bool published = false;
  const auto cleanup = [&] { if (!published) fs::remove(staging, error); };
  std::ofstream output(staging, std::ios::binary | std::ios::trunc);
  if (!output) return {Status::kIoError, "A private HSSCAN staging file could not be opened."};
  for (Entry& entry : entries) {
    const std::streampos position = output.tellp();
    if (position < 0 || static_cast<uint64_t>(position) > std::numeric_limits<uint32_t>::max()) {
      output.close(); cleanup();
      return {Status::kLimitExceeded, "The HSSCAN archive exceeds the classic ZIP offset limit."};
    }
    entry.local_offset = static_cast<uint32_t>(position);
    if (!write_local_header(&output, entry) || !copy_entry(&output, entry)) {
      output.close(); cleanup();
      return {Status::kIoError, "The committed project changed or could not be read during HSSCAN export."};
    }
  }
  const std::streampos central_position = output.tellp();
  if (central_position < 0 || static_cast<uint64_t>(central_position) > std::numeric_limits<uint32_t>::max()) {
    output.close(); cleanup();
    return {Status::kLimitExceeded, "The HSSCAN archive exceeds the classic ZIP offset limit."};
  }
  const uint32_t central_offset = static_cast<uint32_t>(central_position);
  for (const Entry& entry : entries) {
    if (!write_central_header(&output, entry)) {
      output.close(); cleanup();
      return {Status::kIoError, "The HSSCAN central directory could not be written."};
    }
  }
  const std::streampos after_central = output.tellp();
  const uint64_t central_size = after_central < central_position ? 0U : static_cast<uint64_t>(after_central - central_position);
  if (central_size > std::numeric_limits<uint32_t>::max() ||
      !write_u32(&output, 0x06054b50U) || !write_u16(&output, 0U) || !write_u16(&output, 0U) ||
      !write_u16(&output, static_cast<uint16_t>(entries.size())) || !write_u16(&output, static_cast<uint16_t>(entries.size())) ||
      !write_u32(&output, static_cast<uint32_t>(central_size)) || !write_u32(&output, central_offset) || !write_u16(&output, 0U)) {
    output.close(); cleanup();
    return {Status::kIoError, "The HSSCAN archive trailer could not be written."};
  }
  output.close();
  if (!output) { cleanup(); return {Status::kIoError, "The HSSCAN staging file could not be finalized."}; }
  fs::create_hard_link(staging, destination, error);
  if (error) { cleanup(); return {Status::kIoError, "The finished HSSCAN archive could not be published."}; }
  published = true;
  fs::remove(staging, error);
  return {Status::kOk, {}};
}

Result write_portable_hsscan_archive(const std::string& project_directory, const std::string& destination_path) {
  const fs::path source(project_directory), destination(destination_path);
  std::error_code error;
  if (destination.empty() || destination.filename().empty() || fs::exists(destination, error))
    return {Status::kIoError, "The portable archive destination already exists or is unavailable."};
  const auto status = fs::symlink_status(source, error);
  if (error || fs::is_symlink(status) || !fs::is_directory(status))
    return invalid_project("The saved project directory is unavailable.");
  std::set<std::string> sources{"project.json", "active.manifest"};
  if (!has_no_symlink_components(source, "active.manifest") ||
      !parse_active_manifest(source / "active.manifest", &sources))
    return invalid_project("The committed project has no valid active source inventory.");
  for (const std::string& relative : sources) {
    uint64_t bytes = 0U;
    if (!has_no_symlink_components(source, relative) || !regular_file(source / relative, &bytes) ||
        bytes > (relative == "project.json" || relative == "active.manifest" ? kMaximumManifestBytes : kMaximumChunkBytes))
      return invalid_project("The committed source inventory is linked, unavailable, or oversized.");
  }
  fs::create_directories(destination.parent_path(), error);
  const auto parent_status = fs::symlink_status(destination.parent_path(), error);
  if (error || fs::is_symlink(parent_status) || !fs::is_directory(parent_status))
    return {Status::kIoError, "The portable archive destination directory is unavailable."};
  const auto canonical_source = fs::weakly_canonical(source, error);
  if (error) return invalid_project("The saved project directory is unavailable.");
  const auto canonical_parent = fs::weakly_canonical(destination.parent_path(), error);
  if (error || canonical_parent == canonical_source || canonical_parent.string().rfind(canonical_source.string() + "/", 0U) == 0U)
    return invalid_project("A portable archive cannot be staged inside its source project.");
  std::string pattern = (canonical_parent / ".hss-portable-XXXXXX").string();
  std::vector<char> path(pattern.begin(), pattern.end());
  path.push_back('\0');
  const char* created = ::mkdtemp(path.data());
  if (created == nullptr) return {Status::kIoError, "Private portable archive staging could not be created."};
  struct PrivateStage {
    fs::path path;
    ~PrivateStage() { std::error_code ignored; fs::remove_all(path, ignored); }
  } stage{fs::path(created)};
  storage::NativeFileSystem original(source.string()), portable(stage.path.string());
  storage::RetentionLimits limits;
  limits.max_epoch_records = 1'024;
  limits.max_depth_records = 8'192;
  limits.max_mesh_records = 8'192;
  limits.max_command_records = 1'024;
  limits.max_retained_bytes = kMaximumProjectBytes;
  limits.max_chunk_bytes = kMaximumChunkBytes;
  storage::DurableSourceStore store(original, limits);
  const auto copied = store.write_portable_copy(portable);
  if (copied != storage::StoreStatus::kOk)
    return invalid_project("The committed project could not produce a validated clock-relative archive. The local source was preserved.");
  return write_hsscan_archive(stage.path.string(), destination_path);
}

}  // namespace hss::native_export
