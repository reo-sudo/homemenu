// homemenu: a modern HOME-menu style launcher for the Nintendo 3DS.
// Glass UI, tabs, icon grid, themes, favorites and stereoscopic parallax.
// Built with devkitARM, libctru and citro2d.

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240
#define MAX_APPS 512
#define MAX_FAVS 256
#define PAGE_SIZE 10
#define COLS 5

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 255)

// ---------- themes ----------
typedef struct {
    const char *name;
    u32 sky1, sky2, sky3; // top, middle, horizon
    u32 mountBack, mountFront;
    u32 accent;
} Theme;

#define THEME_COUNT 4
static const Theme themes[THEME_COUNT] = {
    {"Dusk", RGB(14, 18, 58), RGB(86, 56, 140), RGB(236, 128, 120), RGB(52, 42, 104), RGB(24, 22, 56), RGB(70, 160, 255)},
    {"Ember", RGB(30, 10, 20), RGB(120, 36, 48), RGB(255, 150, 60), RGB(78, 26, 38), RGB(36, 12, 22), RGB(255, 140, 60)},
    {"Aurora", RGB(6, 22, 34), RGB(16, 90, 110), RGB(90, 230, 170), RGB(14, 66, 80), RGB(8, 30, 40), RGB(60, 230, 180)},
    {"Graphite", RGB(14, 16, 22), RGB(34, 38, 52), RGB(96, 104, 128), RGB(30, 34, 46), RGB(14, 16, 22), RGB(130, 170, 255)},
};

static const float depthMul[3] = {0.0f, 0.55f, 1.0f};
static const char *depthName[3] = {"Off", "Low", "High"};

// ---------- SMDH layout (icon + titles stored in every title's ExeFS) ----------
typedef struct {
    u16 shortDesc[0x40];
    u16 longDesc[0x80];
    u16 publisher[0x40];
} SmdhTitle;

typedef struct {
    u32 magic; // 'SMDH'
    u16 version;
    u16 reserved;
    SmdhTitle titles[16];
    u8 settings[0x30];
    u8 reserved2[8];
    u16 smallIcon[0x240]; // 24x24 RGB565, tiled
    u16 largeIcon[0x900]; // 48x48 RGB565, tiled
} Smdh;

typedef struct {
    u64 id;
    FS_MediaType media;
    char name[0x101];
    char publisher[0x101];
    bool hasIcon;
    bool fav;
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
} App;

// ---------- saved settings ----------
typedef struct {
    u32 magic;
    u8 theme, depth, sortDesc, star;
    u32 favCount;
    u64 favs[MAX_FAVS];
} Config;

#define CONFIG_MAGIC 0x484D4E31
#define CONFIG_DIR "sdmc:/3ds/homemenu"
#define CONFIG_PATH "sdmc:/3ds/homemenu/config.bin"

// ---------- state ----------
static App apps[MAX_APPS];
static int appCount = 0;
static int view[MAX_APPS];
static int viewCount = 0;

static int tab = 0; // 0 All, 1 Favorites, 2 Themes, 3 Settings
static int sel = 0;
static int themeCur = 0;
static int depthSet = 2;
static int setCur = 0;
static bool starOn = true;
static bool sortDesc = false;
static float pagePos = 0;
static u32 frameNo = 0;
static float g3d = 0; // 3D slider value
static float gx = 0;  // horizontal offset applied by every helper (parallax)

static C2D_TextBuf textBuf;
static C3D_RenderTarget *topL, *topR, *botT;

static const char *tabNames[4] = {"All", "Favorites", "Themes", "Settings"};

// ---------- small helpers ----------
static u32 shade(u32 c, float f) {
    float r = (c & 255) * f, g = ((c >> 8) & 255) * f, b = ((c >> 16) & 255) * f;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB((u32)r, (u32)g, (u32)b);
}

static u32 withAlpha(u32 c, u32 a) { return (c & 0x00FFFFFF) | (a << 24); }

