// menu.cpp — C++17, arm64-v8a — fixed forward decls + clean struct ordering
#include "shared.hpp"

// ============================================================================
// Forward decls from features.cpp
// ============================================================================
extern bool f_fly,f_velocityFly,f_speed,f_noclip,f_lowGravity,f_noGravity;
extern bool f_platforms,f_punch,f_esp,f_skybox,f_skyRainbow;
extern bool f_worldBlocks,f_blockRainbow,f_portalGun,f_orbitCam;
extern bool f_freeze,f_spin,f_bigHead,f_invisible,f_nameSpoof,f_antiEac;
extern int  g_speedTier,g_skyColorIdx;

void Feature_SaveSpot();
void Feature_GoToSpot();
void Feature_AllOff();
void Feature_NextSpeed();
void Feature_NextSkyColor();
const char* Feature_SkyName();
const char* Feature_SpeedName();
size_t      Feature_PlayerCount();
const char* Feature_PlayerName(int i);
float       Feature_PlayerDist(int i);
bool        Feature_PlayerIsLocal(int i);
bool        Feature_LeftStickClick();
bool        Feature_BtnA();
bool        Feature_RTrigger();
float       Feature_RAxisX();
float       Feature_RAxisY();
void        Soundboard_Play(int idx);
int         Soundboard_Count();
const char* Soundboard_Name(int i);

// ============================================================================
// Tab definitions
// ============================================================================
enum Tab {
    TAB_PLAYER,TAB_EXPLOITS,TAB_VISUAL,TAB_PLAYERS,
    TAB_SOUNDBOARD,TAB_TEXT,TAB_MISC,TAB_CONFIG,TAB_COUNT
};
static const char *kTabNames[TAB_COUNT]={
    "Player","Exploits","Visual","Players","Soundboard","Text","Misc","Config"
};
static Tab  g_activeTab = TAB_PLAYER;
static bool g_menuOpen  = false;
static bool g_docked    = false;
static double g_openAt  = 0;

// ============================================================================
// Unity UI class cache — plain struct, no anonymous struct
// ============================================================================
struct UIClassCache {
    bool ready=false;
    Il2CppClass *Canvas,*Text,*Image,*Button,*Toggle,*Slider,
                *RectTransform,*Font,*GameObject,*Transform,
                *Camera,*Input,*Shader,*Material,*Object;
    MethodInfo  *CanvasSetMode,
                *TrGetPos,*TrSetPos,*TrGetRot,*TrSetRot,
                *TrSetParent,*TrSetLP,*TrSetLR,*TrSetLS,
                *RtSetSD,*RtSetAnchorMin,*RtSetAnchorMax,*RtSetPivot,
                *TextSetText,*TextSetFontSize,*TextSetFont,
                *TextSetAlign,*TextSetBestFit,*TextSetMinSize,*TextSetMaxSize,
                *ImageSetColor,
                *GoGetTr,*GoAddComp,*GoGetComp,*GoSetActive,
                *ObjDestroy,*ObjAlive,
                *GraphicSetColor,
                *CamGetMain,
                *InputGetAxis,*InputGetButton,*InputGetButtonDown,
                *ShaderFind,*MatNew,*MatSetColor;
};
static UIClassCache UI;

