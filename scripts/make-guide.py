#!/usr/bin/env python3
"""Builds docs/Dropocalypse-User-Guide.pdf, the printable guide shipped with every release.

    python3 scripts/make-guide.py

Requires: pip install reportlab. Uses DejaVu Sans if present (for arrow and key symbols),
otherwise falls back to Helvetica.
"""
import os
import sys

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (CondPageBreak, Image, KeepTogether, PageBreak, Paragraph, SimpleDocTemplate, Spacer,
                                Table, TableStyle)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "Dropocalypse-User-Guide.pdf")
VERSION_FILE = os.path.join(ROOT, "docs", "guide-version.txt")


def guide_version():
    """The app version this guide describes: $GUIDE_VERSION, or the newest entry in the wiki changelog."""
    import re
    v = os.environ.get("GUIDE_VERSION", "").strip().lstrip("v")
    if v:
        return v
    with open(os.path.join(ROOT, "docs", "wiki", "Changelog.md"), encoding="utf-8") as f:
        m = re.search(r"^## (\d+\.\d+(?:\.\d+)?)\s*$", f.read(), re.M)
    return m.group(1) if m else "dev"


VERSION = guide_version()
import datetime as _dt
_today = _dt.date.today()
VERSION_DATE = f"{_today:%B} {_today.day}, {_today.year}"

# ---------------------------------------------------------------------------- fonts

FONT, BOLD = "Helvetica", "Helvetica-Bold"
for regular, bold in [("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                       "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
                      ("/Library/Fonts/DejaVuSans.ttf", "/Library/Fonts/DejaVuSans-Bold.ttf")]:
    if os.path.exists(regular) and os.path.exists(bold):
        pdfmetrics.registerFont(TTFont("Guide", regular))
        pdfmetrics.registerFont(TTFont("Guide-Bold", bold))
        pdfmetrics.registerFontFamily("Guide", normal="Guide", bold="Guide-Bold", italic="Guide",
                                      boldItalic="Guide-Bold")
        FONT, BOLD = "Guide", "Guide-Bold"
        break

ARROW = "→" if FONT == "Guide" else "->"
LEFT, RIGHT = ("←", "→") if FONT == "Guide" else ("Left", "Right")
CMD = "⌘" if FONT == "Guide" else "Cmd "

# ---------------------------------------------------------------------------- styles

ACCENT = colors.HexColor("#C2187A")
ACCENT_DARK = colors.HexColor("#5B1A8C")
INK = colors.HexColor("#1B1B22")
MUTED = colors.HexColor("#5F6170")
RULE = colors.HexColor("#DADBE3")
HEADER_BG = colors.HexColor("#2A1B3D")
ZEBRA = colors.HexColor("#F6F4FA")
TIP_BG = colors.HexColor("#FBEFF6")

body = ParagraphStyle("body", fontName=FONT, fontSize=10.5, leading=15, textColor=INK, spaceAfter=6)
small = ParagraphStyle("small", parent=body, fontSize=9.5, leading=13, spaceAfter=0)
cell = ParagraphStyle("cell", parent=body, fontSize=9.5, leading=12.5, spaceAfter=0)
cell_head = ParagraphStyle("cellhead", parent=cell, fontName=BOLD, textColor=colors.white)
h1 = ParagraphStyle("h1", fontName=BOLD, fontSize=20, leading=25, textColor=ACCENT_DARK, spaceBefore=4, keepWithNext=1,
                    spaceAfter=10)
h2 = ParagraphStyle("h2", fontName=BOLD, fontSize=13.5, leading=18, textColor=ACCENT, spaceBefore=12, keepWithNext=1,
                    spaceAfter=6)
bullet = ParagraphStyle("bullet", parent=body, leftIndent=16, bulletIndent=4, spaceAfter=3)
step = ParagraphStyle("step", parent=body, leftIndent=20, bulletIndent=2, spaceAfter=4)
tip = ParagraphStyle("tip", parent=body, fontSize=10, leading=14, spaceAfter=0)


# One neon color per section, so every page has its own look.
SECTION_COLORS = [
    colors.HexColor("#C2187A"),  # magenta
    colors.HexColor("#7A3FD1"),  # violet
    colors.HexColor("#1E88E5"),  # electric blue
    colors.HexColor("#00A6B8"),  # cyan
    colors.HexColor("#E8590C"),  # orange
    colors.HexColor("#D6336C"),  # pink
    colors.HexColor("#2F9E44"),  # green
    colors.HexColor("#F08C00"),  # amber
    colors.HexColor("#E03131"),  # red
    colors.HexColor("#5C3BC2"),  # indigo
]
RAINBOW = [colors.HexColor(c) for c in
           ["#C2187A", "#E03131", "#E8590C", "#F08C00", "#2F9E44", "#00A6B8", "#1E88E5", "#7A3FD1"]]
CUR = {"color": SECTION_COLORS[0]}


def tint(c, amount):
    """Mix a color toward white (amount 0 = color, 1 = white)."""
    return colors.Color(c.red + (1 - c.red) * amount, c.green + (1 - c.green) * amount,
                        c.blue + (1 - c.blue) * amount)


def hsv_color(h, s_, v):
    import colorsys
    r, g, b = colorsys.hsv_to_rgb(h % 1.0, s_, v)
    return colors.Color(r, g, b)


def section(number, title):
    """Full-width colored banner heading; also sets the color used by tables and tips that follow."""
    color = SECTION_COLORS[(number - 1) % len(SECTION_COLORS)]
    CUR["color"] = color
    badge = ParagraphStyle("badge", fontName=BOLD, fontSize=18 if number < 10 else 15, leading=22,
                           textColor=color, alignment=TA_CENTER)
    title_style = ParagraphStyle("bannertitle", fontName=BOLD, fontSize=18, leading=22, textColor=colors.white)
    t = Table([[Paragraph(str(number), badge), Paragraph(title, title_style)]],
              colWidths=[0.6 * inch, 5.9 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (0, 0), colors.white),
        ("BACKGROUND", (1, 0), (1, 0), color),
        ("BOX", (0, 0), (0, 0), 2, color),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("TOPPADDING", (0, 0), (-1, -1), 8),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
        ("LEFTPADDING", (1, 0), (1, 0), 12),
    ]))
    t.keepWithNext = 1
    # Start a new page rather than leave a banner stranded at the bottom of one.
    return [CondPageBreak(2.6 * inch), Spacer(1, 6), t, Spacer(1, 8)]