static float rnd(int i, int k) {
    u32 h = (u32)i * 73856093u ^ (u32)k * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

static void rect(float x, float y, float w, float h, float z, u32 c) { C2D_DrawRectSolid(x + gx, y, z, w, h, c); }

// opaque rounded rectangle (circles overlap the rects, so only use with alpha = 255)
static void roundRect(float x, float y, float w, float h, float r, float z, u32 c) {
    C2D_DrawRectSolid(x + r + gx, y, z, w - 2 * r, h, c);
    C2D_DrawRectSolid(x + gx, y + r, z, w, h - 2 * r, c);
    C2D_DrawCircleSolid(x + r + gx, y + r, z, r, c);
    C2D_DrawCircleSolid(x + w - r + gx, y + r, z, r, c);
    C2D_DrawCircleSolid(x + r + gx, y + h - r, z, r, c);
    C2D_DrawCircleSolid(x + w - r + gx, y + h - r, z, r, c);
}

// translucent rounded rectangle built from non-overlapping strips, so alpha blends evenly
static void glass(float x, float y, float w, float h, float r, float z, u32 c) {
    for (int i = 0; i < (int)r; i++) {
        float dy = r - i - 0.5f;
        float inset = r - sqrtf(r * r - dy * dy);
        rect(x + inset, y + i, w - 2 * inset, 1, z, c);
        rect(x + inset, y + h - 1 - i, w - 2 * inset, 1, z, c);
    }
    rect(x, y + r, w, h - 2 * r, z, c);
    rect(x + r, y, w - 2 * r, 1, z + 0.001f, RGBA(255, 255, 255, 70)); // top sheen
}

// align: 0 left, 1 center, 2 right. maxW > 0 shrinks the text to fit.
static void drawText(const char *s, float x, float y, float scale, float maxW, u32 clr, int align) {
    C2D_Text t;
    C2D_TextParse(&t, textBuf, s);
    C2D_TextOptimize(&t);
    float w, h;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    if (maxW > 0 && w > maxW) {
        scale *= maxW / w;
        w = maxW;
    }
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    C2D_DrawText(&t, C2D_WithColor, x + gx, y, 0.9f, scale, scale, clr);
}

static void brackets(float x, float y, float s, float z, u32 clr) {
    float t = 3, l = s * 0.28f;
    rect(x, y, l, t, z, clr);
    rect(x, y, t, l, z, clr);
    rect(x + s - l, y, l, t, z, clr);
    rect(x + s - t, y, t, l, z, clr);
    rect(x, y + s - t, l, t, z, clr);
    rect(x, y + s - l, t, l, z, clr);
    rect(x + s - l, y + s - t, l, t, z, clr);
    rect(x + s - t, y + s - l, t, l, z, clr);
}

static u32 appColor(u64 id) {
    u32 h = (u32)(id ^ (id >> 32)) * 2654435761u;
    return RGB(90 + ((h >> 0) & 0x7F), 90 + ((h >> 8) & 0x7F), 90 + ((h >> 16) & 0x7F));
}

static void drawIcon(const App *a, float cx, float cy, float size, float z) {
    if (a->hasIcon) {
        C2D_DrawImageAt(a->img, cx - size / 2 + gx, cy - size / 2, z, NULL, size / 48.0f, size / 48.0f);
    } else {
        roundRect(cx - size / 2, cy - size / 2, size, size, 7, z, appColor(a->id));
        char ch[2] = {a->name[0] ? a->name[0] : '?', 0};
        drawText(ch, cx, cy - size * 0.34f, size / 48.0f, 0, RGB(255, 255, 255), 1);
    }
}

static void keyChip(const char *label, float cx, float cy) {
    C2D_DrawCircleSolid(cx + gx, cy, 0.6f, 8, RGBA(255, 255, 255, 60));
    drawText(label, cx, cy - 7, 0.45f, 0, RGB(255, 255, 255), 1);
}

// ---------- loading ----------
static void utf16(char *out, size_t outSize, const u16 *in) {
    memset(out, 0, outSize);
    utf16_to_utf8((uint8_t *)out, in, outSize - 1);
}

static bool loadSmdh(App *a) {
    u32 archivePath[] = {(u32)(a->id & 0xFFFFFFFF), (u32)(a->id >> 32), (u32)a->media, 0};
    static const u32 filePath[] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000}; // ExeFS "icon"
    FS_Path ap = {PATH_BINARY, 0x10, archivePath};
    FS_Path fp = {PATH_BINARY, 0x14, filePath};

    Handle file;
    if (R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, ap, fp, FS_OPEN_READ, 0)))
        return false;

    Smdh *s = malloc(sizeof(Smdh));
    if (!s) { FSFILE_Close(file); return false; }

    u32 read = 0;
    Result r = FSFILE_Read(file, &read, 0, s, sizeof(Smdh));
    FSFILE_Close(file);
    if (R_FAILED(r) || read < sizeof(Smdh) || s->magic != 0x48444D53) { free(s); return false; }

    int lang = 1; // English
    if (s->titles[lang].shortDesc[0] == 0) lang = 0;
    utf16(a->name, sizeof(a->name), s->titles[lang].shortDesc);
    utf16(a->publisher, sizeof(a->publisher), s->titles[lang].publisher);

    if (C3D_TexInit(&a->tex, 64, 64, GPU_RGB565)) {
        u16 *dst = (u16 *)a->tex.data;
        memset(dst, 0, 64 * 64 * sizeof(u16));
        // The SMDH icon is already in the GPU's 8x8 tile order; 48x48 = 6x6 tiles placed into a 64x64 (8x8 tile) texture.
        for (int ty = 0; ty < 6; ty++)
            for (int tx = 0; tx < 6; tx++)
                memcpy(dst + (ty * 8 + tx) * 64, s->largeIcon + (ty * 6 + tx) * 64, 64 * sizeof(u16));
        GSPGPU_FlushDataCache(a->tex.data, 64 * 64 * sizeof(u16));
        C3D_TexSetFilter(&a->tex, GPU_LINEAR, GPU_LINEAR);

        a->sub.width = 48;
        a->sub.height = 48;
        a->sub.left = 0.0f;
        a->sub.top = 1.0f;
        a->sub.right = 48.0f / 64.0f;
        a->sub.bottom = 1.0f - 48.0f / 64.0f;
        a->img.tex = &a->tex;
        a->img.subtex = &a->sub;
        a->hasIcon = true;
    }
    free(s);
    return true;
}

