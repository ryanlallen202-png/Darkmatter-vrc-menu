#include "shared.hpp"

// ============================================================================
// Forward decls from other modules
// ============================================================================
extern bool f_fly, f_velocityFly, f_speed, f_noclip, f_lowGravity, f_noGravity;
extern bool f_platforms, f_punch, f_esp, f_skybox, f_skyRainbow;
extern bool f_worldBlocks, f_blockRainbow, f_portalGun, f_orbitCam;
extern bool f_freeze, f_spin, f_bigHead, f_invisible, f_nameSpoof, f_antiEac;
extern int  g_speedTier, g_skyColorIdx;

void Feature_SaveSpot();
void Feature_GoToSpot();
void Feature_AllOff();
void Feature_NextSpeed();
void Feature_NextSkyColor();
const char* Feature_SkyName();
const char* Feature_SpeedName();
size_t Feature_PlayerCount();
const char* Feature_PlayerName(int);
float Feature_PlayerDist(int);
bool  Feature_PlayerIsLocal(int);
bool  Feature_LeftStickClick();
bool  Feature_BtnA();
bool  Feature_RTrigger();
float Feature_RAxisX();
float Feature_RAxisY();

void Soundboard_Play(int);
int  Soundboard_Count();
const char* Soundboard_Name(int);

// ============================================================================
// Menu layout — matches screenshot exactly
// Dark background, no animated dots/lines, tab bar across top,
// checkboxes + sliders in content area, FPS top-left, title top-left,
// "Dock in Front of Face" button under title.
//
// Rendered as a world-space Unity Canvas parented to left hand.
// Cursor: right hand index finger position → hovering a button highlights it,
// right trigger = click.
// Open/close: left joystick click (L3).
// ============================================================================

// ---- Tab definitions -------------------------------------------------------
enum Tab { TAB_PLAYER, TAB_EXPLOITS, TAB_VISUAL, TAB_PLAYERS, TAB_SOUNDBOARD,
           TAB_TEXT, TAB_MISC, TAB_CONFIG, TAB_COUNT };
static const char *kTabNames[TAB_COUNT] = {
    "Player","Exploits","Visual","Players","Soundboard","Text","Misc","Config"
};
static Tab  g_activeTab   = TAB_PLAYER;
static bool g_menuOpen    = false;
static bool g_docked      = false;   // docked in front of face vs left-hand
static double g_openAt    = 0;

// ---- Unity UI class cache --------------------------------------------------
static struct UIClasses {
    bool ready = false;
    Il2CppClass *Canvas, *Text, *Image, *Button, *Toggle, *Slider,
                *RectTransform, *Font, *GameObject, *Transform,
                *ContentSizeFitter, *VerticalLayoutGroup, *HorizontalLayoutGroup;
    MethodInfo  *CanvasSetMode, *CanvasSetSortOrder,
                *TrGetPos, *TrSetPos, *TrGetRot, *TrSetRot,
                *TrSetParent, *TrSetLP, *TrSetLR, *TrSetLS,
                *RtSetSD, *RtSetAnchorMin, *RtSetAnchorMax, *RtSetPivot,
                *TextSetText, *TextSetFontSize, *TextSetColor, *TextSetFont,
                *TextSetAlign, *TextSetBestFit, *TextSetMinSize, *TextSetMaxSize,
                *ImageSetColor, *ImageSetSprite,
                *GoGetTr, *GoGetComp, *GoAddComp, *GoSetActive, *GoNew,
                *ObjDestroy, *ObjAlive,
                *FontGetDefault;
    Il2CppClass *Camera, *Input;
    MethodInfo  *CamGetMain, *CamWStoSS,
                *InputGetButton, *InputGetButtonDown, *InputGetAxis;
    MethodInfo  *ShaderFind;
    Il2CppClass *Shader, *Material;
    MethodInfo  *MatNew, *MatSetColor, *MatSetShader;
} UI;

