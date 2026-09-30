// ============================================================================
// demo_menu.cpp — this project's implementation of Menu::SetupClient() /
// Menu::TickClient(). The framework knows nothing about aimbot / esp /
// weapons / configs / etc; it just calls SetupClient once at Init and
// TickClient every frame. This file is where the whole demo widget schema
// lives, along with the backing state each widget writes to and the
// per-frame sync into the DebugWindow / ConfigSystem modules.
// ============================================================================

#include "menu.h"
#include "menu_builder.h"
#include "menu_style.h"
#include "ui_components.h"
#include "config_system.h"
#include "debug_window.h"
#include "user_system.h"
#include "keybind_system.h"
#include "fa_icons.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ─── Menu state ────────────────────────────────────────────────────────────

// Combat — aimbot
static bool  s_aimbot          = false;
static float s_aimbotFov       = 30.0f;
static float s_aimbotSmoothing = 0.5f;
static int   s_aimbotBone      = 0;
static bool  s_hitboxHead      = true;
static bool  s_hitboxChest     = true;
static bool  s_hitboxArms      = false;
static bool  s_hitboxLegs      = false;
// Aimbot — cog settings
static bool  s_fovPrediction  = false;
static int   s_fovShape       = 0;   // 0=circle, 1=square
static const char* const s_fovShapeItems[] = { "circle", "square" };
static int   s_smoothCurve    = 0;   // 0=linear, 1=ease, 2=snap
static const char* const s_smoothCurveItems[] = { "linear", "ease-out", "snap" };
static bool  s_boneSway       = true;

// Combat — triggerbot
static bool s_trigger        = false;
static int  s_triggerDelay   = 50;
static bool s_triggerVisible = true;
// Triggerbot — cog settings
static int  s_triggerJitter  = 10;
static bool s_triggerBurst   = false;

// Combat — anti-aim demo (font preview labels)
static float s_yawJitter      = 12.5f;
static float s_yawAdditive    = 0.0f;
static float s_yawJitterRange = 45.0f;
static bool  s_fakeLag        = false;

// Combat — per-weapon configs (icon-selector demo)
enum WeaponSlot { WEP_AR = 0, WEP_SMG, WEP_LMG, WEP_SNIPER, WEP_PISTOL, WEP_COUNT };
static int   s_wepClass               = WEP_AR;
static bool  s_wepEnabled[WEP_COUNT]  = { true, false, false, false, false };
static float s_wepFov     [WEP_COUNT] = { 25.0f, 18.0f, 30.0f, 8.0f, 12.0f };
static float s_wepSmooth  [WEP_COUNT] = { 0.5f, 0.3f, 0.7f, 0.85f, 0.4f };
static bool  s_wepShared              = true;
static int   s_wepPriority            = 1;
static const char* const s_wepPriorityItems[] = { "closest", "lowest hp", "crosshair" };

// Visuals — ESP
static bool  s_espBox       = true;
static bool  s_espSkeleton  = false;
static bool  s_espHealth    = true;
static bool  s_espName      = true;
static float s_espColor[4]  = { 0.518f, 0.545f, 0.761f, 1.0f };  // Steel accent #848bc2
// Skeleton fill+outline — demo of the multi-color inline picker (2 slots).
static float s_espSkelColors[2][4] = {
    { 1.0f, 1.0f, 1.0f, 1.0f },  // fill
    { 0.0f, 0.0f, 0.0f, 1.0f },  // outline
};
static const char* const s_espSkelColorNames[] = { "fill", "outline" };
static bool  s_fullbright   = false;
static float s_accentColor[4] = { 0.518f, 0.545f, 0.761f, 1.0f };
// ESP box — cog settings
static float s_espBoxThickness = 1.5f;
static int   s_espBoxStyle     = 0;
static const char* const s_espBoxStyleItems[] = { "corners", "full", "3d" };
static bool  s_espBoxOutline   = true;
// Accent color — cog settings
static bool  s_syncAccentToMenu = true;
static float s_accentGlow       = 0.3f;

// Visuals — searchable list demo (Apex Legends roster)
static const char* const s_legendItems[] = {
    "Alter", "Ash", "Ballistic", "Bangalore", "Bloodhound", "Catalyst",
    "Caustic", "Conduit", "Crypto", "Fuse", "Gibraltar", "Horizon",
    "Lifeline", "Loba", "Mad Maggie", "Mirage", "Newcastle", "Octane",
    "Pathfinder", "Rampart", "Revenant", "Seer", "Valkyrie", "Vantage",
    "Wattson", "Wraith"
};
static int s_legendSelected = 0;

