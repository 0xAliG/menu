#pragma once

// ─── FontAwesome 6 Free Solid — UTF-8 glyph literals ──────────────────────
//
// Small, curated set of icons the menu framework and its clients actually
// use. Every entry is the UTF-8 encoding of the FA codepoint noted in the
// trailing comment (e.g. f013 = cog). Extend as needed — encode with any
// UTF-8 calculator or by hand: for codepoints 0x0800..0xFFFF the bytes
// are 0xE0 | (cp>>12), 0x80 | ((cp>>6)&0x3F), 0x80 | (cp & 0x3F).
//
// Consumers just `#include "fa_icons.h"` — no linkage cost, the macros
// expand to string literals at every call site.

#define ICON_FA_COG         "\xef\x80\x93"   // f013
#define ICON_FA_EYE         "\xef\x81\xae"   // f06e
#define ICON_FA_SLIDERS     "\xef\x87\x9e"   // f1de
#define ICON_FA_PALETTE     "\xef\x94\xbf"   // f53f
#define ICON_FA_USER        "\xef\x80\x87"   // f007
#define ICON_FA_CROSSHAIRS  "\xef\x81\x9b"   // f05b
#define ICON_FA_RUNNING     "\xef\x9c\x8c"   // f70c
#define ICON_FA_GLOBE       "\xef\x82\xac"   // f0ac
#define ICON_FA_WRENCH      "\xef\x82\xad"   // f0ad
#define ICON_FA_CODE        "\xef\x84\xa1"   // f121
#define ICON_FA_BOLT        "\xef\x83\xa7"   // f0e7
#define ICON_FA_FLOPPY      "\xef\x83\x87"   // f0c7
#define ICON_FA_BUG         "\xef\x86\x88"   // f188
