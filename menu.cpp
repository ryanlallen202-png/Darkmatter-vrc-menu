#include "shared.hpp"

// ============================================================================
// In-world menu
//
// World-space uGUI canvas parented to the left hand. Cursor is a small quad
// driven by the right index finger; right trigger clicks. L3 opens/closes.
//
// Layout (canvas pixels, 1 px = 1 mm):
//   ┌──────────────────────────────────────────────┐
//   │ Dark Matter   VRChat Quest        FPS: 72.0  │
//   │ [Dock in Front of Face]                      │
//   │ [Player][Exploits][Visual][Players][Sound]…  │
//   │  content rows                                │
//   └──────────────────────────────────────────────┘
//
// Every component is created with a real System.Type (AddComponent(Type)) —
// passing a raw Il2CppClass* is the classic silent failure and is not done
// anywhere in this file.
// ============================================================================

// ---- module surface -------------------------------------------------------
extern bool f_fly, f_velocityFly, f_speed, f_noclip, f_lowGravity, f_noGravity;
extern bool f_platforms, f_punch, f_esp, f_skybox, f_skyRainbow;
extern bool f_worldBlocks, f_blockRainbow, f_portalGun, f_orbitCam;
extern bool f_freeze, f_spin, f_bigHead, f_invisible, f_nameSpoof, f_antiEac;
extern int  g_speedTier, g_skyColorIdx;
extern float g_flySpeed, g_flapMult;

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
int   Feature_PlayerActor(int);
bool  Feature_LeftStickClick();
bool  Feature_BtnA();
bool  Feature_RTrigger();
bool  Feature_LTrigger();
float Feature_RAxisX();
float Feature_RAxisY();
float* Feature_FlySpeedPtr();
float* Feature_FlapMultPtr();

namespace GunLib {
    bool  Ready();
    bool  CrashTarget(int actorNr);
    bool  StartLag(float ratePerSec);
    void  StopLag();
    bool  Kick(int actorNr);
    bool  IsLagging();
    float LagRate();
    void  Shutdown();
}
namespace Soundboard  { bool PlayLocal(int); bool PlayNetwork(int); void StopAll(); }
namespace EventPump   { uint64_t AudioFrames(); uint64_t Dropped(); }

// ============================================================================
// GunLib + soundboard state (targets, path input, status strings)
// ============================================================================
static int   g_gunTarget = 0;
static int   g_soundSel  = 0;
static char  g_gunTargetLabel[96] = "Target: (none)";
static float g_lagRate   = 60.f;
static char  g_soundPath[512] = "";
static char  g_soundStatus[128] = "";

static void SetGunTarget(int idx) {
    if (idx < 0) { g_gunTarget = 0; snprintf(g_gunTargetLabel, sizeof g_gunTargetLabel, "Target: (none)"); return; }
    g_gunTarget = Feature_PlayerActor(idx);
    snprintf(g_gunTargetLabel, sizeof g_gunTargetLabel, "Target: %s (#%d)",
             Feature_PlayerName(idx), g_gunTarget);
    LOGI("gun target -> %s (%d)", Feature_PlayerName(idx), g_gunTarget);
}

static void Action_Crash() { GunLib::CrashTarget(g_gunTarget); }
static void Action_Lag()   {
    if (GunLib::IsLagging()) GunLib::StopLag();
    else GunLib::StartLag(g_lagRate);
}
static void Action_StopAudio() { Soundboard::StopAll(); }
static void Action_Broadcast() {
    // Broadcasts the highlighted clip. Selection follows the last click in the
    // clip list, so the button works as "send what I just picked".
    if (g_soundSel < 0 || g_soundSel >= Soundboard::Count()) return;
    Soundboard::PlayNetwork(g_soundSel);
}
static void Action_Kick()  { GunLib::Kick(g_gunTarget); }
static void Action_LoadSound() {
    if (!g_soundPath[0]) { snprintf(g_soundStatus, sizeof g_soundStatus, "no path set"); return; }
    if (Soundboard::AddFile(g_soundPath)) {
        snprintf(g_soundStatus, sizeof g_soundStatus, "loaded: %s", Soundboard::Name(Soundboard::Count()-1));
    } else {
        snprintf(g_soundStatus, sizeof g_soundStatus, "failed: %s", Soundboard::FileError());
    }
}

// ============================================================================
// Tabs
// ============================================================================
enum Tab {
    TAB_PLAYER, TAB_EXPLOITS, TAB_VISUAL, TAB_PLAYERS,
    TAB_SOUNDBOARD, TAB_TEXT, TAB_MISC, TAB_CREDITS, TAB_AUTH, TAB_COUNT
};
static const char *kTabNames[TAB_COUNT] = {
    "Player","Exploits","Visual","Players","Soundboard","Text","Misc","Credits","Auth"
};

// ============================================================================
// uGUI cache — every method resolved once, null-checked at call site
// ============================================================================
static struct UIClasses {
    bool ready = false;
    Il2CppClass *Canvas=nullptr, *Text=nullptr, *Image=nullptr,
                *RectTransform=nullptr, *Font=nullptr,
                *GameObject=nullptr, *Transform=nullptr,
                *Graphic=nullptr, *Object=nullptr, *Resources=nullptr,
                *Tex2D=nullptr, *Sprite=nullptr, *Color32=nullptr,
                *Camera=nullptr, *Input=nullptr;
    MethodInfo  *CanvasSetMode=nullptr, *CanvasSetWorldCam=nullptr,
                *TrSetParent=nullptr, *TrSetLP=nullptr, *TrSetLR=nullptr,
                *TrSetLS=nullptr, *TrGetPos=nullptr, *TrSetPos=nullptr,
                *TrGetRot=nullptr, *TrSetRot=nullptr, *TrGetFwd=nullptr,
                *TrGetParent=nullptr,
                *RtSetSD=nullptr, *RtSetPivot=nullptr, *RtSetAnchorMin=nullptr,
                *RtSetAnchorMax=nullptr,
                *TextSetText=nullptr, *TextSetFontSize=nullptr,
                *TextSetFont=nullptr, *TextSetAlign=nullptr,
                *TextSetBestFit=nullptr, *TextSetMinSize=nullptr,
                *TextSetMaxSize=nullptr, *TextSetHLayout=nullptr,
                *GraphicSetColor=nullptr,
                *ImageSetType=nullptr, *ImageSetPixelsPPU=nullptr,
                *TexCtor=nullptr, *TexSetPixels=nullptr, *TexApply=nullptr,
                *GoGetTr=nullptr, *GoAddComp=nullptr, *GoGetComp=nullptr,
                *GoSetActive=nullptr, *GoGetActive=nullptr,
                *ObjDestroy=nullptr, *ObjAlive=nullptr,
                *FontFromOSFont=nullptr, *ResourcesBuiltin=nullptr,
                *CamGetMain=nullptr, *InputGetAxis=nullptr,
                *RtCtor_=nullptr;
} UI;