// Movement
static float s_speedMult  = 1.0f;
static bool  s_bunnyhop   = false;
static bool  s_bhopAutoJump   = true;
static bool  s_bhopHoldSpace  = false;
static float s_bhopStrength   = 1.0f;

// Legend filter — per-legend settings (behind per-item cogs)
static int   s_legendPriority[26] = {};
static bool  s_legendMuted[26]    = {};

// Misc
static bool s_watermark   = false;
static bool s_noRecoil    = false;
static char s_nameTag[64] = "";
static float s_nameTagColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static int   s_nameTagFontSize = 14;
static bool  s_nameTagOutline  = true;

// Debug window multi-instance demo
static bool s_dbgPerf   = false;
static bool s_dbgAimbot = false;
static bool s_dbgWorld  = false;

// Config schema verification
static bool s_schemaTestFlag  = false;
static int  s_schemaTestValue = 50;

// Scroll-demo state
static constexpr int kScrollDemoRows = 24;
static bool  s_scrollDemoBools[kScrollDemoRows]  = {};
static float s_scrollDemoFloats[kScrollDemoRows] = {};
static const char* const s_scrollDemoComboItems[] = {
    "off", "low", "medium", "high", "ultra"
};
static int s_scrollDemoCombos[kScrollDemoRows] = {};

// Configs UI state — backing storage for the listbox + input textbox
static std::vector<std::string> s_cfgNamesOwned;
static std::vector<const char*> s_cfgNamesPtrs;
static int                       s_cfgSelected     = 0;
static int                       s_cfgActiveIndex  = -1;
static char                      s_cfgInputName[64] = {};
static bool                      s_cfgAutoSave     = false;

static const char* GetSelectedCfgName() {
    if (s_cfgSelected < 0 || s_cfgSelected >= (int)s_cfgNamesOwned.size()) return "";
    return s_cfgNamesOwned[s_cfgSelected].c_str();
}

static void RefreshConfigList() {
    s_cfgNamesOwned = ConfigSystem::List();
    s_cfgNamesPtrs.clear();
    s_cfgNamesPtrs.reserve(s_cfgNamesOwned.size());
    for (auto& s : s_cfgNamesOwned) s_cfgNamesPtrs.push_back(s.c_str());
    if (s_cfgSelected >= (int)s_cfgNamesOwned.size())
        s_cfgSelected = std::max(0, (int)s_cfgNamesOwned.size() - 1);
    MenuBuilder::Builder::Get().UpdateListItems(
        "misc.configs.list",
        s_cfgNamesPtrs.empty() ? nullptr : s_cfgNamesPtrs.data(),
        (int)s_cfgNamesPtrs.size());
}

static const char* const s_boneItems[] = { "head", "neck", "chest" };

// Aimbot — target filter demo (MultiCombo)
static const char* const s_targetItems[] = { "enemies", "friendlies", "bots", "npcs" };
static bool s_targetSelected[4] = { true, false, false, false };

// Regression reproduction: dropdown immediately above a cog-bearing row.
static int  s_cogClickCombo = 0;
static bool s_cogClickCheckbox = false;
static const char* const s_cogClickItems[] = { "first", "second", "third" };

// ─── Demo Menu Setup ───────────────────────────────────────────────────────

