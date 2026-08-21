#include "ui.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#define COL_BG C2D_Color32(148, 184, 104, 255)
#define COL_BG2 C2D_Color32(126, 166, 86, 255)
#define COL_BG_BOT C2D_Color32(140, 176, 96, 255)
#define COL_BANNER C2D_Color32(248, 248, 248, 255)
#define COL_GOLD C2D_Color32(232, 176, 48, 255)
#define COL_GOLD_DK C2D_Color32(196, 140, 28, 255)
#define COL_TEXT C2D_Color32(252, 252, 252, 255)
#define COL_INK C2D_Color32(48, 72, 128, 255)
#define COL_MUTED C2D_Color32(230, 236, 220, 255)
#define COL_SHADOW C2D_Color32(40, 64, 32, 80)
#define COL_EMPTY C2D_Color32(118, 154, 78, 180)
#define COL_SLOT C2D_Color32(160, 196, 112, 255)
#define COL_ARROW C2D_Color32(220, 36, 36, 255)
#define COL_SHINY C2D_Color32(248, 208, 64, 255)
#define COL_LOCK C2D_Color32(72, 64, 64, 160)
#define COL_TAG C2D_Color32(120, 48, 40, 255)
#define COL_PILL C2D_Color32(244, 248, 252, 255)
#define COL_MALE C2D_Color32(80, 160, 232, 255)
#define COL_FEMALE C2D_Color32(240, 128, 176, 255)
#define COL_LINE C2D_Color32(255, 255, 255, 90)
#define COL_TRI C2D_Color32(255, 255, 255, 22)
#define COL_SQ C2D_Color32(255, 255, 255, 28)

#define Z 0.5f
#define SCALE_TITLE 0.55f
#define SCALE_BODY 0.45f
#define SCALE_SMALL 0.40f
#define SCALE_BTN 0.48f
#define SCALE_BTN_SM 0.38f
#define SCALE_SUM 0.42f

#define GRID_OX 8.f
#define GRID_OY 30.f
#define GRID_CW 34.f
#define GRID_CH 30.f
#define GRID_GAP 2.f

typedef struct {
    float x, y, w, h;
} UiRect;

static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;
static C2D_TextBuf g_buf;
static C2D_SpriteSheet g_sheet;
static char g_host[64];
static char g_account[40];
static char g_status[96];

static const UiRect BTN_DEP = {224, 32, 90, 28};
static const UiRect BTN_WIT = {224, 66, 90, 28};
static const UiRect BTN_WR = {224, 100, 90, 28};
static const UiRect BTN_SWAP = {288, 4, 26, 22};
static const UiRect BTN_BACK = {276, 206, 36, 30};
static const UiRect ARR_L = {10, 4, 24, 22};
static const UiRect ARR_R = {186, 4, 24, 22};

static void copy_str(char *dst, size_t n, const char *src)
{
    if (!src)
        src = "";
    snprintf(dst, n, "%s", src);
}

void ui_set_chrome(const char *host, const char *account, const char *status)
{
    copy_str(g_host, sizeof(g_host), host);
    copy_str(g_account, sizeof(g_account), account);
    copy_str(g_status, sizeof(g_status), status);
}

static void frame_begin(void)
{
    C2D_TextBufClear(g_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
}

static void frame_end(void)
{
    C3D_FrameEnd(0);
}

static void draw_text(float x, float y, float scale, u32 color, u32 align, const char *s)
{
    C2D_Text t;
    if (!s || !s[0])
        return;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | align, x, y, Z, scale, scale, color);
}

static void draw_text_sh(float x, float y, float scale, u32 color, u32 align, const char *s)
{
    draw_text(x + 1.f, y + 1.f, scale, COL_SHADOW, align, s);
    draw_text(x, y, scale, color, align, s);
}

static void draw_wrap(float x, float y, float width, float scale, u32 color, const char *s)
{
    C2D_Text t;
    if (!s || !s[0])
        return;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x, y, Z, scale, scale, color, width);
}

static void draw_centered(float cx, float y, float width, float scale, u32 color, const char *s)
{
    C2D_Text t;
    if (!s || !s[0])
        return;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter | C2D_WordWrap, cx, y, Z, scale, scale,
                 color, width);
}

