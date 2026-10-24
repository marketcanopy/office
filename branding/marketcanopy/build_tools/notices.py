#
# Market Canopy Office: the Appropriate Legal Notices shown in About.
# Copyright (C) 2026 Market Canopy
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License version 3 as published by
# the Free Software Foundation, together with the additional terms in the
# LICENSE file at the root of this repository.
#
# This program is distributed WITHOUT ANY WARRANTY; without even the implied
# warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# AGPL at https://www.gnu.org/licenses/agpl-3.0.html.
#
# SPDX-License-Identifier: AGPL-3.0-only
#
# Turns branding/marketcanopy/notices/notice.json and the license texts into the
# files the two About dialogs read (ADR 0527 decision 2):
#
#   desktop-apps/common/loginpage/src/data/marketcanopy-notice.json   start page
#   desktop-apps/common/loginpage/src/data/marketcanopy-<text>.txt    its license texts
#   web-apps/apps/common/main/lib/template/MarketCanopyNotice.template  every editor
#
# The texts are bundled, so both dialogs show them with no network. The start
# page imports them with Vite's ?raw; the editors' About.js loads the template
# with RequireJS's text! plugin, which the web-apps build inlines.

import html
import json
import os

START_PAGE_DATA = "desktop-apps/common/loginpage/src/data"
EDITOR_TEMPLATE = "web-apps/apps/common/main/lib/template/MarketCanopyNotice.template"
TEXT_KEYS = ("agpl", "thirdParty", "cc")
TEXT_FILES = {
    "agpl": "marketcanopy-license-agpl.txt",
    "thirdParty": "marketcanopy-license-3dparty.txt",
    "cc": "marketcanopy-license-cc.txt",
}


def _fill(value, fields):
    for name, field in fields.items():
        value = value.replace("{" + name + "}", str(field))
    return value


def resolve(branding_dir, build):
    """The notice with its build fields filled in. `build` holds version,
    number, flavor, upstreamTag, sourceCommit and modified {first, last}."""
    with open(os.path.join(branding_dir, "notices", "notice.json"), "r", encoding="utf-8") as handle:
        notice = json.load(handle)
    fields = dict(build)
    fields.update(build["modified"])
    fields["year"] = build["modified"]["last"][:4]
    first_year = build["modified"]["first"][:4]
    fields["modificationYears"] = first_year if first_year == fields["year"] else first_year + "-" + fields["year"]
    resolved = {key: value for key, value in notice.items() if key not in ("$comment", "labels", "links", "texts")}
    resolved["links"] = {
        key: {"label": link["label"], "url": _fill(link["url"], fields)} for key, link in notice["links"].items()
    }
    labels = dict(notice["labels"])
    modified = (
        labels.pop("modifiedOn") if build["modified"]["first"] == build["modified"]["last"] else labels["modified"]
    )
    labels.pop("modifiedOn", None)
    labels["modified"] = modified
    resolved["copyright"] = [_fill(line, fields) for line in notice["copyright"]]
    resolved["lines"] = {key: _fill(labels.pop(key), fields) for key in ("build", "based", "modified")}
    resolved["labels"] = labels
    resolved["build"] = build
    texts = {}
    for key in TEXT_KEYS:
        with open(os.path.join(branding_dir, notice["texts"][key]), "r", encoding="utf-8") as handle:
            texts[key] = handle.read()
    return resolved, texts


def editor_template(notice, texts):
    """The HTML fragment appended to the editors' About panel. Every value is
    escaped, so the fragment holds no underscore template delimiters."""
    esc = html.escape
    paragraph = '<p style="margin: 0 0 8px;">%s</p>'
    parts = [
        '<div class="mc-about-notice" style="max-width: 560px; margin: 24px auto 16px; padding: 0 16px; text-align: start; line-height: 1.5;">'
    ]
    for key in ("origin", "modification"):
        parts.append(paragraph % esc(notice[key]))
    lines = notice["lines"]
    parts.append(paragraph % "<br>".join(esc(lines[key]) for key in ("modified", "based", "build")))
    parts.append(paragraph % "<br>".join(esc(line) for line in notice["copyright"]))
    for key in ("license", "warranty", "trademarks", "nonCode"):
        parts.append(paragraph % esc(notice[key]))
    links = notice["links"]
    anchors = [
        '<a href="%s" target="_blank" rel="noopener">%s</a>' % (esc(link["url"]), esc(link["label"]))
        for link in links.values()
    ]
    parts.append(paragraph % " &middot; ".join(anchors))
    parts.append(
        '<details style="margin-top: 8px;"><summary style="cursor: pointer;">%s</summary>'
        % esc(notice["labels"]["texts"])
    )
    for key in TEXT_KEYS:
        label = esc(notice["labels"][key])
        parts.append('<h4 style="margin: 12px 0 4px;">%s</h4>' % label)
        parts.append(
            '<pre tabindex="0" role="document" aria-label="%s" style="max-height: 240px; overflow: auto; white-space: pre-wrap; font-size: 11px; margin: 0;">%s</pre>'
            % (label, esc(texts[key]))
        )
    parts.append("</details></div>")
    return "\n".join(parts) + "\n"


def write(workspace, branding_dir, build):
    """Write the notice files into the checkout at `workspace`. Returns the
    paths written, relative to it."""
    notice, texts = resolve(branding_dir, build)
    written = []
    data_dir = os.path.join(workspace, START_PAGE_DATA)
    os.makedirs(data_dir, exist_ok=True)
    with open(os.path.join(data_dir, "marketcanopy-notice.json"), "w", encoding="utf-8") as handle:
        json.dump(notice, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    written.append(START_PAGE_DATA + "/marketcanopy-notice.json")
    for key in TEXT_KEYS:
        with open(os.path.join(data_dir, TEXT_FILES[key]), "w", encoding="utf-8") as handle:
            handle.write(texts[key])
        written.append(START_PAGE_DATA + "/" + TEXT_FILES[key])
    template = os.path.join(workspace, EDITOR_TEMPLATE)
    os.makedirs(os.path.dirname(template), exist_ok=True)
    with open(template, "w", encoding="utf-8") as handle:
        handle.write(editor_template(notice, texts))
    written.append(EDITOR_TEMPLATE)
    return written
