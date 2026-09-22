#include "hss/privacy_policy.hpp"

#include <algorithm>

namespace hss::privacy {
namespace {

bool valid_artifact(const Artifact &artifact) {
  return !artifact.id.empty() &&
         std::all_of(artifact.id.begin(), artifact.id.end(), [](unsigned char value) {
           return value >= 0x21U && value <= 0x7eU;
         });
}

bool has_id(const std::vector<Artifact> &available, const std::string &id) {
  return std::any_of(available.begin(), available.end(), [&id](const Artifact &artifact) {
    return artifact.id == id && valid_artifact(artifact);
  });
}

}  // namespace

Decision automatic_capture_decision(const Artifact &artifact) {
  if (!valid_artifact(artifact)) return {Status::kInvalidArtifact, Action::kKeepLocal, false, false};
  return {Status::kOk, Action::kKeepLocal, false, false};
}

Decision explicit_share_decision(const ShareRequest &request,
                                 const std::vector<Artifact> &available) {
  if (request.artifact_ids.empty())
    return {Status::kEmptySelection, Action::kKeepLocal, false, false};
  if (!request.explicit_user_confirmation)
    return {Status::kMissingExplicitConfirmation, Action::kKeepLocal, false, false};
  if (request.destination != Destination::kUserSelectedFile &&
      request.destination != Destination::kExternalShare)
    return {Status::kInvalidDestination, Action::kKeepLocal, false, false};
  for (const std::string &id : request.artifact_ids) {
    if (!has_id(available, id))
      return {Status::kInvalidArtifact, Action::kKeepLocal, false, false};
  }
  // Network behavior belongs to the OS file/share provider. The app has only
  // established that the user approved this exact selection and destination.
  return {Status::kOk, Action::kPrepareExplicitShare, true,
          request.destination == Destination::kExternalShare};
}

const char *status_name(Status status) {
  switch (status) {
    case Status::kOk: return "ok";
    case Status::kInvalidArtifact: return "invalid_artifact";
    case Status::kEmptySelection: return "empty_selection";
    case Status::kMissingExplicitConfirmation: return "missing_explicit_confirmation";
    case Status::kInvalidDestination: return "invalid_destination";
  }
  return "unknown";
}

const char *action_name(Action action) {
  switch (action) {
    case Action::kKeepLocal: return "keep_local";
    case Action::kPrepareExplicitShare: return "prepare_explicit_share";
  }
  return "unknown";
}

}  // namespace hss::privacy
