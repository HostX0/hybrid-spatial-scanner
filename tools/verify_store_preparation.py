#!/usr/bin/env python3
"""Finite source/publication checks; never builds an Apple target or certifies it."""
import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import plistlib
import re
import sys
from urllib.parse import urljoin, urlsplit
from urllib.request import Request, urlopen


class CheckFailure(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise CheckFailure(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "Duplicate JSON key: " + key)
        result[key] = value
    return result


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)


PRIVATE_CONTENT = re.compile(
    r"github\.com|/Users/|/home/|file://|\b[0-9a-f]{40}\b|\b(?:ctest|ASan|UBSan|XCTest|Codex|AGENTS)\b|"
    r"\bENG-[A-Z0-9-]+|\b[\w-]+\.(?:md|swift|mm|cpp|py)\b", re.IGNORECASE)


def public_text(text):
    require(not PRIVATE_CONTENT.search(text), "Internal engineering material in public content")
    require(not re.search(r"to be confirmed|placeholder|TODO|lorem ipsum|coming soon", text, re.I),
            "Unfinished public content")


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links = []
        self.ids = set()
        self.language = self.direction = None
        self.headings = []
        self.details = 0

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        require(tag not in {"script", "iframe", "form", "input", "object", "embed"}, "Unrequested active content: " + tag)
        require(not any(name.startswith("on") for name in values), "Inline event handler")
        if tag == "html":
            self.language, self.direction = values.get("lang"), values.get("dir")
        if "id" in values:
            require(values["id"] not in self.ids, "Duplicate HTML id")
            self.ids.add(values["id"])
        if tag == "h1":
            self.headings.append(tag)
        if tag == "details":
            self.details += 1
        for attribute in ("href", "src"):
            if attribute in values:
                self.links.append(values[attribute])


def verify_metadata(source, content):
    metadata = read_json(source / "docs/release/app-store-metadata.json")
    require(metadata["publisher"] == content["publisher"], "Publisher mismatch")
    sizes = {}
    for language, fields in metadata["localizations"].items():
        for field, limit in (("name", 30), ("subtitle", 30), ("promotional_text", 170), ("description", 4000)):
            value = fields[field]
            require(isinstance(value, str) and 0 < len(value) <= limit, language + ": invalid " + field)
            require("<" not in value and ">" not in value, "Metadata must be plain text")
            public_text(value)
        keywords = fields["keywords"]
        require(len(keywords.encode("utf-8")) <= 100, "Keywords exceed 100 UTF-8 bytes")
        require(all(len(word.strip()) > 2 for word in keywords.split(",")), "Keyword shorter than three characters")
        require(len(set(keywords.split(","))) == len(keywords.split(",")), "Duplicate keywords")
        require(fields["name"] == content["product_name"], "Product name mismatch")
        prefix = "/ar" if language == "ar-SA" else ""
        for field, path in (("support_url", "/support/"), ("privacy_policy_url", "/privacy/"), ("marketing_url", "/")):
            require(fields[field] == content["origin"] + prefix + path, "Metadata URL mismatch")
        sizes[language] = {"name_characters": len(fields["name"]), "subtitle_characters": len(fields["subtitle"]),
                           "promotional_characters": len(fields["promotional_text"]), "description_characters": len(fields["description"]),
                           "keyword_utf8_bytes": len(keywords.encode("utf-8"))}
    return metadata, sizes


