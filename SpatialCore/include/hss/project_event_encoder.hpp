#pragma once

#include "hss/hss_project_store.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace hss::project {

struct Context {
  HSSId capture_id{};
  uint64_t event_sequence{0};
  uint64_t accepted_scene_revision{0};
  uint32_t session_epoch{0};
  double monotonic_time_s{0.0};
};

struct CommandControl {
  Context context;
  uint64_t before_scene_revision{0};
  uint64_t after_scene_revision{0};
  std::vector<uint8_t> command_json;
  std::vector<uint8_t> result_json;
};

struct PlanReferenceControl {
  Context context;
  HSSMat4d project_from_floor{};
  double cut_height_m{0.0};
  uint64_t before_scene_revision{0};
  uint64_t after_scene_revision{0};
};

using Control = std::variant<CommandControl, PlanReferenceControl>;

enum class ControlStatus : uint8_t {
  kOk,
  kInvalidArgument,
  kResourceLimit,
  kCorrupt
};

struct DecodedControl {
  ControlStatus status{ControlStatus::kCorrupt};
  Control control{CommandControl{}};
};

ControlStatus encode_control(const Control &control, uint64_t max_record_bytes,
                             std::vector<uint8_t> *bytes);
DecodedControl decode_control(const std::vector<uint8_t> &bytes,
                              uint64_t max_record_bytes);
bool valid_utf8(const uint8_t *data, uint64_t length);

} // namespace hss::project
