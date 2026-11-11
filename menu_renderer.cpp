// menu_renderer.cpp — GLES2 immediate-mode overlay drawn into a SurfaceView
// Java side creates a SurfaceView and passes it here; we render via EGL/GLES2.
// Input: Quest controller thumbstick click (intercepted via VRCInput hook) → toggle.
// All tabs drawn here: Soundboard, Movement, Player, GunLib.

#include "menu_renderer.h"
#include "soundboard.h"
#include "movement.h"
#include "player_module.h"
#include "gunlib.h"
#include "il2cpp_init.h"
#include "offsets.h"
#include "shadowhook.h"

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

#define TAG "VRCMenu/Render"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ── Extern: NativeToggleMenu declared in main.cpp ─────────────────────────
extern void NativeToggleMenu();

namespace MenuRenderer
{
    // ── EGL/GLES state ────────────────────────────────────────────────────
    static EGLDisplay g_display = EGL_NO_DISPLAY;
    static EGLSurface g_surface = EGL_NO_SURFACE;
    static EGLContext g_context = EGL_NO_CONTEXT;
    static ANativeWindow* g_window = nullptr;
    static int g_width = 680, g_height = 820;

    // ── Menu state ────────────────────────────────────────────────────────
    static std::atomic<bool>  g_visible{false};
    static std::atomic<int>   g_activeTab{0};
    static std::mutex         g_mu;

    // Soundboard UI state
    static char  g_pathBuf[512]  = "/sdcard/Music/";
    static bool  g_loop          = false;
    static float g_volume        = 1.0f;
    static int   g_selectedClip  = -1;

    // Movement UI state
    static bool  g_speedPatched  = false;

    // Player UI state
    static bool  g_npHidden      = false;
    static bool  g_invisible     = false;

    // GunLib UI state
    static char  g_targetBuf[64] = "";
    static float g_lagRate       = 60.0f;
    static std::vector<Gunlib::PlayerEntry> g_players;

    // ── GLES2 simple renderer ─────────────────────────────────────────────
    // Minimal immediate-mode quad/text renderer — no deps, pure GLES2.

    static GLuint g_prog = 0;

