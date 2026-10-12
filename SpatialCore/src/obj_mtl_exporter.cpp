#include "hss/obj_mtl_exporter.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <string_view>

namespace hss::obj_mtl {
namespace {

Result failure(Status status) { return {status, std::nullopt}; }

bool stable_identifier(std::string_view value) {
  if (value.empty() || value.size() > 128U)
    return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-' ||
           character == '_' || character == '.';
  });
}

bool valid(AppearanceState state) {
  switch (state) {
  case AppearanceState::kNone:
  case AppearanceState::kUnknown:
  case AppearanceState::kRejected:
  case AppearanceState::kVerified:
    return true;
  }
  return false;
}

bool valid_options(const Options &options) {
  const auto &name = options.material_library_filename;
  if (name.empty() || name.size() > 96 || !name.ends_with(".mtl") ||
      !std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
      }) || name.front() == '.' || name.find("..") != std::string::npos)
    return false;
  return options.max_vertices > 0U && options.max_vertices <= kHardMaximumVertices &&
         options.max_faces > 0U && options.max_faces <= kHardMaximumFaces &&
         options.max_texture_coordinates <= kHardMaximumTextureCoordinates &&
         options.max_appearance_slots <= kHardMaximumAppearanceSlots &&
         options.max_warnings <= 4'096U && std::isfinite(options.max_abs_coordinate_m) &&
         options.max_abs_coordinate_m > 0.0;
}

bool finite_vertex(const Vertex &vertex, const Options &options) {
  return std::isfinite(vertex.x_m) && std::isfinite(vertex.y_m) &&
         std::isfinite(vertex.z_m) && std::fabs(vertex.x_m) <= options.max_abs_coordinate_m &&
         std::fabs(vertex.y_m) <= options.max_abs_coordinate_m &&
         std::fabs(vertex.z_m) <= options.max_abs_coordinate_m;
}

bool finite_texture_coordinate(const TextureCoordinate &coordinate) {
  return std::isfinite(coordinate.u) && std::isfinite(coordinate.v) &&
         coordinate.u >= 0.0 && coordinate.u <= 1.0 && coordinate.v >= 0.0 &&
         coordinate.v <= 1.0;
}