def sub(title):
    st = ParagraphStyle("h2c", parent=h2, textColor=CUR["color"])
    return Paragraph(title, st)


def P(text, style=body):
    return Paragraph(text, style)


def bullets(items):
    return [Paragraph(t, bullet, bulletText="•") for t in items]


def steps(items):
    return [Paragraph(t, step, bulletText=f"{i}.") for i, t in enumerate(items, 1)]


def table(header, rows, widths, swatches=None):
    """swatches: optional list of colors, one per row, painted into the first column."""
    color = CUR["color"]
    data = [[P(h, cell_head) for h in header]] + [[P(c, cell) for c in r] for r in rows]
    t = Table(data, colWidths=widths, repeatRows=1)
    style = [
        ("BACKGROUND", (0, 0), (-1, 0), color),
        ("LINEBELOW", (0, 0), (-1, 0), 2, tint(color, 0.0)),
        ("LINEBEFORE", (0, 1), (0, -1), 3, color),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
    ]
    for i in range(1, len(data)):
        if i % 2 == 0:
            style.append(("BACKGROUND", (0, i), (-1, i), tint(color, 0.9)))
    if swatches:
        for i, sw in enumerate(swatches, 1):
            style.append(("BACKGROUND", (0, i), (0, i), sw))
            style.append(("TEXTCOLOR", (0, i), (0, i), colors.white))
    t.setStyle(TableStyle(style))
    return t


def tip_box(text):
    color = CUR["color"]
    label = f'<font color="{color.hexval().replace("0x", "#")}"><b>\u2605 Tip:</b></font>'
    t = Table([[P(f"{label} {text}", tip)]], colWidths=[6.5 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), tint(color, 0.88)),
        ("LINEBEFORE", (0, 0), (0, -1), 4, color),
        ("LEFTPADDING", (0, 0), (-1, -1), 10),
        ("TOPPADDING", (0, 0), (-1, -1), 7),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 7),
    ]))
    return t


def whats_new_box(items):
    color = SECTION_COLORS[0]
    rows = [[P(f'<font color="{color.hexval().replace("0x", "#")}"><b>\u2605</b></font>', body), P(t, body)] for t in items]
    t = Table(rows, colWidths=[0.3 * inch, 6.2 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), tint(color, 0.9)),
        ("LINEBEFORE", (0, 0), (0, -1), 4, color),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 8),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]))
    return t


# ---------------------------------------------------------------------------- page decoration

def cover_page(canvas, doc):
    w, h = letter
    canvas.saveState()
    canvas.setFillColor(HEADER_BG)
    canvas.rect(0, 0, w, h, stroke=0, fill=1)
    # A few glowing rings as a simple "visualizer" motif.
    cx, cy = w / 2, h * 0.68
    for i, (r, col) in enumerate([(1.75, ACCENT), (1.4, colors.HexColor("#7A3FD1")), (1.05, colors.HexColor("#2EC5E6"))]):
        canvas.setStrokeColor(col)
        canvas.setLineWidth(3 - i * 0.6)
        canvas.circle(cx, cy, r * inch, stroke=1, fill=0)
    # Spectrum spikes around the inner ring, like the app's Bass Ring style.
    import math
    canvas.setStrokeColor(colors.HexColor("#F2C6E0"))
    canvas.setLineWidth(2.2)
    for k in range(64):
        a = k / 64 * 2 * math.pi
        length = (0.12 + 0.28 * abs(math.sin(k * 0.7)) * abs(math.cos(k * 0.23))) * inch
        r0 = 0.7 * inch
        canvas.line(cx + math.cos(a) * r0, cy + math.sin(a) * r0,
                    cx + math.cos(a) * (r0 + length), cy + math.sin(a) * (r0 + length))
    canvas.setFillColor(colors.white)
    canvas.setFont(BOLD, 44)
    canvas.drawCentredString(w / 2, h * 0.40, "DROPOCALYPSE")
    canvas.setFont(FONT, 15)
    canvas.setFillColor(colors.HexColor("#F2C6E0"))
    canvas.drawCentredString(w / 2, h * 0.40 - 30, "The drop hits. Your logo goes off.")
    canvas.setFillColor(colors.white)
    canvas.setFont(BOLD, 22)
    canvas.drawCentredString(w / 2, h * 0.22, "User Guide")
    canvas.setFont(FONT, 13)
    canvas.drawCentredString(w / 2, h * 0.22 - 24, "for Mac and Windows")
    canvas.setFont(BOLD, 12)
    canvas.setFillColor(colors.HexColor("#F2C6E0"))
    canvas.drawCentredString(w / 2, h * 0.22 - 50, f"Version {VERSION}  ·  {VERSION_DATE}"
                             if FONT == "Guide" else f"Version {VERSION}  -  {VERSION_DATE}")
    canvas.setFont(FONT, 10)
    canvas.setFillColor(colors.HexColor("#B9B3C9"))
    canvas.drawCentredString(w / 2, 0.75 * inch, "Free and open source  ·  github.com/allenrobberson/dropocalypse"
                             if FONT == "Guide" else
                             "Free and open source  -  github.com/allenrobberson/dropocalypse")
    canvas.restoreState()