static bool InitUI() {
    if (UI.ready) return true;
    if (!IL2.ready) return false;
#define C(v,ns,n,a) UI.v=IL2.GetClass(ns,n,a);
#define M(v,c,n,argc) UI.v=IL2.GetMethod(UI.c,n,argc);
    C(Canvas,       "UnityEngine",    "Canvas",             "UnityEngine")
    C(Text,         "UnityEngine.UI", "Text",               "UnityEngine.UI")
    C(Image,        "UnityEngine.UI", "Image",              "UnityEngine.UI")
    C(Button,       "UnityEngine.UI", "Button",             "UnityEngine.UI")
    C(Toggle,       "UnityEngine.UI", "Toggle",             "UnityEngine.UI")
    C(Slider,       "UnityEngine.UI", "Slider",             "UnityEngine.UI")
    C(RectTransform,"UnityEngine",    "RectTransform",      "UnityEngine")
    C(Font,         "UnityEngine",    "Font",               "UnityEngine")
    C(GameObject,   "UnityEngine",    "GameObject",         "UnityEngine")
    C(Transform,    "UnityEngine",    "Transform",          "UnityEngine")
    C(Camera,       "UnityEngine",    "Camera",             "UnityEngine")
    C(Input,        "UnityEngine",    "Input",              "UnityEngine")
    C(Shader,       "UnityEngine",    "Shader",             "UnityEngine")
    C(Material,     "UnityEngine",    "Material",           "UnityEngine")

    M(CanvasSetMode,    Canvas,   "set_renderMode",          1)
    M(TrGetPos,         Transform,"get_position",            0)
    M(TrSetPos,         Transform,"set_position",            1)
    M(TrGetRot,         Transform,"get_rotation",            0)
    M(TrSetRot,         Transform,"set_rotation",            1)
    M(TrSetParent,      Transform,"SetParent",               2)
    M(TrSetLP,          Transform,"set_localPosition",       1)
    M(TrSetLR,          Transform,"set_localRotation",       1)
    M(TrSetLS,          Transform,"set_localScale",          1)
    M(RtSetSD,          RectTransform,"set_sizeDelta",       1)
    M(RtSetAnchorMin,   RectTransform,"set_anchorMin",       1)
    M(RtSetAnchorMax,   RectTransform,"set_anchorMax",       1)
    M(RtSetPivot,       RectTransform,"set_pivot",           1)
    M(TextSetText,      Text,     "set_text",                1)
    M(TextSetFontSize,  Text,     "set_fontSize",            1)
    M(TextSetColor,     Text,     "set_color",               1) // via Graphic
    M(TextSetFont,      Text,     "set_font",                1)
    M(TextSetAlign,     Text,     "set_alignment",           1)
    M(TextSetBestFit,   Text,     "set_resizeTextForBestFit",1)
    M(TextSetMinSize,   Text,     "set_resizeTextMinSize",   1)
    M(TextSetMaxSize,   Text,     "set_resizeTextMaxSize",   1)
    M(ImageSetColor,    Image,    "set_color",               1) // via Graphic
    M(GoGetTr,          GameObject,"get_transform",          0)
    M(GoGetComp,        GameObject,"GetComponent",           1)
    M(GoAddComp,        GameObject,"AddComponent",           1)
    M(GoSetActive,      GameObject,"SetActive",              1)
    M(ObjDestroy,       GameObject,"Destroy",                1) // static on Object
    M(CamGetMain,       Camera,   "get_main",                0)
    M(InputGetButton,   Input,    "GetButton",               1)
    M(InputGetButtonDown,Input,   "GetButtonDown",           1)
    M(InputGetAxis,     Input,    "GetAxis",                 1)
    M(ShaderFind,       Shader,   "Find",                    1)
    M(MatSetColor,      Material, "set_color",               1)
    M(MatSetShader,     Material, "set_shader",              1)
#undef C
#undef M
    // Font.GetDefault() or use Resources.GetBuiltinResource
    Il2CppClass *res = IL2.GetClass("UnityEngine","Resources","UnityEngine");
    if (res) {
        UI.FontGetDefault = IL2.GetMethod(res,"GetBuiltinResource",2);
    }
    Il2CppClass *obj = IL2.GetClass("UnityEngine","Object","UnityEngine");
    if (obj) UI.ObjAlive = IL2.GetMethod(obj,"op_Implicit",1);
    Il2CppClass *obj2 = IL2.GetClass("UnityEngine","Object","UnityEngine");
    if (obj2) UI.ObjDestroy = IL2.GetMethod(obj2,"Destroy",1);

    UI.ready = UI.Canvas && UI.Text && UI.RectTransform && UI.GameObject;
    LOGI("UI classes %s", UI.ready?"ready":"INCOMPLETE");
    return UI.ready;
}

// ---- Theme (matches screenshot: dark grey bg, white text, blue-ish tabs) ---
static const Color kBgColor       = {0.08f,0.08f,0.10f,0.96f};
static const Color kPanelColor    = {0.12f,0.12f,0.15f,1.00f};
static const Color kTabActive     = {0.25f,0.35f,0.55f,1.00f};
static const Color kTabInactive   = {0.18f,0.18f,0.22f,1.00f};
static const Color kBtnColor      = {0.20f,0.20f,0.25f,1.00f};
static const Color kBtnHover      = {0.30f,0.40f,0.65f,1.00f};
static const Color kCheckOn       = {0.25f,0.55f,0.90f,1.00f};
static const Color kCheckOff      = {0.18f,0.18f,0.22f,1.00f};
static const Color kTextColor     = {0.90f,0.90f,0.95f,1.00f};
static const Color kTextDim       = {0.55f,0.55f,0.60f,1.00f};
static const Color kTitleColor    = {0.75f,0.85f,1.00f,1.00f};
static const Color kSliderBg      = {0.15f,0.15f,0.18f,1.00f};
static const Color kSliderFill    = {0.30f,0.50f,0.90f,1.00f};