static void SetupDemoMenu() {
    auto& b = MenuBuilder::Builder::Get();
    b.Clear();

    // Tab 1: Combat
    auto combat    = b.AddTab("Combat", ICON_FA_CROSSHAIRS);

    auto combatAim = combat.AddSubTab("Aimbot", ICON_FA_CROSSHAIRS);
    combatAim.AddContainer("Aimbot", 0)
        .AddCheckbox("Enabled", &s_aimbot, "combat.aimbot.enabled")
            .WithKeybindName("aimbot")
        .AddSliderFloat("Fov", &s_aimbotFov, 0, 180, 1, "\xC2\xB0", "combat.aimbot.fov")
            .BeginSettings()
                .AddCheckbox("Prediction",  &s_fovPrediction, "combat.aimbot.fov.prediction")
                .AddCombo   ("Shape",       &s_fovShape, s_fovShapeItems, 2,
                                                              "combat.aimbot.fov.shape")
            .EndSettings()
        .AddSliderFloat("Smoothing", &s_aimbotSmoothing, 0.0f, 1.0f, 2, nullptr, "combat.aimbot.smoothing")
            .BeginSettings()
                .AddCombo("Curve", &s_smoothCurve, s_smoothCurveItems, 3,
                                                              "combat.aimbot.smoothing.curve")
            .EndSettings()
        .AddCombo("Target bone", &s_aimbotBone, s_boneItems, 3, "combat.aimbot.bone")
            .BeginSettings()
                .AddCheckbox("Sway compensation", &s_boneSway, "combat.aimbot.bone.sway")
            .EndSettings()
        .AddMultiCombo("Targets", s_targetSelected, s_targetItems, 4, "combat.aimbot.targets");
    combatAim.AddContainer("UI bug reproduction", 1)
        .AddCombo("Dropdown", &s_cogClickCombo, s_cogClickItems, 3,
                  "demo.cog_click.combo")
        .AddCheckbox("Checkbox with cog", &s_cogClickCheckbox,
                     "demo.cog_click.checkbox")
            .BeginSettings()
                .AddCheckbox("Cog option", &s_cogClickCheckbox,
                             "demo.cog_click.option")
            .EndSettings();
    combatAim.AddContainer("Hitboxes", 1)
        .AddCheckbox("Head",  &s_hitboxHead,  "combat.hitbox.head")
        .AddCheckbox("Chest", &s_hitboxChest, "combat.hitbox.chest")
        .AddCheckbox("Arms",  &s_hitboxArms,  "combat.hitbox.arms")
        .AddCheckbox("Legs",  &s_hitboxLegs,  "combat.hitbox.legs");
    combatAim.AddContainer("Anti-aim", 1)
        .AddSliderFloat("Yaw jitter",       &s_yawJitter,      0.0f, 180.0f, 1, "\xC2\xB0", "combat.aa.yaw_jitter")
        .AddSliderFloat("Yaw additive",     &s_yawAdditive,   -90.0f,  90.0f, 1, "\xC2\xB0", "combat.aa.yaw_additive")
        .AddSliderFloat("Yaw jitter range", &s_yawJitterRange, 0.0f, 180.0f, 1, "\xC2\xB0", "combat.aa.yaw_jitter_range")
        .AddCheckbox   ("Fake lag",         &s_fakeLag,                                     "combat.aa.fake_lag");

    auto combatTrig = combat.AddSubTab("Triggerbot", ICON_FA_BOLT);
    combatTrig.AddContainer("Triggerbot", 0)
        .AddCheckbox("Enabled", &s_trigger, "combat.trigger.enabled")
            .WithKeybindName("triggerbot")
        .AddSliderInt("Delay", &s_triggerDelay, 0, 500, "ms", "combat.trigger.delay")
            .BeginSettings()
                .AddSliderInt("Jitter",     &s_triggerJitter, 0, 100, "ms",
                                                             "combat.trigger.delay.jitter")
                .AddCheckbox ("Burst mode", &s_triggerBurst, "combat.trigger.burst")
            .EndSettings()
        .AddCheckbox("Visible only", &s_triggerVisible, "combat.trigger.visible");

    // SubTab demonstrating MenuBuilder's icon-tab selector.
    auto combatWep = combat.AddSubTab("Weapons", ICON_FA_CROSSHAIRS);
    auto wsel = combatWep.AddIconSelector(&s_wepClass);
    wsel.AddOption("AR",      ICON_FA_CROSSHAIRS)
        .AddOption("SMG",     ICON_FA_RUNNING)
        .AddOption("LMG",     ICON_FA_BOLT)
        .AddOption("SNIPER",  ICON_FA_EYE)
        .AddOption("PISTOL",  ICON_FA_WRENCH);

    combatWep.AddContainer("Aim", 0).ForSelector(WEP_AR)
        .AddCheckbox("Enabled", &s_wepEnabled[WEP_AR], "combat.wep.ar.enabled")
        .AddSliderFloat("Fov", &s_wepFov[WEP_AR], 0, 30, 1, "\xC2\xB0", "combat.wep.ar.fov")
        .AddSliderFloat("Smoothing", &s_wepSmooth[WEP_AR], 0.0f, 1.0f, 2, nullptr, "combat.wep.ar.smooth");
    combatWep.AddContainer("Aim", 0).ForSelector(WEP_SMG)
        .AddCheckbox("Enabled", &s_wepEnabled[WEP_SMG], "combat.wep.smg.enabled")
        .AddSliderFloat("Fov", &s_wepFov[WEP_SMG], 0, 30, 1, "\xC2\xB0", "combat.wep.smg.fov")
        .AddSliderFloat("Smoothing", &s_wepSmooth[WEP_SMG], 0.0f, 1.0f, 2, nullptr, "combat.wep.smg.smooth");
    combatWep.AddContainer("Aim", 0).ForSelector(WEP_LMG)
        .AddCheckbox("Enabled", &s_wepEnabled[WEP_LMG], "combat.wep.lmg.enabled")
        .AddSliderFloat("Fov", &s_wepFov[WEP_LMG], 0, 30, 1, "\xC2\xB0", "combat.wep.lmg.fov")
        .AddSliderFloat("Smoothing", &s_wepSmooth[WEP_LMG], 0.0f, 1.0f, 2, nullptr, "combat.wep.lmg.smooth");
    combatWep.AddContainer("Aim", 0).ForSelector(WEP_SNIPER)
        .AddCheckbox("Enabled", &s_wepEnabled[WEP_SNIPER], "combat.wep.snp.enabled")
        .AddSliderFloat("Fov", &s_wepFov[WEP_SNIPER], 0, 30, 1, "\xC2\xB0", "combat.wep.snp.fov")
        .AddSliderFloat("Smoothing", &s_wepSmooth[WEP_SNIPER], 0.0f, 1.0f, 2, nullptr, "combat.wep.snp.smooth");
    combatWep.AddContainer("Aim", 0).ForSelector(WEP_PISTOL)
        .AddCheckbox("Enabled", &s_wepEnabled[WEP_PISTOL], "combat.wep.pis.enabled")
        .AddSliderFloat("Fov", &s_wepFov[WEP_PISTOL], 0, 30, 1, "\xC2\xB0", "combat.wep.pis.fov")
        .AddSliderFloat("Smoothing", &s_wepSmooth[WEP_PISTOL], 0.0f, 1.0f, 2, nullptr, "combat.wep.pis.smooth");

    combatWep.AddContainer("Shared", 1)
        .AddCheckbox("Apply globally", &s_wepShared, "combat.wep.shared.apply")
        .AddCombo("Target priority", &s_wepPriority, s_wepPriorityItems, 3,
                  "combat.wep.shared.priority");

    // Tab 2: Visuals
    auto visuals = b.AddTab("Visuals", ICON_FA_EYE);

    auto visEsp = visuals.AddSubTab("Esp", ICON_FA_EYE);
    visEsp.AddContainer("Players", 0)
        .AddCheckbox("Box",      &s_espBox,      "visuals.esp.box")
            .WithInlineColor("visuals.esp.color", s_espColor)
            .BeginSettings()
                .AddSliderFloat("Thickness", &s_espBoxThickness, 0.5f, 4.0f, 2, "px",
                                                                    "visuals.esp.box.thickness")
                .AddCombo      ("Style",     &s_espBoxStyle, s_espBoxStyleItems, 3,
                                                                    "visuals.esp.box.style")
                .AddCheckbox   ("Outline",   &s_espBoxOutline, "visuals.esp.box.outline")
            .EndSettings()
        .AddCheckbox("Skeleton", &s_espSkeleton, "visuals.esp.skeleton")
            .WithInlineColor("visuals.esp.skel.colors",
                             s_espSkelColors, s_espSkelColorNames, 2)
        .AddCheckbox("Health",   &s_espHealth,   "visuals.esp.health")
        .AddCheckbox("Name",     &s_espName,     "visuals.esp.name");
    visEsp.AddContainer("World", 1)
        .AddCheckbox("Fullbright", &s_fullbright, "visuals.world.fullbright")
        .AddColorPicker("Accent", s_accentColor)
            .BeginSettings()
                .AddCheckbox   ("Sync with menu accent", &s_syncAccentToMenu,
                                                             "visuals.world.accent.sync")
                .AddSliderFloat("Glow",                  &s_accentGlow, 0.0f, 1.0f, 2, nullptr,
                                                             "visuals.world.accent.glow")
            .EndSettings();

    // SubTab demonstrating SearchableListBox + per-item cogs.
    auto visFilter = visuals.AddSubTab("Legend filter", ICON_FA_USER);
    visFilter.AddContainer("Legends", 0)
        .AddSearchableListBox("Roster", &s_legendSelected,
                              s_legendItems,
                              (int)(sizeof(s_legendItems) / sizeof(*s_legendItems)),
                              10 /* visible rows */,
                              "type a name...",
                              "visuals.legend_filter.roster")
            .BeginItemSettings(0)   // Alter
                .AddSliderInt("Priority", &s_legendPriority[0], 0, 5, nullptr,
                              "legend.alter.priority")
                .AddCheckbox ("Mute callouts", &s_legendMuted[0],
                              "legend.alter.muted")
            .EndItemSettings()
            .BeginItemSettings(4)   // Bloodhound
                .AddSliderInt("Priority", &s_legendPriority[4], 0, 5, nullptr,
                              "legend.bloodhound.priority")
                .AddCheckbox ("Mute callouts", &s_legendMuted[4],
                              "legend.bloodhound.muted")
            .EndItemSettings()
            .BeginItemSettings(6)   // Caustic
                .AddSliderInt("Priority", &s_legendPriority[6], 0, 5, nullptr,
                              "legend.caustic.priority")
                .AddCheckbox ("Mute callouts", &s_legendMuted[6],
                              "legend.caustic.muted")
            .EndItemSettings()
            .BeginItemSettings(15)  // Mirage
                .AddSliderInt("Priority", &s_legendPriority[15], 0, 5, nullptr,
                              "legend.mirage.priority")
                .AddCheckbox ("Mute callouts", &s_legendMuted[15],
                              "legend.mirage.muted")
            .EndItemSettings()
            .BeginItemSettings(17)  // Octane
                .AddSliderInt("Priority", &s_legendPriority[17], 0, 5, nullptr,
                              "legend.octane.priority")
                .AddCheckbox ("Mute callouts", &s_legendMuted[17],
                              "legend.octane.muted")
            .EndItemSettings()
            .BeginItemSettings(25)  // Wraith
                .AddSliderInt("Priority", &s_legendPriority[25], 0, 5, nullptr,
                              "legend.wraith.priority")
                .AddCheckbox ("Mute callouts", &s_legendMuted[25],
                              "legend.wraith.muted")
            .EndItemSettings();

    // Tab 3: Movement
    auto movement = b.AddTab("Movement", ICON_FA_RUNNING);
    auto moveGen  = movement.AddSubTab("General", ICON_FA_RUNNING);
    moveGen.AddContainer("Speed", 0)
        .AddSliderFloat("Multiplier", &s_speedMult, 0.5f, 5.0f, 2, "x", "movement.speed.mult");
    moveGen.AddContainer("Jump", 1)
        .AddCheckbox("Bunny hop", &s_bunnyhop, "movement.bhop")
            .WithKeybindName("bunny hop")
            .BeginSettings()
                .AddCheckbox("Auto-jump",       &s_bhopAutoJump,   "movement.bhop.auto")
                .AddCheckbox("Hold space only", &s_bhopHoldSpace,  "movement.bhop.hold")
                .AddSliderFloat("Strength",     &s_bhopStrength, 0.1f, 2.0f, 2, "x",
                                                                  "movement.bhop.strength")
            .EndSettings();

    // Tab 4: Misc
    auto misc    = b.AddTab("Misc", ICON_FA_WRENCH);
    auto miscGen = misc.AddSubTab("General", ICON_FA_WRENCH);
    miscGen.AddContainer("Hud", 0)
        .AddCheckbox("Watermark", &s_watermark, "misc.watermark")
            .WithTooltip("draws a small fps + username badge in the corner")
        .AddCheckbox("No recoil", &s_noRecoil, "misc.norecoil")
        .AddCheckbox("Debug overlay", DebugWindow::GetVisiblePtr(), "misc.debug.overlay")
            .WithTooltip("shows a floating debug window with values you push via DebugWindow::Set")
        .AddInputText("Name tag", s_nameTag, sizeof(s_nameTag))
            .BeginSettings()
                .AddColorPicker("Color",     s_nameTagColor)
                .AddSliderInt  ("Font size", &s_nameTagFontSize, 8, 32, "px",
                                                                 "misc.nametag.fontsize")
                .AddCheckbox   ("Outline",   &s_nameTagOutline, "misc.nametag.outline")
            .EndSettings();

    // ── Multi-instance debug windows demo ─────────────────────────────
    miscGen.AddContainer("Debug windows", 1)
        .AddCheckbox("Performance debug", &s_dbgPerf,   "misc.dbg.perf")
            .WithTooltip("multi-instance test: shows a 'Performance' overlay")
        .AddCheckbox("Aimbot debug",      &s_dbgAimbot, "misc.dbg.aimbot")
            .WithTooltip("multi-instance test: shows an 'Aimbot' overlay")
        .AddCheckbox("World debug",       &s_dbgWorld,  "misc.dbg.world")
            .WithTooltip("multi-instance test: shows a 'World' overlay");

    // Schema-verification widgets — flip + save, delete + re-load to test.
    miscGen.AddContainer("Schema test", 1)
        .AddCheckbox("Test flag", &s_schemaTestFlag, "schema.test.flag")
            .WithTooltip("default = off. flip + save, or delete this widget to verify add/remove")
        .AddSliderInt("Test value", &s_schemaTestValue, 0, 100, nullptr, "schema.test.value")
            .WithTooltip("default = 50. change + save, or delete this widget to verify add/remove");

    // Scroll-test container — intentionally tall so misc.general overflows.
    {
        char idBuf[64];
        char lblBuf[32];
        auto scrollCol = miscGen.AddContainer("Scroll test (long)", 0);
        for (int i = 0; i < kScrollDemoRows; i++) {
            snprintf(lblBuf, sizeof(lblBuf), "row %02d  toggle", i + 1);
            snprintf(idBuf,  sizeof(idBuf),  "scroll.demo.bool.%d", i);
            scrollCol.AddCheckbox(lblBuf, &s_scrollDemoBools[i], idBuf);

            snprintf(lblBuf, sizeof(lblBuf), "row %02d  slider", i + 1);
            snprintf(idBuf,  sizeof(idBuf),  "scroll.demo.float.%d", i);
            scrollCol.AddSliderFloat(lblBuf, &s_scrollDemoFloats[i], 0.0f, 1.0f, 2,
                                     nullptr, idBuf);

            snprintf(lblBuf, sizeof(lblBuf), "row %02d  combo", i + 1);
            snprintf(idBuf,  sizeof(idBuf),  "scroll.demo.combo.%d", i);
            scrollCol.AddCombo(lblBuf, &s_scrollDemoCombos[i],
                               s_scrollDemoComboItems,
                               (int)(sizeof(s_scrollDemoComboItems) / sizeof(*s_scrollDemoComboItems)),
                               idBuf);
        }
    }
    // ── Configs subtab ────────────────────────────────────────────────
    auto miscCfg = misc.AddSubTab("Configs", ICON_FA_FLOPPY);
    miscCfg.AddContainer("Saved configs", 0)
        .AddListBox("Configs", &s_cfgSelected,
                    s_cfgNamesPtrs.empty() ? nullptr : s_cfgNamesPtrs.data(),
                    (int)s_cfgNamesPtrs.size(), 8, "misc.configs.list")
            .WithBoldIndex(&s_cfgActiveIndex)
            .Transient()
        .AddInputText("Name", s_cfgInputName, sizeof(s_cfgInputName))
            .Transient()
            .WithTooltip("auto-fills with the selected config; type a different name to save under that name instead")
        .AddButton("Save", []{
            const char* target = s_cfgInputName[0] ? s_cfgInputName : GetSelectedCfgName();
            if (!*target) return;
            std::string name = target;
            if (ConfigSystem::Save(name.c_str())) {
                for (int i = 0; i < (int)s_cfgNamesOwned.size(); i++) {
                    if (s_cfgNamesOwned[i] == name) { s_cfgSelected = i; break; }
                }
            }
        })
        .AddButton("Load", []{
            const char* n = GetSelectedCfgName();
            if (*n) ConfigSystem::Load(n);
        })
        .AddButton("Rename", []{
            const char* src = GetSelectedCfgName();
            if (!*src || !s_cfgInputName[0]) return;
            std::string newName = s_cfgInputName;
            if (ConfigSystem::Rename(src, newName.c_str())) {
                for (int i = 0; i < (int)s_cfgNamesOwned.size(); i++) {
                    if (s_cfgNamesOwned[i] == newName) { s_cfgSelected = i; break; }
                }
            }
        })
        .AddButton("Delete", []{
            const char* n = GetSelectedCfgName();
            if (*n) ConfigSystem::Delete(n);
        });
    miscCfg.AddContainer("Options", 1)
        .AddCheckbox("Auto-save active", &s_cfgAutoSave)
            .WithTooltip("automatically saves the active config 0.5s after any change")
        .AddButton("Set as default", []{
            const char* n = GetSelectedCfgName();
            if (*n) ConfigSystem::SetDefault(n);
        })
        .AddButton("Clear default", []{
            ConfigSystem::SetDefault("");
        })
        .AddButton("Open folder", []{ ConfigSystem::OpenGameFolder(); });
}

