#ifndef HSS_PROJECT_ARCHIVE_IMPORT_HPP
#define HSS_PROJECT_ARCHIVE_IMPORT_HPP

#include <string>

namespace hss::native_import {

// Extracts one HSSCAN ZIP archive only after every entry has passed the
// portable B-039 validation gate. The archive must use ZIP's stored or raw
// deflate methods and every regular entry must carry its uncompressed SHA-256
// in the HSSCAN extra field (header ID 0x4853, 32 byte payload). The caller
// supplies a new private staging directory; no project is adopted here.
enum class Status : unsigned char {
  kOk,
  kInvalidArgument,
  kUnsupportedArchive,
  kValidationFailed,
  kExtractionFailed,
  kIoError,
};

struct Result {
  Status status{Status::kInvalidArgument};
  std::string message;
};

Result extract_hsscan_archive(const std::string& archive_path,
                              const std::string& staging_directory);

}  // namespace hss::native_import

#endif