// ---- Simple GO/component helpers -------------------------------------------
static bool Alive(Il2CppObject *o) {
    if (!o || !UI.ObjAlive) return o!=nullptr;
    Il2CppObject *r = IL2.Invoke(UI.ObjAlive, nullptr, (void**)&o);
    return r ? IL2.Unbox<int32_t>(r)!=0 : false;
}
static void Kill(Il2CppObject *o) {
    if (Alive(o) && UI.ObjDestroy) { void*a[1]={o}; IL2.Invoke(UI.ObjDestroy, nullptr, a); }
}
static Il2CppObject *Tr(Il2CppObject *go) {
    return Alive(go)&&UI.GoGetTr ? IL2.Invoke(UI.GoGetTr, go) : nullptr;
}
static void SetLP(Il2CppObject *go, Vec3 p) {
    if (!Alive(go)||!UI.TrSetLP) return;
    Il2CppObject *t=Tr(go); void*a[1]={&p}; IL2.Invoke(UI.TrSetLP,t,a);
}
static void SetLS(Il2CppObject *go, Vec3 s) {
    if (!Alive(go)||!UI.TrSetLS) return;
    Il2CppObject *t=Tr(go); void*a[1]={&s}; IL2.Invoke(UI.TrSetLS,t,a);
}
static void SetSD(Il2CppObject *go, Vec2 sd) {
    if (!Alive(go)||!UI.RtSetSD) return;
    Il2CppObject *t=Tr(go); void*a[1]={&sd}; IL2.Invoke(UI.RtSetSD,t,a);
}
static void Parent(Il2CppObject *child, Il2CppObject *parent, bool worldPos=false) {
    if (!Alive(child)||!Alive(parent)||!UI.TrSetParent) return;
    Il2CppObject *ct=Tr(child),*pt=Tr(parent);
    int b=worldPos?1:0; void*a[2]={pt,&b}; IL2.Invoke(UI.TrSetParent,ct,a);
}
static void Show(Il2CppObject *go, bool on) {
    if (Alive(go)&&UI.GoSetActive){void*a[1]={&on};IL2.Invoke(UI.GoSetActive,go,a);}
}

static Il2CppObject *NewGO(const char *name, Il2CppObject *parent=nullptr) {
    if (!UI.GameObject||!IL2.object_new) return nullptr;
    Il2CppObject *go=IL2.object_new(UI.GameObject);
    MethodInfo *ctor=IL2.GetMethod(UI.GameObject,".ctor",1);
    if(ctor){void*a[1]={IL2.StrNew(name)};IL2.Invoke(ctor,go,a);}
    if(parent) Parent(go,parent,false);
    return go;
}
static Il2CppObject *AddComp(Il2CppObject *go, Il2CppClass *cls) {
    if (!Alive(go)||!cls||!UI.GoAddComp) return nullptr;
    void*a[1]={(void*)cls};  // pass MonoType* — works for AddComponent(Type)
    // Need to pass as System.Type: get the MonoType
    BNM::MonoType *mt = nullptr; // IL2CPP wraps GetMonoType internally
    // Fallback: use the class pointer directly (works in most VRC IL2CPP builds)
    return IL2.Invoke(UI.GoAddComp, go, (void**)&cls);
}
static Il2CppObject *GetComp(Il2CppObject *go, Il2CppClass *cls) {
    if (!Alive(go)||!cls||!UI.GoGetComp) return nullptr;
    return IL2.Invoke(UI.GoGetComp, go, (void**)&cls);
}

static Il2CppObject *g_font = nullptr;
static Il2CppObject *GetFont() {
    if (g_font && Alive(g_font)) return g_font;
    // Try Font.CreateDynamicFontFromOSFont
    Il2CppClass *fontCls = UI.Font;
    if (!fontCls) return nullptr;
    MethodInfo *create = IL2.GetMethod(fontCls,"CreateDynamicFontFromOSFont",2);
    if (create) {
        int size=24;
        void *a[2]={IL2.StrNew("Arial"),(void*)&size};
        g_font = IL2.Invoke(create, nullptr, a);
    }
    if (!g_font || !Alive(g_font)) {
        // Fallback: Resources.GetBuiltinResource<Font>("Arial.ttf")
        if (UI.FontGetDefault) {
            void *a[2]={(void*)fontCls,IL2.StrNew("Arial.ttf")};
            g_font = IL2.Invoke(UI.FontGetDefault, nullptr, a);
        }
    }
    return g_font;
}

// ---- Make a background image (cube quad) -----------------------------------
static Il2CppObject *MakePanel(Il2CppObject *canvasGO, Vec2 size, Vec3 pos, Color c) {
    Il2CppObject *go  = NewGO("Panel", canvasGO);
    if (!go) return nullptr;
    Il2CppObject *img = AddComp(go, UI.Image);
    SetSD(go, size);
    SetLP(go, {pos.x, pos.y, 0});
    if (img && UI.ImageSetColor) { void*a[1]={&c}; IL2.Invoke(UI.ImageSetColor,img,a); }
    return go;
}