static bool InitUI() {
    if (UI.ready) return true;
    if (!IL2.ready) return false;

#define C(v,ns,n,a) UI.v=IL2.GetClass(ns,n,a);
#define M(v,c,n,argc) UI.v=IL2.GetMethod(UI.c,n,argc);
    C(Canvas,       "UnityEngine",   "Canvas",       "UnityEngine.CoreModule")
    if (!UI.Canvas) C(Canvas, "UnityEngine", "Canvas", "UnityEngine")
    C(Text,         "UnityEngine.UI","Text",         "UnityEngine.UI")
    C(Image,        "UnityEngine.UI","Image",        "UnityEngine.UI")
    C(Graphic,      "UnityEngine.UI","Graphic",      "UnityEngine.UI")
    C(RectTransform,"UnityEngine",   "RectTransform","UnityEngine.CoreModule")
    if (!UI.RectTransform) C(RectTransform, "UnityEngine", "RectTransform", "UnityEngine")
    C(Font,         "UnityEngine",   "Font",         "UnityEngine.TextRenderingModule")
    if (!UI.Font) C(Font, "UnityEngine", "Font", "UnityEngine")
    C(GameObject,   "UnityEngine",   "GameObject",   "UnityEngine.CoreModule")
    if (!UI.GameObject) C(GameObject, "UnityEngine", "GameObject", "UnityEngine")
    C(Transform,    "UnityEngine",   "Transform",    "UnityEngine.CoreModule")
    if (!UI.Transform) C(Transform, "UnityEngine", "Transform", "UnityEngine")
    C(Object,       "UnityEngine",   "Object",       "UnityEngine.CoreModule")
    C(Resources,    "UnityEngine",   "Resources",    "UnityEngine.CoreModule")
    C(Camera,       "UnityEngine",   "Camera",       "UnityEngine.CoreModule")
    C(Tex2D,        "UnityEngine",   "Texture2D",    "UnityEngine.CoreModule")
    C(Sprite,       "UnityEngine",   "Sprite",       "UnityEngine.CoreModule")
    C(Input,        "UnityEngine",   "Input",        "UnityEngine.InputLegacyModule")

    // Rendering a canvas in world space needs a camera on the canvas.
    M(CanvasSetMode,     Canvas, "set_renderMode",      1)
    M(CanvasSetWorldCam, Canvas, "set_worldCamera",     1)
    M(TextSetText,       Text,   "set_text",            1)
    M(TextSetFontSize,   Text,   "set_fontSize",        1)
    M(TextSetFont,       Text,   "set_font",            1)
    M(TextSetAlign,      Text,   "set_alignment",       1)
    M(TextSetBestFit,    Text,   "set_resizeTextForBestFit", 1)
    M(TextSetMinSize,    Text,   "set_resizeTextMinSize",    1)
    M(TextSetMaxSize,    Text,   "set_resizeTextMaxSize",    1)
    M(GraphicSetColor,   Graphic,"set_color",           1)
    M(ImageSetType,      Image,  "set_type",            1)
    M(ImageSetPixelsPPU, Image,  "set_pixelsPerUnitMultiplier", 1)
    M(TexCtor,           Tex2D,  ".ctor",               2)
    M(TexSetPixels,      Tex2D,  "SetPixels32",         1)
    M(TexApply,          Tex2D,  "Apply",               1)
    M(FontFromOSFont,    Font,   "CreateDynamicFontFromOSFont", 2)
    M(ResourcesBuiltin,  Resources, "GetBuiltinResource", 2)
    M(TrSetParent,       Transform, "SetParent",        2)
    M(TrSetLP,           Transform, "set_localPosition", 1)
    M(TrSetLR,           Transform, "set_localRotation", 1)
    M(TrSetLS,           Transform, "set_localScale",    1)
    M(TrGetPos,          Transform, "get_position",      0)
    M(TrSetPos,          Transform, "set_position",      1)
    M(TrGetRot,          Transform, "get_rotation",      0)
    M(TrSetRot,          Transform, "set_rotation",      1)
    M(TrGetFwd,          Transform, "get_forward",       0)
    M(TrGetParent,       Transform, "get_parent",        0)
    M(RtSetSD,           RectTransform, "set_sizeDelta", 1)
    M(RtSetPivot,        RectTransform, "set_pivot",     1)
    M(RtSetAnchorMin,    RectTransform, "set_anchorMin", 1)
    M(RtSetAnchorMax,    RectTransform, "set_anchorMax", 1)
    M(GoGetTr,           GameObject, "get_transform",    0)
    M(GoAddComp,         GameObject, "AddComponent",     1)
    M(GoGetComp,         GameObject, "GetComponent",     1)
    M(GoSetActive,       GameObject, "SetActive",        1)
    M(ObjDestroy,        Object,     "Destroy",          1)
    M(ObjAlive,          Object,     "op_Implicit",      1)
    M(CamGetMain,        Camera,     "get_main",         0)
    M(InputGetAxis,      Input,      "GetAxis",          1)
#undef C
#undef M

    UI.ready = UI.Canvas && UI.Text && UI.RectTransform && UI.GameObject &&
               UI.GoAddComp && UI.GoGetTr && IL2.TypeOf(UI.Text) != nullptr;
    LOGI("UI classes %s (Type%p)", UI.ready ? "ready" : "INCOMPLETE",
         (void*)IL2.TypeOf(UI.Text));
    return UI.ready;
}

// ============================================================================
// Object helpers — all take/return raw objects; ownership is by Ref at the
// call site where the object must outlive the frame.
// ============================================================================
static bool  Alive(Il2CppObject* o) {
    if (!o) return false;
    if (!UI.ObjAlive) return true;
    void* a[1] = { o };
    Il2CppObject* r = IL2.Invoke(UI.ObjAlive, nullptr, a);
    return r && IL2.Unbox<bool>(r);
}
static void  Kill(Il2CppObject* o) {
    if (!o || !UI.ObjDestroy) return;
    void* a[1] = { o };
    IL2.Invoke(UI.ObjDestroy, nullptr, a);
}
static Il2CppObject* Tr(Il2CppObject* go) {
    if (!go || !UI.GoGetTr) return nullptr;
    return IL2.Invoke(UI.GoGetTr, go);
}
static void SetLP(Il2CppObject* go, Vec3 p) {
    Il2CppObject* t = Tr(go); if (!t || !UI.TrSetLP) return;
    void* a[1] = { &p }; IL2.Invoke(UI.TrSetLP, t, a);
}
static void SetLS(Il2CppObject* go, Vec3 s) {
    Il2CppObject* t = Tr(go); if (!t || !UI.TrSetLS) return;
    void* a[1] = { &s }; IL2.Invoke(UI.TrSetLS, t, a);
}
static void SetSD(Il2CppObject* go, float w, float h) {
    if (!go || !UI.RtSetSD) return;
    // set_sizeDelta lives on RectTransform; the GameObject's Transform pointer
    // is the same native object for uGUI elements.
    Il2CppObject* t = Tr(go); if (!t) return;
    Vec2 sd{ w, h };
    void* a[1] = { &sd };
    IL2.Invoke(UI.RtSetSD, t, a);
}
static void Parent(Il2CppObject* child, Il2CppObject* parent, bool worldStays) {
    if (!child || !parent || !UI.TrSetParent) return;
    Il2CppObject* ct = Tr(child); Il2CppObject* pt = Tr(parent);
    if (!ct || !pt) return;
    bool b = worldStays;
    void* a[2] = { pt, &b };
    IL2.Invoke(UI.TrSetParent, ct, a);
}
static void Show(Il2CppObject* go, bool on) {
    if (!go || !UI.GoSetActive) return;
    void* a[1] = { &on };
    IL2.Invoke(UI.GoSetActive, go, a);
}
static Il2CppObject* AddComp(Il2CppObject* go, Il2CppClass* cls) {
    if (!go || !cls || !UI.GoAddComp) return nullptr;
    Il2CppObject* type = IL2.TypeOf(cls);          // System.Type, not Il2CppClass
    if (!type) { LOGE("AddComp: no System.Type for %p", (void*)cls); return nullptr; }
    void* a[1] = { type };
    return IL2.Invoke(UI.GoAddComp, go, a);
}
static Il2CppObject* GetComp(Il2CppObject* go, Il2CppClass* cls) {
    if (!go || !cls || !UI.GoGetComp) return nullptr;
    Il2CppObject* type = IL2.TypeOf(cls);
    if (!type) return nullptr;
    void* a[1] = { type };
    return IL2.Invoke(UI.GoGetComp, go, a);
}