static float measure_text(const char *s, float scale)
{
    C2D_Text t;
    float w = 0.f, h = 0.f;
    if (!s || !s[0])
        return 0.f;
    C2D_TextParse(&t, g_buf, s);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

static void draw_pksm_bg(int w, int h)
{
    int y, gx, gy;
    for (y = 0; y < h; y += 16)
        C2D_DrawRectSolid(0, (float)y, 0, (float)w, 16.f, (y / 16) % 2 ? COL_BG2 : COL_BG);
    for (gy = -8; gy < h; gy += 36) {
        for (gx = -10; gx < w; gx += 44) {
            C2D_DrawTriangle((float)gx, (float)gy + 30, COL_TRI, (float)gx + 20, (float)gy, COL_TRI,
                             (float)gx + 40, (float)gy + 30, COL_TRI, 0);
        }
    }
}

static void draw_round_rect(float x, float y, float w, float h, u32 color)
{
    float r = h * 0.5f;
    if (r > w * 0.5f)
        r = w * 0.5f;
    C2D_DrawRectSolid(x + r, y, 0, w - 2.f * r, h, color);
    C2D_DrawCircleSolid(x + r, y + r, 0, r, color);
    C2D_DrawCircleSolid(x + w - r, y + r, 0, r, color);
}

static void draw_banner(float cx, float y, float w, float h, const char *title)
{
    draw_round_rect(cx - w * 0.5f, y, w, h, COL_BANNER);
    draw_centered(cx, y + h * 0.5f - 8.f, w - 8.f, SCALE_BODY, C2D_Color32(48, 48, 48, 255), title);
}

static void draw_lr_key(float x, float y, const char *lab)
{
    C2D_DrawRectSolid(x, y, 0, 18, 16, COL_GOLD);
    C2D_DrawRectSolid(x + 1, y + 1, 0, 16, 14, COL_GOLD_DK);
    draw_centered(x + 9.f, y + 1.f, 16.f, 0.36f, COL_TEXT, lab);
}

static void draw_top(const char *title, const char *body)
{
    draw_pksm_bg(400, 240);
    C2D_DrawRectSolid(310, 4, 0, 86, 16, COL_TAG);
    draw_text(392, 5, 0.32f, COL_TEXT, C2D_AlignRight, "pocket");
    draw_lr_key(96, 8, "L");
    draw_banner(200, 6, 160, 20, title && title[0] ? title : "Pockettransfer");
    draw_lr_key(286, 8, "R");

    C2D_DrawRectSolid(28, 70, 0, 10, 10, COL_SQ);
    C2D_DrawRectSolid(90, 130, 0, 8, 8, COL_SQ);
    C2D_DrawRectSolid(50, 180, 0, 12, 12, COL_SQ);

    draw_text_sh(16, 40, SCALE_SMALL, COL_MUTED, 0, g_host[0] ? g_host : "bank.saltbox.cc");
    draw_text_sh(16, 58, SCALE_BODY, COL_TEXT, 0, g_account[0] ? g_account : "Not signed in");
    if (body && body[0])
        draw_wrap(16, 88, 368, SCALE_BODY, COL_TEXT, body);
    if (g_status[0])
        draw_text_sh(16, 214, SCALE_SMALL, COL_MUTED, 0, g_status);
}

static void draw_bot_bg(void)
{
    C2D_TargetClear(g_bot, COL_BG_BOT);
    C2D_SceneBegin(g_bot);
    draw_pksm_bg(320, 240);
}

static void draw_footer(const char *hint)
{
    draw_round_rect(20, 214, 280, 20, C2D_Color32(255, 255, 255, 40));
    draw_centered(160, 217, 270, SCALE_SMALL, COL_TEXT, hint);
}

static void draw_button(const UiRect *r, const char *label, int selected)
{
    float scale = (r->h < 40.f || r->w < 150.f) ? SCALE_BTN_SM : SCALE_BTN;
    u32 fill = selected ? C2D_Color32(255, 255, 255, 255) : COL_PILL;
    draw_round_rect(r->x + 1, r->y + 2, r->w, r->h, COL_SHADOW);
    draw_round_rect(r->x, r->y, r->w, r->h, fill);
    if (selected)
        C2D_DrawRectSolid(r->x + 4, r->y + 4, 0, 3, r->h - 8, COL_GOLD);
    draw_centered(r->x + r->w * 0.5f, r->y + r->h * 0.5f - 8.f, r->w - 16.f, scale, COL_INK, label);
}

static void layout_grid(int count, int i, UiRect *r)
{
    const float footer = 28.f;
    const float top = 12.f;
    const float side = 12.f;
    const float gap = 8.f;
    int cols = (count <= 3) ? 1 : 2;
    int rows = (count + cols - 1) / cols;
    float avail_h = 240.f - top - footer - gap * (float)(rows - 1);
    float avail_w = 320.f - side * 2 - gap * (float)(cols - 1);
    int col = i % cols;
    int row = i / cols;
    r->w = avail_w / (float)cols;
    r->h = avail_h / (float)rows;
    if (r->h > 56.f)
        r->h = 56.f;
    r->x = side + (float)col * (r->w + gap);
    r->y = top + (float)row * (r->h + gap);
}

static int hit_rect(const UiRect *r, int px, int py)
{
    return (float)px >= r->x && (float)px < r->x + r->w && (float)py >= r->y &&
           (float)py < r->y + r->h;
}

static int wait_touch_up(void)
{
    while (aptMainLoop()) {
        hidScanInput();
        if (!(hidKeysHeld() & KEY_TOUCH))
            return 0;
        gspWaitForVBlank();
    }
    return -1;
}

void ui_init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_buf = C2D_TextBufNew(16384);
    g_sheet = NULL;
    copy_str(g_host, sizeof(g_host), "bank.saltbox.cc");
    copy_str(g_account, sizeof(g_account), "Not signed in");
    copy_str(g_status, sizeof(g_status), "Starting");
}

