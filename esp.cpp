// esp.cpp — C++17, arm64-v8a
// World-space ESP: box + name + distance label drawn via Unity GL lines each frame.
// Rendered on the same Canvas layer as the menu (uses a GL.IssuePluginEvent hook).
// Falls back to 2D screen-space overlay if 3D canvas isn't wired yet.
#include "shared.hpp"
#include <vector>
#include <cstdio>

// ============================================================================
// Extern: player list from features.cpp
// ============================================================================
extern std::vector<struct PlayerInfo> g_players;
struct PlayerInfo{ Il2CppObject *api; std::string name; Vec3 pos; float dist; bool isLocal; int id; };

// ============================================================================
// GL line layer — uses UnityEngine.GL
// ============================================================================
static struct {
    bool ready = false;
    Il2CppClass *GL,*Camera,*Matrix4x4Cls,*Graphics,*Material,*Shader,*GameObject,*Transform;
    MethodInfo  *GL_PushMatrix,*GL_PopMatrix,*GL_LoadProjectionMatrix,*GL_MultMatrix,
                *GL_Begin,*GL_End,*GL_Vertex3,*GL_Color,
                *Cam_GetMain,*Cam_WorldToScreen,*Cam_GetProjMat,*Cam_GetViewMat,
                *GoGetTr,*TrGetPos,*TrGetRot,
                *ShaderFind,*MatNew,*MatSetColor,*MatSetPass;
} E;

static Ref g_espMatRef;

static bool InitESP(){
    if (E.ready) return true;
    if (!IL2.ready) return false;
#define C(v,ns,n,a) E.v=IL2.GetClass(ns,n,a);
#define M(v,c,n,argc) E.v=IL2.GetMethod(E.c,n,argc);
    C(GL,       "UnityEngine","GL",        "UnityEngine")
    C(Camera,   "UnityEngine","Camera",    "UnityEngine")
    C(Material, "UnityEngine","Material",  "UnityEngine")
    C(Shader,   "UnityEngine","Shader",    "UnityEngine")
    C(GameObject,"UnityEngine","GameObject","UnityEngine")
    C(Transform, "UnityEngine","Transform", "UnityEngine")

    M(GL_PushMatrix,         GL,"PushMatrix",          0)
    M(GL_PopMatrix,          GL,"PopMatrix",           0)
    M(GL_Begin,              GL,"Begin",               1)
    M(GL_End,                GL,"End",                 0)
    M(GL_Vertex3,            GL,"Vertex3",             3) // float,float,float overload
    M(GL_Color,              GL,"Color",               1)
    M(Cam_GetMain,           Camera,"get_main",        0)
    M(Cam_WorldToScreen,     Camera,"WorldToScreenPoint",1)
    M(Cam_GetProjMat,        Camera,"get_projectionMatrix",0)
    M(Cam_GetViewMat,        Camera,"get_worldToCameraMatrix",0)
    M(GoGetTr,               GameObject,"get_transform",0)
    M(TrGetPos,              Transform, "get_position", 0)
    M(ShaderFind,            Shader,    "Find",         1)
    M(MatNew,                Material,  ".ctor",        1)
    M(MatSetColor,           Material,  "set_color",    1)
    M(MatSetPass,            Material,  "SetPass",      1)
#undef C
#undef M
    E.ready = E.GL && E.GL_Begin && E.Cam_GetMain && E.ShaderFind;
    // Build the line material (Hidden/Internal-Colored)
    if (E.ready && !g_espMatRef.alive() && E.ShaderFind && E.MatNew){
        const char *shaders[]={
            "Hidden/Internal-Colored",
            "Unlit/Color",
            "Universal Render Pipeline/Unlit",
            nullptr
        };
        Il2CppObject *sh=nullptr;
        for (int i=0;shaders[i]&&!sh;i++){
            void*a[1]={IL2.StrNew(shaders[i])};
            sh=IL2.Invoke(E.ShaderFind,nullptr,a);
        }
        if (sh){
            Il2CppObject *mat=IL2.object_new(E.Material);
            if (E.MatNew){void*a[1]={sh};IL2.Invoke(E.MatNew,mat,a);}
            // ZTest Always, ZWrite Off, Blend SrcAlpha OneMinusSrcAlpha
            // Can't set those via API alone without property ids — accept default
            g_espMatRef.set(mat);
        }
    }
    LOGI("ESP %s", E.ready?"ready":"INCOMPLETE");
    return E.ready;
}