def later_pages(canvas, doc):
    w, h = letter
    canvas.saveState()
    # Rainbow "spectrum" stripe across the top of every page.
    seg = w / len(RAINBOW)
    for i, c in enumerate(RAINBOW):
        canvas.setFillColor(c)
        canvas.rect(i * seg, h - 0.16 * inch, seg + 1, 0.16 * inch, stroke=0, fill=1)
    # Little equalizer in the top-right corner.
    import math
    for k in range(14):
        bh = (0.06 + 0.16 * abs(math.sin(k * 1.3 + doc.page))) * inch
        canvas.setFillColor(RAINBOW[k % len(RAINBOW)])
        canvas.rect(w - inch - (14 - k) * 0.075 * inch, h - 0.42 * inch - bh, 0.05 * inch, bh, stroke=0, fill=1)
    # Footer.
    canvas.setFillColor(HEADER_BG)
    canvas.rect(0, 0, w, 0.55 * inch, stroke=0, fill=1)
    canvas.setFont(BOLD, 9)
    canvas.setFillColor(colors.HexColor("#F2C6E0"))
    canvas.drawString(inch, 0.22 * inch, "DROPOCALYPSE")
    canvas.setFont(FONT, 9)
    canvas.setFillColor(colors.white)
    canvas.drawString(inch + 1.15 * inch, 0.22 * inch, f"User Guide  ·  Version {VERSION}"
                      if FONT == "Guide" else f"User Guide  -  Version {VERSION}")
    canvas.drawRightString(w - inch, 0.22 * inch, f"Page {doc.page}")
    canvas.restoreState()


# ---------------------------------------------------------------------------- content

# Signature color of each style (its base hue in the app), used for the swatches in section 6.
STYLE_HUES = [0.55, 0.75, 0.08, 0.33, 0.9, 0.6, 0.15, 0.45, 0.8, 0.02,
              0.55, 0.8, 0.0, 0.86, 0.6, 0.5, 0.66, 0.33, 0.95, 0.05,
              0.97, 0.25, 0.7, 0.55]


IMAGES = os.path.join(ROOT, "docs", "guide-images")


def shot(name, width, caption=None):
    """A screenshot from docs/guide-images, scaled to `width`, with a thin frame and optional caption."""
    from reportlab.lib.utils import ImageReader
    path = os.path.join(IMAGES, name)
    iw, ih = ImageReader(path).getSize()
    img = Image(path, width=width, height=width * ih / iw)
    cell = [img]
    if caption:
        cell.append(Spacer(1, 3))
        cell.append(P(caption, ParagraphStyle("cap", parent=small, alignment=TA_CENTER, textColor=MUTED)))
    t = Table([[cell]], colWidths=[width + 6])
    t.setStyle(TableStyle([("BOX", (0, 0), (0, 0), 0.6, colors.HexColor("#D8D2E6")),
                           ("BACKGROUND", (0, 0), (0, 0), colors.HexColor("#F7F5FB")),
                           ("ALIGN", (0, 0), (-1, -1), "CENTER"), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                           ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3),
                           ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 4)]))
    return t


def side_by_side(*cells, widths):
    """Lay screenshots or paragraph lists out in one row."""
    t = Table([list(cells)], colWidths=widths)
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("RIGHTPADDING", (0, 0), (-1, -1), 8), ("TOPPADDING", (0, 0), (-1, -1), 0)]))
    return t


def latest_releases(count=2):
    """The newest `count` entries of docs/wiki/Changelog.md as [(version, [bullet html, ...])]."""
    import re
    text = open(os.path.join(ROOT, "docs", "wiki", "Changelog.md"), encoding="utf-8").read()
    out = []
    for m in re.finditer(r"^## (\d+\.\d+(?:\.\d+)?)\s*\n(.*?)(?=^## |\Z)", text, re.M | re.S):
        bullets_ = []
        for line in m.group(2).splitlines():
            line = line.strip()
            if not line.startswith("- "):
                continue
            line = line[2:]
            # Drop emoji and other symbols the PDF font can't draw, then convert Markdown to the guide's markup.
            line = "".join(ch for ch in line if ord(ch) < 0x2190 or ch in "→←×·–—…‘’“”")
            line = line.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            line = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", line)
            line = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", line)
            line = re.sub(r"\[\[([^\]]+)\]\]", r"\1", line)
            bullets_.append(line.strip())
        out.append((m.group(1), bullets_))
        if len(out) == count:
            break
    return out