static bool InitUI() {
    if (UI.ready) return true;
    if (!IL2.ready) return false;
#define C(v,ns,n,a) UI.v=IL2.GetClass(ns,n,a);
#define M(v,c,n,argc) UI.v=IL2.GetMethod(UI.c,n,argc);
    C(Canvas,        "UnityEngine",    "Canvas",          "UnityEngine")
    C(Text,          "UnityEngine.UI", "Text",            "UnityEngine.UI")
    C(Image,         "UnityEngine.UI", "Image",           "UnityEngine.UI")
    C(Button,        "UnityEngine.UI", "Button",          "UnityEngine.UI")
    C(Toggle,        "UnityEngine.UI", "Toggle",          "UnityEngine.UI")
    C(Slider,        "UnityEngine.UI", "Slider",          "UnityEngine.UI")
    C(RectTransform, "UnityEngine",    "RectTransform",   "UnityEngine")
    C(Font,          "UnityEngine",    "Font",            "UnityEngine")
    C(GameObject,    "UnityEngine",    "GameObject",      "UnityEngine")
    C(Transform,     "UnityEngine",    "Transform",       "UnityEngine")
    C(Camera,        "UnityEngine",    "Camera",          "UnityEngine")
    C(Input,         "UnityEngine",    "Input",           "UnityEngine")
    C(Shader,        "UnityEngine",    "Shader",          "UnityEngine")
    C(Material,      "UnityEngine",    "Material",        "UnityEngine")
    C(Object,        "UnityEngine",    "Object",          "UnityEngine")

    M(CanvasSetMode,    Canvas,       "set_renderMode",           1)
    M(TrGetPos,         Transform,    "get_position",             0)
    M(TrSetPos,         Transform,    "set_position",             1)
    M(TrGetRot,         Transform,    "get_rotation",             0)
    M(TrSetRot,         Transform,    "set_rotation",             1)
    M(TrSetParent,      Transform,    "SetParent",                2)
    M(TrSetLP,          Transform,    "set_localPosition",        1)
    M(TrSetLR,          Transform,    "set_localRotation",        1)
    M(TrSetLS,          Transform,    "set_localScale",           1)
    M(RtSetSD,          RectTransform,"set_sizeDelta",            1)
    M(RtSetAnchorMin,   RectTransform,"set_anchorMin",            1)
    M(RtSetAnchorMax,   RectTransform,"set_anchorMax",            1)
    M(RtSetPivot,       RectTransform,"set_pivot",                1)
    M(TextSetText,      Text,         "set_text",                 1)
    M(TextSetFontSize,  Text,         "set_fontSize",             1)
    M(TextSetFont,      Text,         "set_font",                 1)
    M(TextSetAlign,     Text,         "set_alignment",            1)
    M(TextSetBestFit,   Text,         "set_resizeTextForBestFit", 1)
    M(TextSetMinSize,   Text,         "set_resizeTextMinSize",    1)
    M(TextSetMaxSize,   Text,         "set_resizeTextMaxSize",    1)
    M(ImageSetColor,    Image,        "set_color",                1)
    M(GoGetTr,          GameObject,   "get_transform",            0)
    M(GoAddComp,        GameObject,   "AddComponent",             1)
    M(GoGetComp,        GameObject,   "GetComponent",             1)
    M(GoSetActive,      GameObject,   "SetActive",                1)
    M(CamGetMain,       Camera,       "get_main",                 0)
    M(InputGetAxis,     Input,        "GetAxis",                  1)
    M(InputGetButton,   Input,        "GetButton",                1)
    M(InputGetButtonDown,Input,       "GetButtonDown",            1)
    M(ShaderFind,       Shader,       "Find",                     1)
    M(MatSetColor,      Material,     "set_color",                1)
    M(ObjAlive,         Object,       "op_Implicit",              1)
    M(ObjDestroy,       Object,       "Destroy",                  1)
#undef C
#undef M
    Il2CppClass *graphic=IL2.GetClass("UnityEngine.UI","Graphic","UnityEngine.UI");
    if (graphic) UI.GraphicSetColor=IL2.GetMethod(graphic,"set_color",1);
    UI.MatNew=IL2.GetMethod(UI.Material,".ctor",1);
    UI.ready=UI.Canvas&&UI.Text&&UI.RectTransform&&UI.GameObject;
    LOGI("UI classes %s",UI.ready?"ready":"INCOMPLETE");
    return UI.ready;
}

// ---- Theme -----------------------------------------------------------------
static const Color kBgColor    ={0.08f,0.08f,0.10f,0.96f};
static const Color kPanelColor ={0.12f,0.12f,0.15f,1.00f};
static const Color kTabActive  ={0.25f,0.35f,0.55f,1.00f};
static const Color kTabInactive={0.18f,0.18f,0.22f,1.00f};
static const Color kBtnColor   ={0.20f,0.20f,0.25f,1.00f};
static const Color kBtnHover   ={0.30f,0.40f,0.65f,1.00f};
static const Color kCheckOn    ={0.25f,0.55f,0.90f,1.00f};
static const Color kCheckOff   ={0.18f,0.18f,0.22f,1.00f};
static const Color kTextColor  ={0.90f,0.90f,0.95f,1.00f};
static const Color kTextDim    ={0.55f,0.55f,0.60f,1.00f};
static const Color kTitleColor ={0.75f,0.85f,1.00f,1.00f};
static const Color kSliderBg   ={0.15f,0.15f,0.18f,1.00f};
static const Color kSliderFill ={0.30f,0.50f,0.90f,1.00f};

