#pragma once

#include <string>

namespace hss::native_export {

enum class Status {
  kOk = 0,
  kInvalidProject,
  kLimitExceeded,
  kIoError,
};

struct Result {
  Status status{Status::kOk};
  std::string message;
};

// Writes a local HSSCAN archive from one already-committed project directory.
// The archive uses classic, store-only ZIP entries and carries the SHA-256 of
// every file in the importer-required 0x4853 ZIP extra field. Only the active
// manifest and its referenced immutable chunks are included; temporary files,
// prior exports, symlinks, and unreferenced files are never archived.
Result write_hsscan_archive(const std::string& project_directory,
                            const std::string& destination_path);

// Shipping share path: creates a private, temporary clock-relative derivative
// using the existing source encoders, then the ordinary validated ZIP writer.
// Source project bytes never change. The source clock origin is not exported.
Result write_portable_hsscan_archive(const std::string& project_directory,
                                     const std::string& destination_path);

}  // namespace hss::native_export