// ---- Make a text label -----------------------------------------------------
static Il2CppObject *MakeText(Il2CppObject *canvasGO, const char *str, Vec2 size, Vec3 pos,
                               Color c={0.9f,0.9f,0.95f,1}, int fontSize=20, bool bold=false) {
    Il2CppObject *go  = NewGO("Label", canvasGO);
    if (!go) return nullptr;
    Il2CppObject *txt = AddComp(go, UI.Text);
    SetSD(go, size);
    SetLP(go, {pos.x,pos.y,0});
    if (txt) {
        if (UI.TextSetText)     { void*a[1]={IL2.StrNew(str)};  IL2.Invoke(UI.TextSetText,txt,a); }
        if (UI.TextSetFontSize) { void*a[1]={&fontSize};        IL2.Invoke(UI.TextSetFontSize,txt,a); }
        if (UI.TextSetColor) {
            Il2CppClass *graphic = IL2.GetClass("UnityEngine.UI","Graphic","UnityEngine.UI");
            MethodInfo  *gc = graphic ? IL2.GetMethod(graphic,"set_color",1) : nullptr;
            if (gc){ void*a[1]={&c}; IL2.Invoke(gc,txt,a); }
        }
        if (UI.TextSetFont) {
            Il2CppObject *f=GetFont();
            if(f){ void*a[1]={f}; IL2.Invoke(UI.TextSetFont,txt,a); }
        }
        if (UI.TextSetAlign) { int align=4; void*a[1]={&align}; IL2.Invoke(UI.TextSetAlign,txt,a); }
        if (UI.TextSetBestFit)  { int v=1; void*a[1]={&v};  IL2.Invoke(UI.TextSetBestFit,txt,a); }
        if (UI.TextSetMinSize)  { int v=8; void*a[1]={&v};  IL2.Invoke(UI.TextSetMinSize,txt,a); }
        if (UI.TextSetMaxSize)  {          void*a[1]={&fontSize}; IL2.Invoke(UI.TextSetMaxSize,txt,a); }
    }
    return go;
}

// ---- Update text on a label GO ---------------------------------------------
static Il2CppObject *GetTextComp(Il2CppObject *go) {
    return Alive(go) ? GetComp(go, UI.Text) : nullptr;
}
static void SetText(Il2CppObject *go, const char *s) {
    Il2CppObject *t=GetTextComp(go);
    if (t&&UI.TextSetText){ void*a[1]={IL2.StrNew(s)}; IL2.Invoke(UI.TextSetText,t,a); }
}
static void SetPanelColor(Il2CppObject *go, Color c) {
    Il2CppObject *img=GetComp(go,UI.Image);
    if (img&&UI.ImageSetColor){ void*a[1]={&c}; IL2.Invoke(UI.ImageSetColor,img,a); }
}

// ============================================================================
// Menu structure
// ============================================================================
// Window: 0.6m wide × 0.44m tall in world space (matches screenshot proportions)
// Canvas scale: 1 unit = 1000 pixels (so sizeDelta in units maps to meters/1000)
static constexpr float kW   = 600;  // canvas pixels wide
static constexpr float kH   = 440;  // canvas pixels tall
static constexpr float kTabH= 36;
static constexpr float kTabY= (kH/2) - kTabH/2 - 42;  // below title area
static constexpr float kContentTop = kTabY - kTabH/2 - 4;
static constexpr float kRowH = 36;
static constexpr float kCheckSize = 22;
static constexpr float kSliderW = 160;

struct MenuRow {
    const char *label;
    bool       *flag;        // nullptr = action or header
    void      (*action)();   // for action buttons
    float      *sliderVal;   // nullptr = not a slider
    float       sliderMin;
    float       sliderMax;
    Tab         tab;
    // Runtime handles
    Il2CppObject *rowGO    = nullptr;
    Il2CppObject *checkGO  = nullptr;
    Il2CppObject *labelGO  = nullptr;
    Il2CppObject *sliderBgGO = nullptr;
    Il2CppObject *sliderFillGO = nullptr;
};

static float g_flySpeed  = 15.0f;
static float g_flapMult  = 5.5f;