void ui_exit(void)
{
    if (g_sheet)
        C2D_SpriteSheetFree(g_sheet);
    g_sheet = NULL;
    if (g_buf)
        C2D_TextBufDelete(g_buf);
    g_buf = NULL;
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

void ui_after_swkbd(void)
{
    C2D_Prepare();
}

void ui_busy(const char *title, const char *body)
{
    if (title && title[0])
        copy_str(g_status, sizeof(g_status), title);
    frame_begin();
    C2D_TargetClear(g_top, COL_BG);
    C2D_SceneBegin(g_top);
    draw_top(title, body);
    draw_bot_bg();
    {
        UiRect r = {40, 88, 240, 48};
        draw_button(&r, "Working...", 1);
    }
    draw_footer("Please wait");
    frame_end();
}

int ui_alert(const char *title, const char *body)
{
    UiRect btn = {36, 88, 248, 52};
    if (wait_touch_up() != 0)
        return -1;
    if (title && title[0])
        copy_str(g_status, sizeof(g_status), title);
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & (KEY_A | KEY_START))
            return 0;
        if (k & KEY_TOUCH) {
            touchPosition t;
            hidTouchRead(&t);
            if (hit_rect(&btn, t.px, t.py))
                return 0;
        }
        frame_begin();
        C2D_TargetClear(g_top, COL_BG);
        C2D_SceneBegin(g_top);
        draw_top(title, body);
        draw_bot_bg();
        draw_button(&btn, "Continue", 1);
        draw_footer("A / tap  ·  START skip");
        frame_end();
    }
    return -1;
}

int ui_confirm(const char *title, const char *body)
{
    UiRect yes = {12, 88, 142, 56};
    UiRect no = {166, 88, 142, 56};
    int sel = 0;
    if (wait_touch_up() != 0)
        return -1;
    if (title && title[0])
        copy_str(g_status, sizeof(g_status), title);
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_LEFT)
            sel = 0;
        if (k & KEY_RIGHT)
            sel = 1;
        if (k & KEY_A)
            return sel == 0 ? 1 : 0;
        if (k & KEY_B)
            return 0;
        if (k & KEY_START)
            return -1;
        if (k & KEY_TOUCH) {
            touchPosition t;
            hidTouchRead(&t);
            if (hit_rect(&yes, t.px, t.py))
                return 1;
            if (hit_rect(&no, t.px, t.py))
                return 0;
        }
        frame_begin();
        C2D_TargetClear(g_top, COL_BG);
        C2D_SceneBegin(g_top);
        draw_top(title, body);
        draw_bot_bg();
        draw_button(&yes, "Yes", sel == 0);
        draw_button(&no, "No", sel == 1);
        draw_footer("A confirm  ·  B no");
        frame_end();
    }
    return -1;
}

