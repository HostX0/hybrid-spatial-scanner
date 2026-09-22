#include "hss/privacy_policy.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
}  // namespace

int main() {
  using namespace hss::privacy;
  const Artifact capture{"capture-1", ArtifactKind::kCapture};
  const Artifact project{"project-1", ArtifactKind::kProject};
  const std::vector<Artifact> available{capture, project};

  check(!kAutomaticUploadEnabled, "automatic upload is disabled by the contract");
  const Decision local = automatic_capture_decision(capture);
  check(local.status == Status::kOk && local.action == Action::kKeepLocal &&
            !local.requires_preview && !local.permits_network_transfer,
        "capture remains local without a share action");
  check(automatic_capture_decision({"", ArtifactKind::kExport}).status ==
            Status::kInvalidArtifact,
        "invalid artifacts cannot enter the local policy");

  const ShareRequest unconfirmed{{"project-1"}, Destination::kExternalShare, false};
  const Decision blocked = explicit_share_decision(unconfirmed, available);
  check(blocked.status == Status::kMissingExplicitConfirmation &&
            blocked.action == Action::kKeepLocal,
        "share without explicit confirmation remains local");

  const ShareRequest selected{{"project-1"}, Destination::kUserSelectedFile, true};
  const Decision file = explicit_share_decision(selected, available);
  check(file.status == Status::kOk && file.action == Action::kPrepareExplicitShare &&
            file.requires_preview && !file.permits_network_transfer,
        "confirmed file export is scoped and previewed");

  const ShareRequest external{{"capture-1", "project-1"}, Destination::kExternalShare, true};
  const Decision share = explicit_share_decision(external, available);
  check(share.status == Status::kOk && share.permits_network_transfer,
        "external transfer is only prepared after explicit confirmation");
  check(explicit_share_decision({{"missing"}, Destination::kExternalShare, true}, available).status ==
            Status::kInvalidArtifact,
        "unavailable artifacts cannot be shared");
  check(explicit_share_decision({{}, Destination::kExternalShare, true}, available).status ==
            Status::kEmptySelection,
        "empty sharing selection is rejected");

  if (failures != 0) return 1;
  std::cout << "Privacy policy tests passed\n";
  return 0;
}
