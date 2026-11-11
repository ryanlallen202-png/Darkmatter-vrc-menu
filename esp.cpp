#include "shared.hpp"

extern bool f_esp;

// ============================================================================
// ESP — name + distance above every remote player
//
// Render target: same world-space uGUI canvas family as the menu, but a
// separate canvas per player so a broken label cannot take the menu down.
//
// Projection is done here (not via Camera.WorldToScreenPoint) because the
// camera path needs a Vector3 out-parameter marshalled through il2cpp and
// returns screen pixels, which we would have to map back into a world-space
// canvas anyway. Straight view-space math is fewer moving parts and does not
// depend on which render pipeline the world uses.
//
// Refresh budget: labels are repositioned every frame (cheap transform write)
// while the player list itself only refreshes at the Features tick rate.
// ============================================================================

namespace {

struct Label {
    Ref go;
    Ref txt;
    int  playerIdx = -1;
};

std::vector<Label> g_labels;
std::vector<Ref>   g_labelTexts;

Il2CppClass* TextClass() {
    Il2CppClass* c = IL2.GetClass("UnityEngine.UI", "Text", "UnityEngine.UI");
    return c;
}
Il2CppClass* GoClass()   { return IL2.GetClass("UnityEngine", "GameObject", "UnityEngine.CoreModule"); }
Il2CppClass* TrClass()   { return IL2.GetClass("UnityEngine", "Transform", "UnityEngine.CoreModule"); }
Il2CppClass* CamClass()  { return IL2.GetClass("UnityEngine", "Camera", "UnityEngine.CoreModule"); }

// Minimal helpers — ESP keeps its own copies so a menu rebuild never leaves
// dangling method pointers behind.
struct EspApi {
    bool ready = false;
    MethodInfo *goCtor1 = nullptr, *goGetTr = nullptr, *goSetActive = nullptr,
               *addComp = nullptr, *trSetPos = nullptr, *trGetPos = nullptr,
               *getMain = nullptr, *textSetText = nullptr,
               *textSetSize = nullptr, *textSetFont = nullptr, *grafSetColor = nullptr,
               *fontFromOS = nullptr, *camGetTr = nullptr, *count = nullptr;
} A;

static bool InitEspApi() {
    if (A.ready) return true;
    Il2CppClass* go  = GoClass();
    Il2CppClass* tr  = TrClass();
    Il2CppClass* cam = CamClass();
    Il2CppClass* txt = TextClass();
    Il2CppClass* gr  = IL2.GetClass("UnityEngine.UI", "Graphic", "UnityEngine.UI");
    Il2CppClass* fnt = IL2.GetClass("UnityEngine", "Font", "UnityEngine.TextRenderingModule");
    if (!fnt) fnt = IL2.GetClass("UnityEngine", "Font", "UnityEngine");
    if (!go || !tr || !cam || !txt) { LOGE("esp: classes missing"); return false; }

    A.goCtor1     = IL2.GetMethod(go, ".ctor", 1);
    A.goGetTr     = IL2.GetMethod(go, "get_transform", 0);
    A.goSetActive = IL2.GetMethod(go, "SetActive", 1);
    A.addComp     = IL2.GetMethod(go, "AddComponent", 1);
    A.trSetPos    = IL2.GetMethod(tr, "set_position", 1);
    A.trGetPos    = IL2.GetMethod(tr, "get_position", 0);
    A.getMain     = IL2.GetMethod(cam, "get_main", 0);
    A.camGetTr    = A.goGetTr;
    A.textSetText = IL2.GetMethod(txt, "set_text", 1);
    A.textSetSize = IL2.GetMethod(txt, "set_fontSize", 1);
    A.textSetFont = IL2.GetMethod(txt, "set_font", 1);
    A.grafSetColor= gr ? IL2.GetMethod(gr, "set_color", 1) : nullptr;
    A.fontFromOS  = fnt ? IL2.GetMethod(fnt, "CreateDynamicFontFromOSFont", 2) : nullptr;

    A.ready = A.goCtor1 && A.goGetTr && A.addComp && A.trSetPos && A.getMain &&
              A.textSetText && A.textSetSize;
    LOGI("esp api %s", A.ready ? "ready" : "INCOMPLETE");
    return A.ready;
}

Ref g_espFont;
Il2CppObject* EspFont() {
    if (g_espFont) return g_espFont.get();
    if (!A.fontFromOS) return nullptr;
    int size = 20;
    void* a[2] = { IL2.StrNew("Roboto"), &size };
    Il2CppObject* f = IL2.Invoke(A.fontFromOS, nullptr, a);
    if (f) g_espFont.set(f);
    return f;
}

Il2CppObject* TrOf(Il2CppObject* go) {
    return (go && A.goGetTr) ? IL2.Invoke(A.goGetTr, go) : nullptr;
}

// World → canvas-plane pixel coords using the camera basis.
// Returns false when the point is behind the camera or off frustum.
bool ProjectToPixels(Vec3 world, Il2CppObject* camTr, Vec2& px, float& depth) {
    if (!camTr) return false;
    Vec3 c   = IL2.Unbox<Vec3>(IL2.Invoke(A.trGetPos, camTr));
    // Camera basis from the transform's rotation.
    __attribute__((unused)) auto noop = 0;
    Quat q{};
    {
        MethodInfo* gr = IL2.GetMethod(TrClass(), "get_rotation", 0);
        if (gr) q = IL2.Unbox<Quat>(IL2.Invoke(gr, camTr));
    }
    float x2=q.x+q.x, y2=q.y+q.y, z2=q.z+q.z;
    float xx=q.x*x2, xy=q.x*y2, xz=q.x*z2;
    float yy=q.y*y2, yz=q.y*z2, zz=q.z*z2;
    float wx=q.w*x2, wy=q.w*y2, wz=q.w*z2;
    Vec3 right { 1-(yy+zz), xy+wz,     xz-wy };
    Vec3 up    { xy-wz,     1-(xx+zz), yz+wx };
    Vec3 fwd   { xz+wy,     yz-wx,     1-(xx+yy) };

    Vec3 d = world - c;
    float z = V3Dot(d, fwd);
    if (z <= 0.1f) return false;
    depth = z;
    px.x = V3Dot(d, right) / z;
    px.y = V3Dot(d, up)    / z;
    return true;
}

} // namespace