// ---- Basic helpers ---------------------------------------------------------
static bool Alive(Il2CppObject *o) {
    if (!o||!UI.ObjAlive) return o!=nullptr;
    Il2CppObject *r=IL2.Invoke(UI.ObjAlive,nullptr,(void**)&o);
    return r?IL2.Unbox<int32_t>(r)!=0:false;
}
static void Kill(Il2CppObject *o) {
    if (Alive(o)&&UI.ObjDestroy){void*a[1]={o};IL2.Invoke(UI.ObjDestroy,nullptr,a);}
}
static Il2CppObject *Tr(Il2CppObject *go) {
    return Alive(go)&&UI.GoGetTr?IL2.Invoke(UI.GoGetTr,go):nullptr;
}
static void SetLP(Il2CppObject *go,Vec3 p) {
    if (!Alive(go)||!UI.TrSetLP) return;
    Il2CppObject *t=Tr(go); void*a[1]={&p}; IL2.Invoke(UI.TrSetLP,t,a);
}
static void SetLS(Il2CppObject *go,Vec3 s) {
    if (!Alive(go)||!UI.TrSetLS) return;
    Il2CppObject *t=Tr(go); void*a[1]={&s}; IL2.Invoke(UI.TrSetLS,t,a);
}
static void SetSD(Il2CppObject *go,Vec2 sd) {
    if (!Alive(go)||!UI.RtSetSD) return;
    Il2CppObject *t=Tr(go); void*a[1]={&sd}; IL2.Invoke(UI.RtSetSD,t,a);
}
static void Parent(Il2CppObject *child,Il2CppObject *parent,bool worldPos=false) {
    if (!Alive(child)||!Alive(parent)||!UI.TrSetParent) return;
    Il2CppObject *ct=Tr(child),*pt=Tr(parent);
    int b=worldPos?1:0; void*a[2]={pt,&b}; IL2.Invoke(UI.TrSetParent,ct,a);
}
static void Show(Il2CppObject *go,bool on) {
    if (Alive(go)&&UI.GoSetActive){void*a[1]={&on};IL2.Invoke(UI.GoSetActive,go,a);}
}
static Il2CppObject *NewGO(const char *name,Il2CppObject *parent=nullptr) {
    if (!UI.GameObject||!IL2.object_new) return nullptr;
    Il2CppObject *go=IL2.object_new(UI.GameObject);
    MethodInfo *ctor=IL2.GetMethod(UI.GameObject,".ctor",1);
    if (ctor){void*a[1]={IL2.StrNew(name)};IL2.Invoke(ctor,go,a);}
    if (parent) Parent(go,parent,false);
    return go;
}
static Il2CppObject *AddComp(Il2CppObject *go,Il2CppClass *cls) {
    if (!Alive(go)||!cls||!UI.GoAddComp||!IL2.class_type) return nullptr;
    void *type=IL2.class_type(cls);
    if (!type) return nullptr;
    Il2CppClass *typeCls=IL2.GetClass("System","RuntimeType","mscorlib");
    if (!typeCls) typeCls=IL2.GetClass("System","Type","mscorlib");
    Il2CppObject *boxed=nullptr;
    if (typeCls&&IL2.val_box)
        boxed=(Il2CppObject*)IL2.val_box(typeCls,&type);
    void *arg=boxed?(void*)boxed:(void*)type;
    return IL2.Invoke(UI.GoAddComp,go,(void**)&arg);
}
static Il2CppObject *GetComp(Il2CppObject *go,Il2CppClass *cls) {
    if (!Alive(go)||!cls||!UI.GoGetComp||!IL2.class_type) return nullptr;
    void *type=IL2.class_type(cls);
    if (!type) return nullptr;
    return IL2.Invoke(UI.GoGetComp,go,(void**)&type);
}