static int cmpApps(const void *x, const void *y) {
    int r = strcasecmp(((const App *)x)->name, ((const App *)y)->name);
    return sortDesc ? -r : r;
}

static void fixImagePointers(void) {
    // qsort moves whole structs, so the image pointers must point at their new locations
    for (int i = 0; i < appCount; i++) {
        if (apps[i].hasIcon) {
            apps[i].img.tex = &apps[i].tex;
            apps[i].img.subtex = &apps[i].sub;
        }
    }
}

static void rebuildView(void) {
    viewCount = 0;
    for (int i = 0; i < appCount; i++) {
        if (tab == 1 && !apps[i].fav) continue;
        view[viewCount++] = i;
    }
    if (sel >= viewCount) sel = viewCount ? viewCount - 1 : 0;
}

static void sortApps(void) {
    u64 keep = (viewCount > 0 && sel < viewCount) ? apps[view[sel]].id : 0;
    qsort(apps, appCount, sizeof(App), cmpApps);
    fixImagePointers();
    rebuildView();
    if (keep)
        for (int i = 0; i < viewCount; i++)
            if (apps[view[i]].id == keep) { sel = i; break; }
}

static void scanTitles(void) {
    u32 count = 0;
    if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) || count == 0) return;

    u64 *ids = malloc(sizeof(u64) * count);
    if (!ids) return;

    u32 read = 0;
    if (R_SUCCEEDED(AM_GetTitleList(&read, MEDIATYPE_SD, count, ids))) {
        for (u32 i = 0; i < read && appCount < MAX_APPS; i++) {
            if ((ids[i] >> 32) != 0x00040000) continue; // regular applications only
            App *a = &apps[appCount];
            memset(a, 0, sizeof(*a));
            a->id = ids[i];
            a->media = MEDIATYPE_SD;
            if (!loadSmdh(a)) snprintf(a->name, sizeof(a->name), "%016llX", (unsigned long long)a->id);
            appCount++;
        }
    }
    free(ids);
}