// ─── SetupClient / TickClient — framework-called entry points ─────────────

namespace Menu {

void SetupClient() {
    SetupDemoMenu();

    // Demo user identity — real clients pull this from HV/backend.
    UserSystem::SetUsername("player");
    UserSystem::SetSubscription(30);

    // Refresh the configs listbox whenever the on-disk set changes.
    ConfigSystem::AddOnConfigsChanged([]{ RefreshConfigList(); });
}

void TickClient() {
    // Autosave toggle — push local UI value into ConfigSystem each frame,
    // one-shot pull on first frame so the checkbox reflects settings.json.
    {
        static bool s_cfgAutoSavePulled = false;
        if (!s_cfgAutoSavePulled) {
            s_cfgAutoSave = ConfigSystem::GetAutoSave();
            s_cfgAutoSavePulled = true;
        }
        ConfigSystem::SetAutoSave(s_cfgAutoSave);
    }

    // Resolve active config → row index so the listbox can bold it.
    {
        std::string active = ConfigSystem::GetActive();
        s_cfgActiveIndex = -1;
        if (!active.empty()) {
            for (int i = 0; i < (int)s_cfgNamesOwned.size(); i++) {
                if (s_cfgNamesOwned[i] == active) { s_cfgActiveIndex = i; break; }
            }
        }
    }

    // Mirror the listbox selection into the name textbox on selection change
    // so "save" defaults to overwriting the selected config.
    {
        static int s_cfgPrevSelected = -2; // distinct from initial 0 to force first-frame sync
        if (s_cfgSelected != s_cfgPrevSelected) {
            s_cfgPrevSelected = s_cfgSelected;
            if (s_cfgSelected >= 0 && s_cfgSelected < (int)s_cfgNamesOwned.size()) {
                const std::string& name = s_cfgNamesOwned[s_cfgSelected];
                size_t n = name.size();
                if (n >= sizeof(s_cfgInputName)) n = sizeof(s_cfgInputName) - 1;
                memcpy(s_cfgInputName, name.data(), n);
                s_cfgInputName[n] = '\0';
            } else {
                s_cfgInputName[0] = '\0';
            }
        }
    }

    // Demo debug pushers — a few values into the default debug overlay.
    DebugWindow::Set("fov", s_aimbotFov, 0);
    DebugWindow::Set("aimbot", s_aimbot);
    DebugWindow::Set("fps", (int)ImGui::GetIO().Framerate);

    // Multi-instance debug demo — three separate windows, each gated by
    // its own checkbox. Push per-window entries every frame so live data
    // populates while they're open.
    {
        DebugWindow::SetVisible("perf",   s_dbgPerf);
        DebugWindow::SetVisible("aimbot", s_dbgAimbot);
        DebugWindow::SetVisible("world",  s_dbgWorld);

        auto& io = ImGui::GetIO();
        MenuBuilder::Debug("perf", "Performance")
            .Set("fps",       (int)io.Framerate)
            .Set("frame ms",  io.DeltaTime * 1000.0f, 2)
            .Set("dpi scale", MenuStyle::g_dpiScale, 2)
            .Set("vsync",     true);

        MenuBuilder::Debug("aimbot", "Aimbot")
            .Set("enabled",   s_aimbot)
            .Set("fov",       s_aimbotFov, 1)
            .Set("smoothing", s_aimbotSmoothing, 2)
            .Set("bone",      s_boneItems[std::clamp(s_aimbotBone, 0, 2)]);

        MenuBuilder::Debug("world", "World")
            .Set("watermark",  s_watermark)
            .Set("fullbright", s_fullbright)
            .Set("legend",     s_legendItems[std::clamp(s_legendSelected, 0,
                                             (int)(sizeof(s_legendItems) /
                                                   sizeof(*s_legendItems)) - 1)]);
    }
}

} // namespace Menu