// ============================================================================
// GL helpers
// ============================================================================
static void GLBeginLines(){
    if (!E.GL_Begin) return;
    int mode=1; // GL.LINES = 1
    void*a[1]={&mode}; IL2.Invoke(E.GL_Begin,nullptr,a);
}
static void GLVertex(float x,float y,float z){
    if (!E.GL_Vertex3) return;
    void*a[3]={&x,&y,&z}; IL2.Invoke(E.GL_Vertex3,nullptr,a);
}
static void GLSetColor(Color c){
    if (!E.GL_Color) return;
    void*a[1]={&c}; IL2.Invoke(E.GL_Color,nullptr,a);
}

// World→screen (returns Vec3; z=depth, x/y=pixels)
static Vec3 W2S(Il2CppObject *cam, Vec3 wp){
    if (!cam||!E.Cam_WorldToScreen) return {-1,-1,-1};
    void*a[1]={&wp};
    Il2CppObject *o=IL2.Invoke(E.Cam_WorldToScreen,cam,a);
    return o?IL2.Unbox<Vec3>(o):Vec3{-1,-1,-1};
}

// ============================================================================
// Draw a world-space axis-aligned box around a player
// ============================================================================
// Half-extents for a ~1.8m tall capsule
static constexpr float kBoxW=0.35f, kBoxH=0.9f;

static void DrawBox(Il2CppObject *cam, Vec3 center, Color c){
    // 8 corners of the AABB
    Vec3 corners[8]={
        {center.x-kBoxW, center.y-kBoxH, center.z-kBoxW},
        {center.x+kBoxW, center.y-kBoxH, center.z-kBoxW},
        {center.x+kBoxW, center.y-kBoxH, center.z+kBoxW},
        {center.x-kBoxW, center.y-kBoxH, center.z+kBoxW},
        {center.x-kBoxW, center.y+kBoxH, center.z-kBoxW},
        {center.x+kBoxW, center.y+kBoxH, center.z-kBoxW},
        {center.x+kBoxW, center.y+kBoxH, center.z+kBoxW},
        {center.x-kBoxW, center.y+kBoxH, center.z+kBoxW},
    };
    // Project all corners; skip if any behind camera (z<=0)
    Vec2 sc[8];
    for (int i=0;i<8;i++){
        Vec3 s=W2S(cam,corners[i]);
        if (s.z<=0) return;
        sc[i]={s.x,s.y};
    }
    GLSetColor(c);
    // Bottom face
    static const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto &e:edges){
        GLVertex(corners[e[0]].x,corners[e[0]].y,corners[e[0]].z);
        GLVertex(corners[e[1]].x,corners[e[1]].y,corners[e[1]].z);
    }
}

// ============================================================================
// 2D screen-space label (via Unity Debug.Log-level fallback — no IMGUI)
// Real label draw needs a Canvas/Text GO; we reuse the menu's MakeText pattern.
// esp.cpp only owns the GL lines; name labels are drawn by the ESP canvas layer.
// ============================================================================

// ============================================================================
// ESP Canvas layer — a separate World Space Canvas parented to main camera
// that holds one Text GO per visible player (max 32 slots, recycled)
// ============================================================================
static constexpr int kMaxSlots=32;
struct ESPSlot{
    Il2CppObject *go=nullptr; // the Text GO
    bool inUse=false;
};
static ESPSlot g_slots[kMaxSlots];
static Il2CppObject *g_espCanvas=nullptr;
static bool g_espCanvasBuilt=false;