// NewGO: construct, name, optionally parent. Returns owned raw pointer.
static Il2CppObject* NewGO(const char* name, Il2CppObject* parent=nullptr) {
    if (!UI.GameObject) return nullptr;
    Il2CppObject* go = IL2.object_new(UI.GameObject);
    if (!go) return nullptr;
    MethodInfo* ctor1 = IL2.GetMethod(UI.GameObject, ".ctor", 1);
    MethodInfo* ctor0 = IL2.GetMethod(UI.GameObject, ".ctor", 0);
    if (ctor1) { void* a[1] = { IL2.StrNew(name) }; IL2.Invoke(ctor1, go, a); }
    else if (ctor0) { IL2.Invoke(ctor0, go); }
    if (parent) Parent(go, parent, false);
    return go;
}

// ---------------------------------------------------------------------------
// Rounded-corner sprite, generated once.
//
// The reference menu's rows and buttons are rounded rectangles with a subtle
// border. uGUI needs a Sprite for that: a white RGBA texture with an alpha mask
// shaped like a rounded rect, sliced 9 ways by Image.type = Sliced. Generated
// here so no asset bundle or Resources lookup is involved.
// ---------------------------------------------------------------------------
static Ref g_roundSprite;
static Ref g_roundTex;

static void MakeRoundedSprite()
{
    if (g_roundSprite || !UI.Tex2D) return;
    const int S = 32;              // texture edge, uniform alpha mask
    const float r = 9.f;           // corner radius in texels
    Il2CppObject* tex = IL2.object_new(UI.Tex2D);
    if (!tex) return;
    MethodInfo* ctor = IL2.GetMethod(UI.Tex2D, ".ctor", 2);
    if (!ctor) return;
    int32_t w = S, h = S;
    void* c[2] = { &w, &h };
    IL2.Invoke(ctor, tex, c);

    // Color32[] — 4 bytes per texel, Unity RGBA order.
    Il2CppClass* col32 = IL2.GetClass("UnityEngine","Color32","UnityEngine.CoreModule");
    if (!col32) col32 = UI.Object ? nullptr : nullptr;
    Il2CppClass* arrCls = col32;
    if (!arrCls || !IL2.arr_new) { g_roundTex.set(tex); return; }
    Il2CppObject* pixels = (Il2CppObject*)IL2.arr_new(arrCls, (uintptr_t)(S*S));
    if (!pixels) { g_roundTex.set(tex); return; }
    uint8_t* px = (uint8_t*)pixels + kArrDataAt + 0;   // Color32[] data (struct array)
    // Struct arrays in IL2CPP are laid out inline; skip the object header only.
    px = (uint8_t*)pixels + kArrDataAt;

    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float cx = fx < r ? r - fx : (fx > S - r ? fx - (S - r) : 0.f);
            float cy = fy < r ? r - fy : (fy > S - r ? fy - (S - r) : 0.f);
            float d  = sqrtf(cx*cx + cy*cy);
            uint8_t a = 255;
            if (d > r) a = 0;
            else if (d > r - 1.f) a = (uint8_t)(255.f * (r - d));
            size_t o = ((size_t)y * S + x) * 4;
            px[o+0] = 255; px[o+1] = 255; px[o+2] = 255; px[o+3] = a;
        }
    }

    MethodInfo* setPixels = IL2.GetMethod(UI.Tex2D, "SetPixels32", 1);
    MethodInfo* apply     = IL2.GetMethod(UI.Tex2D, "Apply", 1);
    if (setPixels) { void* a[1] = { pixels }; IL2.Invoke(setPixels, tex, a); }
    if (apply) { IL2.Invoke(apply, tex); }
    g_roundTex.set(tex);

    // Sprite.Create(Texture2D, Rect, Vector2 pivot, float ppu, uint extrude,
    //               SpriteMeshType, Vector4 border)  — 7 parameters.
    // Border 12 px on all four sides matches the 32 px mask with a 9 px radius,
    // so a 9-sliced Image never stretches a corner.
    Il2CppClass* spr = UI.Sprite;
    MethodInfo*  create = spr ? IL2.GetMethod(spr, "Create", 7) : nullptr;
    if (create) {
        struct RectF   { float x, y, w, h; };
        struct Vec2F   { float x, y; };
        struct Vec4F   { float x, y, z, w; };
        RectF rect{ 0, 0, (float)S, (float)S };
        Vec2F pivot{ 0.5f, 0.5f };
        Vec4F border{ 12, 12, 12, 12 };
        uint32_t extrude = 0;
        int32_t  meshType = 1;      // SpriteMeshType.FullRect
        void* a[7] = { tex, &rect, &pivot, nullptr, &extrude, &meshType, &border };
        float ppu = 100.f;
        a[3] = &ppu;
        Il2CppObject* sp = IL2.Invoke(create, nullptr, a);
        if (sp) g_roundSprite.set(sp);
    }
    LOGI("rounded sprite %s", g_roundSprite ? "ready" : "tex-only");
}

static Ref g_font;
static Il2CppObject* GetFont() {
    if (g_font && Alive(g_font.get())) return g_font.get();
    if (!UI.Font) return nullptr;
    // Preferred: dynamic OS font, always present on Quest.
    if (UI.FontFromOSFont) {
        int size = 24;
        void* a[2] = { IL2.StrNew("Roboto"), &size };
        Il2CppObject* f = IL2.Invoke(UI.FontFromOSFont, nullptr, a);
        if (f) { g_font.set(f); return f; }
    }
    // Fallback: builtin resource.
    if (UI.ResourcesBuiltin && UI.Font && IL2.TypeOf(UI.Font)) {
        void* a[2] = { IL2.TypeOf(UI.Font), IL2.StrNew("Arial.ttf") };
        Il2CppObject* f = IL2.Invoke(UI.ResourcesBuiltin, nullptr, a);
        if (f) { g_font.set(f); return f; }
    }
    LOGE("font unresolved — labels will render blank");
    return nullptr;
}

// ============================================================================
// Theme
// ============================================================================
// Palette lifted from the reference: near-black surfaces, a single-pixel
// lighter border on every panel, white type, one grey for inactive tabs.
static const Color kBg        {0.055f,0.055f,0.062f,0.98f};
static const Color kPanel     {0.085f,0.085f,0.095f,1.00f};
static const Color kPanelAlt  {0.105f,0.105f,0.118f,1.00f};
static const Color kTabActive {0.145f,0.145f,0.160f,1.00f};
static const Color kTabIdle   {0.105f,0.105f,0.118f,1.00f};
static const Color kBtn       {0.115f,0.115f,0.128f,1.00f};
static const Color kBtnHover  {0.190f,0.190f,0.215f,1.00f};
static const Color kCheckOn   {0.960f,0.960f,0.970f,1.00f};   // white check
static const Color kCheckOff  {0.055f,0.055f,0.062f,1.00f};
static const Color kText      {0.960f,0.960f,0.970f,1.00f};
static const Color kTextDim   {0.620f,0.620f,0.650f,1.00f};
static const Color kTitle     {1.000f,1.000f,1.000f,1.00f};
static const Color kSliderBg  {0.070f,0.070f,0.080f,1.00f};
static const Color kSliderFill{0.930f,0.930f,0.950f,1.00f};   // white knob/fill
static const Color kAccent    {0.960f,0.960f,0.970f,1.00f};
static const Color kDanger    {0.900f,0.340f,0.380f,1.00f};
static const Color kOk        {0.360f,0.880f,0.560f,1.00f};
static const Color kBorder    {0.230f,0.230f,0.255f,1.00f};   // hairline