static Ref g_fontRef;
static Il2CppObject *GetFont() {
    if (g_fontRef.alive()) return g_fontRef.get();
    if (!UI.Font) return nullptr;
    MethodInfo *create=IL2.GetMethod(UI.Font,"CreateDynamicFontFromOSFont",2);
    Il2CppObject *f=nullptr;
    if (create){
        int size=24; void *a[2]={IL2.StrNew("Arial"),(void*)&size};
        f=IL2.Invoke(create,nullptr,a);
    }
    if (!f||!Alive(f)){
        Il2CppClass *res=IL2.GetClass("UnityEngine","Resources","UnityEngine");
        MethodInfo  *gbr=res?IL2.GetMethod(res,"GetBuiltinResource",2):nullptr;
        if (gbr){ void*a[2]={(void*)UI.Font,IL2.StrNew("Arial.ttf")}; f=IL2.Invoke(gbr,nullptr,a); }
    }
    if (f) g_fontRef.set(f);
    return f;
}

static Il2CppObject *MakePanel(Il2CppObject *parent,Vec2 size,Vec3 pos,Color c) {
    Il2CppObject *go=NewGO("Panel",parent);
    if (!go) return nullptr;
    Il2CppObject *img=AddComp(go,UI.Image);
    SetSD(go,size); SetLP(go,{pos.x,pos.y,0});
    if (img&&UI.ImageSetColor){void*a[1]={&c};IL2.Invoke(UI.ImageSetColor,img,a);}
    return go;
}
static Il2CppObject *MakeText(Il2CppObject *parent,const char *str,
                               Vec2 size,Vec3 pos,
                               Color c={0.9f,0.9f,0.95f,1},int fontSize=20) {
    Il2CppObject *go=NewGO("Label",parent);
    if (!go) return nullptr;
    Il2CppObject *txt=AddComp(go,UI.Text);
    SetSD(go,size); SetLP(go,{pos.x,pos.y,0});
    if (txt){
        if (UI.TextSetText){  void*a[1]={IL2.StrNew(str)};IL2.Invoke(UI.TextSetText,txt,a);}
        if (UI.TextSetFontSize){void*a[1]={&fontSize};IL2.Invoke(UI.TextSetFontSize,txt,a);}
        if (UI.GraphicSetColor){void*a[1]={&c};IL2.Invoke(UI.GraphicSetColor,txt,a);}
        Il2CppObject *fnt=GetFont();
        if (fnt&&UI.TextSetFont){void*a[1]={fnt};IL2.Invoke(UI.TextSetFont,txt,a);}
        if (UI.TextSetAlign){int al=4;void*a[1]={&al};IL2.Invoke(UI.TextSetAlign,txt,a);}
        if (UI.TextSetBestFit){int v=1;void*a[1]={&v};IL2.Invoke(UI.TextSetBestFit,txt,a);}
        if (UI.TextSetMinSize){int v=8;void*a[1]={&v};IL2.Invoke(UI.TextSetMinSize,txt,a);}
        if (UI.TextSetMaxSize){void*a[1]={&fontSize};IL2.Invoke(UI.TextSetMaxSize,txt,a);}
    }
    return go;
}
static void SetText(Il2CppObject *go,const char *s) {
    Il2CppObject *t=GetComp(go,UI.Text);
    if (t&&UI.TextSetText){void*a[1]={IL2.StrNew(s)};IL2.Invoke(UI.TextSetText,t,a);}
}
static void SetPanelColor(Il2CppObject *go,Color c) {
    Il2CppObject *img=GetComp(go,UI.Image);
    if (img&&UI.ImageSetColor){void*a[1]={&c};IL2.Invoke(UI.ImageSetColor,img,a);}
}

// ============================================================================
// Layout constants
// ============================================================================
static constexpr float kW=600,kH=440,kTabH=36;
static constexpr float kTabY=(kH/2)-kTabH/2-42;
static constexpr float kContentTop=kTabY-kTabH/2-4;
static constexpr float kRowH=36,kCheckSize=22,kSliderW=160;

// ============================================================================
// Menu row
// ============================================================================
struct MenuRow {
    const char *label;
    bool  *flag;
    void  (*action)();
    float *sliderVal;
    float  sliderMin,sliderMax;
    Tab    tab;
    Il2CppObject *rowGO=nullptr,*checkGO=nullptr,*labelGO=nullptr;
    Il2CppObject *sliderBgGO=nullptr,*sliderFillGO=nullptr;
};

static float g_flySpeed=15.f, g_flapMult=5.5f;