// shared helpers (declared in menu.cpp, mirrored here as statics to keep linkage clean)
static Il2CppObject *ESP_NewGO(const char *name,Il2CppObject *parent=nullptr){
    if (!IL2.object_new) return nullptr;
    Il2CppClass *goCls=IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    if (!goCls) return nullptr;
    Il2CppObject *go=IL2.object_new(goCls);
    MethodInfo *ctor=IL2.GetMethod(goCls,".ctor",1);
    if (ctor){void*a[1]={IL2.StrNew(name)};IL2.Invoke(ctor,go,a);}
    if (parent){
        Il2CppClass *trCls=IL2.GetClass("UnityEngine","Transform","UnityEngine");
        MethodInfo  *sp  =IL2.GetMethod(trCls,"SetParent",2);
        MethodInfo  *gtr =IL2.GetMethod(goCls,"get_transform",0);
        if (gtr&&sp){
            Il2CppObject *ct=IL2.Invoke(gtr,go),*pt=IL2.Invoke(gtr,parent);
            int b=0; void*a[2]={pt,&b}; IL2.Invoke(sp,ct,a);
        }
    }
    return go;
}

static bool ESP_Alive(Il2CppObject *o){
    if (!o) return false;
    Il2CppClass *obj=IL2.GetClass("UnityEngine","Object","UnityEngine");
    MethodInfo  *op =obj?IL2.GetMethod(obj,"op_Implicit",1):nullptr;
    if (!op) return true;
    Il2CppObject *r=IL2.Invoke(op,nullptr,(void**)&o);
    return r?IL2.Unbox<int32_t>(r)!=0:false;
}

static void ESP_SetActive(Il2CppObject *go,bool on){
    Il2CppClass *goCls=IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    MethodInfo  *sa  =goCls?IL2.GetMethod(goCls,"SetActive",1):nullptr;
    if (sa){void*a[1]={&on};IL2.Invoke(sa,go,a);}
}

static void ESP_SetLocalPos(Il2CppObject *go,Vec3 p){
    Il2CppClass *goCls=IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    Il2CppClass *trCls=IL2.GetClass("UnityEngine","Transform","UnityEngine");
    MethodInfo *gtr=goCls?IL2.GetMethod(goCls,"get_transform",0):nullptr;
    MethodInfo *slp=trCls?IL2.GetMethod(trCls,"set_localPosition",1):nullptr;
    if (!gtr||!slp) return;
    Il2CppObject *tr=IL2.Invoke(gtr,go);
    void*a[1]={&p}; IL2.Invoke(slp,tr,a);
}

static void ESP_SetText(Il2CppObject *go,const char *s){
    Il2CppClass *txtCls=IL2.GetClass("UnityEngine.UI","Text","UnityEngine.UI");
    MethodInfo  *set  =txtCls?IL2.GetMethod(txtCls,"set_text",1):nullptr;
    Il2CppClass *goCls=IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    MethodInfo  *gc  =goCls?IL2.GetMethod(goCls,"GetComponent",1):nullptr;
    if (!set||!gc||!IL2.class_type) return;
    void *type=IL2.class_type(txtCls);
    Il2CppObject *txt=IL2.Invoke(gc,go,(void**)&type);
    if (txt){void*a[1]={IL2.StrNew(s)};IL2.Invoke(set,txt,a);}
}

