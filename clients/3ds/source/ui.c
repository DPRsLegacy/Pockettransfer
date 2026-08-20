#include "ui.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#define COL_BG C2D_Color32(11, 28, 44, 255)
#define COL_BG_BOT C2D_Color32(8, 22, 36, 255)
#define COL_HEADER C2D_Color32(14, 92, 92, 255)
#define COL_ACCENT C2D_Color32(46, 196, 182, 255)
#define COL_GOLD C2D_Color32(212, 175, 55, 255)
#define COL_CARD C2D_Color32(19, 42, 64, 255)
#define COL_BTN C2D_Color32(24, 54, 78, 255)
#define COL_BTN_SEL C2D_Color32(22, 86, 92, 255)
#define COL_TEXT C2D_Color32(244, 247, 250, 255)
#define COL_MUTED C2D_Color32(138, 164, 184, 255)
#define COL_SHADOW C2D_Color32(6, 14, 24, 255)
#define COL_EMPTY C2D_Color32(12, 28, 42, 255)
#define COL_SLOT C2D_Color32(28, 62, 86, 255)
#define COL_SLOT_SEL C2D_Color32(36, 110, 118, 255)
#define COL_PICKED C2D_Color32(56, 92, 48, 255)
#define COL_LOCKED C2D_Color32(48, 40, 44, 255)

#define Z 0.5f
#define SCALE_TITLE 0.55f
#define SCALE_BODY 0.45f
#define SCALE_SMALL 0.40f
#define SCALE_BTN 0.48f
#define SCALE_BTN_SM 0.40f

typedef struct {
    float x, y, w, h;
} UiRect;

static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;
static C2D_TextBuf g_buf;
static char g_host[64];
static char g_account[40];
static char g_status[96];

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

static void draw_top(const char *title, const char *body)
{
    C2D_TargetClear(g_top, COL_BG);
    C2D_SceneBegin(g_top);

    C2D_DrawRectSolid(0, 0, 0, 400, 36, COL_HEADER);
    C2D_DrawRectSolid(0, 36, 0, 400, 2, COL_GOLD);
    C2D_DrawRectSolid(10, 10, 0, 10, 16, COL_GOLD);
    C2D_DrawRectSolid(12, 12, 0, 6, 12, COL_HEADER);
    draw_text(26, 8, SCALE_TITLE, COL_TEXT, 0, "POCKETTRANSFER");
    draw_text(388, 10, SCALE_SMALL, COL_MUTED, C2D_AlignRight, "BANK");

    C2D_DrawRectSolid(12, 48, 0, 376, 78, COL_CARD);
    C2D_DrawRectSolid(12, 48, 0, 4, 78, COL_ACCENT);
    draw_text(24, 54, SCALE_SMALL, COL_MUTED, 0, "HOST");
    draw_text(100, 54, SCALE_BODY, COL_TEXT, 0, g_host[0] ? g_host : "—");
    draw_text(24, 74, SCALE_SMALL, COL_MUTED, 0, "ACCOUNT");
    draw_text(100, 74, SCALE_BODY, COL_TEXT, 0, g_account[0] ? g_account : "—");
    draw_text(24, 94, SCALE_SMALL, COL_MUTED, 0, "STATUS");
    draw_wrap(100, 94, 274, SCALE_BODY, COL_ACCENT, g_status[0] ? g_status : "—");

    if (title && title[0])
        draw_text(12, 136, SCALE_TITLE, COL_GOLD, 0, title);
    if (body && body[0])
        draw_wrap(12, 160, 376, SCALE_BODY, COL_TEXT, body);
}

static void draw_bot_bg(void)
{
    C2D_TargetClear(g_bot, COL_BG_BOT);
    C2D_SceneBegin(g_bot);
}

static void draw_footer(const char *hint)
{
    C2D_DrawRectSolid(0, 216, 0, 320, 24, COL_HEADER);
    C2D_DrawRectSolid(0, 216, 0, 320, 2, COL_GOLD);
    draw_centered(160, 220, 300, SCALE_SMALL, COL_TEXT, hint);
}