bool valid_sha256(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool valid_relative_path(std::string_view path) {
  if (path.empty() || path.size() > 1'024U || path.front() == '/' ||
      path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) {
    return false;
  }
  size_t start = 0U;
  while (start < path.size()) {
    const size_t end = path.find('/', start);
    const size_t length = (end == std::string_view::npos ? path.size() : end) - start;
    const std::string_view segment = path.substr(start, length);
    if (segment.empty() || segment == "." || segment == "..")
      return false;
    start = end == std::string_view::npos ? path.size() : end + 1U;
  }
  return true;
}

bool valid_texture(const TextureReference &texture) {
  return stable_identifier(texture.local_asset_id) &&
         valid_relative_path(texture.bundle_relative_path) &&
         valid_sha256(texture.sha256_hex);
}

std::string number(double value) {
  if (value == 0.0)
    value = 0.0;
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setprecision(17) << value;
  return output.str();
}

std::string json_escape(std::string_view value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  for (const unsigned char character : value) {
    switch (character) {
    case '"': output << "\\\""; break;
    case '\\': output << "\\\\"; break;
    case '\b': output << "\\b"; break;
    case '\f': output << "\\f"; break;
    case '\n': output << "\\n"; break;
    case '\r': output << "\\r"; break;
    case '\t': output << "\\t"; break;
    default:
      if (character < 0x20U) {
        output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<unsigned int>(character) << std::dec
               << std::setfill(' ');
      } else {
        output << static_cast<char>(character);
      }
    }
  }
  return output.str();
}

void json_string(std::string &output, std::string_view value) {
  output.push_back('"');
  output.append(json_escape(value));
  output.push_back('"');
}

void append_string_array(std::string &output, const std::vector<std::string> &values) {
  output.push_back('[');
  for (size_t index = 0U; index < values.size(); ++index) {
    if (index != 0U)
      output.push_back(',');
    json_string(output, values[index]);
  }
  output.push_back(']');
}

std::string material_name(std::string_view target_id) {
  return "hss_appearance_" + std::string(target_id);
}

Status validate_snapshot(const Snapshot &snapshot, const Options &options,
                         std::map<std::string, const AppearanceSlot *> *slots) {
  if (!stable_identifier(snapshot.export_id) || !stable_identifier(snapshot.project_id) ||
      snapshot.scene_revision == 0U || !stable_identifier(snapshot.captured_mesh_id) ||
      !stable_identifier(snapshot.quality_state) || snapshot.vertices_m.empty() ||
      snapshot.faces.empty() || snapshot.vertices_m.size() > options.max_vertices ||
      snapshot.faces.size() > options.max_faces ||
      snapshot.texture_coordinates.size() > options.max_texture_coordinates ||
      snapshot.appearance_slots.size() > options.max_appearance_slots ||
      snapshot.warnings.size() > options.max_warnings) {
    return Status::kInvalidSnapshot;
  }
  for (const Vertex &vertex : snapshot.vertices_m) {
    if (!finite_vertex(vertex, options))
      return Status::kInvalidTopology;
  }
  for (const TextureCoordinate &coordinate : snapshot.texture_coordinates) {
    if (!finite_texture_coordinate(coordinate))
      return Status::kInvalidAppearance;
  }
  std::map<std::string, TextureReference> texture_paths, texture_assets;
  for (const AppearanceSlot &slot : snapshot.appearance_slots) {
    if (!stable_identifier(slot.target_id) || !valid(slot.state) ||
        !slots->emplace(slot.target_id, &slot).second) {
      return Status::kInvalidAppearance;
    }
    const bool has_candidate = !slot.candidate_asset_id.empty();
    if (has_candidate && !stable_identifier(slot.candidate_asset_id))
      return Status::kInvalidAppearance;
    if (slot.state == AppearanceState::kNone) {
      if (has_candidate || slot.binding || slot.texture)
        return Status::kInvalidAppearance;
      continue;
    }
    if (slot.state == AppearanceState::kUnknown || slot.state == AppearanceState::kRejected) {
      if (!has_candidate || slot.binding || slot.texture)
        return Status::kInvalidAppearance;
      continue;
    }
    if (!slot.binding || !slot.texture || !has_candidate)
      return Status::kInvalidAppearance;
    if (!stable_identifier(slot.binding->keyframe_id) ||
        !std::isfinite(slot.binding->visual_confidence) ||
        slot.binding->visual_confidence < 0.0 ||
        slot.binding->visual_confidence > 1.0) {
      return Status::kInvalidAppearance;
    }
    if (slot.binding->target.entity_id != slot.target_id ||
        slot.binding->target.measured_scene_revision != snapshot.scene_revision ||
        slot.binding->local_asset_id != slot.candidate_asset_id ||
        slot.texture->local_asset_id != slot.candidate_asset_id) {
      return Status::kRevisionMismatch;
    }
    if (!valid_texture(*slot.texture)) {
      return Status::kInvalidTextureReference;
    }
    const auto [path, new_path] = texture_paths.emplace(slot.texture->bundle_relative_path, *slot.texture);
    const auto [asset, new_asset] = texture_assets.emplace(slot.texture->local_asset_id, *slot.texture);
    if ((!new_path && path->second != *slot.texture) || (!new_asset && asset->second != *slot.texture))
      return Status::kInvalidTextureReference;
  }
  std::set<std::string> face_ids;
  for (const Face &face : snapshot.faces) {
    if (!stable_identifier(face.id) || !face_ids.insert(face.id).second ||
        face.source_ids.empty() || face.source_ids.size() > kHardMaximumSourcesPerFace) {
      return Status::kInvalidProvenance;
    }
    for (const std::string &source_id : face.source_ids) {
      if (!stable_identifier(source_id))
        return Status::kInvalidProvenance;
    }
    if (std::set<std::string>(face.source_ids.begin(), face.source_ids.end()).size() !=
        face.source_ids.size()) {
      return Status::kInvalidProvenance;
    }
    const auto &indices = face.vertex_indices;
    if (indices[0] >= snapshot.vertices_m.size() || indices[1] >= snapshot.vertices_m.size() ||
        indices[2] >= snapshot.vertices_m.size() || indices[0] == indices[1] ||
        indices[1] == indices[2] || indices[0] == indices[2]) {
      return Status::kInvalidTopology;
    }
    if (face.appearance_target_id.empty()) {
      if (face.texture_coordinate_indices)
        return Status::kInvalidAppearance;
      continue;
    }
    if (!stable_identifier(face.appearance_target_id))
      return Status::kInvalidAppearance;
    const auto found = slots->find(face.appearance_target_id);
    if (found == slots->end())
      return Status::kInvalidAppearance;
    const AppearanceSlot &slot = *found->second;
    if (slot.state == AppearanceState::kVerified) {
      if (!face.texture_coordinate_indices)
        return Status::kInvalidAppearance;
      for (uint32_t texture_index : *face.texture_coordinate_indices) {
        if (texture_index >= snapshot.texture_coordinates.size())
          return Status::kInvalidAppearance;
      }
    } else if (face.texture_coordinate_indices) {
      return Status::kInvalidAppearance;
    }
  }
  return std::all_of(snapshot.warnings.begin(), snapshot.warnings.end(), stable_identifier)
             ? Status::kOk
             : Status::kInvalidSnapshot;
}

void append_obj_vertex(std::string &output, const Vertex &vertex) {
  output.append("v ");
  output.append(number(vertex.x_m));
  output.push_back(' ');
  output.append(number(vertex.y_m));
  output.push_back(' ');
  output.append(number(vertex.z_m));
  output.push_back('\n');
}

void append_obj_texture_coordinate(std::string &output,
                                   const TextureCoordinate &coordinate) {
  output.append("vt ");
  output.append(number(coordinate.u));
  output.push_back(' ');
  output.append(number(coordinate.v));
  output.push_back('\n');
}

void append_face(std::string &output, const Face &face, bool include_texture) {
  output.append("f");
  for (size_t index = 0U; index < 3U; ++index) {
    output.push_back(' ');
    output.append(std::to_string(static_cast<uint64_t>(face.vertex_indices[index]) + 1U));
    if (include_texture) {
      output.push_back('/');
      output.append(std::to_string(
          static_cast<uint64_t>((*face.texture_coordinate_indices)[index]) + 1U));
    }
  }
  output.push_back('\n');
}

std::string obj(const Snapshot &snapshot,
                const std::map<std::string, const AppearanceSlot *> &slots,
                bool include_textures, const std::string &library_filename) {
  std::string output;
  output.reserve(snapshot.vertices_m.size() * 48U + snapshot.faces.size() * 32U);
  output.append("# HSS frozen captured mesh; metres declared in manifest\nmtllib ");
  output.append(library_filename); output.push_back('\n');
  for (const Vertex &vertex : snapshot.vertices_m)
    append_obj_vertex(output, vertex);
  if (include_textures) {
    for (const TextureCoordinate &coordinate : snapshot.texture_coordinates)
      append_obj_texture_coordinate(output, coordinate);
  }
  std::string active_material;
  for (const Face &face : snapshot.faces) {
    const AppearanceSlot *slot = nullptr;
    if (!face.appearance_target_id.empty())
      slot = slots.at(face.appearance_target_id);
    const bool textured = include_textures && slot != nullptr &&
                          slot->state == AppearanceState::kVerified;
    const std::string material = textured ? material_name(slot->target_id) : "hss_untextured";
    if (material != active_material) {
      output.append("usemtl ");
      output.append(material);
      output.push_back('\n');
      active_material = material;
    }
    append_face(output, face, textured);
  }
  return output;
}

std::string mtl(const Snapshot &snapshot, bool include_textures) {
  std::string output{"# HSS optional appearance materials\nnewmtl hss_untextured\nKd 0.7 0.7 0.7\nd 1.0\n"};
  if (!include_textures)
    return output;
  for (const AppearanceSlot &slot : snapshot.appearance_slots) {
    if (slot.state != AppearanceState::kVerified)
      continue;
    output.append("newmtl ");
    output.append(material_name(slot.target_id));
    output.append("\nKd 1.0 1.0 1.0\nd 1.0\nmap_Kd ");
    output.append(slot.texture->bundle_relative_path);
    output.push_back('\n');
  }
  return output;
}

void append_slot_json(std::string &output, const AppearanceSlot &slot,
                      bool include_textures) {
  output.append("{\"target_id\":");
  json_string(output, slot.target_id);
  output.append(",\"state\":");
  json_string(output, appearance_state_name(slot.state));
  output.append(",\"candidate_asset_id\":");
  if (slot.candidate_asset_id.empty())
    output.append("null");
  else
    json_string(output, slot.candidate_asset_id);
  output.append(",\"keyframe_id\":");
  if (slot.state != AppearanceState::kVerified)
    output.append("null");
  else
    json_string(output, slot.binding->keyframe_id);
  output.append(",\"texture\":");
  if (!include_textures || slot.state != AppearanceState::kVerified) {
    output.append("null");
  } else {
    output.append("{\"path\":");
    json_string(output, slot.texture->bundle_relative_path);
    output.append(",\"sha256\":");
    json_string(output, slot.texture->sha256_hex);
    output.push_back('}');
  }
  output.push_back('}');
}

std::string manifest(const Snapshot &snapshot, bool include_textures) {
  std::string output;
  output.reserve(4'096U);
  output.append("{\"format\":\"HSS-OBJ-MTL-1\",\"export_id\":");
  json_string(output, snapshot.export_id);
  output.append(",\"project_id\":");
  json_string(output, snapshot.project_id);
  output.append(",\"scene_revision\":");
  json_string(output, std::to_string(snapshot.scene_revision));
  output.append(",\"captured_mesh_id\":");
  json_string(output, snapshot.captured_mesh_id);
  output.append(",\"units\":\"m\",\"axes\":");
  json_string(output, kAxesDescription);
  output.append(",\"source_is_synthetic\":");
  output.append(snapshot.source_is_synthetic ? "true" : "false");
  output.append(",\"quality_state\":");
  json_string(output, snapshot.quality_state);
  output.append(",\"texture_mode\":");
  json_string(output, include_textures ? "included" : "texture_free");
  output.append(",\"warnings\":");
  append_string_array(output, snapshot.warnings);
  output.append(",\"appearance\":[");
  for (size_t index = 0U; index < snapshot.appearance_slots.size(); ++index) {
    if (index != 0U)
      output.push_back(',');
    append_slot_json(output, snapshot.appearance_slots[index], include_textures);
  }
  output.append("],\"faces\":[");
  for (size_t index = 0U; index < snapshot.faces.size(); ++index) {
    if (index != 0U)
      output.push_back(',');
    const Face &face = snapshot.faces[index];
    output.append("{\"id\":");
    json_string(output, face.id);
    output.append(",\"source_ids\":");
    append_string_array(output, face.source_ids);
    output.append(",\"appearance_target_id\":");
    if (face.appearance_target_id.empty())
      output.append("null");
    else
      json_string(output, face.appearance_target_id);
    output.push_back('}');
  }
  output.append("]}");
  return output;
}

} // namespace

Result export_snapshot(const Snapshot &snapshot, const Options &options) {
  if (!valid_options(options))
    return failure(Status::kInvalidOptions);
  try {
    std::map<std::string, const AppearanceSlot *> slots;
    const Status validation = validate_snapshot(snapshot, options, &slots);
    if (validation != Status::kOk)
      return failure(validation);
    Artifact artifact;
    artifact.obj_utf8 = obj(snapshot, slots, options.include_textures, options.material_library_filename);
    artifact.mtl_utf8 = mtl(snapshot, options.include_textures);
    artifact.manifest_json = manifest(snapshot, options.include_textures);
    if (options.include_textures) {
      std::set<std::string> included_paths;
      for (const AppearanceSlot &slot : snapshot.appearance_slots) {
        if (slot.state == AppearanceState::kVerified && included_paths.insert(slot.texture->bundle_relative_path).second)
          artifact.texture_references.push_back(*slot.texture);
      }
    }
    return {Status::kOk, std::move(artifact)};
  } catch (const std::bad_alloc &) {
    return failure(Status::kResourceLimitExceeded);
  } catch (...) {
    return failure(Status::kInternalFailure);
  }
}

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk: return "ok";
  case Status::kInvalidOptions: return "invalid_options";
  case Status::kInvalidSnapshot: return "invalid_snapshot";
  case Status::kRevisionMismatch: return "revision_mismatch";
  case Status::kInvalidTopology: return "invalid_topology";
  case Status::kInvalidProvenance: return "invalid_provenance";
  case Status::kInvalidAppearance: return "invalid_appearance";
  case Status::kInvalidTextureReference: return "invalid_texture_reference";
  case Status::kResourceLimitExceeded: return "resource_limit_exceeded";
  case Status::kInternalFailure: return "internal_failure";
  }
  return "unknown_status";
}

const char *appearance_state_name(AppearanceState state) {
  switch (state) {
  case AppearanceState::kNone: return "none";
  case AppearanceState::kUnknown: return "unknown";
  case AppearanceState::kRejected: return "rejected";
  case AppearanceState::kVerified: return "verified";
  }
  return "invalid";
}

bool is_valid_texture_reference(const TextureReference &texture) { return valid_texture(texture); }

} // namespace hss::obj_mtl