static void SetColor(Il2CppObject* go, Color c, Il2CppClass* compCls) {
    if (!go) return;
    Il2CppObject* comp = GetComp(go, compCls);
    if (!comp || !UI.GraphicSetColor) return;
    void* a[1] = { &c };
    IL2.Invoke(UI.GraphicSetColor, comp, a);
}

// ============================================================================
// Widgets
// ============================================================================
// Rounded panel. Every surface in the menu goes through here so the corner
// radius, the slicer, and the colour path are identical everywhere — one
// shape language instead of a pile of one-off quads.
static Il2CppObject* MakePanel(Il2CppObject* parent, float w, float h, Vec3 pos, Color c,
                               bool square=false) {
    Il2CppObject* go = NewGO("Panel", parent);
    if (!go) return nullptr;
    Il2CppObject* img = AddComp(go, UI.Image);
    if (!img) { Kill(go); return nullptr; }
    SetSD(go, w, h);
    SetLP(go, pos);

    if (!square && UI.ImageSetType && g_roundSprite) {
        // Image.Type.Sliced = 1
        int32_t sliced = 1;
        void* a[1] = { &sliced };
        IL2.Invoke(UI.ImageSetType, img, a);
        // sprite is a property; write via the component's setter.
        Il2CppClass* imgCls = IL2.GetClass("UnityEngine.UI","Image","UnityEngine.UI");
        MethodInfo* setSprite = imgCls ? IL2.GetMethod(imgCls, "set_sprite", 1) : nullptr;
        if (setSprite) { void* b[1] = { g_roundSprite.get() }; IL2.Invoke(setSprite, img, b); }
    }
    if (UI.GraphicSetColor) { void* a[1] = { &c }; IL2.Invoke(UI.GraphicSetColor, img, a); }
    return go;
}

static Il2CppObject* MakeText(Il2CppObject* parent, const char* s, float w, float h,
                              Vec3 pos, Color c, int fontSize, int align=4) {
    Il2CppObject* go = NewGO("Label", parent);
    if (!go) return nullptr;
    Il2CppObject* txt = AddComp(go, UI.Text);
    if (!txt) { Kill(go); return nullptr; }
    SetSD(go, w, h);
    SetLP(go, pos);
    if (UI.TextSetText)     { void* a[1] = { IL2.StrNew(s) }; IL2.Invoke(UI.TextSetText, txt, a); }
    if (UI.TextSetFontSize) { void* a[1] = { &fontSize };     IL2.Invoke(UI.TextSetFontSize, txt, a); }
    if (UI.TextSetAlign)    { void* a[1] = { &align };        IL2.Invoke(UI.TextSetAlign, txt, a); }
    if (UI.GraphicSetColor) { void* a[1] = { &c };            IL2.Invoke(UI.GraphicSetColor, txt, a); }
    Il2CppObject* f = GetFont();
    if (f && UI.TextSetFont) { void* a[1] = { f }; IL2.Invoke(UI.TextSetFont, txt, a); }
    return go;
}

static void SetText(Il2CppObject* go, const char* s) {
    Il2CppObject* t = GetComp(go, UI.Text);
    if (!t || !UI.TextSetText) return;
    void* a[1] = { IL2.StrNew(s) };
    IL2.Invoke(UI.TextSetText, t, a);
}
static void SetTextColor(Il2CppObject* go, Color c) {
    SetColor(go, c, UI.Text);
}

// ============================================================================
// Row model
// ============================================================================
struct MenuRow {
    const char* label   = "";
    bool*       flag    = nullptr;
    void      (*action)() = nullptr;
    float*      slider  = nullptr;
    float       lo      = 0;
    float       hi      = 0;
    Tab         tab     = TAB_PLAYER;