static Il2CppObject *ESP_MakeTextGO(Il2CppObject *parent,const char *str,Vec3 lp){
    Il2CppClass *txtCls=IL2.GetClass("UnityEngine.UI","Text","UnityEngine.UI");
    Il2CppClass *goCls =IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    if (!txtCls||!goCls) return nullptr;
    Il2CppObject *go=ESP_NewGO("ESPLabel",parent);
    if (!go) return nullptr;
    // AddComponent — same pattern as menu.cpp
    if (IL2.class_type){
        MethodInfo *ac=IL2.GetMethod(goCls,"AddComponent",1);
        if (ac){
            void *type=IL2.class_type(txtCls);
            Il2CppObject *txt=IL2.Invoke(ac,go,(void**)&type);
            if (txt){
                // font size 14, white
                MethodInfo *sfs=IL2.GetMethod(txtCls,"set_fontSize",1);
                MethodInfo *stt=IL2.GetMethod(txtCls,"set_text",1);
                Il2CppClass *grp=IL2.GetClass("UnityEngine.UI","Graphic","UnityEngine.UI");
                MethodInfo *sc=grp?IL2.GetMethod(grp,"set_color",1):nullptr;
                if (sfs){int fs=14;void*a[1]={&fs};IL2.Invoke(sfs,txt,a);}
                if (stt){void*a[1]={IL2.StrNew(str)};IL2.Invoke(stt,txt,a);}
                if (sc){ Color c={1,1,1,1};void*a[1]={&c};IL2.Invoke(sc,txt,a);}
            }
        }
    }
    // Size delta
    Il2CppClass *rtCls=IL2.GetClass("UnityEngine","RectTransform","UnityEngine");
    MethodInfo *gtr=IL2.GetMethod(goCls,"get_transform",0);
    MethodInfo *ssd=rtCls?IL2.GetMethod(rtCls,"set_sizeDelta",1):nullptr;
    MethodInfo *slp=IL2.GetMethod(IL2.GetClass("UnityEngine","Transform","UnityEngine"),"set_localPosition",1);
    if (gtr&&slp){ Il2CppObject *tr=IL2.Invoke(gtr,go); void*a[1]={&lp};IL2.Invoke(slp,tr,a);}
    if (gtr&&ssd){ Vec2 sd={120,30}; Il2CppObject *tr=IL2.Invoke(gtr,go); void*a[1]={&sd};IL2.Invoke(ssd,tr,a);}
    return go;
}

static void BuildESPCanvas(){
    // Lightweight canvas parented to camera
    Il2CppClass *camCls=IL2.GetClass("UnityEngine","Camera","UnityEngine");
    MethodInfo  *gm   =camCls?IL2.GetMethod(camCls,"get_main",0):nullptr;
    if (!gm) return;
    Il2CppObject *cam=IL2.Invoke(gm,nullptr);
    if (!cam) return;

    Il2CppObject *canvasGO=ESP_NewGO("ESPCanvas",cam);
    if (!canvasGO) return;
    Il2CppClass *canvasCls=IL2.GetClass("UnityEngine","Canvas","UnityEngine");
    if (canvasCls&&IL2.class_type){
        MethodInfo *ac=IL2.GetMethod(IL2.GetClass("UnityEngine","GameObject","UnityEngine"),"AddComponent",1);
        if (ac){
            void *type=IL2.class_type(canvasCls);
            Il2CppObject *cv=IL2.Invoke(ac,canvasGO,(void**)&type);
            if (cv){
                MethodInfo *srm=IL2.GetMethod(canvasCls,"set_renderMode",1);
                if (srm){int mode=2;void*a[1]={&mode};IL2.Invoke(srm,cv,a);}
            }
        }
    }
    // Scale 1px = 1mm
    Il2CppClass *trCls=IL2.GetClass("UnityEngine","Transform","UnityEngine");
    Il2CppClass *goCls=IL2.GetClass("UnityEngine","GameObject","UnityEngine");
    MethodInfo *gtr=IL2.GetMethod(goCls,"get_transform",0);
    MethodInfo *sls=IL2.GetMethod(trCls,"set_localScale",1);
    if (gtr&&sls){ Il2CppObject *tr=IL2.Invoke(gtr,canvasGO); Vec3 s={0.001f,0.001f,0.001f}; void*a[1]={&s};IL2.Invoke(sls,tr,a);}
    g_espCanvas=canvasGO;
    // Pre-allocate slots
    for (int i=0;i<kMaxSlots;i++){
        g_slots[i].go=ESP_MakeTextGO(canvasGO,"",{0,0,0});
        g_slots[i].inUse=false;
        if (g_slots[i].go) ESP_SetActive(g_slots[i].go,false);
    }
    g_espCanvasBuilt=true;
    LOGI("ESP canvas built, %d slots",kMaxSlots);
}

