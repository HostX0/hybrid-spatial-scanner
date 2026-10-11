#!/usr/bin/env python3
"""Generate buildless public product pages from an explicitly public content set.

No capture, engineering evidence, source metadata, credential or arbitrary file
is copied. Output is a new directory, outside the source tree, for Sites hosting.
"""
import argparse
import html
import json
from pathlib import Path
import shutil


def escaped(value):
    return html.escape(value, quote=True)


def route(language, page):
    prefix = "/ar" if language == "ar" else ""
    return prefix + ("/" if page == "overview" else "/" + page + "/")


def paragraphs(section):
    return "\n".join("<p>" + escaped(value) + "</p>" for value in section["paragraphs"])


def render(content, language, page):
    locale = content[language]
    current = locale[page]
    other = "en" if language == "ar" else "ar"
    nav = "\n".join(
        '<a href="' + route(language, item) + '"' + (' aria-current="page"' if item == page else '') + '>'
        + escaped(label) + '</a>' for item, label in locale["nav"].items())
    nav += '<a class="lang" href="' + route(other, page) + '" lang="' + other + '" hreflang="' + other + '">' + escaped(locale["language_label"]) + '</a>'
    title = current["title"] + " — " + content["product_name"]
    date = '<p class="date"><time datetime="' + content["effective_date"] + '">' + escaped(current["date_label"]) + '</time></p>' if page == "privacy" else ""
    if page == "overview":
        body = '<div class="cards">' + "\n".join('<section class="card"><span class="number" aria-hidden="true">0' + str(i + 1) + '</span><h2>' + escaped(section["title"]) + '</h2>' + paragraphs(section) + '</section>' for i, section in enumerate(current["sections"])) + '</div>'
        body += '<aside class="note"><h2>' + escaped(current["note_title"]) + '</h2><p>' + escaped(current["note"]) + '</p></aside>'
    else:
        contents = '<aside class="contents" aria-label="' + escaped(locale["contents_label"]) + '"><strong>' + escaped(locale["contents_label"]) + '</strong><ol>' + "".join('<li><a href="#section-' + str(i) + '">' + escaped(section["title"]) + '</a></li>' for i, section in enumerate(current["sections"])) + '</ol></aside>'
        body = '<div class="layout">' + contents + '<div class="article">'
        if page == "support":
            body += '<section class="contact"><h2>' + escaped(locale["contact_label"]) + '</h2><p>' + escaped(current["contact_intro"]) + '</p><div class="contact-links"><a href="mailto:' + escaped(content["support_email"]) + '?subject=Hybrid%20Spatial%20Scanner%20support"><small>' + escaped(locale["email_label"]) + '</small><bdi>' + escaped(content["support_email"]) + '</bdi></a><a href="tel:' + content["support_phone"] + '"><small>' + escaped(locale["phone_label"]) + '</small><bdi>' + escaped(content["support_phone_display"]) + '</bdi></a></div></section>'
        for i, section in enumerate(current["sections"]):
            if page == "support":
                body += '<details class="faq" id="section-' + str(i) + '"><summary>' + escaped(section["title"]) + '</summary><div class="answer">' + paragraphs(section) + '</div></details>'
            else:
                body += '<section id="section-' + str(i) + '"><h2>' + escaped(section["title"]) + '</h2>' + paragraphs(section) + '</section>'
        body += '</div></div>'
    actions = '<div class="actions">' + "".join('<a href="' + route(language, item) + '">' + escaped(locale["nav"][item]) + '</a>' for item in ("support", "privacy")) + '</div>' if page == "overview" else ""
    return f'''<!doctype html>
<html lang="{language}" dir="{"rtl" if language == "ar" else "ltr"}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="description" content="{escaped(current['description'])}">
<meta name="referrer" content="no-referrer">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'">
<title>{escaped(title)}</title>
<link rel="canonical" href="{content['origin'] + route(language, page)}">
<link rel="alternate" hreflang="en" href="{content['origin'] + route('en', page)}">
<link rel="alternate" hreflang="ar" href="{content['origin'] + route('ar', page)}">
<link rel="stylesheet" href="/style.css">
</head>
<body>
<a class="skip" href="#content">{escaped(locale['skip'])}</a>
<header><div class="top"><a class="brand" href="{route(language, 'overview')}"><span class="mark" aria-hidden="true">HSS</span><span><strong class="safe-identity">{escaped(content['product_name'])}</strong><small class="safe-identity">{escaped(content['publisher'])}</small></span></a><nav aria-label="{escaped(locale['nav']['overview'])}">{nav}</nav></div></header>
<main id="content"><div class="hero"><p class="eyebrow">{escaped(current['eyebrow'])}</p><h1>{escaped(current['title'])}</h1><p class="lead">{escaped(current['intro'])}</p>{date}{actions}</div>{body}</main>
<footer><span class="footer-copy">© 2026 {escaped(content['publisher'])}<br>{escaped(locale['footer'])}</span><div><a href="{route(language, 'support')}">{escaped(locale['nav']['support'])}</a><a href="{route(language, 'privacy')}">{escaped(locale['nav']['privacy'])}</a><a href="{content['company_url']}" rel="noreferrer">THE THIRD ACT</a></div></footer>
</body></html>
'''


def generate(source, destination):
    source = source.resolve(strict=True)
    destination = destination.resolve()
    if destination.exists() or destination.is_relative_to(source):
        raise ValueError("Output must be a new directory outside the source tree.")
    content = json.loads((source / "website/content.json").read_text())
    destination.mkdir(parents=True)
    shutil.copyfile(source / "website/style.css", destination / "style.css")
    for language in ("en", "ar"):
        for page in ("overview", "privacy", "support"):
            path = destination / route(language, page).lstrip("/") / "index.html"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(render(content, language, page), encoding="utf-8")
    # No source paths, internal filenames or commit IDs in public HTML/metadata.
    print(json.dumps({"pages": 6, "languages": ["en", "ar"], "remote_assets": 0, "scripts": 0}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    generate(args.source, args.output)