    Ref rowGO{}, labelGO{}, checkGO{}, sliderBg{}, sliderFill{}, valueGO{}, checkMark{};
};
static MenuRow g_rows[] = {
    // ---- Player ----
    {"Movement",        nullptr,       nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Fly Speed (m/s)", nullptr,       nullptr,             nullptr,           1,  40, TAB_PLAYER},
    {"Flap Multiplier", nullptr,       nullptr,             nullptr,           1,  20, TAB_PLAYER},
    {"Fly",             &f_fly,        nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Velocity Fly",    &f_velocityFly,nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Noclip",          &f_noclip,     nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Punch Mod",       &f_punch,      nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Low Gravity",     &f_lowGravity, nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"No Gravity",      &f_noGravity,  nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Speed Hack",      &f_speed,      nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Next Speed Tier", nullptr,       Feature_NextSpeed,   nullptr,           0,   0,  TAB_PLAYER},
    {"Spin",            &f_spin,       nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Portal Gun",      &f_portalGun,  nullptr,             nullptr,           0,   0,  TAB_PLAYER},
    {"Save Spot",       nullptr,       Feature_SaveSpot,    nullptr,           0,   0,  TAB_PLAYER},
    {"Go To Spot",      nullptr,       Feature_GoToSpot,    nullptr,           0,   0,  TAB_PLAYER},

    // ---- Exploits ----
    {"Exploits",        nullptr,       nullptr,             nullptr,           0,   0,  TAB_EXPLOITS},
    {"Freeze Players",  &f_freeze,     nullptr,             nullptr,           0,   0,  TAB_EXPLOITS},
    {"Big Head",        &f_bigHead,    nullptr,             nullptr,           0,   0,  TAB_EXPLOITS},
    {"Invisible",       &f_invisible,  nullptr,             nullptr,           0,   0,  TAB_EXPLOITS},
    {"Name Spoof",      &f_nameSpoof,  nullptr,             nullptr,           0,   0,  TAB_EXPLOITS},
    {"Anti-EAC",        &f_antiEac,    nullptr,             nullptr,           0,   0,  TAB_EXPLOITS},

    // ---- Visual ----
    {"Visual",          nullptr,       nullptr,             nullptr,           0,   0,  TAB_VISUAL},
    {"ESP",             &f_esp,        nullptr,             nullptr,           0,   0,  TAB_VISUAL},
    {"Skybox",          &f_skybox,     nullptr,             nullptr,           0,   0,  TAB_VISUAL},
    {"Rainbow Skybox",  &f_skyRainbow, nullptr,             nullptr,           0,   0,  TAB_VISUAL},
    {"Next Sky Color",  nullptr,       Feature_NextSkyColor,nullptr,           0,   0,  TAB_VISUAL},
    {"World Blocks",    &f_worldBlocks,nullptr,             nullptr,           0,   0,  TAB_VISUAL},
    {"Rainbow Blocks",  &f_blockRainbow,nullptr,            nullptr,           0,   0,  TAB_VISUAL},
    {"Orbit Cam",       &f_orbitCam,   nullptr,             nullptr,           0,   0,  TAB_VISUAL},

    // ---- Players (dynamic rows built at runtime) ----
    {"Players",         nullptr,       nullptr,             nullptr,           0,   0,  TAB_PLAYERS},

    // ---- Soundboard: control rows above the clip list ----
    {"Soundboard",      nullptr,       nullptr,             nullptr,           0,   0,  TAB_SOUNDBOARD},
    {"Play Broadcast",  nullptr,       Action_Broadcast,    nullptr,           0,   0,  TAB_SOUNDBOARD},
    {"Stop All Audio",  nullptr,       Action_StopAudio,    nullptr,           0,   0,  TAB_SOUNDBOARD},

    // ---- GunLib (lives in the Text slot so the tab strip stays 8 wide) ----
    {"GunLib",          nullptr,       nullptr,             nullptr,           0,   0,  TAB_TEXT},
    {"Crash Target",    nullptr,       Action_Crash,        nullptr,           0,   0,  TAB_TEXT},
    {"Lag Room",        nullptr,       Action_Lag,          nullptr,           0,   0,  TAB_TEXT},
    {"Kick Target",     nullptr,       Action_Kick,         nullptr,           0,   0,  TAB_TEXT},
    {"Target: (none)",  nullptr,       nullptr,             nullptr,           0,   0,  TAB_TEXT},   // label updated live

    // ---- Misc ----
    {"Misc",            nullptr,       nullptr,             nullptr,           0,   0,  TAB_MISC},
    {"All Off",         nullptr,       Feature_AllOff,      nullptr,           0,   0,  TAB_MISC},

    // ---- Credits ----
    {"Credits",         nullptr,       nullptr,             nullptr,           0,   0,  TAB_CREDITS},
    {"Dark Matter",     nullptr,       nullptr,             nullptr,           0,   0,  TAB_CREDITS},
    {"Quest build",     nullptr,       nullptr,             nullptr,           0,   0,  TAB_CREDITS},
    {"ill2cpp",         nullptr,       nullptr,             nullptr,           0,   0,  TAB_CREDITS},
    {"Photon",          nullptr,       nullptr,             nullptr,           0,   0,  TAB_CREDITS},

    // ---- Auth ----
    {"Auth",            nullptr,       nullptr,             nullptr,           0,   0,  TAB_AUTH},
    {"Key",             nullptr,       nullptr,             nullptr,           0,   0,  TAB_AUTH},
};
static constexpr int kRowCount = (int)(sizeof(g_rows)/sizeof(g_rows[0]));

static constexpr float kW = 900.f, kH = 720.f;
static constexpr float kTabH = 46.f;
static constexpr float kTabY = kH/2 - 190.f;
static constexpr float kContentTop = kTabY - kTabH/2 - 14.f;
static constexpr float kRowH = 42.f;
static constexpr float kRowW = kW - 30.f;
static constexpr float kSliderW = 640.f;
static constexpr float kCheckSize = 32.f;

static Tab      g_activeTab = TAB_PLAYER;
static bool     g_menuOpen  = false;
static bool     g_docked    = false;
static double   g_openAt    = 0;
static bool     g_built     = false;

static Ref g_root, g_canvas, g_bg, g_title, g_fpsTxt, g_dockBtn, g_dockTxt,
           g_content, g_cursor, g_cursorDot, g_status;
static Ref g_tabGO[TAB_COUNT], g_tabTxt[TAB_COUNT];
static int  g_hoverRow = -1;
static int  g_hoverTab = -1;
static bool g_triggerWas = false;
static double g_nextFps = 0;
static int  g_frames = 0;
static float g_fps = 60.f;
static double g_nextStatus = 0;

// ---- Dynamic rows (Players / Soundboard) -----------------------------------
// Built once at a per-tab slot and refreshed in place. Never reallocated while
// the tab is open, so a label pointer cannot go stale mid-click.
static constexpr int kDynMax = 32;
struct DynRow {
    Ref rowGO, labelGO, actionGO;
    int playerIdx = -1;
};
static DynRow g_dyn[kDynMax];
static int    g_dynCount = 0;

static void BuildDynRow(int slot, const char* text, Color c) {
    if (slot < 0 || slot >= kDynMax) return;
    DynRow& d = g_dyn[slot];
    if (!d.rowGO) {
        d.rowGO    = MakePanel(g_content.get(), kRowW, kRowH-3, {0,0,0}, kPanel);
        d.labelGO  = MakeText(g_content.get(), text, kRowW-30, kRowH-8, {0,0,-1}, c, 15, 3);
    } else {
        SetText(d.labelGO.get(), text);
        SetTextColor(d.labelGO.get(), c);
    }
}

static void UpdateDynRows() {
    int n = 0;
    if (g_activeTab == TAB_PLAYERS) {
        int pc = (int)Feature_PlayerCount();
        if (pc > kDynMax) pc = kDynMax;
        for (int i=0;i<pc && n<kDynMax;i++) {
            char buf[128];
            snprintf(buf, sizeof buf, "%s  %.0fm%s",
                     Feature_PlayerName(i), Feature_PlayerDist(i),
                     Feature_PlayerIsLocal(i) ? "  (you)" : "");
            BuildDynRow(n, buf, Feature_PlayerIsLocal(i) ? kTextDim : kText);
            g_dyn[n].playerIdx = i;
            n++;
        }
    } else if (g_activeTab == TAB_SOUNDBOARD) {
        int sc = Soundboard::Count();
        for (int i=0;i<sc && n<kDynMax;i++) {
            BuildDynRow(n, Soundboard::Name(i), kText);
            g_dyn[n].playerIdx = i;
            n++;
        }
        if (g_soundStatus[0]) BuildDynRow(n++, g_soundStatus, kAccent);
    }
    // Park unused rows
    for (int i=n;i<kDynMax;i++) {
        if (g_dyn[i].rowGO) {
            Show(g_dyn[i].rowGO.get(), false);
            Show(g_dyn[i].labelGO.get(), false);
        }
    }
    g_dynCount = n;

    float y = -kRowH/2;
    for (int i=0;i<n;i++) {
        Show(g_dyn[i].rowGO.get(), true);
        Show(g_dyn[i].labelGO.get(), true);
        SetLP(g_dyn[i].rowGO.get(), {0,y,0});
        SetLP(g_dyn[i].labelGO.get(), {0,y,-1});
        y -= kRowH;
    }
}

// ============================================================================
// Scene objects
// ============================================================================

// Left/right hand transforms. Resolved from the local avatar's animator when
// available; camera transform is the fallback so the menu is never unreachable.
static Ref g_leftHand, g_rightHand;

static void ResolveHands() {
    if (g_leftHand && Alive(g_leftHand.get())) return;

    // VRCPlayerApi -> gameObject -> transform is the reliable path. The
    // animator's hand bones are named on the Unity humanoid rig.
    Il2CppClass *animCls = IL2.GetClass("UnityEngine","Animator","UnityEngine.AnimationModule");
    if (!animCls) animCls = IL2.GetClass("UnityEngine","Animator","UnityEngine");
    Il2CppClass *hbc = IL2.GetClass("UnityEngine","HumanBodyBones","UnityEngine.AnimationModule");
    if (!hbc) hbc = IL2.GetClass("UnityEngine","HumanBodyBones","UnityEngine");

    // Fast path: VRChat exposes VRCPlayerApi.GetBoneTransform(HumanBodyBones).
    Il2CppClass *api = IL2.GetClass("","VRCPlayerApi","Assembly-CSharp");
    MethodInfo *getBone = api ? IL2.GetMethod(api,"GetBoneTransform",1) : nullptr;
    Il2CppClass *net  = IL2.GetClass("","Networking","Assembly-CSharp");
    MethodInfo *getLocal = net ? IL2.GetMethod(net,"get_LocalPlayer",0) : nullptr;

    if (getBone && getLocal) {
        Il2CppObject *local = IL2.Invoke(getLocal, nullptr);
        if (local) {
            // HumanBodyBones.LeftHand = 17, RightHand = 18 in Unity's enum.
            int32_t left = 17, right = 18;
            void* al[1] = { &left };
            void* ar[1] = { &right };
            Il2CppObject *lt = IL2.Invoke(getBone, local, al);
            Il2CppObject *rt = IL2.Invoke(getBone, local, ar);
            if (lt) g_leftHand.set(lt);
            if (rt) g_rightHand.set(rt);
        }
    }
    if (g_leftHand && g_rightHand) {
        LOGI("hands bound via GetBoneTransform");
        return;
    }

    // Fallback: camera. Menu still appears and is usable, just anchored to head.
    Il2CppObject *cam = UI.CamGetMain ? IL2.Invoke(UI.CamGetMain, nullptr) : nullptr;
    if (cam) {
        Il2CppObject *camTr = Tr(cam);
        if (camTr) { g_leftHand.set(camTr); g_rightHand.set(camTr); }
        LOGI("hands fallback: camera");
    }
}

// ============================================================================
// Build
// ============================================================================
static void RebuildContent();

static void BuildMenu() {
    if (!UI.ready) return;
    MakeRoundedSprite();
    LOGI("building menu");

    g_root = NewGO("DarkMatterRoot");
    if (!g_root) { LOGE("root alloc failed"); return; }
    SetLS(g_root.get(), {1,1,1});

    g_canvas = NewGO("DarkMatterCanvas", g_root.get());
    if (!g_canvas) return;
    Il2CppObject* canvas = AddComp(g_canvas.get(), UI.Canvas);
    if (!canvas) { LOGE("canvas component failed"); return; }
    if (UI.CanvasSetMode) { int mode = 2; void* a[1] = {&mode}; IL2.Invoke(UI.CanvasSetMode, canvas, a); }
    Il2CppObject* cam = UI.CamGetMain ? IL2.Invoke(UI.CamGetMain, nullptr) : nullptr;
    if (cam && UI.CanvasSetWorldCam) { void* a[1] = {cam}; IL2.Invoke(UI.CanvasSetWorldCam, canvas, a); }
    SetSD(g_canvas.get(), kW, kH);
    SetLS(g_canvas.get(), {0.001f, 0.001f, 0.001f});   // 1 px = 1 mm

    g_bg = MakePanel(g_canvas.get(), kW, kH, {0,0,0}, kBg);

    g_title = MakeText(g_canvas.get(), "Dark Matter  <3", 520, 40,
                       {-kW/2 + (kW-360)/2 + 20, kH/2-34, -1}, kTitle, 30, 3);
    g_fpsTxt= MakeText(g_canvas.get(), "FPS: 60.0", 200, 26,
                       {-kW/2+118, kH/2-78, -1}, kText, 20, 3);
    g_status= MakeText(g_canvas.get(), "", kW-420, 22,
                       {kW/2-220, kH/2-78, -1}, kTextDim, 15, 5);

    g_dockBtn = MakePanel(g_canvas.get(), 330, 48, {-kW/2+180, kH/2-130, -1}, kBtn);
    g_dockTxt = MakeText(g_canvas.get(), "Dock in Front of Face", 320, 30,
                         {-kW/2+180, kH/2-130, -2}, kText, 20, 4);

    // Tab bar
    float tabW = kW / (float)TAB_COUNT;
    for (int i=0;i<TAB_COUNT;i++){
        float x = -kW/2 + tabW*(i+0.5f);
        g_tabGO[i] = MakePanel(g_canvas.get(), tabW-6, kTabH, {x, kTabY, -1},
                               i==(int)g_activeTab ? kTabActive : kTabIdle, true);
        g_tabTxt[i] = MakeText(g_canvas.get(), kTabNames[i], tabW-10, kTabH-10,
                               {x, kTabY, -2}, kText, 15, 4);
    }

    g_content = NewGO("Content", g_canvas.get());
    SetLP(g_content.get(), {0, kContentTop, -2});

    // Cursor: a 12 mm quad, no image sprite, tinted so it reads on dark panels.
    g_cursor = NewGO("Cursor", g_root.get());
    if (g_cursor) {
        g_cursorDot = MakePanel(g_cursor.get(), 10, 10, {0,0,0}, Color{1,1,1,1}, true);
        SetLS(g_cursor.get(), {0.002f,0.002f,0.002f});
    }

    // Pre-create one row per menu entry; visibility flips with the tab.
    //
    // Row anatomy, matching the reference:
    //   header   ▾ Label                         full width, darker, triangle
    //   slider   [====|-------]   15.000        Fly Speed
    //   toggle   [✓] Label                       square box, no rounding
    //   action   Label                           full-width button
    for (int i=0;i<kRowCount;i++){
        MenuRow& r = g_rows[i];
        bool isHeader = !r.flag && !r.action && !r.slider;
        bool isSlider = r.slider != nullptr || (i==1 || i==2);
        r.rowGO = MakePanel(g_content.get(), kRowW, kRowH-4, {0,0,0},
                            isHeader ? kPanelAlt : kPanel, isHeader ? false : false);
        if (isSlider) {
            // Wide track on the left, numeric value centered over it, label on
            // the right — exactly the reference ordering.
            r.sliderBg  = MakePanel(g_content.get(), kSliderW, 26, {0,0,-1}, kSliderBg);
            r.sliderFill= MakePanel(g_content.get(), kSliderW, 26, {0,0,-2}, kSliderFill);
            r.valueGO   = MakeText(g_content.get(), "", 200, 34, {0,0,-1}, kText, 26, 4);
            r.labelGO   = MakeText(g_content.get(), r.label, 300, kRowH-8, {0,0,-1}, kText, 22, 5);
        } else if (r.flag) {
            r.checkGO   = MakePanel(g_content.get(), 34, 34, {0,0,-1}, *r.flag ? kCheckOn : kCheckOff, true);
            r.checkMark = MakeText(g_content.get(), "✓", 30, 30, {0,0,-2}, kCheckOff, 24, 4);
            r.labelGO   = MakeText(g_content.get(), r.label, kRowW-90, kRowH-8, {0,0,-1}, kText, 22, 3);
        } else if (isHeader) {
            char hdr[80];
            snprintf(hdr, sizeof hdr, "\u25BC   %s", r.label);
            r.labelGO   = MakeText(g_content.get(), hdr, kRowW-130, kRowH-8,
                                   {-kRowW/2 + 130, 0, -1}, kTitle, 24, 3);
        } else {
            r.labelGO   = MakeText(g_content.get(), r.label, kRowW-40, kRowH-8, {0,0,-1}, kText, 22, 3);
        }
        Show(r.rowGO.get(), false);
        Show(r.labelGO.get(), false);
        Show(r.checkGO.get(), false);
        Show(r.checkMark.get(), false);
        Show(r.sliderBg.get(), false);
        Show(r.sliderFill.get(), false);
        Show(r.valueGO.get(), false);
    }

    Show(g_root.get(), false);
    g_built = true;
    LOGI("menu built");
    RebuildContent();
}

// ============================================================================
// Content layout
// ============================================================================
static void LayoutRow(MenuRow& r, int i, float y) {
    // Horizontal split, matching the reference geometry:
    //   track x from -kRowW/2+28 to that + kSliderW; value centered on the
    //   track; label right-aligned in the remaining 210 px gutter.
    const float trackX  = -kRowW/2 + 28.f;
    const float trackY  = y;
    const float valX    = trackX + kSliderW * 0.5f;
    const float labelX  = kRowW/2 - 150.f;

    bool isSlider = r.slider || i==1 || i==2;
    bool isHeader = !r.flag && !r.action && !r.slider;

    SetLP(r.rowGO.get(), {0, y, 0});

    if (isSlider) {
        SetLP(r.sliderBg.get(),   {trackX + kSliderW*0.5f, trackY, -1});
        SetLP(r.sliderFill.get(), {trackX + kSliderW*0.5f, trackY, -2});
        SetLP(r.valueGO.get(),    {valX, trackY, -3});
        SetLP(r.labelGO.get(),    {labelX, y, -1});
    } else if (r.flag) {
        SetLP(r.checkGO.get(),   {-kRowW/2 + 28, y, -1});
        SetLP(r.checkMark.get(), {-kRowW/2 + 28, y, -2});
        SetLP(r.labelGO.get(),   {-kRowW/2 + 62, y, -1});
        // left-aligned label: widen and left-anchor via size, position handles it
    } else if (isHeader) {
        SetLP(r.labelGO.get(), {0, y, -1});
    } else {
        SetLP(r.labelGO.get(), {0, y, -1});
    }
}

static void RebuildContent() {
    if (!g_content) return;
    float y = -kRowH/2;
    for (int i=0;i<kRowCount;i++){
        MenuRow& r = g_rows[i];
        bool vis = (r.tab == g_activeTab);
        if (vis && r.tab == TAB_PLAYERS) {
            // Players tab: one row per player, built on demand
            Show(r.rowGO.get(), false);
            Show(r.labelGO.get(), false);
            continue;
        }
        Show(r.rowGO.get(), vis);
        Show(r.labelGO.get(), vis);
        Show(r.checkGO.get(), vis);
        Show(r.checkMark.get(), vis);
        Show(r.sliderBg.get(), vis);
        Show(r.sliderFill.get(), vis);
        Show(r.valueGO.get(), vis);
        if (!vis) continue;

        // Slider rows are indices 1 and 2; resolve the float pointer late so the
        // menu reflects the live value rather than a snapshot.
        float* val = (i==1) ? Feature_FlySpeedPtr() : (i==2 ? Feature_FlapMultPtr() : nullptr);
        if (val && r.sliderFill) {
            float lo = r.lo, hi = r.hi;
            float frac = (hi > lo) ? (*val - lo) / (hi - lo) : 0.f;
            if (frac < 0) frac = 0;
            if (frac > 1) frac = 1;
            float fillW = kSliderW * frac;
            if (fillW < 4.f) fillW = 4.f;
            Vec2 sd{ fillW, 26.f };
            Il2CppObject* fillTr = Tr(r.sliderFill.get());
            if (fillTr && UI.RtSetSD) { void* a[1] = {&sd}; IL2.Invoke(UI.RtSetSD, fillTr, a); }
            float trackX = -kRowW/2 + 28.f;
            SetLP(r.sliderFill.get(), { trackX + fillW*0.5f, y, -2 });
            char buf[32]; snprintf(buf, sizeof buf, "%.3f", *val);
            SetText(r.valueGO.get(), buf);
        }
        if (r.flag) {
            SetColor(r.checkGO.get(), *r.flag ? kCheckOn : kCheckOff, UI.Image);
            SetTextColor(r.checkMark.get(), *r.flag ? kCheckOff : kCheckOn);
            SetText(r.checkMark.get(), "✓");
        }
        LayoutRow(r, i, y);
        y -= kRowH;
    }
    for (int i=0;i<TAB_COUNT;i++)
        SetColor(g_tabGO[i].get(), i==(int)g_activeTab ? kTabActive : kTabIdle, UI.Image);
    UpdateDynRows();
    // GunLib target row reflects the current selection
    for (int i=0;i<kRowCount;i++) {
        if (g_rows[i].tab == TAB_TEXT && strstr(g_rows[i].label, "Target:")) {
            SetText(g_rows[i].labelGO.get(), g_gunTargetLabel);
        }
    }
}

// ============================================================================
// Placement
// ============================================================================
static void PlaceMenu() {
    ResolveHands();
    if (!g_root || !g_root.alive()) return;
    Il2CppObject* anchor = g_docked ? g_rightHand.get() : g_leftHand.get();
    if (!anchor) return;
    Il2CppObject* anchorTr = Tr(anchor);
    if (!anchorTr) return;
    Il2CppObject* rootTr = Tr(g_root.get());
    if (!rootTr) return;

    Vec3 pos = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, anchorTr));
    Quat rot = IL2.Unbox<Quat>(IL2.Invoke(UI.TrGetRot, anchorTr));

    if (g_docked) {
        // 0.55 m in front of the face, facing back at the head.
        Vec3 fwd = { 0, 0, 1 };
        // Unity forward is +Z; use the head's forward.
        Vec3 headFwd = V3Norm(Vec3{ (rot.x*rot.z + rot.w*rot.y)*2,
                                     (rot.y*rot.z - rot.w*rot.x)*2,
                                     1 - 2*(rot.x*rot.x + rot.y*rot.y) });
        (void)fwd;
        pos = pos + headFwd * 0.55f;
        rot = Quat{ rot.x, rot.y, rot.z, rot.w };
    } else {
        pos = pos + Vec3{0.03f, 0.12f, 0.02f};
    }

    void* a[1] = { &pos };
    IL2.Invoke(UI.TrSetPos, rootTr, a);
    void* b[1] = { &rot };
    IL2.Invoke(UI.TrSetRot, rootTr, b);
}