static MenuRow g_rows[]={
    {"Movement",       nullptr,         nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Fly Speed",      nullptr,         nullptr,              &g_flySpeed,  1,40,  TAB_PLAYER},
    {"Flap Mult",      nullptr,         nullptr,              &g_flapMult,  1,20,  TAB_PLAYER},
    {"JFly",           &f_fly,          nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Velocity Fly",   &f_velocityFly,  nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Platforms",      &f_platforms,    nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Noclip",         &f_noclip,       nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Punch Mod",      &f_punch,        nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Low Gravity",    &f_lowGravity,   nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"No Gravity",     &f_noGravity,    nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Speed Hack",     &f_speed,        nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Next Speed",     nullptr,         Feature_NextSpeed,    nullptr,      0, 0,  TAB_PLAYER},
    {"Spin",           &f_spin,         nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Portal Gun",     &f_portalGun,    nullptr,              nullptr,      0, 0,  TAB_PLAYER},
    {"Save Spot",      nullptr,         Feature_SaveSpot,     nullptr,      0, 0,  TAB_PLAYER},
    {"Go To Spot",     nullptr,         Feature_GoToSpot,     nullptr,      0, 0,  TAB_PLAYER},
    {"Exploits",       nullptr,         nullptr,              nullptr,      0, 0,  TAB_EXPLOITS},
    {"Freeze Players", &f_freeze,       nullptr,              nullptr,      0, 0,  TAB_EXPLOITS},
    {"Big Head",       &f_bigHead,      nullptr,              nullptr,      0, 0,  TAB_EXPLOITS},
    {"Invisible",      &f_invisible,    nullptr,              nullptr,      0, 0,  TAB_EXPLOITS},
    {"Name Spoof",     &f_nameSpoof,    nullptr,              nullptr,      0, 0,  TAB_EXPLOITS},
    {"Anti-EAC",       &f_antiEac,      nullptr,              nullptr,      0, 0,  TAB_EXPLOITS},
    {"Visual",         nullptr,         nullptr,              nullptr,      0, 0,  TAB_VISUAL},
    {"ESP",            &f_esp,          nullptr,              nullptr,      0, 0,  TAB_VISUAL},
    {"Skybox",         &f_skybox,       nullptr,              nullptr,      0, 0,  TAB_VISUAL},
    {"Rainbow Skybox", &f_skyRainbow,   nullptr,              nullptr,      0, 0,  TAB_VISUAL},
    {"Next Sky Color", nullptr,         Feature_NextSkyColor, nullptr,      0, 0,  TAB_VISUAL},
    {"World Blocks",   &f_worldBlocks,  nullptr,              nullptr,      0, 0,  TAB_VISUAL},
    {"Rainbow Blocks", &f_blockRainbow, nullptr,              nullptr,      0, 0,  TAB_VISUAL},
    {"Orbit Cam",      &f_orbitCam,     nullptr,              nullptr,      0, 0,  TAB_VISUAL},
    {"Players",        nullptr,         nullptr,              nullptr,      0, 0,  TAB_PLAYERS},
    {"Soundboard",     nullptr,         nullptr,              nullptr,      0, 0,  TAB_SOUNDBOARD},
    {"Text",           nullptr,         nullptr,              nullptr,      0, 0,  TAB_TEXT},
    {"Misc",           nullptr,         nullptr,              nullptr,      0, 0,  TAB_MISC},
    {"All Off",        nullptr,         Feature_AllOff,       nullptr,      0, 0,  TAB_MISC},
    {"Config",         nullptr,         nullptr,              nullptr,      0, 0,  TAB_CONFIG},
};
static constexpr int kRowCount=(int)(sizeof(g_rows)/sizeof(g_rows[0]));

// ============================================================================
// Scene handles
// ============================================================================
static Il2CppObject *g_menuRoot=nullptr,*g_canvasGO=nullptr;
static Il2CppObject *g_bgPanel=nullptr,*g_titleText=nullptr,*g_fpsText=nullptr;
static Il2CppObject *g_tabGOs[TAB_COUNT]={},*g_tabTextGOs[TAB_COUNT]={};
static Il2CppObject *g_contentRoot=nullptr,*g_cursor=nullptr;
static bool  g_built=false;
static int   g_hoverRow=-1;
static bool  g_triggerWas=false;
static double g_nextFpsUpdate=0;
static int   g_frameCount=0;
static float g_fps=60.f;
static Il2CppObject *g_leftHandTr=nullptr,*g_rightHandTr=nullptr;

// ============================================================================
// Content builder
// ============================================================================
static void BuildContentRows() {
    for (int i=0;i<kRowCount;i++){
        MenuRow &r=g_rows[i];
        Kill(r.rowGO);     r.rowGO=nullptr;
        Kill(r.checkGO);   r.checkGO=nullptr;
        Kill(r.labelGO);   r.labelGO=nullptr;
        Kill(r.sliderBgGO);r.sliderBgGO=nullptr;
        Kill(r.sliderFillGO);r.sliderFillGO=nullptr;
    }
    float y=0;
    for (int i=0;i<kRowCount;i++){
        MenuRow &row=g_rows[i];
        if (row.tab!=g_activeTab) continue;
        bool isHeader =!row.flag&&!row.action&&!row.sliderVal;
        bool isSlider  = row.sliderVal!=nullptr;
        bool isToggle  = row.flag!=nullptr;
        bool isAction  = row.action!=nullptr;
        Color bg=isHeader?Color{0.15f,0.15f,0.20f,1}:isAction?kBtnColor:kPanelColor;
        row.rowGO=MakePanel(g_contentRoot,{kW-20,kRowH-2},{0,y,0},bg);
        if (isHeader){
            MakeText(g_contentRoot,row.label,{kW-30,kRowH-6},{0,y,-0.5f},kTitleColor,16);
        } else if (isSlider){
            MakeText(g_contentRoot,row.label,{180,kRowH-6},{kW/2-110,y,-0.5f},kTextColor,15);
            row.sliderBgGO=MakePanel(g_contentRoot,{kSliderW,10},{-50,y,-0.5f},kSliderBg);
            float frac=(*row.sliderVal-row.sliderMin)/(row.sliderMax-row.sliderMin);
            row.sliderFillGO=MakePanel(g_contentRoot,
                {kSliderW*frac+2,10},{-50-kSliderW/2+kSliderW*frac/2,y,-1},kSliderFill);
            char buf[32]; snprintf(buf,sizeof buf,"%.3f",*row.sliderVal);
            row.labelGO=MakeText(g_contentRoot,buf,{80,kRowH-6},{50,y,-0.5f},kTextDim,15);
        } else if (isToggle){
            bool on=*row.flag;
            row.checkGO=MakePanel(g_contentRoot,{kCheckSize,kCheckSize},
                                   {-kW/2+20,y,-0.5f},on?kCheckOn:kCheckOff);
            if (on) MakeText(g_contentRoot,"\xe2\x9c\x93",{kCheckSize,kCheckSize},{-kW/2+20,y,-1},kTextColor,14);
            row.labelGO=MakeText(g_contentRoot,row.label,{kW-60,kRowH-6},{10,y,-0.5f},kTextColor,16);
        } else if (isAction){
            row.labelGO=MakeText(g_contentRoot,row.label,{kW-30,kRowH-6},{0,y,-0.5f},kTextColor,16);
        }
        y-=kRowH;
    }
}

// ============================================================================
// Full build
// ============================================================================
static void BuildMenu() {
    if (!UI.ready) return;
    LOGI("Building Darkmatter menu");
    g_menuRoot=NewGO("DarkmatterMenu");
    if (!g_menuRoot){LOGE("menuRoot null");return;}
    SetLS(g_menuRoot,{1,1,1});
    g_canvasGO=NewGO("MenuCanvas",g_menuRoot);
    if (!g_canvasGO) return;
    Il2CppObject *canvas=AddComp(g_canvasGO,UI.Canvas);
    if (canvas&&UI.CanvasSetMode){int mode=2;void*a[1]={&mode};IL2.Invoke(UI.CanvasSetMode,canvas,a);}
    SetSD(g_canvasGO,{kW,kH});
    SetLS(g_canvasGO,{0.001f,0.001f,0.001f});
    SetLP(g_canvasGO,{0.1f,0,0});
    g_bgPanel   =MakePanel(g_canvasGO,{kW,kH},{0,0,0},kBgColor);
    g_titleText =MakeText(g_canvasGO,"Dark Matter | VRChat Quest",{kW-20,28},{0,kH/2-18,-1},kTitleColor,22);
    g_fpsText   =MakeText(g_canvasGO,"FPS: 60.0",{120,20},{-kW/2+70,kH/2-36,-1},kTextDim,16);
    MakePanel(g_canvasGO,{160,28},{-kW/2+88,kH/2-60,-1},kBtnColor);
    MakeText(g_canvasGO,"Dock in Front of Face",{155,24},{-kW/2+88,kH/2-60,-1},kTextColor,14);
    float tabW=kW/(float)TAB_COUNT;
    for (int i=0;i<TAB_COUNT;i++){
        float x=-kW/2+tabW*(i+0.5f);
        g_tabGOs[i]    =MakePanel(g_canvasGO,{tabW-2,kTabH},{x,kTabY,-0.5f},
                                   i==g_activeTab?kTabActive:kTabInactive);
        g_tabTextGOs[i]=MakeText(g_canvasGO,kTabNames[i],{tabW-4,kTabH-6},
                                  {x,kTabY,-1},kTextColor,14);
    }
    g_contentRoot=NewGO("ContentRoot",g_canvasGO);
    SetLP(g_contentRoot,{0,kContentTop,-1});
    BuildContentRows();
    g_cursor=NewGO("Cursor");
    if (g_cursor){
        Il2CppObject *img=AddComp(g_cursor,UI.Image);
        SetLS(g_cursor,{0.015f,0.015f,0.015f});
        if (img&&UI.ImageSetColor){Color c={0.5f,0.8f,1,1};void*a[1]={&c};IL2.Invoke(UI.ImageSetColor,img,a);}
    }
    Show(g_menuRoot,false);
    g_built=true;
    LOGI("Menu built");
}

// ============================================================================
// Place / cursor / hit-test
// ============================================================================
static void UpdateHandRefs() {
    if (g_leftHandTr&&Alive(g_leftHandTr)) return;
    if (UI.CamGetMain){
        Il2CppObject *cam=IL2.Invoke(UI.CamGetMain,nullptr);
        if (cam){ g_leftHandTr=Tr(cam); g_rightHandTr=Tr(cam); }
    }
}
static void PlaceMenu() {
    UpdateHandRefs();
    if (!g_menuRoot||!Alive(g_menuRoot)) return;
    Il2CppObject *anchor=g_leftHandTr;
    if (!anchor||!UI.TrGetPos||!UI.TrSetPos||!UI.TrGetRot||!UI.TrSetRot) return;
    Il2CppObject *rootTr=Tr(g_menuRoot);
    Vec3 pos=IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos,anchor));
    if (g_docked){
        Il2CppObject *cam=UI.CamGetMain?IL2.Invoke(UI.CamGetMain,nullptr):nullptr;
        if (cam){
            Il2CppObject *ct=Tr(cam);
            if (ct){
                Il2CppClass *trCls=IL2.GetClass("UnityEngine","Transform","UnityEngine");
                MethodInfo *getFwd=IL2.GetMethod(trCls,"get_forward",0);
                Vec3 fwd={0,0,1};
                if (getFwd) fwd=IL2.Unbox<Vec3>(IL2.Invoke(getFwd,ct));
                pos=IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos,ct));
                pos=pos+fwd*0.5f;
            }
        }
    } else {
        pos.y+=0.15f; pos.x+=0.1f;
    }
    void *a[1]={&pos}; IL2.Invoke(UI.TrSetPos,rootTr,a);
    Quat rot=IL2.Unbox<Quat>(IL2.Invoke(UI.TrGetRot,anchor));
    void *b[1]={&rot}; IL2.Invoke(UI.TrSetRot,rootTr,b);
}
static void UpdateCursor() {
    if (!g_cursor||!Alive(g_cursor)||!g_rightHandTr) return;
    Vec3 rp=IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos,g_rightHandTr));
    Il2CppObject *ct=Tr(g_cursor);
    if (ct&&UI.TrSetPos){void*a[1]={&rp};IL2.Invoke(UI.TrSetPos,ct,a);}
}
static int HitTestRow() {
    if (!g_canvasGO||!Alive(g_canvasGO)||!g_rightHandTr) return -100;
    Vec3 cursorWS=IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos,g_rightHandTr));
    Il2CppObject *canvasTr=Tr(g_canvasGO);
    if (!canvasTr) return -100;
    Vec3 canvasPos=IL2.Unbox<Vec3>(IL2.Invoke(UI.TrGetPos,canvasTr));
    Vec3 d=cursorWS-canvasPos;
    float px=d.x/0.001f, py=d.y/0.001f;
    float tabW=kW/(float)TAB_COUNT;
    if (fabsf(py-kTabY)<kTabH/2){
        int t=(int)((px+kW/2)/tabW);
        if (t>=0&&t<TAB_COUNT) return -(t+2);
    }
    if (fabsf(px-(-kW/2+88))<80&&fabsf(py-(kH/2-60))<14) return -1;
    float y=kContentTop;
    for (int i=0;i<kRowCount;i++){
        if (g_rows[i].tab!=g_activeTab) continue;
        if (fabsf(py-y)<kRowH/2&&fabsf(px)<kW/2-10) return i;
        y-=kRowH;
    }
    return -100;
}
static bool Trigger() {
    if (!UI.Input||!UI.InputGetAxis) return false;
    void*a[1]={IL2.StrNew("Oculus_CrossPlatform_SecondaryIndexTrigger")};
    Il2CppObject *o=IL2.Invoke(UI.InputGetAxis,nullptr,a);
    return o&&IL2.Unbox<float>(o)>0.7f;
}

