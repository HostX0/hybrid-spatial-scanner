"""Public artifact/metadata regressions only, not scanner/native acceptance."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


verify = module("verify_store_preparation")
generate = module("generate_store_site")

# Public hosting security suffix observed on 2026-10-11; no capture data.
HOST_SECURITY_SUFFIX = """<script>(function(){function c(){var b=a.contentDocument||(a.contentWindow&&a.contentWindow.document);if(b){var d=b.createElement('script');d.innerHTML="window.__CF$cv$params={r:'a48fc2b2ed76dff3',t:'MTc5MTc0MTU4Ng=='};var a=document.createElement('script');a.src='/cdn-cgi/challenge-platform/scripts/jsd/main.js';document.getElementsByTagName('head')[0].appendChild(a);";b.getElementsByTagName('head')[0].appendChild(d)}}if(document.body){var a=document.createElement('iframe');a.height=1;a.width=1;a.style.position='absolute';a.style.top=0;a.style.left=0;a.style.border='none';a.style.visibility='hidden';document.body.appendChild(a);if('loading'!==document.readyState)c();else if(window.addEventListener)document.addEventListener('DOMContentLoaded',c);else{var e=document.onreadystatechange||function(){};document.onreadystatechange=function(b){e(b);'loading'!==document.readyState&&(document.onreadystatechange=e,c())}}}})();</script>"""


class StorePreparationTests(unittest.TestCase):
    def setUp(self):
        self.content = verify.read_json(ROOT / "website/content.json")

    def test_exact_public_allowlist_and_language_links(self):
        with tempfile.TemporaryDirectory() as parent:
            destination = Path(parent) / "pages"
            generate.generate(ROOT, destination)
            result = verify.verify_pages(destination, self.content)
            self.assertEqual(len(result), 7)

    def test_reject_extra_file_even_when_not_linked(self):
        with tempfile.TemporaryDirectory() as parent:
            destination = Path(parent) / "pages"
            generate.generate(ROOT, destination)
            (destination / "private-notes.txt").write_text("private")
            with self.assertRaises(verify.CheckFailure):
                verify.verify_pages(destination, self.content)

    def test_reject_hidden_internal_metadata(self):
        with self.assertRaises(verify.CheckFailure):
            verify.public_text('<meta content="/Users/operator/capture.md">')
        with self.assertRaises(verify.CheckFailure):
            verify.public_text('<!-- ENG-SECRET-01 https://github.com/example/repo -->')

    def test_reject_duplicate_json_keys(self):
        with self.assertRaises(verify.CheckFailure):
            verify.unique_object([("support_email", "a"), ("support_email", "b")])

    def test_existing_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as parent:
            destination = Path(parent) / "pages"
            destination.mkdir()
            with self.assertRaises(ValueError):
                generate.generate(ROOT, destination)

    def test_reject_active_uploader_or_automatic_handlers(self):
        for text in ('<form></form>', '<script></script>', '<a onclick="upload()">'):
            with self.subTest(text=text), self.assertRaises(verify.CheckFailure):
                verify.Page().feed(text)

    def test_metadata_limits_include_arabic_utf8_bytes(self):
        _, sizes = verify.verify_metadata(ROOT, self.content)
        self.assertGreater(sizes["ar-SA"]["keyword_utf8_bytes"], len("ليدار,مخطط,غرفة,مجسم,قياس"))
        self.assertLessEqual(sizes["ar-SA"]["keyword_utf8_bytes"], 100)

    def test_native_manifest_and_help_source_only(self):
        metadata, _ = verify.verify_metadata(ROOT, self.content)
        result = verify.verify_native_source(ROOT, self.content, metadata)
        self.assertIn("not Apple compilation", result["checks"])
        self.assertFalse(result["schema_or_abi_change"])

    def test_release_build_must_be_valid_and_match_native_configurations(self):
        metadata, _ = verify.verify_metadata(ROOT, self.content)
        # A real new build is allowed; silently retaining stale store metadata
        # or accepting a malformed CFBundleVersion is not.
        for bad in (None, 2, "0", "01", "-1", "1.0", "10000", "invalid", "1"):
            changed = dict(metadata, build_number=bad)
            with self.subTest(build_number=bad), self.assertRaises(verify.CheckFailure):
                verify.verify_native_source(ROOT, self.content, changed)

    def test_live_delivery_requires_exact_product_and_reviewed_host_suffix(self):
        page = generate.render(self.content, "en", "privacy")
        self.assertEqual(verify.verify_delivery(page, page), "exact")
        hosted = page.replace("</body>", HOST_SECURITY_SUFFIX + "</body>")
        self.assertEqual(verify.verify_delivery(hosted, page), "exact-product-plus-reviewed-host-security-suffix")
        fresh = hosted.replace("a48fc2b2ed76dff3", "0123456789abcdef").replace("MTc5MTc0MTU4Ng==", "MTc5MTc0MjAwMA==")
        self.assertEqual(verify.verify_delivery(fresh, page), "exact-product-plus-reviewed-host-security-suffix")
        for altered in (hosted.replace("THE THIRD ACT LLC", "Another publisher"),
                        hosted.replace("/cdn-cgi/challenge-platform/scripts/jsd/main.js", "https://other.example/track.js"),
                        hosted.replace("</body>", "<script>upload()</script></body>")):
            with self.subTest(altered=altered[-80:]), self.assertRaises(verify.CheckFailure):
                verify.verify_delivery(altered, page)
        with self.assertRaises(verify.CheckFailure):
            verify.verify_delivery("changed stylesheet", "original stylesheet", html_page=False)


if __name__ == "__main__":
    unittest.main()