int ui_pick(const char *title, const char **items, int count)
{
    int sel = 0;
    int cols = (count <= 3) ? 1 : 2;
    if (count <= 0)
        return -1;
    if (wait_touch_up() != 0)
        return -2;
    if (title && title[0])
        copy_str(g_status, sizeof(g_status), title);
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_UP)
            sel = (sel + count - cols) % count;
        if (k & KEY_DOWN)
            sel = (sel + cols) % count;
        if (k & KEY_LEFT)
            sel = (sel + count - 1) % count;
        if (k & KEY_RIGHT)
            sel = (sel + 1) % count;
        if (sel >= count)
            sel = count - 1;
        if (k & KEY_A)
            return sel;
        if (k & KEY_B)
            return -1;
        if (k & KEY_START)
            return -2;
        if (k & KEY_TOUCH) {
            touchPosition t;
            int i;
            hidTouchRead(&t);
            for (i = 0; i < count; i++) {
                UiRect r;
                layout_grid(count, i, &r);
                if (hit_rect(&r, t.px, t.py))
                    return i;
            }
        }
        frame_begin();
        C2D_TargetClear(g_top, COL_BG);
        C2D_SceneBegin(g_top);
        draw_top(title, items[sel]);
        draw_bot_bg();
        {
            int i;
            for (i = 0; i < count; i++) {
                UiRect r;
                layout_grid(count, i, &r);
                draw_button(&r, items[i], i == sel);
            }
        }
        draw_footer("A / tap select  ·  B back");
        frame_end();
    }
    return -2;
}

static void layout_pc_cell(int i, UiRect *r)
{
    int col = i % UI_BOX_COLS;
    int row = i / UI_BOX_COLS;
    r->x = GRID_OX + (float)col * (GRID_CW + GRID_GAP);
    r->y = GRID_OY + (float)row * (GRID_CH + GRID_GAP);
    r->w = GRID_CW;
    r->h = GRID_CH;
}

static const UiPoke *cursor_poke(const UiBoxView *st)
{
    if (st->browse || st->pane == 1)
        return &st->bank[st->bank_cur];
    return &st->save[st->save_cur];
}

static void draw_poke_icon(int species, float x, float y, float scale)
{
    C2D_Image img;
    if (!g_sheet)
        g_sheet = C2D_SpriteSheetLoad("romfs:/pkm_icons.t3x");
    if (!g_sheet || species <= 0)
        return;
    if ((size_t)species >= C2D_SpriteSheetCount(g_sheet))
        return;
    img = C2D_SpriteSheetGetImage(g_sheet, (size_t)species);
    if (!img.tex || !img.subtex)
        return;
    C2D_DrawImageAt(img, x, y, Z, NULL, scale, scale);
}