static void loadConfig(Config *out) {
    memset(out, 0, sizeof(*out));
    out->depth = 2;
    out->star = 1;
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) return;
    Config tmp;
    if (fread(&tmp, sizeof(tmp), 1, f) == 1 && tmp.magic == CONFIG_MAGIC && tmp.theme < THEME_COUNT &&
        tmp.depth < 3 && tmp.favCount <= MAX_FAVS)
        *out = tmp;
    fclose(f);
}

static void saveConfig(void) {
    Config c;
    memset(&c, 0, sizeof(c));
    c.magic = CONFIG_MAGIC;
    c.theme = (u8)themeCur;
    c.depth = (u8)depthSet;
    c.sortDesc = sortDesc;
    c.star = starOn;
    for (int i = 0; i < appCount && c.favCount < MAX_FAVS; i++)
        if (apps[i].fav) c.favs[c.favCount++] = apps[i].id;
    mkdir("sdmc:/3ds", 0777);
    mkdir(CONFIG_DIR, 0777);
    FILE *f = fopen(CONFIG_PATH, "wb");
    if (!f) return;
    fwrite(&c, sizeof(c), 1, f);
    fclose(f);
}

// ---------- top screen ----------
#define LAYER(dz) (gx = (eye ? -1.0f : 1.0f) * parallax * (dz) * 8.0f)