static MenuRow g_rows[] = {
    // TAB_PLAYER — Movement
    {"Movement",         nullptr,      nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Fly Speed",        nullptr,      nullptr,            &g_flySpeed,     1,   40,  TAB_PLAYER},
    {"Flap Multiplier",  nullptr,      nullptr,            &g_flapMult,     1,   20,  TAB_PLAYER},
    {"JFly",             &f_fly,       nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Velocity Fly",     &f_velocityFly,nullptr,           nullptr,         0,   0,   TAB_PLAYER},
    {"Platforms",        &f_platforms, nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Noclip",           &f_noclip,    nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Punch Mod",        &f_punch,     nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Low Gravity",      &f_lowGravity,nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"No Gravity",       &f_noGravity, nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Speed Hack",       &f_speed,     nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Next Speed Tier",  nullptr,      Feature_NextSpeed,  nullptr,         0,   0,   TAB_PLAYER},
    {"Spin",             &f_spin,      nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Portal Gun",       &f_portalGun, nullptr,            nullptr,         0,   0,   TAB_PLAYER},
    {"Save Spot",        nullptr,      Feature_SaveSpot,   nullptr,         0,   0,   TAB_PLAYER},
    {"Go To Spot",       nullptr,      Feature_GoToSpot,   nullptr,         0,   0,   TAB_PLAYER},

    // TAB_EXPLOITS
    {"Exploits",         nullptr,      nullptr,            nullptr,         0,   0,   TAB_EXPLOITS},
    {"Freeze Players",   &f_freeze,    nullptr,            nullptr,         0,   0,   TAB_EXPLOITS},
    {"Punch Mod",        &f_punch,     nullptr,            nullptr,         0,   0,   TAB_EXPLOITS},
    {"Big Head",         &f_bigHead,   nullptr,            nullptr,         0,   0,   TAB_EXPLOITS},
    {"Invisible",        &f_invisible, nullptr,            nullptr,         0,   0,   TAB_EXPLOITS},
    {"Name Spoof",       &f_nameSpoof, nullptr,            nullptr,         0,   0,   TAB_EXPLOITS},
    {"Anti-EAC",         &f_antiEac,   nullptr,            nullptr,         0,   0,   TAB_EXPLOITS},

    // TAB_VISUAL
    {"Visual",           nullptr,      nullptr,            nullptr,         0,   0,   TAB_VISUAL},
    {"ESP",              &f_esp,       nullptr,            nullptr,         0,   0,   TAB_VISUAL},
    {"Skybox",           &f_skybox,    nullptr,            nullptr,         0,   0,   TAB_VISUAL},
    {"Rainbow Skybox",   &f_skyRainbow,nullptr,            nullptr,         0,   0,   TAB_VISUAL},
    {"Next Sky Color",   nullptr,      Feature_NextSkyColor,nullptr,        0,   0,   TAB_VISUAL},
    {"World Blocks",     &f_worldBlocks,nullptr,           nullptr,         0,   0,   TAB_VISUAL},
    {"Rainbow Blocks",   &f_blockRainbow,nullptr,          nullptr,         0,   0,   TAB_VISUAL},
    {"Orbit Cam",        &f_orbitCam,  nullptr,            nullptr,         0,   0,   TAB_VISUAL},

    // TAB_PLAYERS — dynamic, populated at runtime
    {"Players",          nullptr,      nullptr,            nullptr,         0,   0,   TAB_PLAYERS},

    // TAB_SOUNDBOARD — 12 clips, populated at runtime in BuildMenu

    // TAB_TEXT
    {"Text",             nullptr,      nullptr,            nullptr,         0,   0,   TAB_TEXT},
    // (Text / chat tools require VRC's InputField hooks — stubs for now)

    // TAB_MISC
    {"Misc",             nullptr,      nullptr,            nullptr,         0,   0,   TAB_MISC},
    {"All Off",          nullptr,      Feature_AllOff,     nullptr,         0,   0,   TAB_MISC},

    // TAB_CONFIG
    {"Config",           nullptr,      nullptr,            nullptr,         0,   0,   TAB_CONFIG},
};
static constexpr int kRowCount = sizeof(g_rows)/sizeof(g_rows[0]);

// ============================================================================
// Menu scene objects
// ============================================================================
static Il2CppObject *g_menuRoot    = nullptr;  // parented to left hand
static Il2CppObject *g_canvasGO    = nullptr;
static Il2CppObject *g_bgPanel     = nullptr;
static Il2CppObject *g_titleText   = nullptr;
static Il2CppObject *g_fpsText     = nullptr;
static Il2CppObject *g_dockBtn     = nullptr;
static Il2CppObject *g_tabGOs[TAB_COUNT] = {};
static Il2CppObject *g_tabTextGOs[TAB_COUNT] = {};
static Il2CppObject *g_contentRoot = nullptr;
static Il2CppObject *g_cursor      = nullptr;  // small sphere on right hand

static bool g_built = false;
static int  g_hoverRow = -1;
static int  g_hoverTab = -1;
static bool g_triggerWas = false;
static double g_nextFpsUpdate = 0;
static int  g_frameCount = 0;
static float g_fps = 60.0f;

// ============================================================================
// Build menu (called once after IL2CPP + UI classes are ready)
// ============================================================================
static void BuildMenu() {
    if (!UI.ready) return;
    LOGI("Building menu");

    // Root GO — will be parented to left hand each frame
    g_menuRoot = NewGO("VRCQuestMenu");
    if (!g_menuRoot) { LOGE("menuRoot null"); return; }
    SetLS(g_menuRoot, {1,1,1});

    // Canvas
    g_canvasGO = NewGO("MenuCanvas", g_menuRoot);
    if (!g_canvasGO) return;
    Il2CppObject *canvas = AddComp(g_canvasGO, UI.Canvas);
    if (canvas && UI.CanvasSetMode) { int mode=2; void*a[1]={&mode}; IL2.Invoke(UI.CanvasSetMode,canvas,a); } // World Space
    SetSD(g_canvasGO, {kW, kH});
    SetLS(g_canvasGO, {0.001f, 0.001f, 0.001f}); // 1 pixel = 1mm
    SetLP(g_canvasGO, {0.1f, 0, 0}); // offset from hand

    // Background panel — dark, no animated elements
    g_bgPanel = MakePanel(g_canvasGO, {kW, kH}, {0, 0, 0}, kBgColor);

    // Title: "Moony's Animal Company menu <3"  (from screenshot)
    // We'll use "Dark Matter — VRChat Quest" matching the mod name
    g_titleText = MakeText(g_canvasGO, "Dark Matter  |  VRChat Quest",
                           {kW-20, 28}, {0, kH/2-18, -1}, kTitleColor, 22, true);

    // FPS counter top-left
    g_fpsText = MakeText(g_canvasGO, "FPS: 60.0",
                         {120, 20}, {-kW/2+70, kH/2-36, -1}, kTextDim, 16);

    // Dock button
    g_dockBtn = MakePanel(g_canvasGO, {160, 28}, {-kW/2+88, kH/2-60, -1}, kBtnColor);
    MakeText(g_canvasGO, "Dock in Front of Face", {155, 24}, {-kW/2+88, kH/2-60, -1}, kTextColor, 14);

    // Tab bar
    float tabW = kW / (float)TAB_COUNT;
    for (int i=0;i<TAB_COUNT;i++) {
        float x = -kW/2 + tabW*(i+0.5f);
        g_tabGOs[i] = MakePanel(g_canvasGO, {tabW-2, kTabH},
                                {x, kTabY, -0.5f},
                                i==g_activeTab ? kTabActive : kTabInactive);
        g_tabTextGOs[i] = MakeText(g_canvasGO, kTabNames[i],
                                   {tabW-4, kTabH-6}, {x, kTabY, -1},
                                   kTextColor, 14);
    }

    // Content root
    g_contentRoot = NewGO("ContentRoot", g_canvasGO);
    SetLP(g_contentRoot, {0, kContentTop, -1});

    // Build rows
    float y = 0;
    for (int i=0;i<kRowCount;i++) {
        MenuRow &row = g_rows[i];
        if (row.tab != g_activeTab) continue;

        // Row background
        row.rowGO = MakePanel(g_contentRoot, {kW-20, kRowH-2}, {0, y, 0}, kPanelColor);

        bool isHeader  = !row.flag && !row.action && !row.sliderVal;
        bool isSlider  = row.sliderVal != nullptr;
        bool isToggle  = row.flag != nullptr;
        bool isAction  = row.action != nullptr;

        if (isHeader) {
            // Section header — slightly brighter, label only
            SetPanelColor(row.rowGO, {0.15f,0.15f,0.20f,1});
            MakeText(g_contentRoot, row.label, {kW-30, kRowH-6}, {0, y, -0.5f}, kTitleColor, 16, true);
        } else if (isSlider) {
            MakeText(g_contentRoot, row.label, {180, kRowH-6}, {kW/2-200/2-20, y, -0.5f}, kTextColor, 15);
            // Slider background
            row.sliderBgGO = MakePanel(g_contentRoot, {kSliderW, 10}, {-50, y, -0.5f}, kSliderBg);
            // Slider fill (width = fraction of range)
            float frac = (*row.sliderVal - row.sliderMin) / (row.sliderMax - row.sliderMin);
            row.sliderFillGO = MakePanel(g_contentRoot, {kSliderW*frac+2, 10}, {-50-kSliderW/2+kSliderW*frac/2, y, -1}, kSliderFill);
            // Value label
            char buf[32]; snprintf(buf,sizeof buf,"%.3f",*row.sliderVal);
            row.labelGO = MakeText(g_contentRoot, buf, {80, kRowH-6}, {50, y, -0.5f}, kTextDim, 15);
        } else if (isToggle) {
            // Checkbox
            bool on = *row.flag;
            row.checkGO = MakePanel(g_contentRoot, {kCheckSize, kCheckSize}, {-kW/2+20, y, -0.5f}, on?kCheckOn:kCheckOff);
            if (on) MakeText(g_contentRoot,"✓",{kCheckSize,kCheckSize},{-kW/2+20,y,-1},kTextColor,14);
            row.labelGO = MakeText(g_contentRoot, row.label, {kW-60, kRowH-6}, {10, y, -0.5f}, kTextColor, 16);
        } else if (isAction) {
            SetPanelColor(row.rowGO, kBtnColor);
            row.labelGO = MakeText(g_contentRoot, row.label, {kW-30, kRowH-6}, {0, y, -0.5f}, kTextColor, 16);
        }
        y -= kRowH;
    }

    // Cursor — small sphere on right hand
    Il2CppClass *go = UI.GameObject;
    if (go) {
        g_cursor = NewGO("Cursor");
        if (g_cursor) {
            // Add a small image component as a dot
            Il2CppObject *img = AddComp(g_cursor, UI.Image);
            SetLS(g_cursor, {0.015f,0.015f,0.015f});
            if (img && UI.ImageSetColor){ Color c={0.5f,0.8f,1,1}; void*a[1]={&c}; IL2.Invoke(UI.ImageSetColor,img,a); }
        }
    }

    Show(g_menuRoot, false);
    g_built = true;
    LOGI("Menu built");
}

// ============================================================================
// Rebuild content area when tab changes
// ============================================================================
static void RebuildContent() {
    if (!g_contentRoot) return;
    // Hide all rows not in active tab, show those that are
    float y = 0;
    for (int i=0;i<kRowCount;i++) {
        MenuRow &row = g_rows[i];
        bool vis = (row.tab == g_activeTab);
        Show(row.rowGO, vis);
        Show(row.labelGO, vis);
        Show(row.checkGO, vis);
        Show(row.sliderBgGO, vis);
        Show(row.sliderFillGO, vis);
        if (vis) {
            // Reposition
            SetLP(row.rowGO, {0,y,0});
            if (row.labelGO)      SetLP(row.labelGO,{10,y,-0.5f});
            if (row.checkGO)      SetLP(row.checkGO,{-kW/2+20,y,-0.5f});
            if (row.sliderBgGO)   SetLP(row.sliderBgGO,{-50,y,-0.5f});
            if (row.sliderFillGO) SetLP(row.sliderFillGO,{-50,y,-1});
            y -= kRowH;
        }
    }
    // Update tab colors
    for (int i=0;i<TAB_COUNT;i++)
        SetPanelColor(g_tabGOs[i], i==g_activeTab?kTabActive:kTabInactive);
}

// ============================================================================
// Place menu on left hand each frame
// ============================================================================
static Il2CppObject *g_leftHandTr  = nullptr;
static Il2CppObject *g_rightHandTr = nullptr;

static void UpdateHandRefs() {
    // Try to find the hand transforms via VRCPlayer or XR node transforms
    if (g_leftHandTr && Alive(g_leftHandTr)) return;
    Il2CppClass *vrcPlayer = IL2.GetClass("","VRCPlayer","Assembly-CSharp");
    if (!vrcPlayer) return;
    // VRCPlayer stores left/right hand refs — try common field names
    static const char *leftNames[]  = {"leftHandTransform","leftHand","_leftHandTr",nullptr};
    static const char *rightNames[] = {"rightHandTransform","rightHand","_rightHandTr",nullptr};
    // Field scan: iterate IL2CPP fields
    // For now, fall back to Camera as anchor (always works)
    if (!g_leftHandTr) {
        Il2CppClass *cam = IL2.GetClass("UnityEngine","Camera","UnityEngine");
        MethodInfo  *getMain = cam ? IL2.GetMethod(cam,"get_main",0) : nullptr;
        if (getMain) g_leftHandTr  = IL2.Invoke(getMain, nullptr);
        if (getMain) g_rightHandTr = IL2.Invoke(getMain, nullptr);
    }
}

static void PlaceMenu() {
    UpdateHandRefs();
    if (!g_menuRoot || !Alive(g_menuRoot)) return;
    Il2CppObject *anchor = g_docked ? g_leftHandTr : g_leftHandTr; // both same for now
    if (!anchor || !UI.TrGetPos || !UI.TrSetPos || !UI.TrGetRot || !UI.TrSetRot) return;

    // Parent menu root to anchor transform
    Il2CppObject *rootTr = Tr(g_menuRoot);
    Il2CppObject *anchorTr = anchor;
    // Set position to anchor + offset
    Vec3 pos = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, anchorTr));
    pos.y += 0.15f;
    pos.x += g_docked ? 0 : 0.1f;
    void *a[1]={&pos}; IL2.Invoke(UI.TrSetPos, rootTr, a);
    // Match rotation of anchor
    Quat rot = IL2.Unbox<Quat>(IL2.Invoke(UI.TrGetRot, anchorTr));
    void *b[1]={&rot}; IL2.Invoke(UI.TrSetRot, rootTr, b);
}

// ============================================================================
// Cursor — place on right hand, detect hover + click
// ============================================================================
static void UpdateCursor() {
    if (!g_cursor || !Alive(g_cursor)) return;
    if (!g_rightHandTr) return;
    Vec3 rp = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, g_rightHandTr));
    // The cursor just follows right hand position for now
    Il2CppObject *ct = Tr(g_cursor);
    if (ct && UI.TrSetPos){ void*a[1]={&rp}; IL2.Invoke(UI.TrSetPos,ct,a); }
}