static u32 type_color(const char *t)
{
    char b[12];
    int i;
    if (!t)
        t = "";
    for (i = 0; i < 11 && t[i]; i++) {
        char c = t[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        b[i] = c;
    }
    b[i] = 0;
    if (strcmp(b, "fire") == 0)
        return C2D_Color32(240, 128, 48, 255);
    if (strcmp(b, "water") == 0)
        return C2D_Color32(104, 144, 240, 255);
    if (strcmp(b, "grass") == 0)
        return C2D_Color32(120, 200, 80, 255);
    if (strcmp(b, "electric") == 0)
        return C2D_Color32(248, 208, 48, 255);
    if (strcmp(b, "ice") == 0)
        return C2D_Color32(152, 216, 216, 255);
    if (strcmp(b, "fighting") == 0)
        return C2D_Color32(192, 48, 40, 255);
    if (strcmp(b, "poison") == 0)
        return C2D_Color32(160, 64, 160, 255);
    if (strcmp(b, "ground") == 0)
        return C2D_Color32(224, 192, 104, 255);
    if (strcmp(b, "flying") == 0)
        return C2D_Color32(168, 144, 240, 255);
    if (strcmp(b, "psychic") == 0)
        return C2D_Color32(248, 88, 136, 255);
    if (strcmp(b, "bug") == 0)
        return C2D_Color32(168, 184, 32, 255);
    if (strcmp(b, "rock") == 0)
        return C2D_Color32(184, 160, 56, 255);
    if (strcmp(b, "ghost") == 0)
        return C2D_Color32(112, 88, 152, 255);
    if (strcmp(b, "dragon") == 0)
        return C2D_Color32(112, 56, 248, 255);
    if (strcmp(b, "dark") == 0)
        return C2D_Color32(112, 88, 72, 255);
    if (strcmp(b, "steel") == 0)
        return C2D_Color32(184, 184, 208, 255);
    if (strcmp(b, "fairy") == 0)
        return C2D_Color32(238, 153, 172, 255);
    return C2D_Color32(168, 168, 120, 255);
}

static void draw_type_pill(float x, float y, const char *name)
{
    float w;
    char lab[12];
    int i;
    if (!name || !name[0])
        return;
    for (i = 0; i < 11 && name[i]; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        lab[i] = c;
    }
    lab[i] = 0;
    w = measure_text(lab, 0.32f) + 16.f;
    if (w < 52.f)
        w = 52.f;
    draw_round_rect(x, y, w, 14, type_color(name));
    draw_centered(x + w * 0.5f, y + 1.f, w, 0.32f, COL_TEXT, lab);
}

static void draw_dash(float x, float y, float w)
{
    float i;
    for (i = 0; i < w; i += 6.f)
        C2D_DrawRectSolid(x + i, y, 0, 4, 1, COL_LINE);
}

static void draw_cross(float x, float y)
{
    C2D_DrawRectSolid(x - 5, y - 1, 0, 10, 2, COL_TEXT);
    C2D_DrawRectSolid(x - 1, y - 5, 0, 2, 10, COL_TEXT);
}

static void draw_corner_braces(float x, float y, float w, float h)
{
    float n = 10.f;
    C2D_DrawRectSolid(x, y, 0, n, 2, COL_TEXT);
    C2D_DrawRectSolid(x, y, 0, 2, n, COL_TEXT);
    C2D_DrawRectSolid(x + w - n, y, 0, n, 2, COL_TEXT);
    C2D_DrawRectSolid(x + w - 2, y, 0, 2, n, COL_TEXT);
    C2D_DrawRectSolid(x, y + h - 2, 0, n, 2, COL_TEXT);
    C2D_DrawRectSolid(x, y + h - n, 0, 2, n, COL_TEXT);
    C2D_DrawRectSolid(x + w - n, y + h - 2, 0, n, 2, COL_TEXT);
    C2D_DrawRectSolid(x + w - 2, y + h - n, 0, 2, n, COL_TEXT);
}

static void draw_red_arrow(float cx, float y)
{
    C2D_DrawTriangle(cx, y + 11, COL_ARROW, cx - 7, y, COL_ARROW, cx + 7, y, COL_ARROW, 0);
}

static void draw_pc_cell(const UiRect *r, const UiPoke *p, int selected, int picked)
{
    float ix, iy, scale;
    C2D_DrawRectSolid(r->x, r->y, 0, r->w, r->h, p->occupied ? COL_SLOT : COL_EMPTY);
    if (p->locked)
        C2D_DrawRectSolid(r->x, r->y, 0, r->w, r->h, COL_LOCK);
    if (p->occupied && p->species > 0) {
        scale = 0.80f;
        ix = r->x + (r->w - 40.f * scale) * 0.5f;
        iy = r->y + (r->h - 30.f * scale) * 0.5f + 2.f;
        draw_poke_icon(p->species, ix, iy, scale);
        if (!g_sheet)
            draw_centered(r->x + r->w * 0.5f, r->y + 8.f, r->w - 2.f, 0.28f, COL_TEXT,
                          p->name[0] ? p->name : "?");
    }
    if (p->shiny)
        C2D_DrawRectSolid(r->x + r->w - 6, r->y + 2, 0, 4, 4, COL_SHINY);
    if (picked)
        C2D_DrawRectSolid(r->x, r->y, 0, r->w, 2, COL_GOLD);
    if (selected)
        draw_red_arrow(r->x + r->w * 0.5f, r->y - 8.f);
}

static void draw_pc_grid(const UiPoke *slots, int cursor, int picked_id, int show_pick)
{
    int i;
    for (i = 0; i < UI_BOX_SLOTS; i++) {
        UiRect r;
        int picked = show_pick && picked_id && slots[i].id == picked_id && slots[i].occupied;
        layout_pc_cell(i, &r);
        draw_pc_cell(&r, &slots[i], i == cursor, picked);
    }
}

static void draw_summary(const UiPoke *p)
{
    char line[80];
    float x = 214.f;
    float y = 40.f;
    const char *nick;
    const char *sp;

    draw_dash(x, y - 4, 172);
    if (!p->occupied) {
        draw_text_sh(x, y + 8, SCALE_SUM, COL_MUTED, 0, "No Pokemon");
        return;
    }

    nick = p->name[0] ? p->name : (p->species_name[0] ? p->species_name : "Pokemon");
    sp = p->species_name[0] ? p->species_name : nick;
    snprintf(line, sizeof(line), "%s  #%d", nick, p->species);
    draw_text_sh(x, y, SCALE_SUM, COL_TEXT, 0, line);
    if (p->gender == 1)
        draw_text_sh(x + 118, y, SCALE_SUM, COL_FEMALE, 0, "F");
    else if (p->gender == 0)
        draw_text_sh(x + 118, y, SCALE_SUM, COL_MALE, 0, "M");
    snprintf(line, sizeof(line), "Lv.%d", p->level);
    draw_text_sh(x + 138, y, SCALE_SUM, COL_TEXT, 0, line);
    if (p->shiny)
        draw_text_sh(x + 172, y, 0.32f, COL_SHINY, 0, "*");

    y += 22.f;
    draw_dash(x, y, 172);
    y += 6.f;
    draw_text_sh(x, y, SCALE_SUM, COL_TEXT, 0, sp);
    y += 16.f;
    draw_type_pill(x, y, p->type1);
    if (p->type2[0])
        draw_type_pill(x + 64, y, p->type2);

    y += 22.f;
    draw_dash(x, y, 172);
    y += 6.f;
    draw_text_sh(x, y, SCALE_SUM, COL_TEXT, 0, p->ot[0] ? p->ot : "—");
    snprintf(line, sizeof(line), "ID: %d", p->tid);
    draw_text_sh(x, y + 16, SCALE_SUM, COL_TEXT, 0, line);

    y += 38.f;
    draw_dash(x, y, 172);
    y += 6.f;
    draw_text_sh(x, y, SCALE_SUM, COL_TEXT, 0, p->nature[0] ? p->nature : "—");
    snprintf(line, sizeof(line), "IV: %d/%d/%d", p->iv[0], p->iv[1], p->iv[2]);
    draw_text_sh(x, y + 16, SCALE_SMALL, COL_TEXT, 0, line);
    snprintf(line, sizeof(line), "%d/%d/%d", p->iv[3], p->iv[4], p->iv[5]);
    draw_text_sh(x + 78, y + 16, SCALE_SMALL, COL_MUTED, 0, line);
    if (p->met[0])
        draw_text_sh(x, y + 32, SCALE_SMALL, COL_MUTED, 0, p->met);
}

static void draw_viewer_sprite(const UiPoke *p)
{
    draw_cross(24, 48);
    draw_cross(188, 48);
    draw_cross(24, 188);
    draw_cross(188, 188);
    C2D_DrawRectSolid(40, 70, 0, 8, 8, COL_SQ);
    C2D_DrawRectSolid(120, 160, 0, 10, 10, COL_SQ);
    if (p->occupied && p->species > 0) {
        float scale = 3.2f;
        float w = 40.f * scale;
        float h = 30.f * scale;
        float x = 24.f + (164.f - w) * 0.5f;
        float y = 52.f + (132.f - h) * 0.5f;
        draw_poke_icon(p->species, x, y, scale);
        if (!g_sheet)
            draw_centered(106, 110, 160, SCALE_TITLE, COL_TEXT, p->name);
    }
}

static void draw_side_btn(const UiRect *r, const char *label, int selected)
{
    u32 fill = selected ? C2D_Color32(255, 255, 255, 255) : COL_PILL;
    draw_round_rect(r->x, r->y, r->w, r->h, fill);
    C2D_DrawRectSolid(r->x + 3, r->y + 5, 0, 3, r->h - 10, COL_GOLD);
    C2D_DrawTriangle(r->x + 12, r->y + r->h * 0.5f, COL_INK, r->x + 20, r->y + r->h * 0.5f - 5,
                     COL_INK, r->x + 20, r->y + r->h * 0.5f + 5, COL_INK, 0);
    draw_text(r->x + 26, r->y + r->h * 0.5f - 7.f, SCALE_BTN_SM, COL_INK, 0, label);
}

static void draw_swap_btn(void)
{
    draw_round_rect(BTN_SWAP.x, BTN_SWAP.y, BTN_SWAP.w, BTN_SWAP.h, COL_GOLD);
    C2D_DrawRectSolid(BTN_SWAP.x + 4, BTN_SWAP.y + 5, 0, 7, 5, COL_GOLD_DK);
    C2D_DrawRectSolid(BTN_SWAP.x + 15, BTN_SWAP.y + 12, 0, 7, 5, COL_GOLD_DK);
    C2D_DrawTriangle(BTN_SWAP.x + 13, BTN_SWAP.y + 7, COL_TEXT, BTN_SWAP.x + 18, BTN_SWAP.y + 4,
                     COL_TEXT, BTN_SWAP.x + 18, BTN_SWAP.y + 10, COL_TEXT, 0);
}

static void draw_wifi(void)
{
    C2D_DrawCircleSolid(22, 222, 0, 12, C2D_Color32(64, 120, 200, 255));
    C2D_DrawCircleSolid(22, 226, 0, 3, COL_TEXT);
    C2D_DrawRectSolid(20, 214, 0, 4, 6, COL_TEXT);
    C2D_DrawRectSolid(16, 216, 0, 3, 4, COL_TEXT);
    C2D_DrawRectSolid(25, 216, 0, 3, 4, COL_TEXT);
}

static void draw_back_btn(void)
{
    C2D_DrawCircleSolid(294, 222, 0, 12, C2D_Color32(64, 120, 200, 255));
    C2D_DrawTriangle(286, 222, COL_TEXT, 296, 216, COL_TEXT, 296, 228, COL_TEXT, 0);
    C2D_DrawCircleSolid(306, 210, 0, 8, C2D_Color32(232, 140, 48, 255));
    draw_centered(306, 204, 14, 0.32f, COL_TEXT, "B");
}

static void move_cursor(int *cur, int delta)
{
    int n = *cur + delta;
    if (n < 0)
        n += UI_BOX_SLOTS;
    if (n >= UI_BOX_SLOTS)
        n -= UI_BOX_SLOTS;
    *cur = n;
}

static int *active_cursor(UiBoxView *st)
{
    return st->pane ? &st->bank_cur : &st->save_cur;
}

static int *visible_box(UiBoxView *st)
{
    return st->pane ? &st->bank_box : &st->save_box;
}

static int *other_box(UiBoxView *st)
{
    return st->pane ? &st->save_box : &st->bank_box;
}

static int visible_box_count(const UiBoxView *st)
{
    int n = st->pane ? st->bank_boxes : st->save_boxes;
    return n > 0 ? n : 1;
}

static int other_box_count(const UiBoxView *st)
{
    int n = st->pane ? st->save_boxes : st->bank_boxes;
    return n > 0 ? n : 1;
}

static void step_box(int *box, int count, int dir)
{
    if (count <= 0)
        count = 1;
    *box = (*box + count + dir) % count;
}

static int slot_action(UiBoxView *st)
{
    const UiPoke *save = &st->save[st->save_cur];
    const UiPoke *bank = &st->bank[st->bank_cur];
    if (st->mode == 0) {
        if (st->pane == 0 && save->occupied && !save->locked)
            return UI_BOX_DEPOSIT;
        return UI_BOX_NONE;
    }
    if (st->pane == 1 && bank->occupied) {
        st->picked_id = bank->id;
        return UI_BOX_NONE;
    }
    if (st->pane == 0 && st->picked_id && !save->occupied && !save->locked)
        return UI_BOX_WITHDRAW;
    return UI_BOX_NONE;
}

int ui_boxes_poll(UiBoxView *st)
{
    u32 k;
    int *cur;
    hidScanInput();
    k = hidKeysDown();
    cur = active_cursor(st);

    if (k & KEY_LEFT)
        move_cursor(cur, -1);
    if (k & KEY_RIGHT)
        move_cursor(cur, 1);
    if (k & KEY_UP)
        move_cursor(cur, -UI_BOX_COLS);
    if (k & KEY_DOWN)
        move_cursor(cur, UI_BOX_COLS);
    if (k & KEY_L) {
        if (st->browse)
            step_box(&st->bank_box, st->bank_boxes > 0 ? st->bank_boxes : 1, -1);
        else
            step_box(other_box(st), other_box_count(st), -1);
    }
    if (k & KEY_R) {
        if (st->browse)
            step_box(&st->bank_box, st->bank_boxes > 0 ? st->bank_boxes : 1, 1);
        else
            step_box(other_box(st), other_box_count(st), 1);
    }
    if (!st->browse && (k & KEY_Y))
        st->pane = st->pane ? 0 : 1;
    if (!st->browse && (k & KEY_X)) {
        st->mode = st->mode ? 0 : 1;
        st->picked_id = 0;
    }
    if (k & KEY_START)
        return (!st->browse && st->dirty) ? UI_BOX_WRITE : UI_BOX_BACK;
    if (k & KEY_B)
        return UI_BOX_BACK;
    if (!st->browse && (k & KEY_A))
        return slot_action(st);

    if (k & KEY_TOUCH) {
        touchPosition t;
        int i;
        hidTouchRead(&t);
        if (!st->browse && hit_rect(&BTN_DEP, t.px, t.py)) {
            st->mode = 0;
            st->picked_id = 0;
            st->pane = 0;
            return UI_BOX_NONE;
        }
        if (!st->browse && hit_rect(&BTN_WIT, t.px, t.py)) {
            st->mode = 1;
            st->pane = 1;
            return UI_BOX_NONE;
        }
        if (!st->browse && hit_rect(&BTN_WR, t.px, t.py))
            return UI_BOX_WRITE;
        if (!st->browse && hit_rect(&BTN_SWAP, t.px, t.py)) {
            st->pane = st->pane ? 0 : 1;
            return UI_BOX_NONE;
        }
        if (hit_rect(&BTN_BACK, t.px, t.py))
            return UI_BOX_BACK;
        if (hit_rect(&ARR_L, t.px, t.py)) {
            if (st->browse)
                step_box(&st->bank_box, st->bank_boxes > 0 ? st->bank_boxes : 1, -1);
            else
                step_box(visible_box(st), visible_box_count(st), -1);
            return UI_BOX_NONE;
        }
        if (hit_rect(&ARR_R, t.px, t.py)) {
            if (st->browse)
                step_box(&st->bank_box, st->bank_boxes > 0 ? st->bank_boxes : 1, 1);
            else
                step_box(visible_box(st), visible_box_count(st), 1);
            return UI_BOX_NONE;
        }
        for (i = 0; i < UI_BOX_SLOTS; i++) {
            UiRect r;
            layout_pc_cell(i, &r);
            if (hit_rect(&r, t.px, t.py)) {
                if (st->browse || st->pane == 1)
                    st->bank_cur = i;
                else
                    st->save_cur = i;
                if (st->browse)
                    return UI_BOX_NONE;
                return slot_action(st);
            }
        }
    }
    return aptMainLoop() ? UI_BOX_NONE : UI_BOX_QUIT;
}

void ui_boxes_draw(const UiBoxView *st)
{
    const UiPoke *p = cursor_poke(st);
    const UiPoke *visible = (st->browse || st->pane) ? st->bank : st->save;
    int vis_cur = (st->browse || st->pane) ? st->bank_cur : st->save_cur;
    char top_title[32], bot_title[32], wrname[24];
    int show_pick = !st->browse && st->pane == 1;

    if (st->browse) {
        snprintf(top_title, sizeof(top_title), "Storage %d", st->bank_box + 1);
        snprintf(bot_title, sizeof(bot_title), "Storage %d", st->bank_box + 1);
    } else if (st->pane == 0) {
        snprintf(top_title, sizeof(top_title), "Storage %d", st->bank_box + 1);
        snprintf(bot_title, sizeof(bot_title), "Box %d", st->save_box + 1);
    } else {
        snprintf(top_title, sizeof(top_title), "Box %d", st->save_box + 1);
        snprintf(bot_title, sizeof(bot_title), "Storage %d", st->bank_box + 1);
    }
    if (st->edits > 0)
        snprintf(wrname, sizeof(wrname), "Write (%d)", st->edits);
    else
        snprintf(wrname, sizeof(wrname), "Write");

    frame_begin();
    C2D_TargetClear(g_top, COL_BG);
    C2D_SceneBegin(g_top);
    draw_pksm_bg(400, 240);
    C2D_DrawRectSolid(314, 4, 0, 82, 16, COL_TAG);
    draw_text(392, 5, 0.32f, COL_TEXT, C2D_AlignRight, "pocket");
    draw_lr_key(88, 8, "L");
    draw_banner(200, 6, 170, 20, top_title);
    draw_lr_key(296, 8, "R");
    draw_viewer_sprite(p);
    draw_summary(p);

    draw_bot_bg();
    C2D_DrawTriangle(ARR_L.x + 18, ARR_L.y + 4, COL_GOLD, ARR_L.x + 6, ARR_L.y + 11, COL_GOLD,
                     ARR_L.x + 18, ARR_L.y + 18, COL_GOLD, 0);
    draw_banner(110, 5, 130, 20, bot_title);
    C2D_DrawTriangle(ARR_R.x + 6, ARR_R.y + 4, COL_GOLD, ARR_R.x + 18, ARR_R.y + 11, COL_GOLD,
                     ARR_R.x + 6, ARR_R.y + 18, COL_GOLD, 0);
    draw_corner_braces(6, 28, 214, 164);
    draw_pc_grid(visible, vis_cur, st->picked_id, show_pick);
    if (st->browse) {
        draw_text_sh(226, 36, SCALE_SMALL, COL_MUTED, 0, "View only");
        draw_wrap(226, 56, 88, SCALE_SMALL, COL_TEXT,
                  st->bank_label[0] ? st->bank_label : "Bank");
        draw_wrap(226, 120, 88, SCALE_SMALL, COL_MUTED, "L/R box\nTap a mon\nB back");
    } else {
        draw_swap_btn();
        draw_side_btn(&BTN_DEP, "Deposit", st->mode == 0);
        draw_side_btn(&BTN_WIT, "Withdraw", st->mode == 1);
        draw_side_btn(&BTN_WR, wrname, st->edits > 0);
    }
    draw_wifi();
    draw_back_btn();
    frame_end();
}
