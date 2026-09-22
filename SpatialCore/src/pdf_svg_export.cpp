#include "hss/pdf_svg_export.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <new>
#include <sstream>
#include <string_view>

namespace hss::pdf_svg_export {
namespace {

struct Bounds { double min_x; double min_y; double max_x; double max_y; };

Result fail(Status status) { return {status, std::nullopt}; }

bool usable(std::string_view value) { return !value.empty() && value.size() <= 1'024U; }
bool finite(double value) { return std::isfinite(value) && std::fabs(value) <= 10'000.0; }

bool valid_options(const Options &o) {
  const bool paper = o.paper == Paper::kA4Portrait || o.paper == Paper::kA4Landscape ||
                     o.paper == Paper::kA3Portrait || o.paper == Paper::kA3Landscape;
  const bool policy = o.scale_policy == ScalePolicy::kFitToPage ||
                      o.scale_policy == ScalePolicy::kExplicit;
  return paper && policy && std::isfinite(o.margin_mm) && o.margin_mm >= 0.0 &&
         o.margin_mm < 100.0 && std::isfinite(o.scale_bar_m) && o.scale_bar_m > 0.0 &&
         o.max_fragments > 0U && o.max_annotations > 0U && o.max_warning_bytes > 0U &&
         ((o.scale_policy == ScalePolicy::kFitToPage && o.explicit_scale_denominator == 0U) ||
          (o.scale_policy == ScalePolicy::kExplicit && o.explicit_scale_denominator > 0U));
}

std::string number(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(6) << value;
  return out.str();
}

std::string json_escape(std::string_view value) {
  std::ostringstream out;
  for (const unsigned char c : value) {
    switch (c) {
    case '\\': out << "\\\\"; break;
    case '"': out << "\\\""; break;
    case '\n': out << "\\n"; break;
    case '\r': out << "\\r"; break;
    case '\t': out << "\\t"; break;
    default:
      if (c < 0x20U) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
      else out << static_cast<char>(c);
    }
  }
  return out.str();
}

std::string xml_escape(std::string_view value) {
  std::string out;
  for (char c : value) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

std::string pdf_escape(std::string_view value) {
  std::string out;
  for (const unsigned char c : value) {
    if (c == '(' || c == ')' || c == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(c)); }
    else if (c >= 0x20U && c <= 0x7eU) out.push_back(static_cast<char>(c));
    else out.push_back('?');
  }
  return out;
}

const char *availability(annotation_geometry::Availability value) {
  switch (value) {
  case annotation_geometry::Availability::kReady: return "ready";
  case annotation_geometry::Availability::kProvisional: return "provisional";
  case annotation_geometry::Availability::kUnavailable: return "unavailable";
  }
  return "unavailable";
}

const char *quality(plan_semantics::Availability value) {
  switch (value) {
  case plan_semantics::Availability::kReady: return "ready";
  case plan_semantics::Availability::kProvisional: return "provisional";
  case plan_semantics::Availability::kUnavailable: return "invalid";
  case plan_semantics::Availability::kUnsupported: return "needs_review";
  case plan_semantics::Availability::kNotApplicable: return "needs_review";
  }
  return "invalid";
}

bool extent(const plan_semantics::PlanLayer &plan, Bounds &bounds) {
  bool seen = false;
  for (const auto &fragment : plan.fragments) {
    if (!fragment.segment) continue;
    const auto &a = fragment.segment->start_m;
    const auto &b = fragment.segment->end_m;
    if (!finite(a.x_m) || !finite(a.y_m) || !finite(b.x_m) || !finite(b.y_m)) return false;
    if (!seen) { bounds = {a.x_m, a.y_m, a.x_m, a.y_m}; seen = true; }
    bounds.min_x = std::min({bounds.min_x, a.x_m, b.x_m});
    bounds.min_y = std::min({bounds.min_y, a.y_m, b.y_m});
    bounds.max_x = std::max({bounds.max_x, a.x_m, b.x_m});
    bounds.max_y = std::max({bounds.max_y, a.y_m, b.y_m});
  }
  if (!seen) bounds = {0.0, 0.0, 1.0, 1.0};
  if (bounds.max_x == bounds.min_x) bounds.max_x += 1.0;
  if (bounds.max_y == bounds.min_y) bounds.max_y += 1.0;
  return true;
}

std::pair<double, double> paper_points(Paper paper) {
  switch (paper) {
  case Paper::kA4Portrait: return {595.276, 841.890};
  case Paper::kA4Landscape: return {841.890, 595.276};
  case Paper::kA3Portrait: return {841.890, 1190.551};
  case Paper::kA3Landscape: return {1190.551, 841.890};
  }
  return {841.890, 595.276};
}

std::string make_pdf(std::string_view content, double width, double height) {
  std::ostringstream out;
  std::vector<std::streamoff> offsets;
  out << "%PDF-1.4\n%HSS\n";
  auto object = [&](int id, std::string_view body) {
    offsets.push_back(out.tellp()); out << id << " 0 obj\n" << body << "\nendobj\n";
  };
  object(1, "<< /Type /Catalog /Pages 2 0 R >>");
  object(2, "<< /Type /Pages /Count 1 /Kids [3 0 R] >>");
  std::ostringstream page;
  page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << number(width) << ' ' << number(height)
       << "] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>";
  object(3, page.str());
  std::ostringstream stream;
  stream << "<< /Length " << content.size() << " >>\nstream\n" << content << "\nendstream";
  object(4, stream.str());
  object(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  const std::streamoff xref = out.tellp();
  out << "xref\n0 " << offsets.size() + 1U << "\n0000000000 65535 f \n";
  for (const std::streamoff offset : offsets)
    out << std::setw(10) << std::setfill('0') << offset << " 00000 n \n";
  out << std::setfill(' ') << "trailer\n<< /Size " << offsets.size() + 1U
      << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
  return out.str();
}

} // namespace

const char *status_name(Status status) {
  switch (status) {
  case Status::kOk: return "ok";
  case Status::kInvalidOptions: return "invalid_options";
  case Status::kInvalidInput: return "invalid_input";
  case Status::kRevisionMismatch: return "revision_mismatch";
  case Status::kResourceLimitExceeded: return "resource_limit_exceeded";
  case Status::kPageOverflow: return "page_overflow";
  case Status::kInternalFailure: return "internal_failure";
  }
  return "unknown";
}

Result build(const Input &input, const Options &options) {
  if (!valid_options(options)) return fail(Status::kInvalidOptions);
  if (!usable(input.project_id) || !usable(input.plan.scene_revision) ||
      input.annotations.scene_revision != input.plan.scene_revision ||
      input.plan.fragments.size() > options.max_fragments ||
      input.annotations.annotations.size() > options.max_annotations)
    return fail(Status::kInvalidInput);
  for (const auto &annotation : input.annotations.annotations)
    if (annotation.annotation.source_revision != input.plan.scene_revision)
      return fail(Status::kRevisionMismatch);
  try {
    Bounds bounds{};
    if (!extent(input.plan, bounds)) return fail(Status::kInvalidInput);
    const auto [page_w, page_h] = paper_points(options.paper);
    const double margin_pt = options.margin_mm * 72.0 / 25.4;
    const double drawable_w = page_w - 2.0 * margin_pt;
    const double drawable_h = page_h - 2.0 * margin_pt;
    if (drawable_w <= 0.0 || drawable_h <= 0.0) return fail(Status::kInvalidOptions);
    const double width_m = bounds.max_x - bounds.min_x;
    const double height_m = bounds.max_y - bounds.min_y;
    double points_per_m = std::min(drawable_w / width_m, drawable_h / height_m);
    if (options.scale_policy == ScalePolicy::kExplicit)
      points_per_m = 1000.0 * 72.0 / 25.4 / static_cast<double>(options.explicit_scale_denominator);
    if (width_m * points_per_m > drawable_w + 1e-9 || height_m * points_per_m > drawable_h + 1e-9)
      return fail(Status::kPageOverflow);

    std::vector<std::string> warnings = input.caller_warnings;
    if (input.plan.validity != plan_semantics::Availability::kReady)
      warnings.emplace_back("DRAFT: plan quality is not ready; retained geometry may be provisional or unavailable.");
    if (input.plan.source_is_synthetic)
      warnings.emplace_back("SYNTHETIC SOURCE: this export is not physical-device evidence.");
    size_t warning_bytes = 0U;
    for (const auto &warning : warnings) { if (!usable(warning)) return fail(Status::kInvalidInput); warning_bytes += warning.size(); }
    if (warning_bytes > options.max_warning_bytes) return fail(Status::kResourceLimitExceeded);

    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\""
        << " viewBox=\"" << number(bounds.min_x) << ' ' << number(-bounds.max_y) << ' '
        << number(width_m) << ' ' << number(height_m) << "\" data-units=\"m\""
        << " data-y-inversion=\"exactly-one\" data-scene-revision=\"" << xml_escape(input.plan.scene_revision) << "\">\n";
    svg << "<metadata>HSS plan U=+X,V=-Z; SVG screen Y inversion exactly once; revision="
        << xml_escape(input.plan.scene_revision) << "</metadata>\n<g id=\"HSS_BOUNDARY\">\n";
    for (const auto &fragment : input.plan.fragments) {
      if (!fragment.segment) continue;
      const auto &a = fragment.segment->start_m; const auto &b = fragment.segment->end_m;
      svg << "<path id=\"fragment-" << xml_escape(fragment.id) << "\" data-provenance=\""
          << xml_escape(fragment.source_revision) << "\" d=\"M " << number(a.x_m) << ' ' << number(-a.y_m)
          << " L " << number(b.x_m) << ' ' << number(-b.y_m) << "\" fill=\"none\" stroke=\"black\"/>\n";
    }
    svg << "</g>\n<g id=\"HSS_DIMS\">\n";
    for (const auto &annotation : input.annotations.annotations) {
      const auto &anchor = annotation.annotation.anchors.front().position_m;
      svg << "<text id=\"annotation-" << xml_escape(annotation.annotation.id) << "\" x=\"" << number(anchor.x_m)
          << "\" y=\"" << number(-anchor.y_m) << "\" data-availability=\"" << availability(annotation.availability)
          << "\" data-evidence-source=\"" << static_cast<unsigned>(annotation.annotation.evidence_source) << "\">"
          << xml_escape(annotation.annotation.label) << "</text>\n";
    }
    svg << "</g>\n<g id=\"HSS_WARNINGS\">\n";
    for (size_t i = 0; i < warnings.size(); ++i)
      svg << "<text id=\"warning-" << i << "\" x=\"" << number(bounds.min_x) << "\" y=\""
          << number(-bounds.max_y + 0.1 * static_cast<double>(i + 1U)) << "\">" << xml_escape(warnings[i]) << "</text>\n";
    svg << "</g>\n</svg>\n";

    std::ostringstream pdf_content;
    pdf_content << "0 0 0 RG 0.5 w\n";
    for (const auto &fragment : input.plan.fragments) {
      if (!fragment.segment) continue;
      const auto &a = fragment.segment->start_m; const auto &b = fragment.segment->end_m;
      const double ax = margin_pt + (a.x_m - bounds.min_x) * points_per_m;
      const double ay = margin_pt + (a.y_m - bounds.min_y) * points_per_m;
      const double bx = margin_pt + (b.x_m - bounds.min_x) * points_per_m;
      const double by = margin_pt + (b.y_m - bounds.min_y) * points_per_m;
      pdf_content << number(ax) << ' ' << number(ay) << " m " << number(bx) << ' ' << number(by) << " l S\n";
    }
    pdf_content << "BT /F1 9 Tf " << number(margin_pt) << ' ' << number(page_h - margin_pt) << " Td (Project: "
                << pdf_escape(input.project_id) << "; revision: " << pdf_escape(input.plan.scene_revision)
                << "; units: m) Tj ET\n";
    pdf_content << "BT /F1 8 Tf " << number(margin_pt) << ' ' << number(page_h - margin_pt - 12.0)
                << " Td (Scale bar " << number(options.scale_bar_m) << " m; Print at 100%) Tj ET\n";
    for (size_t i = 0; i < warnings.size(); ++i)
      pdf_content << "BT /F1 7 Tf " << number(margin_pt) << ' ' << number(page_h - margin_pt - 24.0 - 9.0 * i)
                  << " Td (" << pdf_escape(warnings[i]) << ") Tj ET\n";

    std::ostringstream manifest;
    manifest << "{\"schema_version\":\"1.1\",\"project_id\":\"" << json_escape(input.project_id)
             << "\",\"scene_revision\":\"" << json_escape(input.plan.scene_revision)
             << "\",\"formats\":[\"PDF\",\"SVG\"],\"units\":\"m\",\"axes\":\"plan: U=+X,V=-Z; SVG screen Y inversion exactly once\""
             << ",\"view\":" << static_cast<unsigned>(input.plan.view)
             << ",\"quality_state\":\"" << quality(input.plan.validity) << "\",\"source_is_synthetic\":"
             << (input.plan.source_is_synthetic ? "true" : "false") << ",\"warnings\":[";
    for (size_t i = 0; i < warnings.size(); ++i) { if (i) manifest << ','; manifest << '"' << json_escape(warnings[i]) << '"'; }
    manifest << "],\"fragments\":[";
    for (size_t i = 0; i < input.plan.fragments.size(); ++i) {
      const auto &f = input.plan.fragments[i]; if (i) manifest << ',';
      manifest << "{\"id\":\"" << json_escape(f.id) << "\",\"source_revision\":\"" << json_escape(f.source_revision)
               << "\",\"has_explicit_segment\":" << (f.segment ? "true" : "false") << ",\"support_ids\":[";
      for (size_t j = 0; j < f.support_ids.size(); ++j) { if (j) manifest << ','; manifest << '"' << json_escape(f.support_ids[j]) << '"'; }
      manifest << "]}";
    }
    manifest << "],\"annotations\":[";
    for (size_t i = 0; i < input.annotations.annotations.size(); ++i) {
      const auto &a = input.annotations.annotations[i]; if (i) manifest << ',';
      manifest << "{\"id\":\"" << json_escape(a.annotation.id) << "\",\"availability\":\"" << availability(a.availability)
               << "\",\"label\":\"" << json_escape(a.annotation.label) << "\",\"evidence_ids\":[";
      for (size_t j = 0; j < a.annotation.evidence_ids.size(); ++j) { if (j) manifest << ','; manifest << '"' << json_escape(a.annotation.evidence_ids[j]) << '"'; }
      manifest << "]}";
    }
    manifest << "]}";
    return {Status::kOk, Artifact{input.plan.scene_revision, make_pdf(pdf_content.str(), page_w, page_h), svg.str(), manifest.str()}};
  } catch (const std::bad_alloc &) { return fail(Status::kResourceLimitExceeded); }
    catch (...) { return fail(Status::kInternalFailure); }
}

} // namespace hss::pdf_svg_export