// Detect which row the cursor is hovering
static int HitTestRow() {
    if (!g_canvasGO || !Alive(g_canvasGO) || !g_rightHandTr) return -1;
    Vec3 cursorWS = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, g_rightHandTr));
    // Transform cursor into canvas local space
    Il2CppObject *canvasTr = Tr(g_canvasGO);
    if (!canvasTr || !UI.TrGetPos) return -1;
    Vec3 canvasPos = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, canvasTr));
    Vec3 d = cursorWS - canvasPos;
    // Convert to pixel coords (canvas scale 0.001)
    float px = d.x / 0.001f;
    float py = d.y / 0.001f;
    // Check tabs
    float tabW = kW / (float)TAB_COUNT;
    if (fabsf(py - kTabY) < kTabH/2) {
        int t = (int)((px + kW/2) / tabW);
        if (t>=0&&t<TAB_COUNT) return -(t+2); // negative = tab index + 2
    }
    // Check dock button
    if (fabsf(px-(-kW/2+88)) < 80 && fabsf(py-(kH/2-60)) < 14) return -1; // dock button
    // Check rows
    float y = kContentTop;
    int row = -100;
    for (int i=0;i<kRowCount;i++) {
        if (g_rows[i].tab != g_activeTab) continue;
        if (fabsf(py - y) < kRowH/2 && fabsf(px) < kW/2-10) { row = i; break; }
        y -= kRowH;
    }
    return row;
}