static void drawTop(int eye) {
    const Theme *T = &themes[themeCur];
    float parallax = g3d * depthMul[depthSet];
    float t = frameNo / 60.0f;
    u32 white = RGB(255, 255, 255);

    // sky
    LAYER(-1.0f);
    C2D_DrawRectangle(gx - 30, 0, 0.00f, TOP_W + 60, 120, T->sky1, T->sky1, T->sky2, T->sky2);
    C2D_DrawRectangle(gx - 30, 120, 0.00f, TOP_W + 60, 120, T->sky2, T->sky2, T->sky3, T->sky3);

    // stars
    LAYER(-0.9f);
    for (int i = 0; i < 48; i++) {
        float tw = 0.5f + 0.5f * sinf(t * 2.0f + i * 1.7f);
        u32 a = (u32)(70 + tw * 150);
        float s = (i % 5 == 0) ? 2.0f : 1.0f;
        rect(rnd(i, 1) * TOP_W, rnd(i, 2) * 130, s, s, 0.05f, RGBA(255, 255, 255, a));
    }

    // one shooting star every 11 seconds
    if (starOn) {
        float ph = fmodf(t, 11.0f) / 0.9f;
        if (ph < 1.0f) {
            LAYER(-0.8f);
            float x1 = 330 - 150 * ph, y1 = 20 + 90 * ph;
            C2D_DrawLine(x1 + gx, y1, RGBA(255, 255, 255, 230), x1 + 60 + gx, y1 - 36, RGBA(255, 255, 255, 0), 2.0f,
                         0.08f);
        }
    }

    // mountains, back then front
    LAYER(-0.6f);
    u32 mb = T->mountBack;
    C2D_DrawTriangle(-40 + gx, 176, mb, 70 + gx, 92, mb, 180 + gx, 176, mb, 0.10f);
    C2D_DrawTriangle(120 + gx, 176, mb, 235 + gx, 70, mb, 350 + gx, 176, mb, 0.10f);
    C2D_DrawTriangle(280 + gx, 176, mb, 370 + gx, 104, mb, 460 + gx, 176, mb, 0.10f);
    LAYER(-0.4f);
    u32 mf = T->mountFront;
    C2D_DrawTriangle(-30 + gx, 180, mf, 60 + gx, 130, mf, 170 + gx, 180, mf, 0.15f);
    C2D_DrawTriangle(150 + gx, 180, mf, 290 + gx, 120, mf, 430 + gx, 180, mf, 0.15f);

    // lake
    LAYER(-0.3f);
    u32 l1 = shade(T->sky3, 0.45f), l2 = shade(T->sky1, 0.7f);
    C2D_DrawRectangle(gx - 30, 176, 0.20f, TOP_W + 60, 64, l1, l1, l2, l2);

    // city lights and their reflection
    LAYER(-0.2f);
    for (int i = 0; i < 40; i++) {
        float x = 190 + rnd(i, 3) * 190, y = 160 + rnd(i, 4) * 16;
        float tw = 0.6f + 0.4f * sinf(t * 1.3f + i);
        rect(x, y, 2, 2, 0.25f, RGBA(255, 214, 120, (u32)(190 * tw)));
        rect(x, 2 * 176 - y, 2, 3, 0.25f, RGBA(255, 214, 120, (u32)(70 * tw)));
    }

    // glass card with the selected title
    LAYER(0.5f);
    glass(16, 40, 226, 86, 10, 0.40f, RGBA(255, 255, 255, 46));

    LAYER(0.9f);
    if (tab < 2 && viewCount > 0) {
        const App *a = &apps[view[sel]];
        drawIcon(a, 52, 83, 56, 0.50f);
        drawText(a->name, 90, 52, 0.7f, 142, white, 0);
        if (a->publisher[0]) drawText(a->publisher, 90, 82, 0.45f, 142, RGBA(255, 255, 255, 200), 0);
        drawText(a->fav ? "Favorite" : "Press A to start", 90, 102, 0.4f, 142, withAlpha(T->accent, 255), 0);
    } else if (tab == 2) {
        drawText(T->name, 30, 56, 0.8f, 190, white, 0);
        drawText("Theme", 30, 92, 0.45f, 190, RGBA(255, 255, 255, 200), 0);
    } else if (tab == 3) {
        drawText("Settings", 30, 56, 0.8f, 190, white, 0);
        drawText("Make it yours", 30, 92, 0.45f, 190, RGBA(255, 255, 255, 200), 0);
    } else {
        drawText(tab == 1 ? "No favorites yet" : "No titles found", 30, 56, 0.6f, 190, white, 0);
        drawText(tab == 1 ? "Press Y on a title" : "Install a .cia and reopen", 30, 86, 0.42f, 190,
                 RGBA(255, 255, 255, 200), 0);
    }

    // status bar (flat, no depth)
    LAYER(0.0f);
    rect(0, 0, TOP_W, 22, 0.70f, RGBA(0, 0, 0, 90));

    u8 wifi = osGetWifiStrength();
    for (int i = 0; i < 4; i++)
        rect(8 + i * 5, 17 - i * 3, 3, 3 + i * 3, 0.80f, i <= wifi ? white : RGBA(255, 255, 255, 70));
    glass(34, 3, 64, 16, 6, 0.75f, withAlpha(T->accent, 200));
    drawText(wifi ? "Internet" : "Offline", 66, 3, 0.4f, 58, white, 1);

    char buf[48];
    snprintf(buf, sizeof(buf), "%d titles", appCount);
    drawText(buf, 112, 3, 0.4f, 0, RGBA(255, 255, 255, 220), 0);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, sizeof(buf), "%m/%d (%a)  %H:%M", tm);
    drawText(buf, TOP_W - 52, 3, 0.45f, 0, white, 2);

    u8 lvl = 0, charging = 0;
    PTMU_GetBatteryLevel(&lvl);
    PTMU_GetBatteryChargeState(&charging);
    rect(TOP_W - 44, 5, 34, 13, 0.80f, RGBA(255, 255, 255, 220));
    rect(TOP_W - 10, 8, 3, 7, 0.80f, RGBA(255, 255, 255, 220));
    rect(TOP_W - 43, 6, 32, 11, 0.82f, RGBA(20, 24, 40, 255));
    rect(TOP_W - 43, 6, 32.0f * lvl / 5.0f, 11, 0.84f, charging ? T->accent : RGB(80, 210, 110));

    // button hints (flat)
    keyChip("Y", 16, 222);
    drawText("Favorite", 30, 214, 0.42f, 0, white, 0);
    keyChip("L", TOP_W - 52, 222);
    keyChip("R", TOP_W - 34, 222);
    drawText("Tabs", TOP_W - 10, 214, 0.42f, 0, white, 2);
    gx = 0;
}