// ============================================================================
// ESP feature flag (declared in features.cpp)
// ============================================================================
extern bool f_esp;

// ============================================================================
// Init / Update
// ============================================================================
void ESP_Init(){
    LOGI("ESP init");
}

void ESP_Update(){
    if (!f_esp) return;
    if (!InitESP()) return;
    if (!g_espCanvasBuilt) BuildESPCanvas();

    Il2CppClass *camCls=IL2.GetClass("UnityEngine","Camera","UnityEngine");
    MethodInfo  *gm   =camCls?IL2.GetMethod(camCls,"get_main",0):nullptr;
    Il2CppObject *cam =gm?IL2.Invoke(gm,nullptr):nullptr;

    // ---- Text label update (canvas slots) ----------------------------------
    // Mark all free first
    for (int i=0;i<kMaxSlots;i++) g_slots[i].inUse=false;

    int slot=0;
    for (auto &p:g_players){
        if (p.isLocal) continue;
        if (slot>=kMaxSlots) break;
        // Build label "Name\n12.3m"
        char buf[128];
        snprintf(buf,sizeof buf,"%s\n%.1fm",p.name.c_str(),p.dist);

        // Place GO at world position + small Y offset for head-height
        Vec3 labelPos={p.pos.x,p.pos.y+1.0f,p.pos.z};
        ESP_SetActive(g_slots[slot].go,true);
        ESP_SetText(g_slots[slot].go,buf);
        // localPosition in canvas space: convert world→canvas local
        // For a camera-parented canvas this is a simplified screen-space place
        if (cam){
            Vec3 ss=W2S(cam,labelPos);
            if (ss.z>0){
                // ss.x/y in pixels (0..screen); remap to canvas units
                // canvas scale 0.001 so world-unit canvas is 1000px wide
                // We parent to camera so local offset is in view space
                // Simple: place at (ss.x-screenW/2, ss.y-screenH/2) in canvas pixels
                // ScreenWidth/Height — use a fixed 2160×1200 Quest2 res as fallback
                // FILL: replace 2160/1200 with actual Screen.width/height if wired
                float sw=2160.f,sh=1200.f;
                Vec3 lp={(ss.x-sw/2)*0.001f,(ss.y-sh/2)*0.001f,-0.5f};
                ESP_SetLocalPos(g_slots[slot].go,lp);
            }
        }
        g_slots[slot].inUse=true;
        slot++;
    }
    // Hide unused slots
    for (int i=slot;i<kMaxSlots;i++)
        if (g_slots[i].go) ESP_SetActive(g_slots[i].go,false);

    // ---- GL lines (3D boxes) -----------------------------------------------
    if (!cam||!E.GL_Begin||!g_espMatRef.alive()) return;
    Il2CppObject *mat=g_espMatRef.get();
    if (E.MatSetPass){ int pass=0; void*a[1]={&pass}; IL2.Invoke(E.MatSetPass,mat,a);}
    if (E.GL_PushMatrix) IL2.Invoke(E.GL_PushMatrix,nullptr);
    GLBeginLines();
    for (auto &p:g_players){
        if (p.isLocal) continue;
        // Colour: green within 10m, yellow 10-30m, red beyond
        Color c=p.dist<10.f?Color{0,1,0,1}:p.dist<30.f?Color{1,1,0,1}:Color{1,0,0,1};
        // Rainbow-tint at close range
        DrawBox(cam,p.pos,c);
    }
    if (E.GL_End)       IL2.Invoke(E.GL_End,nullptr);
    if (E.GL_PopMatrix) IL2.Invoke(E.GL_PopMatrix,nullptr);
}
