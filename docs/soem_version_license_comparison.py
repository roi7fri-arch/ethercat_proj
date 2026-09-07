#!/usr/bin/env python3
"""Generate the SOEM 1.3.1 vs 2.0.0 version + license comparison PDF.

Offline-friendly: uses only reportlab (no network, no external binaries).
Run:  python3 soem_version_license_comparison.py
Output: soem_version_license_comparison.pdf  (next to this script)
"""
import os
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, HRFlowable,
)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "soem_version_license_comparison.pdf")

# ----------------------------------------------------------------------------
# Styles
# ----------------------------------------------------------------------------
ss = getSampleStyleSheet()
NAVY = colors.HexColor("#1a2b4a")
ACCENT = colors.HexColor("#0b5394")
LIGHT = colors.HexColor("#e8eef7")
GREY = colors.HexColor("#555555")

styles = {
    "title": ParagraphStyle("title", parent=ss["Title"], fontSize=20,
                            textColor=NAVY, spaceAfter=4, leading=24),
    "subtitle": ParagraphStyle("subtitle", parent=ss["Normal"], fontSize=10,
                               textColor=GREY, spaceAfter=14),
    "h1": ParagraphStyle("h1", parent=ss["Heading1"], fontSize=14,
                         textColor=ACCENT, spaceBefore=14, spaceAfter=6,
                         leading=17),
    "h2": ParagraphStyle("h2", parent=ss["Heading2"], fontSize=11.5,
                         textColor=NAVY, spaceBefore=10, spaceAfter=4),
    "body": ParagraphStyle("body", parent=ss["Normal"], fontSize=10,
                          leading=14.5, spaceAfter=6, alignment=TA_LEFT),
    "bullet": ParagraphStyle("bullet", parent=ss["Normal"], fontSize=10,
                            leading=14, leftIndent=12, bulletIndent=2,
                            spaceAfter=3),
    "quote": ParagraphStyle("quote", parent=ss["Normal"], fontSize=9.5,
                           leading=13, leftIndent=12, rightIndent=10,
                           textColor=GREY, backColor=LIGHT, borderPadding=6,
                           spaceBefore=4, spaceAfter=8, fontName="Helvetica-Oblique"),
    "cell": ParagraphStyle("cell", parent=ss["Normal"], fontSize=9, leading=12),
    "cellh": ParagraphStyle("cellh", parent=ss["Normal"], fontSize=9,
                           leading=12, textColor=colors.white,
                           fontName="Helvetica-Bold"),
    "small": ParagraphStyle("small", parent=ss["Normal"], fontSize=8.5,
                           leading=11, textColor=GREY, spaceBefore=4),
}


def P(text, style="body"):
    return Paragraph(text, styles[style])


def bullets(items):
    return [Paragraph(f"\u2022&nbsp;&nbsp;{t}", styles["bullet"]) for t in items]


def make_table(rows, col_widths, header=True):
    data = []
    for r, row in enumerate(rows):
        style = "cellh" if (header and r == 0) else "cell"
        data.append([Paragraph(c, styles[style]) for c in row])
    t = Table(data, colWidths=col_widths, repeatRows=1 if header else 0)
    ts = [
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("GRID", (0, 0), (-1, -1), 0.5, colors.HexColor("#c9d3e2")),
    ]
    if header:
        ts += [
            ("BACKGROUND", (0, 0), (-1, 0), NAVY),
            ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, LIGHT]),
        ]
    else:
        ts += [("ROWBACKGROUNDS", (0, 0), (-1, -1), [colors.white, LIGHT])]
    t.setStyle(TableStyle(ts))
    return t


def hr():
    return HRFlowable(width="100%", thickness=0.6, color=colors.HexColor("#c9d3e2"),
                     spaceBefore=6, spaceAfter=6)


# ----------------------------------------------------------------------------
# Content
# ----------------------------------------------------------------------------
story = []
story.append(P("SOEM 1.3.1 vs 2.0.0", "title"))
story.append(P("Version differences and license implications for the EtherCAT master project "
               "&mdash; Xilinx Zynq UltraScale+ MPSoC / PREEMPT-RT Linux. Generated 2026-08-31.",
               "subtitle"))
story.append(hr())

# --- Section 1: License (put first, it's the decisive one) ------------------
story.append(P("1. License change (the decisive difference)", "h1"))
story.append(P("The license changed between the two versions in a way that directly affects "
               "whether this master application can stay proprietary.", "body"))