static bool Trigger() {
    if (!UI.Input || !UI.InputGetButton) return false;
    bool r = false;
    // Right trigger
    const char *ax = "Oculus_CrossPlatform_SecondaryIndexTrigger";
    Il2CppClass *input = UI.Input;
    MethodInfo  *getAxis = IL2.GetMethod(input, "GetAxis", 1);
    if (getAxis){ void*a[1]={IL2.StrNew(ax)}; Il2CppObject*o=IL2.Invoke(getAxis,nullptr,a); r=o&&IL2.Unbox<float>(o)>0.7f; }
    return r;
}

// ============================================================================
// Update loop
// ============================================================================
void Menu_Init() {
    LOGI("Menu init");
}

void Menu_Update() {
    if (!InitUI()) return;
    if (!g_built) { BuildMenu(); return; }

    // FPS counter
    g_frameCount++;
    if (g_time >= g_nextFpsUpdate) {
        g_fps = g_frameCount / (float)(g_time - (g_nextFpsUpdate - 1.0));
        if (g_fps > 999) g_fps = 999;
        g_frameCount = 0;
        g_nextFpsUpdate = g_time + 1.0;
        char buf[32]; snprintf(buf,sizeof buf,"FPS: %.1f",g_fps);
        SetText(g_fpsText, buf);
    }

    // Open / close on left stick click
    if (Feature_LeftStickClick()) {
        g_menuOpen = !g_menuOpen;
        Show(g_menuRoot, g_menuOpen);
        if (g_menuOpen) g_openAt = g_time;
        LOGI("Menu %s", g_menuOpen?"OPEN":"CLOSED");
    }
    if (!g_menuOpen) return;

    PlaceMenu();
    UpdateCursor();

    // Hit test
    int hit = HitTestRow();
    bool trig = Trigger();
    bool trigDown = trig && !g_triggerWas;
    g_triggerWas = trig;

    // Highlight hover
    if (hit != g_hoverRow) {
        // De-highlight old
        if (g_hoverRow >= 0 && g_hoverRow < kRowCount)
            SetPanelColor(g_rows[g_hoverRow].rowGO, kPanelColor);
        // Highlight new
        if (hit >= 0 && hit < kRowCount)
            SetPanelColor(g_rows[hit].rowGO, kBtnHover);
        g_hoverRow = hit;
    }

    if (!trigDown) return;

    // Dock button (hit == -1)
    if (hit == -1) {
        g_docked = !g_docked;
        return;
    }

    // Tab click (hit < -1)
    if (hit < -1) {
        int tabIdx = -(hit+2);
        if (tabIdx>=0 && tabIdx<TAB_COUNT) {
            g_activeTab = (Tab)tabIdx;
            RebuildContent();
        }
        return;
    }

    // Row click
    if (hit >= 0 && hit < kRowCount) {
        MenuRow &row = g_rows[hit];
        // Toggle
        if (row.flag) {
            *row.flag = !*row.flag;
            // Update checkbox color
            SetPanelColor(row.checkGO, *row.flag ? kCheckOn : kCheckOff);
            SetText(row.checkGO, *row.flag ? "✓" : "");
            LOGI("Toggle %s = %d", row.label, *row.flag?1:0);
        }
        // Action
        if (row.action) {
            row.action();
        }
        // Slider: handled via right stick Y axis (held + axis)
        if (row.sliderVal) {
            float delta = Feature_RAxisY() * 0.5f * g_dt;
            *row.sliderVal = fmaxf(row.sliderMin, fminf(row.sliderMax, *row.sliderVal + delta));
            char buf[32]; snprintf(buf,sizeof buf,"%.3f",*row.sliderVal);
            SetText(row.labelGO, buf);
            // Update fill width
            float frac = (*row.sliderVal - row.sliderMin) / (row.sliderMax - row.sliderMin);
            if (Alive(row.sliderFillGO)) {
                Vec2 sd = {kSliderW*frac+2, 10};
                SetSD(row.sliderFillGO, sd);
                SetLP(row.sliderFillGO, {-50-kSliderW/2+kSliderW*frac/2, 0, -1});
            }
        }
        // Soundboard tab: play clip
        if (g_activeTab == TAB_SOUNDBOARD) {
            Soundboard_Play(hit); // row index maps to clip index in soundboard tab
        }
    }
}
