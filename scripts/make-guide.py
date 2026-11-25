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
    data = [[P(h, cell_head) for h in header]] + [[P(c, cell) if isinstance(c, str) else c for c in r] for r in rows]
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
    canvas.drawCentredString(w / 2, h * 0.22 - 24, "for Mac, Windows and Linux")
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

# Signature color of each style (its base hue in the app), used for the swatches in section 7.
STYLE_HUES = [0.55, 0.75, 0.08, 0.33, 0.9, 0.6, 0.15, 0.45, 0.8, 0.02,
              0.55, 0.8, 0.0, 0.86, 0.6, 0.5, 0.66, 0.33, 0.95, 0.05,
              0.97, 0.25, 0.7, 0.55, 0.52, 0.08,
              0.55, 0.75, 0.5, 0.58, 0.9]


IMAGES = os.path.join(ROOT, "docs", "guide-images")


def shot(name, width, caption=None):
    """A screenshot from docs/guide-images, scaled to `width`, with a thin frame and optional caption."""
    from reportlab.lib.utils import ImageReader
    path = os.path.join(IMAGES, name)
    if not os.path.exists(path):   # a screenshot that hasn't been taken yet: leave it out
        print(f"note: {name} is missing from docs/guide-images; the guide leaves it out")
        return Spacer(1, 0)
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


def thumb(name, width):
    """A style still from docs/guide-images/styles (made by scripts/make-style-stills.py)."""
    from reportlab.lib.utils import ImageReader
    path = os.path.join(IMAGES, "styles", name + ".jpg")
    if not os.path.exists(path):
        return P("", cell)
    iw, ih = ImageReader(path).getSize()
    return Image(path, width=width, height=width * ih / iw)