static void UpdateCursor() {
    if (!g_cursor || !g_cursor.alive()) return;
    ResolveHands();
    Il2CppObject* hand = g_rightHand.get();
    if (!hand) return;
    Il2CppObject* handTr = Tr(hand);
    if (!handTr) return;
    Vec3 p = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, handTr));
    Il2CppObject* ctr = Tr(g_cursor.get());
    if (!ctr) return;
    void* a[1] = { &p };
    IL2.Invoke(UI.TrSetPos, ctr, a);
}

static Vec3 g_cursorWS{};   // world-space cursor position, refreshed each frame

// Transform a world point into canvas pixel space using the canvas transform.
static bool WorldToCanvas(Vec3 world, Vec2& outPx) {
    // Simple orthographic projection along the canvas forward:
    // px = (P - C) · right ; py = (P - C) · up
    Il2CppObject* canvasTr = Tr(g_canvas.get());
    if (!canvasTr) return false;
    Vec3 c   = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, canvasTr));
    Quat q   = IL2.Unbox<Quat>(IL2.Invoke(UI.TrGetRot, canvasTr));
    // Build basis from quaternion.
    float x2=q.x+q.x, y2=q.y+q.y, z2=q.z+q.z;
    float xx=q.x*x2, xy=q.x*y2, xz=q.x*z2;
    float yy=q.y*y2, yz=q.y*z2, zz=q.z*z2;
    float wx=q.w*x2, wy=q.w*y2, wz=q.w*z2;
    Vec3 right { 1-(yy+zz), xy+wz,     xz-wy };
    Vec3 up    { xy-wz,     1-(xx+zz), yz+wx };
    Vec3 d = world - c;
    outPx.x =  V3Dot(d, right) / 0.001f;   // canvas localScale is 0.001
    outPx.y =  V3Dot(d, up)    / 0.001f;
    return true;
}