// ============================================================================
// Update loop
// ============================================================================
void Menu_Init()  { LOGI("Menu init"); }
void Menu_Update() {
    if (!InitUI()) return;
    if (!g_built){BuildMenu();return;}
    g_frameCount++;
    if (g_time>=g_nextFpsUpdate){
        double elapsed=g_time-(g_nextFpsUpdate-1.0);
        g_fps=elapsed>0?(float)(g_frameCount/elapsed):60.f;
        if (g_fps>999) g_fps=999;
        g_frameCount=0; g_nextFpsUpdate=g_time+1.0;
        char buf[32]; snprintf(buf,sizeof buf,"FPS: %.1f",g_fps);
        SetText(g_fpsText,buf);
    }
    if (Feature_LeftStickClick()){
        g_menuOpen=!g_menuOpen;
        Show(g_menuRoot,g_menuOpen);
        if (g_menuOpen) g_openAt=g_time;
    }
    if (!g_menuOpen) return;
    PlaceMenu();
    UpdateCursor();
    int  hit=HitTestRow();
    bool trig=Trigger();
    bool trigDown=trig&&!g_triggerWas;
    g_triggerWas=trig;
    if (hit!=g_hoverRow){
        if (g_hoverRow>=0&&g_hoverRow<kRowCount)
            SetPanelColor(g_rows[g_hoverRow].rowGO,kPanelColor);
        if (hit>=0&&hit<kRowCount)
            SetPanelColor(g_rows[hit].rowGO,kBtnHover);
        g_hoverRow=hit;
    }
    if (!trigDown) return;
    if (hit==-1){ g_docked=!g_docked; return; }
    if (hit<-1){
        int idx=-(hit+2);
        if (idx>=0&&idx<TAB_COUNT&&idx!=(int)g_activeTab){
            g_activeTab=(Tab)idx;
            BuildContentRows();
            for (int i=0;i<TAB_COUNT;i++)
                SetPanelColor(g_tabGOs[i],i==g_activeTab?kTabActive:kTabInactive);
        }
        return;
    }
    if (hit>=0&&hit<kRowCount){
        MenuRow &row=g_rows[hit];
        if (row.flag){
            *row.flag=!*row.flag;
            SetPanelColor(row.checkGO,*row.flag?kCheckOn:kCheckOff);
            SetText(row.checkGO,*row.flag?"\xe2\x9c\x93":"");
        }
        if (row.action) row.action();
        if (row.sliderVal){
            float delta=Feature_RAxisY()*0.5f*g_dt;
            *row.sliderVal=fmaxf(row.sliderMin,fminf(row.sliderMax,*row.sliderVal+delta));
            char buf[32]; snprintf(buf,sizeof buf,"%.3f",*row.sliderVal);
            SetText(row.labelGO,buf);
            float frac=(*row.sliderVal-row.sliderMin)/(row.sliderMax-row.sliderMin);
            if (Alive(row.sliderFillGO)){
                SetSD(row.sliderFillGO,{kSliderW*frac+2,10});
                SetLP(row.sliderFillGO,{-50-kSliderW/2+kSliderW*frac/2,0,-1});
            }
        }
        if (g_activeTab==TAB_SOUNDBOARD) Soundboard_Play(hit);
    }
}
