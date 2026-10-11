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


if __name__ == "__main__":
    unittest.main()