def style_row(num, name, slug, text):
    return [num, f"<b>{name}</b>", thumb(slug, 1.15 * inch), text]


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
    items = ["Install on Mac", "Install on Windows", "Install on Linux", "Quick start", "The toolbar",
             "Logo, Motion, Audio and Look settings", "The 31 visual styles", "Keyboard shortcuts",
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
    s.append(P("Dropocalypse is a free, open-source music visualizer for Mac, Windows and Linux in the spirit of "
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
               "This appears because the portable download isn't code-signed yet. To skip the warning, get the "
               "Microsoft Store edition instead: it's already signed by Microsoft.", body))
    s.append(sub("Microsoft Store edition"))
    s.append(P("Dropocalypse is also published through the <b>Microsoft Store</b>. That copy is <b>already signed by "
               "Microsoft</b>, so Windows opens it without a security warning. It is updated by the Store, "
               "so it has no <b>Updates</b> menu and does not check GitHub. Your logo and settings are still saved in "
               "%APPDATA%\\Dropocalypse. The zip from the Releases page is the portable edition, which updates itself "
               "from GitHub. Find it by searching the Store for <b>Dropocalypse</b>.", body))
    s.append(sub("Listening to music on Windows"))
    s.append(P("By default the Windows version listens to <b>what your PC is playing</b> (Spotify, YouTube, DJ "
               "software, games), so no microphone is needed. It listens in shared mode and does not take over "
               "the speakers, so other apps, including Windows Media Player, keep playing. To use a microphone "
               "or line-in instead, choose it "
               f"under <b>Audio</b> in the toolbar, and turn on <b>Settings {ARROW} Privacy &amp; security {ARROW} "
               f"Microphone {ARROW} Let desktop apps access your microphone</b>.", body))

    # ---- Linux
    s += section(3, "Install on Linux")
    s.append(P("64-bit Ubuntu 22.04 or later, Debian 12, Fedora, Linux Mint, Arch and other current distributions, "
               "with X11 or Wayland, PulseAudio or PipeWire, and OpenGL 3.3 graphics.", small))
    s.append(Spacer(1, 6))
    s += steps([
        "Download the <b>x86_64.AppImage</b> file from the Releases page.",
        f"Make it executable: right-click it {ARROW} <b>Properties</b> {ARROW} <b>Allow executing file as program</b> "
        "(or run <b>chmod +x Dropocalypse-*.AppImage</b> in a terminal).",
        "Double-click it. Nothing is installed: the AppImage is the whole app, and it can live anywhere.",
    ])
    s.append(sub("Listening to music on Linux"))
    s.append(P("By default the Linux version listens to <b>what the computer is playing</b> (Spotify, a browser, DJ "
               "software), so no microphone is needed. Under <b>Audio</b> you can also pick one output "
               "(<b>Playing on: ...</b>), a microphone, a USB interface or a line-in. It works with PulseAudio and "
               "with PipeWire, which current desktops use.", body))
    s.append(sub("Recording and file choosers"))
    s.append(P("Recording to MP4 uses <b>ffmpeg</b>. Install it with your package manager, for example "
               "<b>sudo apt install ffmpeg</b>. Choosing a logo or a folder uses the desktop's file chooser (zenity "
               "or kdialog); you can always drag an image onto the window instead. Prefer a plain folder to an "
               "AppImage? Download the <b>Linux-x86_64.tar.gz</b> and run <b>dropocalypse</b> from it.", body))

    # ---- Quick start
    s += section(4, "Quick start")
    s += steps([
        "Open Dropocalypse, click <b>Audio</b> and pick what to listen to: on a Mac, an app like <b>Spotify</b> "
        "(macOS 14.2+) or a mic; on Windows, System audio is already selected. Play some music and the visuals "
        "start reacting right away.",
        "Drag your logo (PNG, JPG and most image types) onto the window, or press <b>L</b> to choose a file. "
        "A PNG with a transparent background looks best. You can add up to 4 logos, or text, from the Logo menu.",
        "Press <b>F</b> for full screen.",
        "Move the mouse to the top of the screen to open the toolbar and try different styles.",
    ])
    s.append(Spacer(1, 4))
    s.append(P("Your logo, the style you picked and all your settings are saved automatically and come back the "
               "next time you open the app.", body))


    # ---- Toolbar
    s += section(5, "The toolbar")
    s.append(P("Every control lives in one slim bar along the <b>top edge</b> of the window. It stays out of the "
               "way during a show:", body))
    s.append(shot("window.jpg", 4.4 * inch, "Dropocalypse on a Mac, with the toolbar showing along the top"))
    s.append(Spacer(1, 6))
    s += bullets([
        "<b>To show it:</b> move the mouse toward the top of the screen, click anywhere, or press <b>T</b>.",
        "<b>It hides itself</b> after about 2.5 seconds without activity, and the mouse cursor hides with it.",
        "<b>It stays open</b> while your pointer is over it or while one of its menus is open.",
        "<b>To hide it right away</b>, press <b>Esc</b>.",
    ])
    s.append(Spacer(1, 6))
    s.append(shot("toolbar.jpg", 6.4 * inch, "The toolbar, left to right: style, auto-cycle, Logo, Motion, Audio, "
                  "Look, Record, status, then Detach, Screen, Updates and Keys"))
    s.append(Spacer(1, 8))
    s.append(table(["Control", "What it does"], [
        ["<b>‹ · Style name · ›</b>", "Previous style, open the style list, next style. In the list, click a name to show it; the <b>checkbox</b> to its left decides whether auto-cycle includes it (All / None buttons at the top)"],
        ["<b>⟳ Auto</b>", "Turns auto-cycle on or off; it glows green while on. Auto-cycle moves to the next <b>checked</b> style with a smooth crossfade. <b>Right-click</b> it to set how long each style stays up (5 seconds to 6 minutes)"],
        ["<b>Logo</b>", "Your logos and text items, plus size, glow, color tint and trails"],
        ["<b>Motion</b>", "How your logo reacts to the bass"],
        ["<b>Audio</b>", "Which input to listen to, sensitivity and beat threshold. Red when audio isn't running"],
        ["<b>Look</b>", "Beat flash, brightness, landscape or portrait, and frame rate"],
        ["<b>● Record</b> / <b>■ Stop</b>", "Records the visuals and music to an MP4 video, without the toolbar. "
                                          "The ▾ next to it sets the video size and where to save."],
        ["Status text", "The input, its sample rate and buffer size, plus a frame time (for example 6.2 ms) while the toolbar is showing"],
        ["<b>Detach</b>", "Moves the controls into their own window, stacked top to bottom, so they stay on your laptop "
                          "while the visuals play on a TV or projector. <b>Attach</b> puts them back."],
        ["<b>Screen</b>", "Picks the display that shows the visuals full screen, and turns full screen on or off (F)"],
        ["<b>Updates</b>", "Your version, <b>Check now</b> and automatic checks. Turns pink (<b>Update X.Y.Z</b>) when a new "
                           "version is out. <b>Update now</b> checks the download and, if the checks pass, installs "
                           "it and reopens the app. If a check fails, nothing is changed."],
        ["<b>Keys</b>", "Shows the keyboard shortcuts"],
    ], [1.9 * inch, 4.6 * inch]))
    s.append(Spacer(1, 6))
    s.append(sub("On small screens: letters"))
    s.append(P("When the window is too narrow for the full names (a 720p projector, a small laptop), each menu shows "
               "one letter: <b>L</b> Logo, <b>M</b> Motion, <b>A</b> Audio, <b>V</b> Look, <b>S</b> Screen, "
               "<b>U</b> Updates and <b>?</b> Keys. Hold the mouse over a letter to see its name, and the open menu "
               "shows its full name as its title. <b>Detach</b> always shows the whole word. The full names come "
               "back by themselves when there's room, and the keyboard shortcuts don't change.", small))
    s.append(Spacer(1, 4))
    s.append(shot("toolbar-letters.jpg", 5.2 * inch, "The toolbar on a small screen (Windows shown; the Mac matches)"))
    s.append(Spacer(1, 4))
    s.append(P("Every button is a raised key and every slider a mixer fader, the same on Mac, Windows and Linux. "
               "A pink key has its menu open.", small))

    # ---- Panels
    s += section(6, "Logo, Motion, Audio and Look settings")
    s.append(sub("Logos and text (the Logo menu)"))
    s.append(P("Show up to <b>4 logos and text items at once</b>: side by side in landscape, stacked top to bottom "
               "in Portrait. Each shrinks to fit its space, and they all hit on the same beat with a small offset "
               "each, so they don't move like clones.", small))
    s.append(Spacer(1, 4))
    s.append(table(["Item", "What it does"], [
        ["Each item", "<b>Replace…</b> (images), the <b>‹ ›</b> keys to move it (up / down in Portrait), <b>Remove</b>"],
        ["<b>Add Logo…</b> / <b>Add image…</b> (L)", "Adds an image. Empty transparent edges are trimmed automatically so each "
                                 "logo moves around its visible shape."],
        ["<b>Add Text</b>", "Adds a text item: type the words right in the list, then pick Bold, Heavy, Serif or "
                            "Mono and one of six colors. It gets an outline so it reads over bright visuals."],
        ["<b>Remove all</b>", "Goes back to the “YOUR LOGO” placeholder (Mac: File → Remove All Logos)"],
        ["<b>Show logos</b> (G)", "Hides or shows them all without removing them, for visuals only"],
    ], [1.9 * inch, 4.6 * inch]))
    s.append(Spacer(1, 4))
    s.append(P("You can also drag images onto the window at any time; drop several to add them all.", small))

    s.append(sub("Motion menu: how the logo reacts to the bass"))
    s.append(P("0 turns a motion off completely; the highest setting is 1.5.", small))
    s.append(Spacer(1, 4))
    s.append(table(["Slider", "Default", "What it does"], [
        ["<b>Pulse</b>", "0.70", "The logo grows with the bass and shrinks as it fades"],
        ["<b>Squash &amp; stretch</b>", "0.60", "Squashes wide and short on each kick, then springs back like rubber"],
        ["<b>Bounce</b>", "0.50", "Kicks launch the logo upward and it falls back with gravity"],
        ["<b>Slide</b>", "0.40", "Each beat pushes the logo left or right, then a spring pulls it back"],
        ["<b>Wobble</b>", "0.35", "Each beat tips the logo into a quick rotation that settles back"],
        ["<b>Wave</b> (layers)", "0", "Logo shapes hop one after another, left to right. 0 = still, 100 = super wild"],
        ["<b>Explode</b> (layers)", "0", "Logo shapes burst outward from the centre and spring back"],
        ["<b>Bands</b> (layers)", "0", "Each logo shape rides the bass, mids or treble"],
        ["<b>Scatter</b> (layers)", "0", "Every logo shape flies off its own way and spins. Mix any of the four"],
        ["<b>Reset to defaults</b>", "", "Resets the Motion, Logo and Look sliders"],
    ], [1.6 * inch, 0.85 * inch, 4.05 * inch]))
    s.append(Spacer(1, 6))
    s.append(KeepTogether([
        sub("Logo layers"),
        P("Wave, Explode, Bands and Scatter split your logo into its separate shapes (each letter, icon or piece "
          "that doesn't touch another) and move them on their own. Each goes from 0 (still) to 100 (super wild), "
          "and you can mix them. Four moments of each, at a high setting:", body),
        Spacer(1, 4),
        shot("layers-wave.jpg", 6.2 * inch, "<b>Wave</b>: the letters hop one after another, left to right"),
        Spacer(1, 4),
        shot("layers-explode.jpg", 6.2 * inch, "<b>Explode</b>: the letters burst outward on the beat and spring back"),
    ]))
    s.append(Spacer(1, 4))
    s.append(KeepTogether([
        shot("layers-bands.jpg", 6.2 * inch, "<b>Bands</b>: each letter rides the bass, mids or treble"),
        Spacer(1, 4),
        shot("layers-scatter.jpg", 6.2 * inch, "<b>Scatter</b>: every letter flies off its own way and spins"),
    ]))
    s.append(Spacer(1, 6))
    s.append(tip_box("For a calm, branded look, try Pulse 0.5 and everything else at 0. For a festival-style "
                     "look, push Squash, Bounce and Wobble to 1.2 or higher."))
    s.append(Spacer(1, 8))
    s.append(KeepTogether(side_by_side(shot("motion.jpg", 2.4 * inch, "The Motion menu, with the four Logo layers sliders under the logo moves"),
                                       shot("audio.jpg", 2.75 * inch, "The Audio menu"),
                                       widths=[3.25 * inch, 3.25 * inch])))

    s.append(sub("Logo size, glow and trails (under the items in the Logo menu)"))
    s.append(table(["Control", "Default", "What it does"], [
        ["<b>Size</b>", "0.32", "How big the logo is, as a fraction of the screen height"],
        ["<b>Glow</b>", "0.60", "A soft, colored halo that flares with the bass. 0 turns it off."],
        ["<b>Color tint</b>", "0.00", "Blends the logo toward the style's color. 0 keeps your original colors."],
        ["<b>Logo trails</b>", "Off", "Leaves echoes of the logo that swirl and fade with the visuals"],
        ["<b>Show logos</b> (G)", "On", "Hides or shows every logo and text item"],
    ], [1.6 * inch, 0.85 * inch, 4.05 * inch]))

    s.append(sub("Audio menu"))
    s.append(table(["Control", "Default", "What it does"], [
        ["<b>Listen to</b> (Mac)", "Default input",
         "Drop-down of everything you can listen to. <b>Mac audio</b> (macOS 14.2+): <b>All Mac audio</b> or "
         "any open app, such as Spotify (playing apps are marked ♪). <b>Microphone or line-in</b>: built-in "
         "mic, USB mic, audio interface or line-in."],
        ["<b>Input</b> (Windows)", "System audio",
         "“System audio” listens to what the PC is playing, in shared mode, so other apps keep "
         "playing. “Playing on: …” listens to one output device. You can also pick a mic or line-in."],
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

    s.append(sub("Look menu"))
    s.append(table(["Control", "Default", "What it does"], [
        ["<b>Beat flash</b>", "0.40", "The whole screen brightens briefly on each beat"],
        ["<b>Brightness</b>", "1.00", "Overall brightness (0.4 to 1.6). Lower it for dark rooms or bright projectors."],
        ["<b>Frame</b>", "Landscape",
         "<b>Landscape</b> fills the window. <b>Portrait</b> centers a 9:16 frame for Reels and TikToks. "
         "Recordings match it (720×1280, 1080×1920 or 2160×3840). It can't be changed while recording."],
        ["<b>Frame rate</b>", "Display",
         "<b>Display</b> matches the monitor, and the picture is unchanged. <b>60</b> or <b>30</b> use less CPU "
         "on a laptop. The status line shows a frame time, such as 6.2 ms, while the toolbar is visible."],
    ], [1.6 * inch, 0.85 * inch, 4.05 * inch]))

    s.append(CondPageBreak(3.4 * inch))
    s.append(sub("Recording a video"))
    rec_steps = [
        P("1. Click <b>Record</b> (or press <b>R</b>). It turns red and shows a running timer.", body),
        P("2. Play your music. The visuals (without the toolbar) and the music are saved together.", body),
        P("3. Click <b>Stop</b> (or press <b>R</b> again). A message offers to show the file.", body),
        Spacer(1, 4),
        P("Open the <b>▾</b> next to Record to pick the video size (720p or 1080p at 60 fps, or 4K at 30 fps) and "
          "the folder. Videos go to Movies (Mac) or Videos (Windows and Linux) unless you choose another. They are MP4 "
          "files that play anywhere and upload straight to YouTube, Instagram or TikTok.", small),
        Spacer(1, 4),
        P("<b>Portrait</b> (Look menu, Frame) records a vertical 9:16 frame: 720×1280, 1080×1920 or 2160×3840. "
          "On a landscape screen the same frame is centered with black bars on either side, so the preview matches "
          "the file. Landscape still fills the window, and a window that isn't 16:9 is letterboxed into the video.",
          small),
    ]
    s.append(side_by_side(shot("record.jpg", 2.7 * inch, "Recording settings (▾ next to Record)"), rec_steps,
                          widths=[3.0 * inch, 3.5 * inch]))

    # ---- Styles
    s += section(7, "The 31 visual styles")
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
    s.append(table(["#", "Style", "Preview", "What it looks like"], [
        style_row("1", "Classic Tunnel", "classic-tunnel", "The waveform bursts out of the center into an endless twisting tunnel"),
        style_row("2", "Spectrum Skyline", "spectrum-skyline", "Spectrum bars along the bottom with trails rising like smoke"),
        style_row("3", "Starburst", "starburst", "Rays shoot out of the center, one per frequency band"),
        style_row("4", "Oscilloscope Drift", "oscilloscope-drift", "A glowing waveform that ripples, rotates and drifts"),
        style_row("5", "Kaleido Bloom", "kaleido-bloom", "A six-way kaleidoscope blooming outward from the center"),
        style_row("6", "Liquid Plasma", "liquid-plasma", "Flowing liquid colors with waveform rings melting into them"),
        style_row("7", "Vortex", "vortex", "A three-armed spiral of dots whipping around the center"),
        style_row("8", "Lissajous Neon", "lissajous-neon", "A mirrored neon scribble drawn by the sound wave itself"),
        style_row("9", "Mirror Hall", "mirror-hall", "Waveforms reflected four ways into an infinite hall of mirrors"),
        style_row("10", "Black Hole", "black-hole", "Everything is pulled inward and spirals into the center"),
        style_row("11", "Bass Ring", "bass-ring", "A circular spectrum wrapped around your logo, with sparks on every kick"),
        style_row("12", "Neon Equalizer", "neon-equalizer", "Smooth neon bars across the screen with a glassy reflection"),
        style_row("13", "Pulsar Ridges", "pulsar-ridges", "Stacked white ridgelines that span the full screen and scroll upward, the classic “joy plot” look"),
        style_row("14", "Outrun Grid", "outrun-grid", "A synthwave sunset: smooth striped sun, a full-width sunset-colored meter and a racing neon grid"),
        style_row("15", "Hyperspace", "hyperspace", "A starfield that jumps to warp speed on the drops"),
        style_row("16", "Particle Storm", "particle-storm", "Hundreds of glowing particles flowing in currents, scattering on beats"),
        style_row("17", "Lightning Storm", "lightning-storm", "Lightning cracking out from behind the logo on the beat"),
        style_row("18", "Digital Rain", "digital-rain", "Columns of green glyphs raining down, faster with the bass"),
        style_row("19", "Lava Lamp", "lava-lamp", "Soft blobs of color drifting and melting together"),
        style_row("20", "Inferno", "inferno", "Embers and flames rising from the bottom of the screen"),
        style_row("21", "Mirror Equalizer", "mirror-equalizer", "Bars grow up and down from a center line, in a sunset rainbow"),
        style_row("22", "LED Meter", "led-meter", "A classic analyzer: green-yellow-red LED segments with peak markers"),
        style_row("23", "Rainbow Peaks", "rainbow-peaks", "Thin rainbow bars with glowing caps that bounce on the beat"),
        style_row("24", "Tesseract", "tesseract", "A neon 4D hypercube turning itself inside out, spinning harder on every beat"),
        style_row("25", "Full Kaleidoscope", "full-kaleidoscope",
         "A full-frame mandala: 4-way mirroring that sometimes reads as 8-way, in neon cyan, magenta, blue, gold and green. Bass zooms and turns it."),
        style_row("26", "Main Stage", "main-stage",
         "The view from an arena floor: beam fans meet overhead like a cathedral ceiling, lasers fire on the beat and a crowd bounces with phones up"),
        style_row("27", "Monstercat Bars", "monstercat-bars", "A tidy row of smooth, pale spectrum bars above the logo"),
        style_row("28", "Lo-fi Chill", "lo-fi-chill", "A snowy night town under a big moon, with fog and a small soft equalizer"),
        style_row("29", "Glitch VHS", "glitch-vhs", "Worn videotape: the picture tears, splits into colour fringes and wobbles on the beat"),
        style_row("30", "Liquid Chrome", "liquid-chrome", "Flowing polished liquid metal that swells and ripples with the bass"),
        style_row("31", "Kinetic Text", "kinetic-text", "Rows of your first logo or text item sliding across; the beat lights a row"),
    ], [0.4 * inch, 1.35 * inch, 1.35 * inch, 3.4 * inch], swatches=[hsv_color(h_, 0.8, 0.85) for h_ in STYLE_HUES]))
    s.append(Spacer(1, 8))
    s.append(tip_box("Bass Ring and Particle Storm are built around a centered logo, so turn Bounce and Slide "
                     "down to keep it in place. For Pulsar Ridges and Outrun Grid, a smaller logo Size (about 0.2) "
                     "keeps it from covering the scene."))
    s.append(Spacer(1, 8))
    s.append(KeepTogether([
        sub("MilkDrop presets"),
        P("After the built-in styles come 9 classic MilkDrop presets, played by the open-source projectM engine: "
          "Acid Mandala, Flame Face, Mind Blob, Geiss Lightning, Dark Galaxy, Hypraxis, Planar Travel, Box of Tricks "
          "and Lorenz Chaser.", body),
        Spacer(1, 4),
        P("<b>Add your own:</b> in the style list, under <b>Your MilkDrop presets</b>, click <b>Choose folder</b> and "
          "pick a folder of .milk files (subfolders too). Each preset becomes a style you can search for and pick. "
          "They stay out of auto-cycle unless you turn on <b>Include these presets in auto-cycle</b>.", body)]))
    milk = [("milk-acid-mandala", "Acid Mandala"), ("milk-flame-face", "Flame Face"), ("milk-mind-blob", "Mind Blob"),
            ("milk-geiss-lightning", "Geiss Lightning"), ("milk-dark-galaxy", "Dark Galaxy"),
            ("milk-hypraxis", "Hypraxis"), ("milk-planar-travel", "Planar Travel"),
            ("milk-box-of-tricks", "Box of Tricks"), ("milk-lorenz-chaser", "Lorenz Chaser")]
    cap = ParagraphStyle("mcap", parent=small, alignment=TA_CENTER)
    grid = [[[thumb(sl, 2.0 * inch), Spacer(1, 2), P(f"<b>{nm}</b>", cap)] for sl, nm in milk[r * 3:r * 3 + 3]]
            for r in range(3)]
    gt = Table(grid, colWidths=[2.17 * inch] * 3)
    gt.setStyle(TableStyle([("ALIGN", (0, 0), (-1, -1), "CENTER"), ("VALIGN", (0, 0), (-1, -1), "TOP"),
                            ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    s.append(Spacer(1, 6))
    s.append(gt)

    # ---- Shortcuts
    s += section(8, "Keyboard shortcuts")
    s.append(table(["Action", "Mac", "Windows and Linux"], [
        ["Full screen on / off", f"F or Ctrl+{CMD}F (or the green window button)", "F or F11"],
        ["Show / hide the toolbar", "T or S", "T or S"],
        ["Hide the toolbar", "Esc (press again to exit full screen)", "Esc (press again to exit full screen)"],
        ["Add a logo", f"L or {CMD}O", "L or Ctrl+O"],
        ["Show / hide the logos", "G", "G"],
        ["Previous / next style", f"{LEFT} / {RIGHT}  (or {CMD}{LEFT} / {CMD}{RIGHT})", f"{LEFT} / {RIGHT}"],
        ["Auto-cycle on / off", "A", "A"],
        ["Detach / attach the controls window", f"D or Shift+{CMD}D", "D"],
        ["Start / stop recording", f"R or Shift+{CMD}R", "R"],
    ], [2.4 * inch, 2.0 * inch, 2.1 * inch]))
    s.append(Spacer(1, 6))
    s.append(P(f"On the Mac, the menu bar has the same actions under <b>File</b> and <b>Visualizer</b>, including "
               f"<b>Full Screen</b> and <b>Restart Audio</b>.", small))

    # ---- Live show
    s += section(9, "Setting up for a live show")
    s += steps([
        "<b>Playing from the same computer?</b> On a Mac, pick your music or DJ app (or All Mac audio) under "
        "<b>Audio</b>. On Windows, choose <b>System audio</b>; on Linux the default already listens to what the computer plays. It's the cleanest signal, with no cables or mic.",
        "<b>Use a direct feed if you can.</b> Run the booth, record or aux output of your mixer or DJ controller "
        "into a USB audio interface and pick it under Audio. A direct feed reacts more cleanly than a mic in a "
        "loud room.",
        "<b>Using a mic?</b> Point it toward the speakers or subwoofer. Raise Sensitivity in a quiet room; lower it "
        "if crowd noise is driving the visuals.",
        "<b>Tune the beat.</b> Watch the logo during a track with a clear kick. If it misses kicks, lower Beat "
        "threshold. If it jumps on hi-hats or vocals, raise it.",
        "<b>Go full screen</b> on the projector or TV: drag the window onto that screen and press F. Full screen "
        "covers every pixel, edge to edge. On the Mac the menu bar and Dock disappear, and on MacBooks with a "
        "camera notch the visuals fill the strip beside the notch too. In <b>Portrait</b>, full screen centers "
        "the 9:16 frame and leaves the sides black.",
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
    s += section(10, "Troubleshooting")
    s.append(table(["Problem", "Fix"], [
        ["<b>Mac: “Dropocalypse can't be opened”</b>",
         "Download the latest version: since 1.0.1 the Mac app is signed and notarized and opens normally. "
         "For older copies, see section 1."],
        ["<b>Windows: “Windows protected your PC”</b>",
         f"Click <b>More info {ARROW} Run anyway</b>. The portable download isn't code-signed yet. "
         "Or get the Microsoft Store edition: it's already signed by Microsoft, so it skips this warning."],
        ["<b>Update now doesn't install</b>",
         "Nothing was changed. Open the <b>Updates</b> menu in the toolbar (not Look). On a Mac the new app must be "
         "signed with the same Developer ID and accepted by Gatekeeper, and Dropocalypse must be in a folder you can "
         "edit, such as Applications. On Windows, once this copy is code-signed the download must be signed by the "
         "same publisher. Or use <b>Download page</b>."],
        ["<b>Linux: the AppImage doesn't open</b>",
         "Make it executable first (Properties, or chmod +x). On Ubuntu 22.04 and older, if it says FUSE is missing, "
         "install <b>libfuse2</b>, or run it with <b>--appimage-extract-and-run</b>."],
        ["<b>Linux: no file chooser, or recording won't start</b>",
         "Install <b>zenity</b> (or kdialog on KDE) for the file chooser and <b>ffmpeg</b> for recording."],
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
        ["<b>Windows: another app can't play while Dropocalypse is open</b>",
         "Fixed in 1.2.9. Windows Media Player used to say it encountered a problem while playing the file. "
         "Update from the <b>Updates</b> menu. Dropocalypse only listens, in shared mode, and the status line "
         "shows a buffer of about 10 ms."],
        ["<b>Windows: two copies stay open after Update now</b>",
         "Fixed in 1.2.10. Update now closes the old Dropocalypse before the new one starts. If you still see "
         "two, quit both in Task Manager and open the app once. Starting it again while it is already open "
         "brings that window forward."],
        ["<b>It reacts, but only a little</b>",
         "Raise Sensitivity and lower Beat threshold. Check the input level in your system Sound settings."],
        ["<b>It twitches when no music is playing</b>", "Lower Sensitivity, or use a direct line-in feed"],
        ["<b>My app or USB interface isn't listed</b>", "Open the app or plug it in, then click <b>Refresh list</b> in the Audio menu"],
        ["<b>My logo looks blurry</b>", "Use a larger image: about 1000 pixels on the long side is ideal"],
        ["<b>My logo has a white box around it</b>",
         "The image has a solid background. Export it as a PNG with a transparent background."],
        ["<b>The toolbar won't appear</b>", "Move the mouse to the very top of the window, click anywhere, or press T"],
    ], [2.3 * inch, 4.2 * inch]))

    # ---- Settings / uninstall
    s += section(11, "Where your settings are saved, and how to remove the app")
    s.append(KeepTogether(table(["", "Mac", "Windows", "Linux"], [
        ["<b>Settings and logo</b>",
         "Saved automatically by macOS for the app, plus your logo in ~/Library/Application Support/Dropocalypse",
         "%APPDATA%\\Dropocalypse",
         "~/.config/dropocalypse"],
        ["<b>To remove the app</b>",
         "Drag Dropocalypse from Applications to the Trash, and delete the Application Support folder above",
         "Delete the Dropocalypse folder you extracted and the %APPDATA%\\Dropocalypse folder",
         "Delete the AppImage (or the folder) and ~/.config/dropocalypse"],
    ], [1.0 * inch, 2.0 * inch, 1.8 * inch, 1.7 * inch])))
    s.append(Spacer(1, 14))
    s.append(KeepTogether([
        sub("About"),
        P("Dropocalypse is free and open-source software under the MIT License: free to use, modify and share, "
          "including commercially. The full source code, the latest downloads and the online version of this "
          "guide are at <b>github.com/allenrobberson/dropocalypse</b>.", body),
        P("Inspired by Ryan Geiss's MilkDrop and Nullsoft's Winamp. The built-in styles are original code; MilkDrop "
          "presets play through projectM (LGPL) and belong to their authors.", small),
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
                            subject="How to install and use Dropocalypse on Mac, Windows and Linux")
    doc.build(build(), onFirstPage=cover_page, onLaterPages=later_pages)
    with open(VERSION_FILE, "w", encoding="utf-8") as f:
        f.write(VERSION + "\n")
    print(f"Wrote {OUT} (version {VERSION})")


if __name__ == "__main__":
    sys.exit(main())
