#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hss::privacy {

// This module is a decision contract only. It has no filesystem or network
// access, so evaluating a policy cannot itself copy a capture anywhere.
inline constexpr bool kAutomaticUploadEnabled = false;

enum class ArtifactKind : uint8_t {
  kCapture,
  kProject,
  kExport,
  kDiagnostics,
};

struct Artifact {
  std::string id;
  ArtifactKind kind{ArtifactKind::kCapture};
};

enum class Destination : uint8_t {
  kUserSelectedFile,
  kExternalShare,
};

struct ShareRequest {
  std::vector<std::string> artifact_ids;
  Destination destination{Destination::kExternalShare};
  bool explicit_user_confirmation{false};
};

enum class Status : uint8_t {
  kOk,
  kInvalidArtifact,
  kEmptySelection,
  kMissingExplicitConfirmation,
  kInvalidDestination,
};

enum class Action : uint8_t {
  kKeepLocal,
  kPrepareExplicitShare,
};

struct Decision {
  Status status{Status::kInvalidArtifact};
  Action action{Action::kKeepLocal};
  bool requires_preview{false};
  bool permits_network_transfer{false};
};

// Capture, save, reopen and ordinary export paths must use this decision.
// It is deliberately deterministic and cannot authorize a transfer.
[[nodiscard]] Decision automatic_capture_decision(const Artifact &artifact);

// A future share UI may ask for a decision after showing the selected files.
// Explicit confirmation is required for every non-local destination. This
// function still performs no transfer and does not imply OS backup control.
[[nodiscard]] Decision explicit_share_decision(const ShareRequest &request,
                                               const std::vector<Artifact> &available);

[[nodiscard]] const char *status_name(Status status);
[[nodiscard]] const char *action_name(Action action);

}  // namespace hss::privacy
