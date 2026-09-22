#include "hss/project_event_encoder.hpp"

#include "hss/coordinate_basis.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace hss::project {
namespace {

constexpr std::array<uint8_t, 8> kMagic{'H', 'S', 'S', 'C', 'T', 'R', 'L', '1'};
constexpr uint16_t kCommand = 1;
constexpr uint16_t kPlanReference = 2;
constexpr uint64_t kFixedHeaderBytes = 72;

bool zero_id(const HSSId &id) {
  for (uint8_t byte : id.bytes)
    if (byte != 0U)
      return false;
  return true;
}

class Writer {
public:
  void u8(uint8_t value) { bytes.push_back(value); }
  void u16(uint16_t value) {
    for (unsigned i = 0; i < 2; ++i)
      u8(static_cast<uint8_t>(value >> (8U * i)));
  }
  void u32(uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
      u8(static_cast<uint8_t>(value >> (8U * i)));
  }
  void u64(uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
      u8(static_cast<uint8_t>(value >> (8U * i)));
  }
  void f64(double value) { u64(std::bit_cast<uint64_t>(value)); }
  void raw(const uint8_t *data, size_t size) {
    bytes.insert(bytes.end(), data, data + size);
  }
  std::vector<uint8_t> bytes;
};

class Reader {
public:
  explicit Reader(const std::vector<uint8_t> &value) : bytes(value) {}
  bool raw(uint8_t *out, size_t size) {
    if (size > bytes.size() - offset)
      return false;
    std::memcpy(out, bytes.data() + offset, size);
    offset += size;
    return true;
  }
  bool u16(uint16_t *value) {
    uint64_t v = 0;
    if (!integer(2, &v))
      return false;
    *value = static_cast<uint16_t>(v);
    return true;
  }
  bool u32(uint32_t *value) {
    uint64_t v = 0;
    if (!integer(4, &v))
      return false;
    *value = static_cast<uint32_t>(v);
    return true;
  }
  bool u64(uint64_t *value) { return integer(8, value); }
  bool f64(double *value) {
    uint64_t bits = 0;
    if (!u64(&bits))
      return false;
    *value = std::bit_cast<double>(bits);
    return true;
  }
  bool done() const { return offset == bytes.size(); }
  size_t remaining() const { return bytes.size() - offset; }
  const uint8_t *current() const { return bytes.data() + offset; }
  void skip(size_t size) { offset += size; }

private:
  bool integer(size_t count, uint64_t *value) {
    if (count > bytes.size() - offset)
      return false;
    *value = 0;
    for (size_t i = 0; i < count; ++i)
      *value |= static_cast<uint64_t>(bytes[offset++]) << (8U * i);
    return true;
  }
  const std::vector<uint8_t> &bytes;
  size_t offset{0};
};

bool valid_context(const Context &context) {
  return !zero_id(context.capture_id) && context.event_sequence != 0U &&
         std::isfinite(context.monotonic_time_s) &&
         context.monotonic_time_s >= 0.0;
}

void write_common(Writer &writer, uint16_t type, const Context &context,
                  uint64_t payload_bytes) {
  writer.raw(kMagic.data(), kMagic.size());
  writer.u16(1U);
  writer.u16(0U);
  writer.u16(type);
  writer.u16(0U);
  writer.u64(kFixedHeaderBytes + payload_bytes);
  writer.raw(context.capture_id.bytes, sizeof(context.capture_id.bytes));
  writer.u64(context.event_sequence);
  writer.u64(context.accepted_scene_revision);
  writer.u32(context.session_epoch);
  writer.u32(0U);
  writer.f64(context.monotonic_time_s);
}

bool read_common(Reader &reader, uint16_t *type, Context *context,
                 uint64_t *total) {
  std::array<uint8_t, 8> magic{};
  uint16_t major = 0, minor = 0, reserved16 = 0;
  uint32_t reserved32 = 0;
  return reader.raw(magic.data(), magic.size()) && magic == kMagic &&
         reader.u16(&major) && reader.u16(&minor) && reader.u16(type) &&
         reader.u16(&reserved16) && reader.u64(total) &&
         reader.raw(context->capture_id.bytes,
                    sizeof(context->capture_id.bytes)) &&
         reader.u64(&context->event_sequence) &&
         reader.u64(&context->accepted_scene_revision) &&
         reader.u32(&context->session_epoch) && reader.u32(&reserved32) &&
         reader.f64(&context->monotonic_time_s) && major == 1U && minor == 0U &&
         reserved16 == 0U && reserved32 == 0U && valid_context(*context);
}

} // namespace