// Hit test in canvas pixels.
// returns: -100 = none, -2..-2-TAB_COUNT-1 = tab index (as -(i+2)), >=0 row index
static int HitTest(const Vec2& px) {
    // Tabs
    float tabW = kW / (float)TAB_COUNT;
    if (fabsf(px.y - kTabY) < kTabH/2) {
        int t = (int)((px.x + kW/2) / tabW);
        if (t >= 0 && t < TAB_COUNT) return -(t + 2);
    }
    // Dock button — 330 x 52, anchored left, under the FPS line
    if (fabsf(px.x - (-kW/2+180)) < 165 && fabsf(px.y - (kH/2-128)) < 26) return -1;
    // Rows: walk the same layout positions RebuildContent produced
    float y = -kRowH/2;
    for (int i=0;i<kRowCount;i++){
        MenuRow& r = g_rows[i];
        if (r.tab != g_activeTab) continue;
        if (fabsf(px.y - y) < kRowH/2 && fabsf(px.x) < kRowW/2) return i;
        y -= kRowH;
    }
    // Dynamic rows live below the static block on Players / Soundboard
    // Return -(1000 + index) so the click handler can tell them apart.
    for (int i=0;i<g_dynCount;i++){
        if (fabsf(px.y - y) < kRowH/2 && fabsf(px.x) < kRowW/2) return -(1000 + i);
        y -= kRowH;
    }
    return -100;
}