// ---------- bottom screen ----------
static void drawBottom(void) {
    const Theme *T = &themes[themeCur];
    gx = 0;
    u32 white = RGB(255, 255, 255);
    u32 dim = RGBA(255, 255, 255, 170);
    u32 b1 = shade(T->sky1, 0.6f), b2 = shade(T->sky2, 0.5f);
    C2D_DrawRectangle(0, 0, 0.0f, BOT_W, SCR_H, b1, b1, b2, b2);

    // tab bar
    glass(8, 6, 304, 28, 10, 0.20f, RGBA(255, 255, 255, 30));
    for (int i = 0; i < 4; i++) {
        float x = 10 + i * 75.0f;
        if (i == tab) roundRect(x, 8, 73, 24, 8, 0.30f, T->accent);
        drawText(tabNames[i], x + 36.5f, 12, 0.5f, 68, i == tab ? white : dim, 1);
    }

    if (tab < 2) {
        if (viewCount == 0) {
            drawText(tab == 1 ? "Press Y on a title to add it here" : "No titles found", BOT_W / 2, 100, 0.5f, 290, dim,
                     1);
        } else {
            for (int vi = 0; vi < viewCount; vi++) {
                int page = vi / PAGE_SIZE;
                float px = (page - pagePos) * BOT_W;
                if (px <= -BOT_W || px >= BOT_W) continue;
                int local = vi % PAGE_SIZE;
                float cx = 40 + (local % COLS) * 60 + px;
                float cy = 74 + (local / COLS) * 64;
                bool on = (vi == sel);
                float size = on ? 54 : 46;
                glass(cx - size / 2 - 4, cy - size / 2 - 4, size + 8, size + 8, 8, 0.30f,
                      RGBA(255, 255, 255, on ? 64 : 26));
                drawIcon(&apps[view[vi]], cx, cy, size, 0.50f);
                if (apps[view[vi]].fav) C2D_DrawCircleSolid(cx + size / 2 - 2, cy - size / 2 + 2, 0.70f, 4, T->accent);
                if (on) {
                    float pulse = 0.5f + 0.5f * sinf(frameNo * 0.1f);
                    brackets(cx - size / 2 - 7, cy - size / 2 - 7, size + 14, 0.80f,
                             withAlpha(T->accent, (u32)(150 + 105 * pulse)));
                }
            }
            int pages = (viewCount + PAGE_SIZE - 1) / PAGE_SIZE;
            int curPage = sel / PAGE_SIZE;
            if (pages > 1)
                for (int i = 0; i < pages; i++)
                    C2D_DrawCircleSolid(BOT_W / 2 - (pages - 1) * 6 + i * 12, 174, 0.5f, i == curPage ? 3.5f : 2.5f,
                                        i == curPage ? T->accent : RGBA(255, 255, 255, 90));
        }
    } else if (tab == 2) {
        for (int i = 0; i < THEME_COUNT; i++) {
            float x = 10 + (i % 2) * 152, y = 46 + (i / 2) * 72;
            const Theme *P = &themes[i];
            C2D_DrawRectangle(x, y, 0.30f, 148, 40, P->sky1, P->sky1, P->sky3, P->sky3);
            C2D_DrawTriangle(x + 10, y + 40, P->mountBack, x + 50, y + 14, P->mountBack, x + 100, y + 40,
                             P->mountBack, 0.31f);
            C2D_DrawTriangle(x + 60, y + 40, P->mountFront, x + 110, y + 8, P->mountFront, x + 148, y + 40,
                             P->mountFront, 0.32f);
            glass(x, y + 40, 148, 22, 4, 0.30f, RGBA(255, 255, 255, 36));
            drawText(P->name, x + 8, y + 43, 0.5f, 100, white, 0);
            C2D_DrawCircleSolid(x + 134, y + 51, 0.40f, 6, P->accent);
            if (i == themeCur) brackets(x - 4, y - 4, 156, 0.80f, T->accent);
        }
    } else {
        static const char *labels[3] = {"3D depth", "Shooting star", "Sort order"};
        const char *values[3] = {depthName[depthSet], starOn ? "On" : "Off", sortDesc ? "Z to A" : "A to Z"};
        for (int i = 0; i < 3; i++) {
            float y = 48 + i * 42;
            glass(10, y, 300, 34, 8, 0.30f, RGBA(255, 255, 255, i == setCur ? 60 : 28));
            if (i == setCur) roundRect(10, y, 5, 34, 2, 0.40f, T->accent);
            drawText(labels[i], 26, y + 8, 0.55f, 150, white, 0);
            drawText(values[i], 298, y + 8, 0.55f, 120, T->accent, 2);
        }
    }

    // info bar
    glass(8, 182, 304, 22, 8, 0.20f, RGBA(255, 255, 255, 34));
    if (tab < 2 && viewCount > 0) {
        const App *a = &apps[view[sel]];
        drawText(a->name, 16, 185, 0.45f, 190, white, 0);
        drawText(a->publisher, 304, 187, 0.38f, 100, dim, 2);
    } else if (tab == 2) {
        drawText("Left/Right to pick a theme", BOT_W / 2, 185, 0.42f, 290, dim, 1);
    } else if (tab == 3) {
        drawText("Up/Down to choose, Left/Right to change", BOT_W / 2, 185, 0.42f, 290, dim, 1);
    }

    // footer
    glass(0, 208, BOT_W, 32, 2, 0.20f, RGBA(255, 255, 255, 26));
    keyChip("B", 20, 224);
    drawText("Close", 34, 216, 0.5f, 0, white, 0);
    C2D_DrawTriangle(BOT_W / 2 - 8, 226, dim, BOT_W / 2 + 8, 226, dim, BOT_W / 2, 215, dim, 0.5f);
    rect(BOT_W / 2 - 5, 226, 10, 6, 0.5f, dim);
    keyChip("A", BOT_W - 66, 224);
    drawText(tab < 2 ? "Start" : "OK", BOT_W - 10, 216, 0.5f, 0, white, 2);
}

