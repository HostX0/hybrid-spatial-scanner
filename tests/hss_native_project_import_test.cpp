#include "HSSProjectArchiveImport.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <zlib.h>

namespace {

constexpr std::array<uint8_t, 32> kAbcSha256{
    0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
    0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x22U, 0x23U,
    0xb0U, 0x03U, 0x61U, 0xa3U, 0x96U, 0x17U, 0x7aU, 0x9cU,
    0xb4U, 0x10U, 0xffU, 0x61U, 0xf2U, 0x00U, 0x15U, 0xadU};

struct Entry {
  std::string path;
  std::vector<uint8_t> bytes;
  std::array<uint8_t, 32> sha{kAbcSha256};
  uint16_t method{0};
  uint32_t external_attributes{0};
  uint16_t made_by{20};
};

void u16(std::vector<uint8_t>* bytes, uint16_t value) {
  bytes->push_back(static_cast<uint8_t>(value));
  bytes->push_back(static_cast<uint8_t>(value >> 8U));
}

void u32(std::vector<uint8_t>* bytes, uint32_t value) {
  u16(bytes, static_cast<uint16_t>(value));
  u16(bytes, static_cast<uint16_t>(value >> 16U));
}

std::vector<uint8_t> deflate_raw(const std::vector<uint8_t>& input) {
  z_stream stream{};
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    std::abort();
  }
  std::vector<uint8_t> output(compressBound(input.size()));
  stream.next_in = const_cast<Bytef*>(input.data());
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = output.data();
  stream.avail_out = static_cast<uInt>(output.size());
  if (deflate(&stream, Z_FINISH) != Z_STREAM_END) std::abort();
  output.resize(output.size() - stream.avail_out);
  deflateEnd(&stream);
  return output;
}

void write_zip(const std::filesystem::path& path, const std::vector<Entry>& entries) {
  std::vector<uint8_t> bytes;
  struct Central { Entry entry; std::vector<uint8_t> payload; uint32_t offset; };
  std::vector<Central> central;
  for (const Entry& entry : entries) {
    const std::vector<uint8_t> payload = entry.method == 8U ? deflate_raw(entry.bytes) : entry.bytes;
    const uint32_t offset = static_cast<uint32_t>(bytes.size());
    u32(&bytes, 0x04034b50U); u16(&bytes, 20U); u16(&bytes, 0U); u16(&bytes, entry.method);
    u16(&bytes, 0U); u16(&bytes, 0U); u32(&bytes, 0U);
    u32(&bytes, static_cast<uint32_t>(payload.size())); u32(&bytes, static_cast<uint32_t>(entry.bytes.size()));
    u16(&bytes, static_cast<uint16_t>(entry.path.size())); u16(&bytes, 36U);
    bytes.insert(bytes.end(), entry.path.begin(), entry.path.end());
    u16(&bytes, 0x4853U); u16(&bytes, 32U); bytes.insert(bytes.end(), entry.sha.begin(), entry.sha.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    central.push_back({entry, payload, offset});
  }
  const uint32_t central_offset = static_cast<uint32_t>(bytes.size());
  for (const Central& item : central) {
    const Entry& entry = item.entry;
    u32(&bytes, 0x02014b50U); u16(&bytes, entry.made_by); u16(&bytes, 20U); u16(&bytes, 0U); u16(&bytes, entry.method);
    u16(&bytes, 0U); u16(&bytes, 0U); u32(&bytes, 0U);
    u32(&bytes, static_cast<uint32_t>(item.payload.size())); u32(&bytes, static_cast<uint32_t>(entry.bytes.size()));
    u16(&bytes, static_cast<uint16_t>(entry.path.size())); u16(&bytes, 36U); u16(&bytes, 0U); u16(&bytes, 0U);
    u16(&bytes, 0U); u32(&bytes, entry.external_attributes); u32(&bytes, item.offset);
    bytes.insert(bytes.end(), entry.path.begin(), entry.path.end());
    u16(&bytes, 0x4853U); u16(&bytes, 32U); bytes.insert(bytes.end(), entry.sha.begin(), entry.sha.end());
  }
  const uint32_t central_size = static_cast<uint32_t>(bytes.size()) - central_offset;
  u32(&bytes, 0x06054b50U); u16(&bytes, 0U); u16(&bytes, 0U);
  u16(&bytes, static_cast<uint16_t>(entries.size())); u16(&bytes, static_cast<uint16_t>(entries.size()));
  u32(&bytes, central_size); u32(&bytes, central_offset); u16(&bytes, 0U);
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!output) std::abort();
}

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void check_rejected(const std::filesystem::path& archive, const std::filesystem::path& staging) {
  const auto result = hss::native_import::extract_hsscan_archive(archive.string(), staging.string());
  check(result.status != hss::native_import::Status::kOk, "hostile archive must be rejected");
  check(!std::filesystem::exists(staging), "rejected archive must not publish staging files");
}

}  // namespace

int main() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "hss-native-project-import-test";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root, error);
  check(!error, "create test root");

  const std::vector<uint8_t> abc{'a', 'b', 'c'};
  Entry stored{"project.json", abc};
  Entry deflated{"chunks/source.bin", abc};
  deflated.method = 8U;
  const auto good = root / "good.hsscan";
  const auto staging = root / "staging";
  write_zip(good, {stored, deflated});
  const auto result = hss::native_import::extract_hsscan_archive(good.string(), staging.string());
  if (result.status != hss::native_import::Status::kOk) std::cerr << result.message << '\n';
  check(result.status == hss::native_import::Status::kOk, "stored and deflated archive is staged");
  std::ifstream staged(staging / "chunks/source.bin", std::ios::binary);
  std::string staged_bytes((std::istreambuf_iterator<char>(staged)), std::istreambuf_iterator<char>());
  check(staged_bytes == "abc", "staged bytes match validated archive input");

  write_zip(root / "traversal.hsscan", {Entry{"../project.json", abc}});
  check_rejected(root / "traversal.hsscan", root / "traversal-stage");
  Entry incorrect{"project.json", abc}; incorrect.sha.fill(0U);
  write_zip(root / "checksum.hsscan", {incorrect});
  check_rejected(root / "checksum.hsscan", root / "checksum-stage");
  Entry link{"project.json", abc}; link.made_by = static_cast<uint16_t>((3U << 8U) | 20U); link.external_attributes = 0120000U << 16U;
  write_zip(root / "link.hsscan", {link});
  check_rejected(root / "link.hsscan", root / "link-stage");
  write_zip(root / "duplicate.hsscan", {stored, stored});
  check_rejected(root / "duplicate.hsscan", root / "duplicate-stage");

  std::filesystem::remove_all(root, error);
  std::cout << "native ZIP staging rejects hostile entries and publishes only fully validated files\n";
}