    static const char* kVertSrc = R"(
        attribute vec2 aPos;
        attribute vec4 aColor;
        uniform vec2 uRes;
        varying vec4 vColor;
        void main() {
            vec2 clip = (aPos / uRes) * 2.0 - 1.0;
            gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
            vColor = aColor;
        }
    )";

    static const char* kFragSrc = R"(
        precision mediump float;
        varying vec4 vColor;
        void main() { gl_FragColor = vColor; }
    )";

    static GLuint CompileShader(GLenum type, const char* src)
    {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char buf[256]; glGetShaderInfoLog(s, 256, nullptr, buf); LOGE("Shader: %s", buf); }
        return s;
    }

    static void InitGL()
    {
        GLuint vs = CompileShader(GL_VERTEX_SHADER,   kVertSrc);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
        g_prog = glCreateProgram();
        glAttachShader(g_prog, vs); glAttachShader(g_prog, fs);
        glBindAttribLocation(g_prog, 0, "aPos");
        glBindAttribLocation(g_prog, 1, "aColor");
        glLinkProgram(g_prog);
        glDeleteShader(vs); glDeleteShader(fs);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    struct Vtx { float x, y, r, g, b, a; };
    static std::vector<Vtx> g_verts;

    static void PushQuad(float x, float y, float w, float h,
                         float r, float g, float b, float a)
    {
        g_verts.push_back({x,   y,   r,g,b,a});
        g_verts.push_back({x+w, y,   r,g,b,a});
        g_verts.push_back({x,   y+h, r,g,b,a});
        g_verts.push_back({x+w, y,   r,g,b,a});
        g_verts.push_back({x+w, y+h, r,g,b,a});
        g_verts.push_back({x,   y+h, r,g,b,a});
    }

    // 5×7 bitmap font — 95 printable ASCII chars starting at 0x20
    // Each char is 5 bytes, each byte = 1 column of 7 pixels (bit0=top)
    static const uint8_t kFont[95][5] = {
        {0x00,0x00,0x00,0x00,0x00}, // ' '
        {0x00,0x00,0x5F,0x00,0x00}, // '!'
        {0x00,0x07,0x00,0x07,0x00}, // '"'
        {0x14,0x7F,0x14,0x7F,0x14}, // '#'
        {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
        {0x23,0x13,0x08,0x64,0x62}, // '%'
        {0x36,0x49,0x55,0x22,0x50}, // '&'
        {0x00,0x05,0x03,0x00,0x00}, // '\''
        {0x00,0x1C,0x22,0x41,0x00}, // '('
        {0x00,0x41,0x22,0x1C,0x00}, // ')'
        {0x14,0x08,0x3E,0x08,0x14}, // '*'
        {0x08,0x08,0x3E,0x08,0x08}, // '+'
        {0x00,0x50,0x30,0x00,0x00}, // ','
        {0x08,0x08,0x08,0x08,0x08}, // '-'
        {0x00,0x60,0x60,0x00,0x00}, // '.'
        {0x20,0x10,0x08,0x04,0x02}, // '/'
        {0x3E,0x51,0x49,0x45,0x3E}, // '0'
        {0x00,0x42,0x7F,0x40,0x00}, // '1'
        {0x42,0x61,0x51,0x49,0x46}, // '2'
        {0x21,0x41,0x45,0x4B,0x31}, // '3'
        {0x18,0x14,0x12,0x7F,0x10}, // '4'
        {0x27,0x45,0x45,0x45,0x39}, // '5'
        {0x3C,0x4A,0x49,0x49,0x30}, // '6'
        {0x01,0x71,0x09,0x05,0x03}, // '7'
        {0x36,0x49,0x49,0x49,0x36}, // '8'
        {0x06,0x49,0x49,0x29,0x1E}, // '9'
        {0x00,0x36,0x36,0x00,0x00}, // ':'
        {0x00,0x56,0x36,0x00,0x00}, // ';'
        {0x08,0x14,0x22,0x41,0x00}, // '<'
        {0x14,0x14,0x14,0x14,0x14}, // '='
        {0x00,0x41,0x22,0x14,0x08}, // '>'
        {0x02,0x01,0x51,0x09,0x06}, // '?'
        {0x32,0x49,0x79,0x41,0x3E}, // '@'
        {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
        {0x7F,0x49,0x49,0x49,0x36}, // 'B'
        {0x3E,0x41,0x41,0x41,0x22}, // 'C'
        {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
        {0x7F,0x49,0x49,0x49,0x41}, // 'E'
        {0x7F,0x09,0x09,0x09,0x01}, // 'F'
        {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
        {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
        {0x00,0x41,0x7F,0x41,0x00}, // 'I'
        {0x20,0x40,0x41,0x3F,0x01}, // 'J'
        {0x7F,0x08,0x14,0x22,0x41}, // 'K'
        {0x7F,0x40,0x40,0x40,0x40}, // 'L'
        {0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
        {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
        {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
        {0x7F,0x09,0x09,0x09,0x06}, // 'P'
        {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
        {0x7F,0x09,0x19,0x29,0x46}, // 'R'
        {0x46,0x49,0x49,0x49,0x31}, // 'S'
        {0x01,0x01,0x7F,0x01,0x01}, // 'T'
        {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
        {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
        {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
        {0x63,0x14,0x08,0x14,0x63}, // 'X'
        {0x07,0x08,0x70,0x08,0x07}, // 'Y'
        {0x61,0x51,0x49,0x45,0x43}, // 'Z'
        {0x00,0x7F,0x41,0x41,0x00}, // '['
        {0x02,0x04,0x08,0x10,0x20}, // '\\'
        {0x00,0x41,0x41,0x7F,0x00}, // ']'
        {0x04,0x02,0x01,0x02,0x04}, // '^'
        {0x40,0x40,0x40,0x40,0x40}, // '_'
        {0x00,0x01,0x02,0x04,0x00}, // '`'
        {0x20,0x54,0x54,0x54,0x78}, // 'a'
        {0x7F,0x48,0x44,0x44,0x38}, // 'b'
        {0x38,0x44,0x44,0x44,0x20}, // 'c'
        {0x38,0x44,0x44,0x48,0x7F}, // 'd'
        {0x38,0x54,0x54,0x54,0x18}, // 'e'
        {0x08,0x7E,0x09,0x01,0x02}, // 'f'
        {0x0C,0x52,0x52,0x52,0x3E}, // 'g'
        {0x7F,0x08,0x04,0x04,0x78}, // 'h'
        {0x00,0x44,0x7D,0x40,0x00}, // 'i'
        {0x20,0x40,0x44,0x3D,0x00}, // 'j'
        {0x7F,0x10,0x28,0x44,0x00}, // 'k'
        {0x00,0x41,0x7F,0x40,0x00}, // 'l'
        {0x7C,0x04,0x18,0x04,0x78}, // 'm'
        {0x7C,0x08,0x04,0x04,0x78}, // 'n'
        {0x38,0x44,0x44,0x44,0x38}, // 'o'
        {0x7C,0x14,0x14,0x14,0x08}, // 'p'
        {0x08,0x14,0x14,0x18,0x7C}, // 'q'
        {0x7C,0x08,0x04,0x04,0x08}, // 'r'
        {0x48,0x54,0x54,0x54,0x20}, // 's'
        {0x04,0x3F,0x44,0x40,0x20}, // 't'
        {0x3C,0x40,0x40,0x40,0x7C}, // 'u'
        {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
        {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
        {0x44,0x28,0x10,0x28,0x44}, // 'x'
        {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
        {0x44,0x64,0x54,0x4C,0x44}, // 'z'
        {0x00,0x08,0x36,0x41,0x00}, // '{'
        {0x00,0x00,0x7F,0x00,0x00}, // '|'
        {0x00,0x41,0x36,0x08,0x00}, // '}'
        {0x10,0x08,0x08,0x10,0x08}, // '~'
    };

    static void DrawText(const char* str, float x, float y, float scale,
                         float r, float g, float b, float a = 1.0f)
    {
        float cx = x;
        for (const char* p = str; *p; p++)
        {
            uint8_t c = (uint8_t)*p;
            if (c < 0x20 || c > 0x7E) { cx += (5+1)*scale; continue; }
            const uint8_t* glyph = kFont[c - 0x20];
            for (int col = 0; col < 5; col++)
            {
                uint8_t bits = glyph[col];
                for (int row = 0; row < 7; row++)
                {
                    if (bits & (1 << row))
                        PushQuad(cx + col*scale, y + row*scale,
                                 scale, scale, r, g, b, a);
                }
            }
            cx += (5+1)*scale;
        }
    }

    static float TextWidth(const char* str, float scale)
    { return strlen(str) * 6.0f * scale; }

    // Returns true if point (tx,ty) is inside rect
    static bool HitTest(float rx, float ry, float rw, float rh,
                        float tx, float ty)
    { return tx >= rx && tx <= rx+rw && ty >= ry && ty <= ry+rh; }

    // ── Touch / click state ───────────────────────────────────────────────
    struct TouchEvt { float x, y; bool down; };
    static std::vector<TouchEvt> g_touches;
    static std::mutex            g_touchMu;

    void OnTouch(float x, float y, bool down)
    {
        std::lock_guard<std::mutex> lk(g_touchMu);
        g_touches.push_back({x, y, down});
    }

    static bool ConsumeClick(float rx, float ry, float rw, float rh)
    {
        std::lock_guard<std::mutex> lk(g_touchMu);
        for (auto it = g_touches.begin(); it != g_touches.end(); ++it)
        {
            if (!it->down && HitTest(rx, ry, rw, rh, it->x, it->y))
            { g_touches.erase(it); return true; }
        }
        return false;
    }

    static void ClearTouches()
    {
        std::lock_guard<std::mutex> lk(g_touchMu);
        g_touches.clear();
    }

    // ── Draw helpers ──────────────────────────────────────────────────────
    static void DrawPanel(float x, float y, float w, float h)
    { PushQuad(x, y, w, h, 0.07f, 0.07f, 0.13f, 0.95f); }

    static void DrawBorder(float x, float y, float w, float h,
                           float r=0.4f, float g=0.3f, float b=0.7f)
    {
        float t = 2;
        PushQuad(x,     y,     w, t,  r,g,b,1); // top
        PushQuad(x,     y+h-t, w, t,  r,g,b,1); // bottom
        PushQuad(x,     y,     t, h,  r,g,b,1); // left
        PushQuad(x+w-t, y,     t, h,  r,g,b,1); // right
    }

    // Button — returns true if clicked this frame
    static bool Button(const char* label, float x, float y, float w, float h,
                       bool active=false)
    {
        float br = active ? 0.5f : 0.18f;
        float bg = active ? 0.2f : 0.12f;
        float bb = active ? 0.9f : 0.25f;
        PushQuad(x, y, w, h, br, bg, bb, 0.92f);
        DrawBorder(x, y, w, h, 0.6f, 0.4f, 1.0f);
        float tw = TextWidth(label, 2.0f);
        DrawText(label, x + (w-tw)*0.5f, y + (h-14)*0.5f, 2.0f,
                 1.0f, 1.0f, 1.0f);
        return ConsumeClick(x, y, w, h);
    }

    // Slider — returns new value
    static float Slider(float x, float y, float w, float val,
                        float mn, float mx)
    {
        float h = 16.0f;
        PushQuad(x, y+5, w, 6, 0.3f, 0.3f, 0.5f, 1.0f);
        float t = (val - mn) / (mx - mn);
        float kx = x + t*(w-12);
        PushQuad(kx, y, 12, h, 0.6f, 0.4f, 1.0f, 1.0f);
        // drag — check if any touch is in the slider track and sliding
        {
            std::lock_guard<std::mutex> lk(g_touchMu);
            for (auto& e : g_touches)
            {
                if (e.down && HitTest(x, y, w, h+4, e.x, e.y))
                {
                    float nt = (e.x - x) / w;
                    nt = nt < 0 ? 0 : (nt > 1 ? 1 : nt);
                    return mn + nt*(mx-mn);
                }
            }
        }
        return val;
    }

    // ── Tab renderers ──────────────────────────────────────────────────────

    static void DrawSoundboard(float ox, float oy)
    {
        float y = oy + 10;
        DrawText("SOUNDBOARD", ox+10, y, 2.5f, 0.8f, 0.5f, 1.0f); y += 30;

        DrawText("Path:", ox+10, y, 1.8f, 0.8f, 0.8f, 0.8f);
        PushQuad(ox+55, y-2, 220, 18, 0.1f, 0.1f, 0.18f, 1.0f);
        DrawBorder(ox+55, y-2, 220, 18);
        DrawText(g_pathBuf, ox+58, y, 1.8f, 1.0f, 1.0f, 0.7f);
        if (Button("Load", ox+282, y-2, 50, 18))
            Soundboard::LoadClip(g_pathBuf);
        y += 24;

        // Loop + volume row
        if (Button(g_loop ? "[x] Loop" : "[ ] Loop", ox+10, y, 90, 20, g_loop))
            g_loop = !g_loop;
        DrawText("Vol", ox+114, y+4, 1.8f, 0.8f, 0.8f, 0.8f);
        g_volume = Slider(ox+140, y+2, 120, g_volume, 0.0f, 1.0f);
        char vol_buf[8]; snprintf(vol_buf, 8, "%.0f%%", g_volume*100);
        DrawText(vol_buf, ox+268, y+4, 1.8f, 1.0f, 1.0f, 1.0f);
        y += 30;

        DrawText("Clips:", ox+10, y, 1.8f, 0.6f, 0.9f, 1.0f); y += 18;
        int n = Soundboard::ClipCount();
        for (int i = 0; i < n && i < 8; i++)
        {
            bool sel = (g_selectedClip == i);
            PushQuad(ox+10, y, 230, 20, sel?0.2f:0.1f, sel?0.1f:0.1f, sel?0.4f:0.2f, 1.0f);
            DrawBorder(ox+10, y, 230, 20, 0.4f, 0.3f, 0.7f);
            DrawText(Soundboard::ClipName(i), ox+14, y+4, 1.8f, 1.0f, 1.0f, 1.0f);
            if (ConsumeClick(ox+10, y, 230, 20)) g_selectedClip = i;

            if (Button("Srv", ox+248, y, 36, 20))
                Soundboard::PlayServer(i, g_loop, g_volume);
            y += 24;
        }

        y += 4;
        if (Button("STOP ALL", ox+10, y, 100, 24))
            Soundboard::StopAll();
    }

    static void DrawMovement(float ox, float oy)
    {
        float y = oy + 10;
        DrawText("MOVEMENT", ox+10, y, 2.5f, 0.5f, 1.0f, 0.7f); y += 30;

        char spd[32]; snprintf(spd, 32, "Speed: %.2fx", Movement::GetSpeed());
        DrawText(spd, ox+10, y, 2.0f, 1.0f, 1.0f, 1.0f); y += 24;
        Movement::SetSpeed(Slider(ox+10, y, 310, Movement::GetSpeed(), 1.0f, 5.0f));
        y += 26;

        // Preset buttons
        float bx = ox+10;
        const char* labels[] = {"1x","2x","3x","4x","5x"};
        float vals[]         = {1.f, 2.f, 3.f, 4.f, 5.f};
        for (int i = 0; i < 5; i++)
        {
            bool a = fabsf(Movement::GetSpeed() - vals[i]) < 0.05f;
            if (Button(labels[i], bx, y, 52, 24, a))
                Movement::SetSpeed(vals[i]);
            bx += 58;
        }
        y += 32;

        bool patched = Movement::IsActive();
        if (!patched)
        {
            if (Button("Apply Speed Patch", ox+10, y, 180, 28))
                Movement::ApplyHook();
        }
        else
        {
            DrawText("Speed patch ACTIVE", ox+10, y+6, 1.8f, 0.4f, 1.0f, 0.4f);
            if (Button("Remove", ox+200, y, 90, 28))
                Movement::Unhook();
        }
    }

    static void DrawPlayer(float ox, float oy)
    {
        float y = oy + 10;
        DrawText("PLAYER", ox+10, y, 2.5f, 1.0f, 0.6f, 0.9f); y += 30;

        bool np = Player::NameplateHidden();
        if (Button(np ? "[x] Hide Nameplate" : "[ ] Hide Nameplate",
                   ox+10, y, 230, 28, np))
        {
            Player::SetNameplate(np);  // toggle: pass current state = set to opposite
        }
        y += 36;

        bool inv = Player::IsInvisible();
        if (Button(inv ? "[x] Invisible" : "[ ] Invisible",
                   ox+10, y, 230, 28, inv))
        {
            Player::SetInvisible(!inv);
        }
        y += 36;

        DrawText("(Invisibility hides all avatar renderers)", ox+10, y, 1.5f,
                 0.5f, 0.5f, 0.6f);
    }

    static void DrawGunLib(float ox, float oy)
    {
        float y = oy + 10;
        DrawText("GUNLIB", ox+10, y, 2.5f, 1.0f, 0.3f, 0.3f); y += 30;

        DrawText("Target Actor# or Name:", ox+10, y, 1.8f, 0.8f, 0.8f, 0.8f); y += 18;
        PushQuad(ox+10, y-2, 300, 20, 0.1f, 0.1f, 0.18f, 1.0f);
        DrawBorder(ox+10, y-2, 300, 20);
        DrawText(g_targetBuf, ox+14, y+1, 1.8f, 1.0f, 1.0f, 0.7f);
        y += 26;

        // Parse target actor from buf
        int targetActor = -1;
        if (g_targetBuf[0]) {
            char* end; long v = strtol(g_targetBuf, &end, 10);
            if (end != g_targetBuf) targetActor = (int)v;
            else {
                // name match
                auto players = Gunlib::GetPlayers();
                for (auto& p : players)
                    if (strstr(p.name, g_targetBuf)) { targetActor = p.actorNr; break; }
            }
        }

        if (Button("CRASH", ox+10, y, 90, 28))
            if (targetActor >= 0) Gunlib::CrashPlayer(targetActor);

        bool lagging = Gunlib::IsLagging();
        if (Button(lagging ? "STOP LAG" : "START LAG", ox+108, y, 100, 28, lagging))
        {
            if (lagging) Gunlib::StopLag();
            else         Gunlib::StartLag(g_lagRate);
        }

        if (Button("KICK", ox+216, y, 70, 28))
            if (targetActor >= 0) Gunlib::KickPlayer(targetActor);
        y += 36;

        char lr[32]; snprintf(lr, 32, "Lag rate: %.0f/s", g_lagRate);
        DrawText(lr, ox+10, y, 1.8f, 0.8f, 0.8f, 0.8f); y += 18;
        g_lagRate = Slider(ox+10, y, 310, g_lagRate, 10.0f, 300.0f);
        Gunlib::SetLagRate(g_lagRate);
        y += 26;

        // Player list
        DrawText("Players (tap to target):", ox+10, y, 1.8f, 0.6f, 0.9f, 1.0f); y += 18;
        // Refresh every 60 frames-ish
        static int s_refresh = 0;
        if (++s_refresh > 60) { g_players = Gunlib::GetPlayers(); s_refresh = 0; }

        for (auto& p : g_players)
        {
            char row[80];
            snprintf(row, 80, "[%d] %s", p.actorNr, p.name);
            bool sel = (targetActor == p.actorNr);
            PushQuad(ox+10, y, 310, 18, sel?0.25f:0.1f, 0.1f, sel?0.4f:0.15f, 1.0f);
            DrawBorder(ox+10, y, 310, 18, 0.5f, 0.3f, 0.8f);
            DrawText(row, ox+14, y+3, 1.8f, 1.0f, 0.9f, 0.7f);
            if (ConsumeClick(ox+10, y, 310, 18))
                snprintf(g_targetBuf, sizeof(g_targetBuf), "%d", p.actorNr);
            y += 22;
        }
    }

    // ── Main frame ────────────────────────────────────────────────────────
    static void Frame()
    {
        if (!g_visible.load()) return;

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        g_verts.clear();

        float W = (float)g_width, H = (float)g_height;
        float px = 0, py = 0, pw = W, ph = H;

        // Background panel
        DrawPanel(px, py, pw, ph);
        DrawBorder(px, py, pw, ph, 0.5f, 0.3f, 1.0f);

        // Title bar
        PushQuad(px, py, pw, 28, 0.15f, 0.07f, 0.3f, 1.0f);
        DrawText("VRCQuestMenu v1.0  |  absolute ollie, built by olivia",
                 px+8, py+8, 1.8f, 0.9f, 0.5f, 1.0f);

        // Tab bar
        float tx = px + 4;
        const char* tabs[] = {"Soundboard","Movement","Player","GunLib"};
        for (int i = 0; i < 4; i++)
        {
            bool a = (g_activeTab.load() == i);
            if (Button(tabs[i], tx, py+30, 80, 22, a))
                g_activeTab.store(i);
            tx += 84;
        }

        // Tab content
        float cy = py + 56;
        switch (g_activeTab.load())
        {
            case 0: DrawSoundboard(px+4, cy); break;
            case 1: DrawMovement(px+4, cy);   break;
            case 2: DrawPlayer(px+4, cy);     break;
            case 3: DrawGunLib(px+4, cy);     break;
        }

        // Flush geometry
        glUseProgram(g_prog);
        GLint uRes = glGetUniformLocation(g_prog, "uRes");
        glUniform2f(uRes, W, H);

        GLuint vbo;
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     g_verts.size() * sizeof(Vtx), g_verts.data(), GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)(2*sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g_verts.size());
        glDeleteBuffers(1, &vbo);

        eglSwapBuffers(g_display, g_surface);
    }

    // ── EGL setup ─────────────────────────────────────────────────────────
    static bool InitEGL(ANativeWindow* win)
    {
        g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(g_display, nullptr, nullptr);

        EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8, EGL_NONE
        };
        EGLConfig cfg; EGLint n;
        eglChooseConfig(g_display, attribs, &cfg, 1, &n);

        EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        g_context = eglCreateContext(g_display, cfg, EGL_NO_CONTEXT, ctx_attribs);
        g_surface = eglCreateWindowSurface(g_display, cfg, win, nullptr);
        eglMakeCurrent(g_display, g_surface, g_surface, g_context);

        InitGL();
        LOGI("EGL init complete %dx%d", g_width, g_height);
        return true;
    }

    // ── Render thread ─────────────────────────────────────────────────────
    static std::thread g_renderThread;
    static std::atomic<bool> g_running{false};

    static void RenderLoop()
    {
        while (g_running.load())
        {
            Frame();
            Soundboard::Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60fps
        }
    }

    // ── VRCInput thumbstick hook ──────────────────────────────────────────
    // Hooks VRC.UserCamera.UserCameraController or VRCInputManager update
    // to detect right thumbstick click (Quest controller Axis5 = RS click)
    // We use a simpler path: hook Update on a known singleton and check
    // Input.GetButtonDown("RightThumbstick") via IL2CPP invoke.
    static void* g_inputHook = nullptr;
    using UpdateFn = void(*)(void* self);
    static UpdateFn g_origUpdate = nullptr;
    static void* g_GetButtonDown = nullptr;

    static void UpdateHook(void* self)
    {
        if (g_origUpdate) g_origUpdate(self);

        if (!g_GetButtonDown) return;
        static void* s_rsStr = nullptr;
        if (!s_rsStr && il2cpp::string_new)
            s_rsStr = il2cpp::string_new("RightThumbstick");
        if (!s_rsStr) return;

        void* exc = nullptr;
        void* args[1] = { s_rsStr };
        void* r = il2cpp::runtime_invoke(g_GetButtonDown, nullptr, args, &exc);
        if (r && !exc)
        {
            bool pressed = *reinterpret_cast<bool*>((uint8_t*)r + 0x10);
            if (pressed) NativeToggleMenu();
        }
    }

    void ApplyInputHook()
    {
        if (!il2cpp::Ready()) return;
        g_GetButtonDown = il2cpp::ResolveMethod(
            UNITY_ASSEMBLY, UNITY_NS, "Input", "GetButtonDown", 1);

        // Hook VRCInputManager.Update or any Update that runs every frame
        void* updatePtr = il2cpp::ResolveMethod(
            VRC_ASSEMBLY, "VRC", "VRCInputManager", "Update", 0);
        if (!updatePtr)
            updatePtr = il2cpp::ResolveMethod(
                VRC_ASSEMBLY, "VRC", "VRCInputManager", "Method_Private_Void_0", 0);

        if (updatePtr)
        {
            g_inputHook = shadowhook_hook_func_addr(
                updatePtr, (void*)UpdateHook, (void**)&g_origUpdate);
            LOGI("Input hook: %s", g_inputHook ? "ok" : "failed");
        }
    }

    // ── Public ────────────────────────────────────────────────────────────
    jobject BuildSurfaceView(JNIEnv* env, jobject ctx)
    {
        // Create a SurfaceView via JNI
        jclass svClass  = env->FindClass("android/view/SurfaceView");
        jmethodID ctor  = env->GetMethodID(svClass, "<init>", "(Landroid/content/Context;)V");
        jobject sv      = env->NewObject(svClass, ctor, ctx);

        // Get SurfaceHolder and set fixed size
        jmethodID getHolder = env->GetMethodID(svClass, "getHolder",
                                               "()Landroid/view/SurfaceHolder;");
        jobject holder = env->CallObjectMethod(sv, getHolder);
        jclass holderClass = env->GetObjectClass(holder);

        jmethodID setFixed = env->GetMethodID(holderClass, "setFixedSize", "(II)V");
        env->CallVoidMethod(holder, setFixed, g_width, g_height);

        // Set callback to know when surface is created
        jclass cbIface = env->FindClass("android/view/SurfaceHolder$Callback");
        // We'll register a simple callback from Java side (OverlayService handles this)

        env->DeleteLocalRef(holderClass);
        env->DeleteLocalRef(holder);
        env->DeleteLocalRef(svClass);

        LOGI("SurfaceView created");
        return sv;
    }

    void OnSurfaceCreated(JNIEnv* env, jobject surface)
    {
        g_window = ANativeWindow_fromSurface(env, surface);
        if (!g_window) { LOGE("ANativeWindow_fromSurface failed"); return; }
        ANativeWindow_setBuffersGeometry(g_window, g_width, g_height,
                                         WINDOW_FORMAT_RGBA_8888);
        InitEGL(g_window);
        ApplyInputHook();

        g_running.store(true);
        g_renderThread = std::thread(RenderLoop);
        LOGI("Render thread started.");
    }

    void OnSurfaceDestroyed()
    {
        g_running.store(false);
        if (g_renderThread.joinable()) g_renderThread.join();
        if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_display, g_surface);
        if (g_context != EGL_NO_CONTEXT) eglDestroyContext(g_display, g_context);
        eglTerminate(g_display);
        if (g_window) ANativeWindow_release(g_window);
        g_window = nullptr;
    }

    void SetVisible(bool v) { g_visible.store(v); }
    bool IsVisible()        { return g_visible.load(); }
}