static void draw_button(const UiRect *r, const char *label, int selected)
{
    float scale = (r->h < 40.f || r->w < 150.f) ? SCALE_BTN_SM : SCALE_BTN;
    u32 fill = selected ? COL_BTN_SEL : COL_BTN;
    C2D_DrawRectSolid(r->x + 1, r->y + 2, 0, r->w, r->h, COL_SHADOW);
    C2D_DrawRectSolid(r->x, r->y, 0, r->w, r->h, fill);
    if (selected)
        C2D_DrawRectSolid(r->x, r->y, 0, 4, r->h, COL_ACCENT);
    draw_centered(r->x + r->w * 0.5f, r->y + r->h * 0.5f - 8.f, r->w - 16.f, scale, COL_TEXT,
                  label);
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
    g_buf = C2D_TextBufNew(8192);
    copy_str(g_host, sizeof(g_host), "bank.saltbox.cc");
    copy_str(g_account, sizeof(g_account), "Not signed in");
    copy_str(g_status, sizeof(g_status), "Starting");
}

void ui_exit(void)
{
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

static void outline_rect(const UiRect *r, u32 color)
{
    C2D_DrawRectSolid(r->x, r->y, 0, r->w, 2, color);
    C2D_DrawRectSolid(r->x, r->y + r->h - 2, 0, r->w, 2, color);
    C2D_DrawRectSolid(r->x, r->y, 0, 2, r->h, color);
    C2D_DrawRectSolid(r->x + r->w - 2, r->y, 0, 2, r->h, color);
}

static void layout_pc_cell(int screen_w, float origin_x, float origin_y, float cw, float ch,
                           float gap, int i, UiRect *r)
{
    int col = i % UI_BOX_COLS;
    int row = i / UI_BOX_COLS;
    (void)screen_w;
    r->x = origin_x + (float)col * (cw + gap);
    r->y = origin_y + (float)row * (ch + gap);
    r->w = cw;
    r->h = ch;
}

static const UiPoke *cursor_poke(const UiBoxView *st)
{
    if (st->pane == 1)
        return &st->bank[st->bank_cur];
    return &st->save[st->save_cur];
}

static void draw_pc_cell(const UiRect *r, const UiPoke *p, int selected, int picked)
{
    u32 fill = COL_EMPTY;
    if (p->locked)
        fill = COL_LOCKED;
    else if (picked)
        fill = COL_PICKED;
    else if (selected)
        fill = COL_SLOT_SEL;
    else if (p->occupied)
        fill = COL_SLOT;
    C2D_DrawRectSolid(r->x, r->y, 0, r->w, r->h, fill);
    if (selected)
        outline_rect(r, COL_ACCENT);
    else if (p->shiny)
        outline_rect(r, COL_GOLD);
    if (p->occupied)
        draw_centered(r->x + r->w * 0.5f, r->y + r->h * 0.5f - 6.f, r->w - 4.f, 0.28f, COL_TEXT,
                      p->name[0] ? p->name : "?");
}

static void draw_pc_grid(int screen_w, float ox, float oy, float cw, float ch, float gap,
                         const UiPoke *slots, int cursor, int picked_id, int show_pick)
{
    int i;
    for (i = 0; i < UI_BOX_SLOTS; i++) {
        UiRect r;
        int picked = show_pick && picked_id && slots[i].id == picked_id && slots[i].occupied;
        layout_pc_cell(screen_w, ox, oy, cw, ch, gap, i, &r);
        draw_pc_cell(&r, &slots[i], i == cursor, picked);
    }
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

static int *active_box(UiBoxView *st)
{
    return st->pane ? &st->bank_box : &st->save_box;
}

static int active_box_count(const UiBoxView *st)
{
    int n = st->pane ? st->bank_boxes : st->save_boxes;
    return n > 0 ? n : 1;
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
        int *box = active_box(st);
        *box = (*box + active_box_count(st) - 1) % active_box_count(st);
    }
    if (k & KEY_R) {
        int *box = active_box(st);
        *box = (*box + 1) % active_box_count(st);
    }
    if (k & KEY_Y)
        st->pane = st->pane ? 0 : 1;
    if (k & KEY_X) {
        st->mode = st->mode ? 0 : 1;
        st->picked_id = 0;
    }
    if (k & KEY_START)
        return st->dirty ? UI_BOX_WRITE : UI_BOX_BACK;
    if (k & KEY_B)
        return UI_BOX_BACK;
    if (k & KEY_A)
        return slot_action(st);

    if (k & KEY_TOUCH) {
        touchPosition t;
        UiRect dep = {8, 186, 98, 24};
        UiRect wit = {111, 186, 98, 24};
        UiRect wr = {214, 186, 98, 24};
        int i;
        hidTouchRead(&t);
        if (hit_rect(&dep, t.px, t.py)) {
            st->mode = 0;
            st->picked_id = 0;
            return UI_BOX_NONE;
        }
        if (hit_rect(&wit, t.px, t.py)) {
            st->mode = 1;
            return UI_BOX_NONE;
        }
        if (hit_rect(&wr, t.px, t.py))
            return UI_BOX_WRITE;
        for (i = 0; i < UI_BOX_SLOTS; i++) {
            UiRect r;
            layout_pc_cell(320, 8, 24, 48, 28, 2, i, &r);
            if (hit_rect(&r, t.px, t.py)) {
                st->pane = 0;
                st->save_cur = i;
                return slot_action(st);
            }
        }
    }
    return aptMainLoop() ? UI_BOX_NONE : UI_BOX_QUIT;
}

void ui_boxes_draw(const UiBoxView *st)
{
    const UiPoke *p = cursor_poke(st);
    char hdr[80], detail[128], hint[96], wrname[24];
    UiRect dep = {8, 186, 98, 24};
    UiRect wit = {111, 186, 98, 24};
    UiRect wr = {214, 186, 98, 24};

    snprintf(hdr, sizeof(hdr), "BANK  %s", st->bank_label);
    if (p->occupied)
        snprintf(detail, sizeof(detail), "%s  Lv.%d%s%s", p->name[0] ? p->name : "Pokemon",
                 p->level, p->shiny ? "  shiny" : "", p->locked ? "  locked" : "");
    else if (st->mode == 1 && st->picked_id)
        snprintf(detail, sizeof(detail), "Place the bank Pokemon in an empty save slot.");
    else
        snprintf(detail, sizeof(detail),
                 st->mode == 0 ? "Deposit: tap a save Pokemon. Keep going, then Write."
                               : "Withdraw: pick bank, then empty save slot. Write when done.");

    if (st->edits > 0)
        snprintf(hint, sizeof(hint), "%d change%s  ·  Write when done  ·  B stay/leave",
                 st->edits, st->edits == 1 ? "" : "s");
    else
        snprintf(hint, sizeof(hint), "%s  Y pane  X mode  L/R box",
                 st->mode == 0 ? "A deposit" : (st->picked_id ? "A place" : "A pick"));

    if (st->edits > 0)
        snprintf(wrname, sizeof(wrname), "Write (%d)", st->edits);
    else
        snprintf(wrname, sizeof(wrname), "Done");

    frame_begin();
    C2D_TargetClear(g_top, COL_BG);
    C2D_SceneBegin(g_top);
    C2D_DrawRectSolid(0, 0, 0, 400, 22, COL_HEADER);
    C2D_DrawRectSolid(0, 22, 0, 400, 2, COL_GOLD);
    draw_text(8, 3, SCALE_SMALL, st->pane == 1 ? COL_GOLD : COL_TEXT, 0, hdr);
    draw_pc_grid(400, 8, 30, 48, 30, 3, st->bank, st->pane == 1 ? st->bank_cur : -1, st->picked_id, 1);
    C2D_DrawRectSolid(0, 198, 0, 400, 42, COL_CARD);
    draw_wrap(10, 204, 380, SCALE_BODY, COL_TEXT, detail);

    draw_bot_bg();
    snprintf(hdr, sizeof(hdr), "SAVE  %s", st->save_label);
    C2D_DrawRectSolid(0, 0, 0, 320, 22, COL_HEADER);
    C2D_DrawRectSolid(0, 22, 0, 320, 2, COL_GOLD);
    draw_text(8, 3, SCALE_SMALL, st->pane == 0 ? COL_GOLD : COL_TEXT, 0, hdr);
    draw_pc_grid(320, 8, 24, 48, 28, 2, st->save, st->pane == 0 ? st->save_cur : -1, 0, 0);
    draw_button(&dep, "Deposit", st->mode == 0);
    draw_button(&wit, "Withdraw", st->mode == 1);
    draw_button(&wr, wrname, st->edits > 0);
    draw_footer(hint);
    frame_end();
}