def build():
    s = []
    s.append(PageBreak())  # page 1 is the drawn cover

    # ---- Contents
    s.append(P("Contents", h1))
    items = ["Install on Mac", "Install on Windows", "Quick start", "The toolbar",
             "Logo, Motion, Audio and Look settings", "The 24 visual styles", "Keyboard shortcuts",
             "Setting up for a live show", "Troubleshooting", "Where your settings are saved, and how to remove the app"]
    rows = []
    for i, name in enumerate(items, 1):
        c = SECTION_COLORS[(i - 1) % len(SECTION_COLORS)]
        rows.append([Paragraph(f"<b>{i}</b>", ParagraphStyle("n", fontName=BOLD, fontSize=12, leading=15,
                                                            textColor=colors.white, alignment=TA_CENTER)),
                     Paragraph(name, ParagraphStyle("c", fontName=FONT, fontSize=11.5, leading=15, textColor=INK))])
    toc = Table(rows, colWidths=[0.45 * inch, 6.05 * inch])
    ts = [("VALIGN", (0, 0), (-1, -1), "MIDDLE"), ("TOPPADDING", (0, 0), (-1, -1), 6),
          ("BOTTOMPADDING", (0, 0), (-1, -1), 6), ("LEFTPADDING", (1, 0), (1, -1), 12)]
    for i in range(len(items)):
        c = SECTION_COLORS[i % len(SECTION_COLORS)]
        ts += [("BACKGROUND", (0, i), (0, i), c), ("BACKGROUND", (1, i), (1, i), tint(c, 0.9)),
               ("LINEBELOW", (0, i), (-1, i), 2, colors.white)]
    toc.setStyle(TableStyle(ts))
    s.append(toc)
    s.append(Spacer(1, 10))
    s.append(P("Dropocalypse is a free, open-source music visualizer for Mac and Windows in the spirit of "
               "Winamp and MilkDrop. It listens to music and turns it into swirling, zooming, pulsing visuals, "
               "while your logo sits on top and pulses, squashes, bounces, slides and wobbles to the bass.", body))
    s.append(Spacer(1, 12))
    s.append(P("What's new", h2))
    news = []
    for version, items in latest_releases(2):
        for i, item in enumerate(items):
            news.append((f"<b>Version {version}:</b> " if i == 0 else "") + item)
    s.append(whats_new_box(news))
    s.append(Spacer(1, 4))
    s.append(P("Every earlier change is listed in the Changelog on the project's wiki.", small))

    # ---- Mac
    s.append(PageBreak())
    s += section(1, "Install on Mac")
    s.append(P("Requires macOS 13 Ventura or later on an Apple Silicon or Intel Mac.", small))
    s.append(Spacer(1, 6))
    s += steps([
        "Download the <b>macOS .dmg</b> file from the Releases page "
        "(github.com/allenrobberson/dropocalypse/releases).",
        "Open the .dmg and drag <b>Dropocalypse</b> into the <b>Applications</b> folder.",
        "Open Dropocalypse from Applications.",
    ])
    s.append(sub("First launch"))
    s.append(P("Since version 1.0.1 Dropocalypse is signed and notarized by Apple, so it opens like any other app "
               "with no security warning. If you still have an older copy that macOS blocks:", body))
    s += bullets([
        f"<b>macOS 15 Sequoia and later:</b> try to open the app once and close the warning. Then open "
        f"<b>System Settings {ARROW} Privacy &amp; Security</b>, scroll down, and click <b>Open Anyway</b> next to "
        f"Dropocalypse.",
        f"<b>macOS 13 and 14:</b> right-click (or Control-click) the app {ARROW} <b>Open</b> {ARROW} <b>Open</b>.",
        "Then update to the latest version and it won't happen again.",
    ])
    s.append(sub("Listening to music on the Mac"))
    s.append(P("Click <b>Audio</b> in the toolbar and open the <b>Listen to</b> drop-down. On macOS 14.2 or later "
               "you can pick <b>any app on your Mac</b>, such as <b>Spotify</b>, Music, Safari, Chrome or your DJ "
               "software, or <b>All Mac audio</b>. Dropocalypse hears exactly what that app plays, at full quality, "
               "with no microphone or extra software. Apps playing right now are listed first.", body))
    s.append(P("The first time, macOS asks to allow <b>system audio recording</b>: click <b>Allow</b>. To use a "
               "microphone or line-in instead, pick it in the same drop-down and allow <b>microphone</b> access. "
               "Audio is analyzed live and never sent anywhere; it's only saved when you press Record. If you clicked Don't Allow by mistake, "
               f"turn it on in <b>System Settings {ARROW} Privacy &amp; Security</b>, under <b>Screen &amp; System "
               f"Audio Recording</b> (apps) or <b>Microphone</b> (mic).", body))

    # ---- Windows
    s += section(2, "Install on Windows")
    s.append(P("Requires Windows 10 or 11 (64-bit). No installer and no administrator rights are needed.", small))
    s.append(Spacer(1, 6))
    s += steps([
        "Download the <b>Windows-portable .zip</b> file from the Releases page.",
        f"Right-click the zip {ARROW} <b>Extract All</b>, then open the extracted <b>Dropocalypse</b> folder.",
        "Double-click <b>Dropocalypse.exe</b>. Nothing is installed: you can keep the folder anywhere, even on a "
        "USB stick.",
    ])
    s.append(sub("First launch"))
    s.append(P(f"If Windows shows <b>“Windows protected your PC”</b>, click <b>More info {ARROW} Run anyway</b>. "
               "This appears because the free app isn't code-signed.", body))
    s.append(sub("Listening to music on Windows"))
    s.append(P("By default the Windows version listens to <b>what your PC is playing</b> (Spotify, YouTube, DJ "
               "software, games), so no microphone is needed. To use a microphone or line-in instead, choose it "
               f"under <b>Audio</b> in the toolbar, and turn on <b>Settings {ARROW} Privacy &amp; security {ARROW} "
               f"Microphone {ARROW} Let desktop apps access your microphone</b>.", body))

    # ---- Quick start
    s += section(3, "Quick start")
    s += steps([
        "Open Dropocalypse, click <b>Audio</b> and pick what to listen to: on a Mac, an app like <b>Spotify</b> "
        "(macOS 14.2+) or a mic; on Windows, System audio is already selected. Play some music and the visuals "
        "start reacting right away.",
        "Drag your logo (PNG, JPG and most image types) onto the window, or press <b>L</b> to choose a file. "
        "A PNG with a transparent background looks best.",
        "Press <b>F</b> for full screen.",
        "Move the mouse to the top of the screen to open the toolbar and try different styles.",
    ])
    s.append(Spacer(1, 4))
    s.append(P("Your logo, the style you picked and all your settings are saved automatically and come back the "
               "next time you open the app.", body))


    # ---- Toolbar
    s += section(4, "The toolbar")
    s.append(P("Every control lives in one slim bar along the <b>top edge</b> of the window. It stays out of the "
               "way during a show:", body))
    s.append(shot("window.jpg", 4.4 * inch, "Dropocalypse on a Mac, with the toolbar showing along the top"))
    s.append(Spacer(1, 6))
    s += bullets([
        "<b>To show it:</b> move the mouse toward the top of the screen, click anywhere, or press <b>T</b>.",
        "<b>It hides itself</b> after about 2.5 seconds without activity, and the mouse cursor hides with it.",
        "<b>It stays open</b> while your pointer is over it or while one of its panels is open.",
        "<b>To hide it right away</b>, press <b>Esc</b>.",
    ])
    s.append(Spacer(1, 6))
    s.append(shot("toolbar.jpg", 6.4 * inch, "The toolbar, left to right: logo, style, auto-cycle, Record, Motion, Logo, "
                  "Audio, Look, Updates, status, Detach, keys and full screen"))
    s.append(Spacer(1, 8))
    s.append(table(["Control", "What it does"], [
        ["<b>Add Logo</b> / logo name", "Opens the Logo menu: choose, remove, show or hide your logo"],
        ["<b>‹  Style name  ›</b>", "Previous style, open the style list, next style. In the list, click a name to show it; the <b>checkbox</b> to its left decides whether auto-cycle includes it (All / None buttons at the top)"],
        ["<b>Auto-cycle</b> (⟳ on Mac)", "Moves to the next <b>checked</b> style automatically, with a smooth crossfade. Unchecked styles are skipped; with nothing checked it uses them all"],
        ["<b>Motion</b>", "How your logo reacts to the bass"],
        ["<b>Logo</b>", "Logo size, glow, color tint and trails"],
        ["<b>Audio</b>", "Which input to listen to, sensitivity and beat threshold"],
        ["<b>Look</b>", "Beat flash, brightness and auto-cycle timing"],
        ["Status text", "The input being used, its sample rate and buffer size (lower ms = faster reaction)"],
        ["<b>Updates</b>", "Your version, <b>Check now</b> and automatic checks. Turns pink (<b>Update X.Y.Z</b>) when a new "
                           "version is out: <b>Update now</b> installs it and reopens the app."],
        ["<b>Record</b> / <b>Stop</b>", "Records the visuals and music to an MP4 video, without the toolbar. "
                                         "The ▾ next to it sets the video size and where to save."],
        ["<b>Screen</b> (display icon on Mac)", "Shows the visuals full screen on another display (TV, projector, second monitor)"],
        ["<b>Detach</b>", "Moves the controls into their own window, stacked top to bottom, so they stay on your main "
                          "screen while the visuals play on another. <b>Attach</b> puts them back."],
        ["<b>Keys</b> (⌨ on Mac)", "Shows the keyboard shortcuts"],
        ["<b>Full screen</b>", "Full screen on or off"],
    ], [1.9 * inch, 4.6 * inch]))
    s.append(Spacer(1, 6))
    s.append(P("The Mac toolbar uses icons for a few buttons and Windows uses words, but the controls work the "
               "same way.", small))

    # ---- Panels
    s += section(5, "Logo, Motion, Audio and Look settings")
    s.append(sub("Logo menu"))
    s.append(table(["Item", "What it does"], [
        ["<b>Choose Logo…</b> (L)", "Pick an image file. Empty transparent edges are trimmed automatically so the "
                                         "logo moves around its visible shape."],
        ["<b>Remove Logo</b>", "Goes back to the “YOUR LOGO” placeholder"],
        ["<b>Show Logo</b> (G)", "Hides or shows the logo without removing it, for visuals only"],
    ], [1.9 * inch, 4.6 * inch]))
    s.append(Spacer(1, 4))
    s.append(P("You can also drag an image onto the window at any time.", small))

    s.append(sub("Motion panel: how the logo reacts to the bass"))
    s.append(P("0 turns a motion off completely; the highest setting is 1.5.", small))
    s.append(Spacer(1, 4))
    s.append(table(["Slider", "Default", "What it does"], [
        ["<b>Pulse</b>", "0.70", "The logo grows with the bass and shrinks as it fades"],
        ["<b>Squash &amp; stretch</b>", "0.60", "Squashes wide and short on each kick, then springs back like rubber"],
        ["<b>Bounce</b>", "0.50", "Kicks launch the logo upward and it falls back with gravity"],
        ["<b>Slide</b>", "0.40", "Each beat pushes the logo left or right, then a spring pulls it back"],
        ["<b>Wobble</b>", "0.35", "Each beat tips the logo into a quick rotation that settles back"],
        ["<b>Reset to defaults</b>", "", "Resets the Motion, Logo and Look sliders"],
    ], [1.6 * inch, 0.85 * inch, 4.05 * inch]))
    s.append(Spacer(1, 6))
    s.append(tip_box("For a calm, branded look, try Pulse 0.5 and everything else at 0. For a festival-style "
                     "look, push Squash, Bounce and Wobble to 1.2 or higher."))
    s.append(Spacer(1, 8))
    s.append(KeepTogether(side_by_side(shot("motion.jpg", 2.9 * inch, "The Motion panel"),
                                       shot("audio.jpg", 2.75 * inch, "The Audio panel"),
                                       widths=[3.25 * inch, 3.25 * inch])))

    s.append(sub("Logo panel"))
    s.append(table(["Control", "Default", "What it does"], [
        ["<b>Size</b>", "0.32", "How big the logo is, as a fraction of the screen height"],
        ["<b>Glow</b>", "0.60", "A soft, colored halo that flares with the bass. 0 turns it off."],
        ["<b>Color tint</b>", "0.00", "Blends the logo toward the style's color. 0 keeps your original colors."],
        ["<b>Logo trails</b>", "Off", "Leaves echoes of the logo that swirl and fade with the visuals"],
        ["<b>Show logo</b>", "On", "Same as Show Logo in the Logo menu"],
    ], [1.6 * inch, 0.85 * inch, 4.05 * inch]))

    s.append(sub("Audio panel"))
    s.append(table(["Control", "Default", "What it does"], [
        ["<b>Listen to</b> (Mac)", "Default input",
         "Drop-down of everything you can listen to. <b>Mac audio</b> (macOS 14.2+): <b>All Mac audio</b> or "
         "any open app, such as Spotify (playing apps are marked ♪). <b>Microphone or line-in</b>: built-in "
         "mic, USB mic, audio interface or line-in."],
        ["<b>Input</b> (Windows)", "System audio",
         "“System audio” listens to what the PC is playing, and “Playing on: …” listens to one "
         "output device. You can also pick a mic or line-in."],
        ["<b>Refresh list</b>", "", "Updates the list after you open an app or plug something in"],
        ["<b>Restart audio</b>", "", "Use if the visuals stop reacting after a device change or sleep"],
        ["<b>Sensitivity</b>", "1.00", "Raise it in a quiet room or with a distant mic; lower it if room noise makes "
                                       "things twitch"],
        ["<b>Beat threshold</b>", "1.35×", "How big a bass jump counts as a beat. Lower = more beats; "
                                              "higher = only big kicks."],
    ], [1.6 * inch, 1.3 * inch, 3.6 * inch]))
    s.append(Spacer(1, 4))
    s.append(P("Dropocalypse adjusts its own levels to the loudness of the input, so a quiet room mic and a loud "
               "line-in both work without fiddling.", small))

    s.append(sub("Look panel"))
    s.append(table(["Control", "Default", "What it does"], [
        ["<b>Beat flash</b>", "0.40", "The whole screen brightens briefly on each beat"],
        ["<b>Brightness</b>", "1.00", "Overall brightness (0.4 to 1.6). Lower it for dark rooms or bright projectors."],
        ["<b>Auto-cycle every</b>", "30 s", "How long each style stays up when auto-cycle is on (5 to 360 s)"],
    ], [1.6 * inch, 0.85 * inch, 4.05 * inch]))

    s.append(CondPageBreak(3.4 * inch))
    s.append(sub("Recording a video"))
    rec_steps = [
        P("1. Click <b>Record</b> (or press <b>R</b>). It turns red and shows a running timer.", body),
        P("2. Play your music. The visuals (without the toolbar) and the music are saved together.", body),
        P("3. Click <b>Stop</b> (or press <b>R</b> again). A message offers to show the file.", body),
        Spacer(1, 4),
        P("Open the <b>▾</b> next to Record to pick the video size (720p or 1080p at 60 fps, or 4K at 30 fps) and "
          "the folder. Videos go to Movies (Mac) or Videos (Windows) unless you choose another. They are MP4 "
          "files that play anywhere and upload straight to YouTube, Instagram or TikTok.", small),
    ]
    s.append(side_by_side(shot("record.jpg", 2.7 * inch, "Recording settings (▾ next to Record)"), rec_steps,
                          widths=[3.0 * inch, 3.5 * inch]))

    # ---- Styles
    s += section(6, "The 24 visual styles")
    s.append(P("Pick a style from the toolbar, use the arrow keys, or turn on auto-cycle. Every style reacts to "
               "the bass.", body))
    s.append(Spacer(1, 6))
    s.append(side_by_side(
        shot("styles.jpg", 2.2 * inch, "The style list"),
        [P("Click the style name in the toolbar to open the list. Click any name to jump to it.", body),
         Spacer(1, 4),
         P("The <b>checkbox</b> beside each style decides whether <b>auto-cycle</b> includes it. "
           "<b>All</b> and <b>None</b> check or clear every box at once.", body),
         Spacer(1, 4),
         P("Turn on <b>Auto-cycle</b> at the top of the list, or with ⟳ in the toolbar or the A key. Set how long "
           "each style stays up (5 seconds to 6 minutes) under <b>Look</b>.", body)],
        widths=[2.5 * inch, 4.0 * inch]))
    s.append(Spacer(1, 8))
    s.append(table(["#", "Style", "What it looks like"], [
        ["1", "<b>Classic Tunnel</b>", "The waveform bursts out of the center into an endless twisting tunnel"],
        ["2", "<b>Spectrum Skyline</b>", "Spectrum bars along the bottom with trails rising like smoke"],
        ["3", "<b>Starburst</b>", "Rays shoot out of the center, one per frequency band"],
        ["4", "<b>Oscilloscope Drift</b>", "A glowing waveform that ripples, rotates and drifts"],
        ["5", "<b>Kaleido Bloom</b>", "A six-way kaleidoscope blooming outward from the center"],
        ["6", "<b>Liquid Plasma</b>", "Flowing liquid colors with waveform rings melting into them"],
        ["7", "<b>Vortex</b>", "A three-armed spiral of dots whipping around the center"],
        ["8", "<b>Lissajous Neon</b>", "A mirrored neon scribble drawn by the sound wave itself"],
        ["9", "<b>Mirror Hall</b>", "Waveforms reflected four ways into an infinite hall of mirrors"],
        ["10", "<b>Black Hole</b>", "Everything is pulled inward and spirals into the center"],
        ["11", "<b>Bass Ring</b>", "A circular spectrum wrapped around your logo, with sparks on every kick"],
        ["12", "<b>Neon Equalizer</b>", "Smooth neon bars across the screen with a glassy reflection"],
        ["13", "<b>Pulsar Ridges</b>", "Stacked white ridgelines that span the full screen and scroll upward, the classic “joy plot” look"],
        ["14", "<b>Outrun Grid</b>", "A synthwave sunset: smooth striped sun, a full-width sunset-colored meter and a racing neon grid"],
        ["15", "<b>Hyperspace</b>", "A starfield that jumps to warp speed on the drops"],
        ["16", "<b>Particle Storm</b>", "Hundreds of glowing particles flowing in currents, scattering on beats"],
        ["17", "<b>Lightning Storm</b>", "Lightning cracking out from behind the logo on the beat"],
        ["18", "<b>Digital Rain</b>", "Columns of green glyphs raining down, faster with the bass"],
        ["19", "<b>Lava Lamp</b>", "Soft blobs of color drifting and melting together"],
        ["20", "<b>Inferno</b>", "Embers and flames rising from the bottom of the screen"],
        ["21", "<b>Mirror Equalizer</b>", "Bars grow up and down from a center line, in a sunset rainbow"],
        ["22", "<b>LED Meter</b>", "A classic analyzer: green-yellow-red LED segments with peak markers"],
        ["23", "<b>Rainbow Peaks</b>", "Thin rainbow bars with glowing caps that bounce on the beat"],
        ["24", "<b>Tesseract</b>", "A neon 4D hypercube turning itself inside out, spinning harder on every beat"],
    ], [0.4 * inch, 1.6 * inch, 4.5 * inch], swatches=[hsv_color(h_, 0.8, 0.85) for h_ in STYLE_HUES]))
    s.append(Spacer(1, 8))
    s.append(tip_box("Bass Ring and Particle Storm are built around a centered logo, so turn Bounce and Slide "
                     "down to keep it in place. For Pulsar Ridges and Outrun Grid, a smaller logo Size (about 0.2) "
                     "keeps it from covering the scene."))

    # ---- Shortcuts
    s += section(7, "Keyboard shortcuts")
    s.append(table(["Action", "Mac", "Windows"], [
        ["Full screen on / off", f"F or Ctrl+{CMD}F (or the green window button)", "F or F11"],
        ["Show / hide the toolbar", "T or S", "T or S"],
        ["Hide the toolbar", "Esc (press again to exit full screen)", "Esc (press again to exit full screen)"],
        ["Choose a logo", f"L or {CMD}O", "L or Ctrl+O"],
        ["Show / hide the logo", "G", "G"],
        ["Previous / next style", f"{LEFT} / {RIGHT}  (or {CMD}{LEFT} / {CMD}{RIGHT})", f"{LEFT} / {RIGHT}"],
        ["Auto-cycle on / off", "A", "A"],
        ["Detach / attach the controls window", f"D or Shift+{CMD}D", "D"],
        ["Start / stop recording", f"R or Shift+{CMD}R", "R"],
    ], [2.4 * inch, 2.0 * inch, 2.1 * inch]))
    s.append(Spacer(1, 6))
    s.append(P(f"On the Mac, the menu bar has the same actions under <b>File</b> and <b>Visualizer</b>, including "
               f"<b>Full Screen</b> and <b>Restart Audio</b>.", small))

    # ---- Live show
    s += section(8, "Setting up for a live show")
    s += steps([
        "<b>Playing from the same computer?</b> On a Mac, pick your music or DJ app (or All Mac audio) under "
        "<b>Audio</b>. On Windows, choose <b>System audio</b>. It's the cleanest signal, with no cables or mic.",
        "<b>Use a direct feed if you can.</b> Run the booth, record or aux output of your mixer or DJ controller "
        "into a USB audio interface and pick it under Audio. A direct feed reacts more cleanly than a mic in a "
        "loud room.",
        "<b>Using a mic?</b> Point it toward the speakers or subwoofer. Raise Sensitivity in a quiet room; lower it "
        "if crowd noise is driving the visuals.",
        "<b>Tune the beat.</b> Watch the logo during a track with a clear kick. If it misses kicks, lower Beat "
        "threshold. If it jumps on hi-hats or vocals, raise it.",
        "<b>Go full screen</b> on the projector or TV: drag the window onto that screen and press F. Full screen "
        "covers every pixel, edge to edge. On the Mac the menu bar and Dock disappear, and on MacBooks with a "
        "camera notch the visuals fill the strip beside the notch too.",
        "Turn on <b>auto-cycle</b> so the show keeps changing on its own: 30 to 60 seconds per style keeps "
        "things lively, and up to 6 minutes suits a slower night.",
    ])
    s.append(CondPageBreak(4.6 * inch))
    s.append(sub("Two screens: controls on one, visuals on the other"))
    s.append(side_by_side(
        shot("detached.jpg", 1.9 * inch, "The detached controls"),
        [P("1. Click <b>Detach</b> in the toolbar (or press <b>D</b>). The controls move into their own "
           "<b>Dropocalypse Controls</b> window, stacked top to bottom.", body),
         Spacer(1, 4),
         P("2. Under <b>Visuals</b>, pick your TV, projector or second monitor. The visuals go full screen there.",
           body),
         Spacer(1, 4),
         P("3. Keep the controls on your laptop and change styles, logo, motion and audio while the show runs. "
           "The audience only sees the visuals.", body),
         Spacer(1, 4),
         P("4. Close the controls window (or click <b>Attach</b> at the bottom) to put the toolbar back on top.",
           body)],
        widths=[2.25 * inch, 4.25 * inch]))

    # ---- Troubleshooting
    s += section(9, "Troubleshooting")
    s.append(table(["Problem", "Fix"], [
        ["<b>Mac: “Dropocalypse can't be opened”</b>",
         "Download the latest version: since 1.0.1 the Mac app is signed and notarized and opens normally. "
         "For older copies, see section 1."],
        ["<b>Windows: “Windows protected your PC”</b>",
         f"Click <b>More info {ARROW} Run anyway</b>. The Windows app isn't code-signed yet."],
        ["<b>Windows: nothing happens when I double-click</b>",
         "Extract the zip first; don't run the app from inside it. On work or school PCs, IT policies can block "
         "unapproved apps, so ask your IT team."],
        ["<b>Nothing reacts to the music</b>",
         f"Check microphone access. <b>Mac:</b> System Settings {ARROW} Privacy &amp; Security {ARROW} Microphone. "
         f"<b>Windows:</b> Settings {ARROW} Privacy &amp; security {ARROW} Microphone {ARROW} Let desktop apps access "
         "your microphone. Then use Audio → Restart audio and check the selected input."
         if FONT == "Guide" else
         "Check microphone access in your system privacy settings, then use Audio > Restart audio."],
        ["<b>Mac: listening to an app, but nothing reacts</b>",
         "Make sure the app is playing. Then allow Dropocalypse in System Settings, Privacy &amp; Security, "
         "<b>Screen &amp; System Audio Recording</b>, and click Restart audio. Needs macOS 14.2 or later."],
        ["<b>Windows: visuals ignore Spotify / YouTube</b>",
         "Under Audio, choose <b>System audio</b>. With both headphones and speakers, pick the “Playing on: "
         "…” entry for the device you're listening on."],
        ["<b>It reacts, but only a little</b>",
         "Raise Sensitivity and lower Beat threshold. Check the input level in your system Sound settings."],
        ["<b>It twitches when no music is playing</b>", "Lower Sensitivity, or use a direct line-in feed"],
        ["<b>My app or USB interface isn't listed</b>", "Open the app or plug it in, then click <b>Refresh list</b> in the Audio panel"],
        ["<b>My logo looks blurry</b>", "Use a larger image: about 1000 pixels on the long side is ideal"],
        ["<b>My logo has a white box around it</b>",
         "The image has a solid background. Export it as a PNG with a transparent background."],
        ["<b>The toolbar won't appear</b>", "Move the mouse to the very top of the window, click anywhere, or press T"],
    ], [2.3 * inch, 4.2 * inch]))

    # ---- Settings / uninstall
    s += section(10, "Where your settings are saved, and how to remove the app")
    s.append(KeepTogether(table(["", "Mac", "Windows"], [
        ["<b>Settings and logo</b>",
         "Saved automatically by macOS for the app, plus your logo in ~/Library/Application Support/Dropocalypse",
         "%APPDATA%\\Dropocalypse"],
        ["<b>To remove the app</b>",
         "Drag Dropocalypse from Applications to the Trash, and delete the Application Support folder above",
         "Delete the Dropocalypse folder you extracted and the %APPDATA%\\Dropocalypse folder"],
    ], [1.5 * inch, 2.6 * inch, 2.4 * inch])))
    s.append(Spacer(1, 14))
    s.append(KeepTogether([
        sub("About"),
        P("Dropocalypse is free and open-source software under the MIT License: free to use, modify and share, "
          "including commercially. The full source code, the latest downloads and the online version of this "
          "guide are at <b>github.com/allenrobberson/dropocalypse</b>.", body),
        P("Inspired by Ryan Geiss's MilkDrop and Nullsoft's Winamp. All visuals are original code.", small),
        sub("Questions, ideas and bug reports"),
        P("Ask questions, suggest new styles or show off your setup in <b>Discussions</b> at "
          "<b>github.com/allenrobberson/dropocalypse/discussions</b>. Found a bug? Open an issue at "
          "<b>github.com/allenrobberson/dropocalypse/issues</b>.", body),
        sub("Support Dropocalypse"),
        P("Dropocalypse is free and always will be. If you enjoy it, please consider sponsoring the project at "
          "<b>github.com/sponsors/allenrobberson</b>. Sponsorships cover the yearly Apple Developer "
          "membership that keeps the Mac app signed, and the time spent on new styles and features.", body),
    ]))
    return s


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    doc = SimpleDocTemplate(OUT, pagesize=letter, leftMargin=inch, rightMargin=inch, topMargin=0.9 * inch,
                            bottomMargin=inch, title="Dropocalypse User Guide", author="Dropocalypse contributors",
                            subject="How to install and use Dropocalypse on Mac and Windows")
    doc.build(build(), onFirstPage=cover_page, onLaterPages=later_pages)
    with open(VERSION_FILE, "w", encoding="utf-8") as f:
        f.write(VERSION + "\n")
    print(f"Wrote {OUT} (version {VERSION})")


if __name__ == "__main__":
    sys.exit(main())