// ---------- main ----------
static void setTab(int n) {
    tab = (n + 4) % 4;
    if (tab < 2) {
        sel = 0;
        pagePos = 0;
        rebuildView();
    }
}

static void changeSetting(int dir) {
    if (setCur == 0) depthSet = (depthSet + dir + 3) % 3;
    else if (setCur == 1) starOn = !starOn;
    else {
        sortDesc = !sortDesc;
        // keep the same title selected after re-sorting
        int savedTab = tab;
        tab = 0;
        rebuildView();
        sortApps();
        tab = savedTab;
    }
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(8192);
    C2D_Prepare();
    amInit();
    ptmuInit();

    topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    botT = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuf = C2D_TextBufNew(8192);

    Config cfg;
    loadConfig(&cfg);
    themeCur = cfg.theme;
    depthSet = cfg.depth;
    sortDesc = cfg.sortDesc;
    starOn = cfg.star != 0;

    scanTitles();
    for (int i = 0; i < appCount; i++)
        for (u32 f = 0; f < cfg.favCount; f++)
            if (apps[i].id == cfg.favs[f]) apps[i].fav = true;
    tab = 0;
    rebuildView();
    sortApps();

    bool launch = false;
    bool is3d = false;
    bool dragging = false;
    touchPosition t0 = {0}, tl = {0};

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        u32 up = hidKeysUp();
        frameNo++;

        if (down & (KEY_B | KEY_START)) break;
        if (down & KEY_R) setTab(tab + 1);
        if (down & KEY_L) setTab(tab - 1);
        if (down & KEY_X) themeCur = (themeCur + 1) % THEME_COUNT;

        bool activate = (down & KEY_A) != 0;

        if (tab < 2) {
            if (viewCount > 0) {
                if (down & KEY_RIGHT && sel < viewCount - 1) sel++;
                if (down & KEY_LEFT && sel > 0) sel--;
                if (down & KEY_DOWN && sel + COLS < viewCount) sel += COLS;
                if (down & KEY_UP && sel - COLS >= 0) sel -= COLS;
                if (down & KEY_Y) {
                    apps[view[sel]].fav = !apps[view[sel]].fav;
                    if (tab == 1) rebuildView();
                }
            }
        } else if (tab == 2) {
            if (down & KEY_RIGHT) themeCur = (themeCur + 1) % THEME_COUNT;
            if (down & KEY_LEFT) themeCur = (themeCur + THEME_COUNT - 1) % THEME_COUNT;
            if (down & KEY_DOWN && themeCur + 2 < THEME_COUNT) themeCur += 2;
            if (down & KEY_UP && themeCur - 2 >= 0) themeCur -= 2;
        } else {
            if (down & KEY_DOWN && setCur < 2) setCur++;
            if (down & KEY_UP && setCur > 0) setCur--;
            if (down & KEY_RIGHT) changeSetting(1);
            if (down & KEY_LEFT) changeSetting(-1);
            if (activate) changeSetting(1);
        }

        // touch
        if (down & KEY_TOUCH) {
            hidTouchRead(&t0);
            tl = t0;
            dragging = false;
        }
        if (held & KEY_TOUCH) {
            hidTouchRead(&tl);
            if (abs((int)tl.px - (int)t0.px) > 12) dragging = true;
        }
        if (up & KEY_TOUCH) {
            int dx = (int)tl.px - (int)t0.px;
            if (dragging && tab < 2 && t0.py > 40 && t0.py < 176 && viewCount > 0) {
                int page = sel / PAGE_SIZE;
                int pages = (viewCount + PAGE_SIZE - 1) / PAGE_SIZE;
                if (dx < -40 && page < pages - 1) sel = (page + 1) * PAGE_SIZE;
                if (dx > 40 && page > 0) sel = (page - 1) * PAGE_SIZE;
            } else if (!dragging) {
                int px = tl.px, py = tl.py;
                if (py < 38) {
                    setTab((px - 8) / 75);
                } else if (py >= 208) {
                    if (px < 110) break;
                    if (px > 210) activate = true;
                } else if (tab < 2 && py >= 40 && py < 172 && viewCount > 0) {
                    int col = (px - 10) / 60, row = (py - 42) / 64;
                    int idx = (sel / PAGE_SIZE) * PAGE_SIZE + row * COLS + col;
                    if (col >= 0 && col < COLS && row >= 0 && row < 2 && idx < viewCount) {
                        if (idx == sel) activate = true;
                        else sel = idx;
                    }
                } else if (tab == 2 && py >= 46) {
                    int col = (px - 10) / 152, row = (py - 46) / 72;
                    int idx = row * 2 + col;
                    if (col >= 0 && col < 2 && row >= 0 && row < 2 && idx < THEME_COUNT) themeCur = idx;
                } else if (tab == 3 && py >= 48) {
                    int row = (py - 48) / 42;
                    if (row >= 0 && row < 3) {
                        setCur = row;
                        changeSetting(1);
                    }
                }
            }
            dragging = false;
        }

        if (activate && tab < 2 && viewCount > 0) {
            launch = true;
            break;
        }

        float target = (float)(sel / PAGE_SIZE);
        pagePos += (target - pagePos) * 0.25f;

        // stereoscopic 3D only when the slider is up and depth is enabled
        g3d = osGet3DSliderState();
        bool want3d = g3d > 0.02f && depthMul[depthSet] > 0.0f;
        if (want3d != is3d) {
            gfxSet3D(want3d);
            is3d = want3d;
        }

        C2D_TextBufClear(textBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(topL, RGB(0, 0, 0));
        C2D_SceneBegin(topL);
        drawTop(0);
        if (is3d) {
            C2D_TargetClear(topR, RGB(0, 0, 0));
            C2D_SceneBegin(topR);
            drawTop(1);
        }
        C2D_TargetClear(botT, RGB(0, 0, 0));
        C2D_SceneBegin(botT);
        drawBottom();
        C3D_FrameEnd(0);
    }

    saveConfig();
    if (launch && viewCount > 0) aptSetChainloader(apps[view[sel]].id, apps[view[sel]].media);

    for (int i = 0; i < appCount; i++)
        if (apps[i].hasIcon) C3D_TexDelete(&apps[i].tex);
    C2D_TextBufDelete(textBuf);
    ptmuExit();
    amExit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