def verify_pages(directory, content):
    expected = {"index.html", "privacy/index.html", "support/index.html", "ar/index.html",
                "ar/privacy/index.html", "ar/support/index.html", "style.css"}
    actual = {str(path.relative_to(directory)) for path in directory.rglob("*") if path.is_file()}
    require(actual == expected, "Public output must contain only the six pages and stylesheet")
    require(not any(path.is_symlink() for path in directory.rglob("*")), "Symlink in public output")
    records = {}
    for name in sorted(expected):
        path = directory / name
        text = path.read_text(encoding="utf-8")
        public_text(text)
        records[name] = {"bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        if not name.endswith(".html"):
            require("prefers-reduced-motion" in text and "focus-visible" in text and "max-width:480px" in text,
                    "Missing responsive/accessibility styling")
            continue
        parser = Page()
        parser.feed(text)
        language = "ar" if name.startswith("ar/") else "en"
        require(parser.language == language and parser.direction == ("rtl" if language == "ar" else "ltr"), "Page language/direction mismatch")
        require(len(parser.headings) == 1 and "content" in parser.ids, "Page needs one main heading and skip destination")
        require(content["publisher"] in text, "Missing public publisher")
        if "support/" in name:
            require(parser.details == 9 and content["support_email"] in text and content["support_phone"] in text,
                    "Support must offer actual help and contact")
        for link in parser.links:
            parts = urlsplit(link)
            if parts.scheme in ("mailto", "tel"):
                require(link in ("mailto:" + content["support_email"] + "?subject=Hybrid%20Spatial%20Scanner%20support",
                                 "tel:" + content["support_phone"]), "Unapproved contact or automatic attachment")
            elif parts.scheme == "https":
                require(link == content["company_url"] or link.startswith(content["origin"] + "/"), "Unapproved remote URL")
            else:
                require(not parts.scheme and not parts.netloc and link.startswith(("/", "#")), "Unapproved relative URL")
                if parts.path:
                    target = directory / parts.path.lstrip("/")
                    if parts.path.endswith("/"):
                        target /= "index.html"
                    require(target.is_file(), "Broken public link: " + link)
                if parts.fragment:
                    require(parts.fragment in parser.ids, "Broken page anchor: " + link)
    return records


def strings(path):
    values = {}
    for key, value in re.findall(r'("(?:[^"\\]|\\.)*")\s*=\s*("(?:[^"\\]|\\.)*")\s*;', path.read_text()):
        key, value = json.loads(key), json.loads(value)
        require(key not in values, "Duplicate localization key: " + key)
        values[key] = value
    return values


def verify_native_source(source, content, metadata):
    app = source / "HSSApp"
    privacy = plistlib.loads((app / "PrivacyInfo.xcprivacy").read_bytes())
    require(privacy["NSPrivacyTracking"] is False and privacy["NSPrivacyCollectedDataTypes"] == [], "Unexpected app collection/tracking declaration")
    categories = {item["NSPrivacyAccessedAPIType"] for item in privacy["NSPrivacyAccessedAPITypes"]}
    require(categories == {"NSPrivacyAccessedAPICategoryUserDefaults", "NSPrivacyAccessedAPICategorySystemBootTime",
                           "NSPrivacyAccessedAPICategoryDiskSpace", "NSPrivacyAccessedAPICategoryFileTimestamp"}, "Required-reason inventory drift")
    protection = plistlib.loads((app / "HybridSpatialScanner.entitlements").read_bytes())
    require(protection["com.apple.developer.default-data-protection"] == "NSFileProtectionCompleteUntilFirstUserAuthentication", "Data Protection source mismatch")
    project = (app / "HybridSpatialScanner.xcodeproj/project.pbxproj").read_text()
    settings = [line for line in project.splitlines() if "INFOPLIST_KEY_NSCameraUsageDescription =" in line]
    require(len(settings) == 2, "Expected both app configurations")
    for line in settings:
        require('INFOPLIST_KEY_CFBundleDisplayName = "' + content["product_name"] + '"' in line, "Unpolished app display name")
        require("MARKETING_VERSION = " + metadata["version"] + ";" in line, "Store/native version mismatch")
        require("CURRENT_PROJECT_VERSION = 1;" in line, "Unexpected build number")
    ui = (app / "ScannerProductUI.swift").read_text()
    for destination in (content["origin"] + "/privacy/", content["origin"] + "/support/", "mailto:" + content["support_email"], "tel:" + content["support_phone"]):
        require(destination in ui, "Native public destination mismatch")
    for identifier in ("support.open", "support.content", "support.email", "support.phone", "support.done", "privacy.policy-link"):
        require('"' + identifier + '"' in ui, "Missing native support integration")
    start = ui.index("private struct ScannerSupportContent: View")
    end = ui.index("private struct ScannerStatusCard: View", start)
    keys = set(re.findall(r'(?:Section|DisclosureGroup|Text|Label|Link)\("([^"\n]+)"', ui[start:end]))
    keys.update(("Help and support", "Publisher and policy", "Full privacy policy"))
    sizes = {}
    for language in ("en", "ar"):
        values = strings(app / (language + ".lproj/Localizable.strings"))
        require(keys <= values.keys(), "Missing " + language + " support translation: " + repr(sorted(keys - values.keys())))
        require(all(values[key] for key in keys), "Empty help translation")
        info = strings(app / (language + ".lproj/InfoPlist.strings"))
        require(info["CFBundleDisplayName"] == content["product_name"] and info["NSCameraUsageDescription"], "Camera/display localization mismatch")
        sizes[language] = len(keys)
    # Only existing Pillow, no image editing or new asset generation.
    from PIL import Image
    icon_manifest = read_json(app / "Assets.xcassets/AppIcon.appiconset/Contents.json")
    icon = app / "Assets.xcassets/AppIcon.appiconset" / icon_manifest["images"][0]["filename"]
    with Image.open(icon) as image:
        require(image.format == "PNG" and image.size == (1024, 1024), "Invalid source app icon size/format")
        require(image.convert("RGBA").getchannel("A").getextrema() == (255, 255), "Source icon is not opaque")
    return {"checks": "source only, not Apple compilation/packaging/enforcement", "localized_help_keys": sizes,
            "icon": {"size": [1024, 1024], "opaque": True}, "schema_or_abi_change": False}


def verify_live(origin, content):
    require(origin == content["origin"], "Only the prepared public origin is allowed")
    receipts = []
    for path in ("/", "/privacy/", "/support/", "/ar/", "/ar/privacy/", "/ar/support/", "/style.css"):
        with urlopen(Request(origin + path, headers={"User-Agent": "HSS-Public-Page-Check/1.0"}), timeout=20) as response:
            raw = response.read(512 * 1024 + 1)
            require(response.status == 200 and len(raw) <= 512 * 1024, "Public page status/size failure")
            final = urlsplit(response.url)
            require(final.scheme == "https" and final.netloc == urlsplit(origin).netloc and final.path == path,
                    "Public page redirected to login or another origin")
            body = raw.decode("utf-8")
            if path != "/style.css":
                require(content["publisher"] in body and '<main id="content">' in body, "Public response is not the product page")
                public_text(body)
            receipts.append({"path": path, "status": response.status, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()})
    return receipts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--website", type=Path, required=True)
    parser.add_argument("--live-origin")
    parser.add_argument("--require-submission-ready", action="store_true")
    args = parser.parse_args()
    result = {"scope": "source-and-store-preparation-only", "native_compiled": False,
              "signed": False, "installed": False, "physically_verified": False, "app_store_submitted": False}
    try:
        content = read_json(args.source / "website/content.json")
        metadata, result["metadata_limits"] = verify_metadata(args.source, content)
        result["public_artifacts"] = verify_pages(args.website, content)
        result["native_source"] = verify_native_source(args.source, content, metadata)
        if args.live_origin:
            result["anonymous_https"] = verify_live(args.live_origin, content)
        result["preparation_checks_passed"] = True
        result["submission_ready"] = False
        result["pending_gates"] = ["exact-tree native/UI/raster/export and archive packaging",
                                  "signing and Apple account/distribution configuration", "actual final-app screenshots",
                                  "supported-device installation and physical acceptance", "App Review submission/approval"]
        print(json.dumps(result, indent=2))
        return 2 if args.require_submission_ready else 0
    except Exception as error:
        result.update(preparation_checks_passed=False, submission_ready=False, failure=type(error).__name__ + ": " + str(error))
        print(json.dumps(result, indent=2))
        return 1


if __name__ == "__main__":
    sys.exit(main())