story.append(P("SOEM 1.3.1 &mdash; GPLv2 <b>with a linking exception</b>", "h2"))
story.append(P("The source header shipped in the workspace states:", "body"))
story.append(P("&ldquo;As a special exception, if other files instantiate templates or use macros "
               "or inline functions from this file, or you compile this file and link it with other "
               "works to produce a work based on this file, this file does not by itself cause the "
               "resulting work to be covered by the GNU General Public License. However the source "
               "code for this file must still be made available in accordance with section (3) of "
               "the GNU General Public License.&rdquo;", "quote"))
story.append(P("Practical effect: you may keep the master application "
               "(<font face='Courier'>elmo_com.c</font>, <font face='Courier'>ecat_diag.c</font>, "
               "<font face='Courier'>ecat_foe.c</font>, the config GUI, etc.) <b>closed / "
               "proprietary</b>. The only copyleft obligation is that if you <i>modify SOEM's own "
               "source files</i>, those modified files must be made available. Merely linking your "
               "app against SOEM does not force your app open.", "body"))

story.append(P("SOEM 2.0.0 (and current master) &mdash; GPLv3 <b>or</b> commercial, no exception", "h2"))
story.append(P("The 2.0.0 LICENSE.md states:", "body"))
story.append(P("&ldquo;This software is dual-licensed. GPL version 3 &mdash; distributed under GPLv3; "
               "you are allowed to use this software for an open-source project with a compatible "
               "license. Commercial license &mdash; also available with options for support and "
               "maintenance (sales@rt-labs.com). If you intend to use this stack in a commercial "
               "product, you likely need to buy a license.&rdquo;", "quote"))
story.append(P("The linking exception is <b>gone</b>. Under plain GPLv3, statically linking your "
               "master into a product makes the <b>combined binary a GPLv3 work</b> &mdash; you must "
               "offer the full corresponding source of your application under GPLv3, or obtain "
               "rt-labs' commercial license.", "body"))

story.append(P("License comparison", "h2"))
story.append(make_table([
    ["", "SOEM 1.3.1", "SOEM 2.0.0"],
    ["Base license", "GPLv2 + linking exception", "GPLv3, or commercial"],
    ["Link a closed app to it?", "Yes &mdash; app stays proprietary",
     "No &mdash; GPLv3 copyleft covers the whole combined work"],
    ["Obligation when you ship",
     "Publish only modified SOEM files",
     "Publish the entire linked program under GPLv3, or buy a commercial license"],
    ["Patent / anti-tivoization", "GPLv2 terms",
     "GPLv3: stronger patent grant + anti-tivoization (Installation Information)"],
], col_widths=[38 * mm, 60 * mm, 62 * mm]))

story.append(P("Embedded-target note: GPLv3's anti-tivoization clause is relevant because this runs "
               "on a locked-down Zynq/embedded board. Distributing GPLv3 code on hardware you lock "
               "down can trigger obligations to provide &ldquo;Installation Information&rdquo; so the "
               "user can run modified versions.", "small"))

story.append(P("License bottom line", "h2"))
for b in bullets([
    "Internal / research use, or you are happy to release your code under GPLv3: the upgrade is fine.",
    "Closed commercial product: 1.3.1&rarr;2.0.0 flips you from &ldquo;link freely, keep code closed&rdquo; "
    "to &ldquo;open your whole app <b>or</b> pay rt-labs.&rdquo; That cost often outweighs the technical gains.",
]):
    story.append(b)

story.append(hr())

# --- Section 2: Technical differences ---------------------------------------
story.append(P("2. Technical differences (what you gain by upgrading)", "h1"))

story.append(P("Bandwidth / performance", "h2"))
for b in bullets([
    "<b>Overlapping IOmap</b> (<font face='Courier'>ec_config_overlap_map</font>): RxPDO and TxPDO "
    "share the same logical address region, roughly halving process-data frame size. 1.3.1 has no "
    "equivalent. The main concrete runtime win &mdash; matters if you scale slave count or push the "
    "cycle rate (your 250&nbsp;&micro;s / 4&nbsp;kHz target).",
]):
    story.append(b)

story.append(P("Robustness (8+ years of fixes)", "h2"))
for b in bullets([
    "Mailbox handling &mdash; including the emergency-reread behavior worked around in the simulator "
    "&mdash; plus EEPROM/SII, DC delay calculation, LRW/segmented frames, and FoE/SoE/EoE fixes.",
    "1.3.1 (~2013/2015) is effectively end-of-life: no upstream fixes, and modern GCC flags much of its code.",
    "Redundancy: <font face='Courier'>ec_init_redundant</font> path has refinements in 2.0.0.",
]):
    story.append(b)