bool valid_utf8(const uint8_t *data, uint64_t length) {
  if ((data == nullptr && length != 0U) ||
      length > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    return false;
  size_t i = 0, size = static_cast<size_t>(length);
  while (i < size) {
    uint8_t c = data[i++];
    if (c == 0U)
      return false;
    if (c < 0x80U)
      continue;
    unsigned need = 0;
    uint32_t code = 0, minimum = 0;
    if ((c & 0xe0U) == 0xc0U) {
      need = 1;
      code = c & 0x1fU;
      minimum = 0x80U;
    } else if ((c & 0xf0U) == 0xe0U) {
      need = 2;
      code = c & 0x0fU;
      minimum = 0x800U;
    } else if ((c & 0xf8U) == 0xf0U) {
      need = 3;
      code = c & 0x07U;
      minimum = 0x10000U;
    } else
      return false;
    if (need > size - i)
      return false;
    for (unsigned n = 0; n < need; ++n) {
      uint8_t t = data[i++];
      if ((t & 0xc0U) != 0x80U)
        return false;
      code = (code << 6U) | (t & 0x3fU);
    }
    if (code < minimum || code > 0x10ffffU ||
        (code >= 0xd800U && code <= 0xdfffU))
      return false;
  }
  return true;
}

ControlStatus encode_control(const Control &control, uint64_t max_record_bytes,
                             std::vector<uint8_t> *bytes) {
  if (bytes == nullptr || max_record_bytes < kFixedHeaderBytes)
    return ControlStatus::kInvalidArgument;
  Writer writer;
  if (const auto *command = std::get_if<CommandControl>(&control)) {
    if (!valid_context(command->context) ||
        command->after_scene_revision < command->before_scene_revision ||
        command->command_json.empty() ||
        !valid_utf8(command->command_json.data(),
                    command->command_json.size()) ||
        !valid_utf8(command->result_json.data(), command->result_json.size()) ||
        command->command_json.size() > std::numeric_limits<uint64_t>::max() -
                                           command->result_json.size() - 32U) {
      return ControlStatus::kInvalidArgument;
    }
    const uint64_t payload =
        32U + command->command_json.size() + command->result_json.size();
    if (payload > max_record_bytes - kFixedHeaderBytes)
      return ControlStatus::kResourceLimit;
    write_common(writer, kCommand, command->context, payload);
    writer.u64(command->before_scene_revision);
    writer.u64(command->after_scene_revision);
    writer.u64(command->command_json.size());
    writer.u64(command->result_json.size());
    writer.raw(command->command_json.data(), command->command_json.size());
    writer.raw(command->result_json.data(), command->result_json.size());
  } else {
    const auto &plan = std::get<PlanReferenceControl>(control);
    constexpr uint64_t payload = 19U * 8U;
    if (!valid_context(plan.context) ||
        !hss::coordinate::is_rigid_transform(plan.project_from_floor) ||
        !std::isfinite(plan.cut_height_m) || plan.cut_height_m <= 0.0 ||
        plan.after_scene_revision < plan.before_scene_revision)
      return ControlStatus::kInvalidArgument;
    if (payload > max_record_bytes - kFixedHeaderBytes)
      return ControlStatus::kResourceLimit;
    write_common(writer, kPlanReference, plan.context, payload);
    writer.u64(plan.before_scene_revision);
    writer.u64(plan.after_scene_revision);
    for (double value : plan.project_from_floor.column_major)
      writer.f64(value);
    writer.f64(plan.cut_height_m);
  }
  *bytes = std::move(writer.bytes);
  return ControlStatus::kOk;
}

DecodedControl decode_control(const std::vector<uint8_t> &bytes,
                              uint64_t max_record_bytes) {
  DecodedControl result;
  if (bytes.size() > max_record_bytes || bytes.size() < kFixedHeaderBytes) {
    result.status = bytes.size() > max_record_bytes
                        ? ControlStatus::kResourceLimit
                        : ControlStatus::kCorrupt;
    return result;
  }
  Reader reader(bytes);
  uint16_t type = 0;
  uint64_t total = 0;
  Context context;
  if (!read_common(reader, &type, &context, &total) || total != bytes.size())
    return result;
  if (type == kCommand) {
    CommandControl command;
    command.context = context;
    uint64_t command_size = 0, result_size = 0;
    if (!reader.u64(&command.before_scene_revision) ||
        !reader.u64(&command.after_scene_revision) ||
        !reader.u64(&command_size) || !reader.u64(&result_size) ||
        command_size > reader.remaining() ||
        result_size > reader.remaining() - command_size ||
        command_size + result_size != reader.remaining() ||
        command_size == 0U ||
        command.after_scene_revision < command.before_scene_revision ||
        command_size >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
        result_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
      return result;
    command.command_json.assign(
        reader.current(), reader.current() + static_cast<size_t>(command_size));
    reader.skip(command_size);
    command.result_json.assign(
        reader.current(), reader.current() + static_cast<size_t>(result_size));
    reader.skip(result_size);
    if (!reader.done() ||
        !valid_utf8(command.command_json.data(), command.command_json.size()) ||
        !valid_utf8(command.result_json.data(), command.result_json.size()))
      return result;
    result.control = std::move(command);
  } else if (type == kPlanReference) {
    PlanReferenceControl plan;
    plan.context = context;
    if (!reader.u64(&plan.before_scene_revision) ||
        !reader.u64(&plan.after_scene_revision))
      return result;
    for (double &value : plan.project_from_floor.column_major)
      if (!reader.f64(&value))
        return result;
    if (!reader.f64(&plan.cut_height_m) || !reader.done() ||
        plan.after_scene_revision < plan.before_scene_revision ||
        !std::isfinite(plan.cut_height_m) || plan.cut_height_m <= 0.0 ||
        !hss::coordinate::is_rigid_transform(plan.project_from_floor))
      return result;
    result.control = plan;
  } else
    return result;
  result.status = ControlStatus::kOk;
  return result;
}

} // namespace hss::project