void ESP_Init() { LOGI("ESP init"); }

void ESP_Update() {
    if (!f_esp) {
        if (A.goSetActive) {
            for (auto& l : g_labels) {
                bool off = false;
                void* b[1] = { &off };
                IL2.Invoke(A.goSetActive, l.go.get(), b);
            }
        }
        return;
    }
    if (!InitEspApi()) return;

    Il2CppObject* cam = IL2.Invoke(A.getMain, nullptr);
    if (!cam) return;
    Il2CppObject* camTr = TrOf(cam);
    if (!camTr) return;

    // Grow the label pool to the player count; never shrink (reuse in place).
    size_t need = g_players.size();
    if (need > 32) need = 32;
    while (g_labels.size() < need) {
        Il2CppObject* go = IL2.object_new(GoClass());
        if (!go) break;
        void* a[1] = { IL2.StrNew("ESP_Label") };
        IL2.Invoke(A.goCtor1, go, a);

        Il2CppObject* txtType = IL2.TypeOf(TextClass());
        if (!txtType) break;
        void* b[1] = { txtType };
        Il2CppObject* txt = IL2.Invoke(A.addComp, go, b);
        if (!txt) break;

        Label l;
        l.go.set(go);
        l.txt.set(txt);

        if (A.textSetSize) { int sz = 18; void* t[1] = {&sz}; IL2.Invoke(A.textSetSize, txt, t); }
        if (A.grafSetColor) {
            Color c{0.55f, 0.90f, 1.0f, 1.0f};
            void* t[1] = {&c};
            IL2.Invoke(A.grafSetColor, txt, t);
        }
        Il2CppObject* font = EspFont();
        if (font && A.textSetFont) { void* t[1] = {font}; IL2.Invoke(A.textSetFont, txt, t); }
        g_labels.push_back(std::move(l));
    }

    // Position each label above its player.
    size_t used = 0;
    for (size_t i=0;i<g_players.size() && used<g_labels.size();i++) {
        PlayerInfo& p = g_players[i];
        if (p.isLocal) continue;

        Vec2 px; float depth = 0;
        Vec3 above = p.pos + Vec3{0, 1.8f, 0};
        if (!ProjectToPixels(above, camTr, px, depth)) continue;
        if (depth > 80.f) continue;                    // 80 m cap keeps the label set useful
        if (fabsf(px.x) > 1.2f || fabsf(px.y) > 1.2f) continue;  // off-frustum

        Label& l = g_labels[used];
        if (A.goSetActive) { bool on = true; void* t[1] = {&on}; IL2.Invoke(A.goSetActive, l.go.get(), t); }

        // Distance-scaled offset in world units so the label sits over the head
        // at any range: 0.35 m per 10 m of depth, clamped.
        float off = 0.28f + depth * 0.012f;
        if (off > 1.0f) off = 1.0f;
        Vec3 draw = above + Vec3{0, off, 0};

        Il2CppObject* ltr = TrOf(l.go.get());
        if (ltr && A.trSetPos) { void* t[1] = {&draw}; IL2.Invoke(A.trSetPos, ltr, t); }

        char buf[96];
        snprintf(buf, sizeof buf, "%s  %.0fm", p.name.c_str(), p.dist);
        if (A.textSetText) { void* t[1] = {IL2.StrNew(buf)}; IL2.Invoke(A.textSetText, l.txt.get(), t); }
        used++;
    }

    // Park unused labels.
    if (A.goSetActive) {
        for (size_t i=used;i<g_labels.size();i++) {
            bool off = false;
            void* t[1] = {&off};
            IL2.Invoke(A.goSetActive, g_labels[i].go.get(), t);
        }
    }
}