story.append(P("API / architecture", "h2"))
for b in bullets([
    "2.0.0 folds all sub-arrays (<font face='Courier'>port, slavelist, grouplist, esibuf, elist, "
    "idxstack</font>) into a single <font face='Courier'>ecx_contextt</font>. In 1.3.1 a context "
    "means wiring ~a dozen separate globals. Enables multiple masters per process and is more "
    "thread-safe (relevant since the recovery thread and RT loop both touch the bus).",
    "<b>CMake</b> build with proper toolchain files instead of hand-rolled per-arch Makefiles &mdash; "
    "cleaner aarch64 cross-compile and out-of-tree builds.",
    "The global <font face='Courier'>ec_*</font> API remains available via EC_VER1, so most of the "
    "app port is mechanical.",
]):
    story.append(b)

story.append(P("Protocol coverage you currently skip", "h2"))
for b in bullets([
    "<b>CoE Complete Access</b> and more robust SDO-info (OD list/description) &mdash; exactly why an "
    "online object-dictionary browser was deferred; far more usable in 2.0.0.",
    "More complete <b>EoE</b> (diagnostics over EtherCAT) and <b>SoE/IDN</b> handling for non-CoE drives.",
]):
    story.append(b)

story.append(P("What you would NOT gain", "h2"))
for b in bullets([
    "Still a soft-RT, single-process, non-hard-DC master. No fundamental architecture change.",
    "Everything already built &mdash; <font face='Courier'>ecat_diag</font> (0x0300 block), EMCY via "
    "<font face='Courier'>ec_iserror</font>/<font face='Courier'>ec_elist2string</font>, identity via "
    "<font face='Courier'>ec_slave[].eep_*</font>, topology via <font face='Courier'>ec_slave[].topology</font>, "
    "DC 0x092C, FoE via <font face='Courier'>ec_FOEwrite</font> &mdash; exists in 2.0.0 under the same "
    "EC_VER1 names.",
]):
    story.append(b)

story.append(hr())

# --- Section 3: Migration cost ----------------------------------------------
story.append(P("3. Migration cost, specific to this repository", "h1"))
for b in bullets([
    "Rebuild the aarch64 static libs (<font face='Courier'>libsoem/liboshw/libosal</font>) for Zynq "
    "via a CMake toolchain file &mdash; the project currently ships prebuilt <font face='Courier'>.a</font> files.",
    "Port the no-hardware sim: <font face='Courier'>nicdrv_sim.c</font> replaces the oshw nicdrv, and "
    "2.0.0's port interface (<font face='Courier'>ecx_portt</font>) is context-based with changed "
    "signatures &mdash; the shim and the sim Makefile need reworking.",
    "Optionally keep the global <font face='Courier'>ec_*</font> wrappers (EC_VER1) to minimize app churn, "
    "then re-run the full test suite.",
    "Offline PC has no internet: fetch the 2.0.0 source on the build machine and copy it in.",
]):
    story.append(b)

story.append(hr())

# --- Section 4: Recommendation ----------------------------------------------
story.append(P("4. Recommendation", "h1"))
for b in bullets([
    "If the current bus (2&ndash;3 Elmo CSP axes, single master) meets timing and the custom parity "
    "features are tested and working, staying on 1.3.1 is low-risk and functionally fine.",
    "Upgrade mainly buys overlapping-IOmap bandwidth, accumulated bug fixes/robustness, OD-browsing / "
    "EoE, and a supported baseline &mdash; weigh against the sim/nicdrv + CMake + cross-lib re-port effort.",
    "<b>License is often the deciding factor:</b> if this may become a closed commercial product, the "
    "GPLv2+exception &rarr; GPLv3/commercial change is the single biggest reason to stay on 1.3.1 (or "
    "budget for an rt-labs commercial license).",
]):
    story.append(b)

story.append(Spacer(1, 8))
story.append(P("This document is engineering guidance, not legal advice. If a commercial product is in "
               "scope, confirm license obligations with counsel or request a quote from rt-labs "
               "(sales@rt-labs.com) before committing to SOEM 2.0.0.", "small"))


# ----------------------------------------------------------------------------
def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 7.5)
    canvas.setFillColor(GREY)
    canvas.drawString(20 * mm, 12 * mm,
                     "SOEM 1.3.1 vs 2.0.0 \u2014 version & license comparison")
    canvas.drawRightString(190 * mm, 12 * mm, "Page %d" % doc.page)
    canvas.restoreState()


def build():
    doc = SimpleDocTemplate(
        OUT, pagesize=A4,
        leftMargin=20 * mm, rightMargin=20 * mm,
        topMargin=18 * mm, bottomMargin=20 * mm,
        title="SOEM 1.3.1 vs 2.0.0 - version & license comparison",
        author="EtherCAT master project",
    )
    doc.build(story, onFirstPage=footer, onLaterPages=footer)
    print("Wrote", OUT)


if __name__ == "__main__":
    build()