static bool Trigger() { return Feature_RTrigger(); }

// Slider drag: while a slider row is held, right stick Y nudges it.
static void DragSlider(int rowIdx) {
    if (rowIdx < 0 || rowIdx >= kRowCount) return;
    MenuRow& r = g_rows[rowIdx];
    float* val = (rowIdx==1) ? Feature_FlySpeedPtr() : (rowIdx==2 ? Feature_FlapMultPtr() : nullptr);
    if (!val) return;
    float delta = Feature_RAxisY() * 0.6f * g_dt * (r.hi - r.lo);
    *val += delta;
    if (*val < r.lo) *val = r.lo;
    if (*val > r.hi) *val = r.hi;
    RebuildContent();
}

// ============================================================================
// Tick
// ============================================================================
void Menu_Init() { LOGI("menu init"); }

void Menu_Update() {
    if (!InitUI()) return;
    if (!g_built) { BuildMenu(); return; }

    // FPS
    g_frames++;
    if (g_time >= g_nextFps) {
        g_fps = (float)g_frames / (float)(g_time - (g_nextFps - 1.0));
        if (g_fps > 999.f) g_fps = 999.f;
        if (g_fps < 0.f)   g_fps = 0.f;
        g_frames = 0; g_nextFps = g_time + 1.0;
        char buf[32]; snprintf(buf, sizeof buf, "FPS %.0f", g_fps);
        SetText(g_fpsTxt.get(), buf);
    }

    // Status line: photon state + room size
    if (g_time >= g_nextStatus) {
        g_nextStatus = g_time + 1.0;
        char buf[96];
        snprintf(buf, sizeof buf, "photon %s   players %d   soundboard %d",
                 Photon::Ready() ? "ok" : "down",
                 Photon::PlayerCount(), Soundboard::Count());
        SetText(g_status.get(), buf);
    }

    if (Feature_LeftStickClick()) {
        g_menuOpen = !g_menuOpen;
        Show(g_root.get(), g_menuOpen);
        if (g_menuOpen) { g_openAt = g_time; RebuildContent(); }
        LOGI("menu %s", g_menuOpen ? "OPEN" : "CLOSED");
    }
    if (!g_menuOpen) return;

    PlaceMenu();
    UpdateCursor();

    // Cursor world position comes from the hand transform
    if (g_rightHand) {
        Il2CppObject* handTr = Tr(g_rightHand.get());
        if (handTr) g_cursorWS = IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos, handTr));
    }

    Vec2 px;
    int hit = WorldToCanvas(g_cursorWS, px) ? HitTest(px) : -100;

    if (hit != g_hoverRow) {
        if (g_hoverRow <= -1000) {
            int d = -(g_hoverRow + 1000);
            if (d >= 0 && d < kDynMax && g_dyn[d].rowGO)
                SetColor(g_dyn[d].rowGO.get(), kPanel, UI.Image);
        }
        if (g_hoverRow >= 0 && g_hoverRow < kRowCount) {
            MenuRow& old = g_rows[g_hoverRow];
            bool hdr = !old.flag && !old.action && !old.slider;
            SetColor(old.rowGO.get(), hdr ? kPanelAlt : kPanel, UI.Image);
        }
        if (hit >= 0 && hit < kRowCount) {
            SetColor(g_rows[hit].rowGO.get(), kBtnHover, UI.Image);
        }
        if (hit <= -1000) {
            int d = -(hit + 1000);
            if (d >= 0 && d < kDynMax && g_dyn[d].rowGO)
                SetColor(g_dyn[d].rowGO.get(), kBtnHover, UI.Image);
        }
        g_hoverRow = hit;
    }
    if (hit < -1) {
        int t = -(hit + 2);
        if (t != g_hoverTab) {
            if (g_hoverTab >= 0) SetColor(g_tabGO[g_hoverTab].get(),
                                          g_hoverTab==(int)g_activeTab ? kTabActive : kTabIdle, UI.Image);
            if (t >= 0 && t < TAB_COUNT && t != (int)g_activeTab)
                SetColor(g_tabGO[t].get(), kBtnHover, UI.Image);
            g_hoverTab = t;
        }
    }

    bool trig = Trigger();
    bool trigDown = trig && !g_triggerWas;
    g_triggerWas = trig;

    if (!trigDown) return;

    if (hit == -1) { g_docked = !g_docked; LOGI("dock %d", g_docked); return; }

    if (hit <= -1000) {
        int d = -(hit + 1000);
        if (d < 0 || d >= g_dynCount) return;
        if (g_activeTab == TAB_PLAYERS) {
            SetGunTarget(g_dyn[d].playerIdx);        // select as gun target
            RebuildContent();
        } else if (g_activeTab == TAB_SOUNDBOARD) {
            int clip = g_dyn[d].playerIdx;
            if (clip >= 0 && clip < Soundboard::Count()) {
                g_soundSel = clip;
                Soundboard::PlayLocal(clip);          // click = hear it
                LOGI("soundboard: local play %d", clip);
            }
        }
        return;
    }

    if (hit < -1) {
        int t = -(hit + 2);
        if (t >= 0 && t < TAB_COUNT) {
            g_activeTab = (Tab)t;
            g_hoverRow = -1;
            RebuildContent();
        }
        return;
    }

    if (hit >= 0 && hit < kRowCount) {
        MenuRow& r = g_rows[hit];
        bool isHeader = !r.flag && !r.action && !r.slider;
        if (isHeader) return;
        if (hit == 1 || hit == 2) { DragSlider(hit); return; }
        if (r.flag) {
            *r.flag = !*r.flag;
            SetColor(r.checkGO.get(), *r.flag ? kCheckOn : kCheckOff, UI.Image);
            SetTextColor(r.checkMark.get(), *r.flag ? kCheckOff : kCheckOn);
            SetText(r.checkMark.get(), "✓");
            LOGI("toggle %s = %d", r.label, *r.flag ? 1 : 0);
            return;
        }
        if (r.action) { r.action(); RebuildContent(); }
    }
}

void Menu_Shutdown() {
    // Explicit teardown: UI objects first, references released after.
    for (int i=0;i<kRowCount;i++) {
        MenuRow& r = g_rows[i];
        r.rowGO.clear(); r.labelGO.clear(); r.checkGO.clear();
        r.sliderBg.clear(); r.sliderFill.clear(); r.valueGO.clear(); r.checkMark.clear();
    }
    for (int i=0;i<TAB_COUNT;i++){ g_tabGO[i].clear(); g_tabTxt[i].clear(); }
    g_cursorDot.clear(); g_cursor.clear();
    g_status.clear(); g_dockTxt.clear(); g_dockBtn.clear();
    g_content.clear(); g_fpsTxt.clear(); g_title.clear(); g_bg.clear();
    g_canvas.clear(); g_root.clear();
    g_leftHand.clear(); g_rightHand.clear();
    g_font.clear();
    g_built = false;
    LOGI("menu shutdown");
}
