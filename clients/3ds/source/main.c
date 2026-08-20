#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "games.h"
#include "http.h"
#include "jsonutil.h"
#include "lite_check.h"
#include "log.h"

#define CONFIG_DIR "sdmc:/3ds/pockettransfer"
#define CONFIG_PATH CONFIG_DIR "/config.json"
#define BACKUP_DIR CONFIG_DIR "/backups"
#define PT_HOST "https://bank.saltbox.cc"

typedef struct {
    char host[256];
    char token[160];
} Config;

static Config g_cfg;
static PrintConsole bottom;
static u32 *g_soc = NULL;
static int g_have_soc;
static int g_have_ac;
static int g_have_am;

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

static void wait_a(void);
static int pick_items(const char *title, const char **items, int count);

static void ensure_dirs(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(CONFIG_DIR, 0777);
    mkdir(BACKUP_DIR, 0777);
}

static void trim_inplace(char *s)
{
    size_t i = 0, n;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')
        i++;
    if (i)
        memmove(s, s + i, strlen(s + i) + 1);
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

static void load_config(void)
{
    FILE *f = fopen(CONFIG_PATH, "rb");
    char buf[1024];
    size_t n;
    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.host, sizeof(g_cfg.host), "%s", PT_HOST);
    if (!f)
        return;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    json_get_string(buf, "token", g_cfg.token, sizeof(g_cfg.token));
    trim_inplace(g_cfg.token);
    snprintf(g_cfg.host, sizeof(g_cfg.host), "%s", PT_HOST);
}

static void save_config(void)
{
    FILE *f;
    ensure_dirs();
    f = fopen(CONFIG_PATH, "wb");
    if (!f)
        return;
    fprintf(f, "{\"host\":\"%s\",\"token\":\"%s\"}\n", g_cfg.host, g_cfg.token);
    fclose(f);
}

static void present(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

static void init_video(void)
{
    /* consoleInit switches the screen to RGB565 and turns double buffering
       off. Only use the bottom screen; two consoles + custom swap was
       presenting an empty buffer (solid black). */
    consoleInit(GFX_BOTTOM, &bottom);
    consoleSelect(&bottom);
}

static int init_net(void)
{
    Result rc;
    u32 wifi = 0;
    int i;

    g_soc = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!g_soc) {
        pt_log("soc memalign fail");
        return -1;
    }
    rc = socInit(g_soc, SOC_BUFFERSIZE);
    pt_log("socInit 0x%08lx", (unsigned long)rc);
    if (R_FAILED(rc)) {
        free(g_soc);
        g_soc = NULL;
        return -1;
    }
    g_have_soc = 1;

    rc = acInit();
    pt_log("acInit 0x%08lx", (unsigned long)rc);
    if (R_SUCCEEDED(rc))
        g_have_ac = 1;
    for (i = 0; i < 80; i++) {
        wifi = 0;
        if (R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi) {
            pt_log("wifi status=%lu", (unsigned long)wifi);
            return 0;
        }
        svcSleepThread(100000000LL);
    }
    pt_log("wifi not ready status=%lu (continuing)", (unsigned long)wifi);
    return 0;
}

static void log_3ds_clock(void)
{
    u64 ms = osGetTime();
    u64 sec = ms / 1000ULL;
    /* osGetTime is ms since 1900-01-01; Unix epoch is 2208988800s later. */
    u64 unix_sec = (sec > 2208988800ULL) ? (sec - 2208988800ULL) : 0;
    int year = 1970;
    u64 days = unix_sec / 86400ULL;
    while (days >= 365ULL) {
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        u64 ylen = leap ? 366ULL : 365ULL;
        if (days < ylen)
            break;
        days -= ylen;
        year++;
        if (year > 2100)
            break;
    }
    pt_log("clock unix=%llu approx_year=%d", (unsigned long long)unix_sec, year);
}

static void wait_a(void)
{
    printf("Press A to continue.\n");
    present();
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_A)
            break;
        present();
    }
}

static int url_join(char *out, size_t n, const char *path)
{
    size_t hlen = strlen(g_cfg.host);
    if (hlen && g_cfg.host[hlen - 1] == '/')
        g_cfg.host[hlen - 1] = 0;
    return snprintf(out, n, "%s%s", g_cfg.host, path);
}

static Result open_save_media(u64 title, FS_MediaType media, FS_Archive *arch)
{
    u32 path[3] = { media, (u32)title, (u32)(title >> 32) };
    Result rc = FSUSER_OpenArchive(arch, ARCHIVE_USER_SAVEDATA,
                                   (FS_Path){ PATH_BINARY, sizeof(path), path });
    pt_log("save open tid=%016llx media=%u user=0x%08lx",
           (unsigned long long)title, (unsigned)media, (unsigned long)rc);
    if (R_SUCCEEDED(rc))
        return rc;
    if (media != MEDIATYPE_GAME_CARD)
        return rc;
    rc = FSUSER_OpenArchive(arch, ARCHIVE_GAMECARD_SAVEDATA, fsMakePath(PATH_EMPTY, ""));
    pt_log("save open cart-generic=0x%08lx", (unsigned long)rc);
    return rc;
}

static int probe_save(u64 title, FS_MediaType media)
{
    FS_Archive arch;
    Result rc = open_save_media(title, media, &arch);
    if (R_FAILED(rc))
        return 0;
    FSUSER_CloseArchive(arch);
    return 1;
}

static u64 cart_title_id(void)
{
    u32 count = 0, nread = 0, i, j;
    u64 tids[8];
    bool inserted = false;
    Result rc = FSUSER_CardSlotIsInserted(&inserted);
    pt_log("cart slot rc=0x%08lx inserted=%d", (unsigned long)rc, inserted ? 1 : 0);
    if (R_SUCCEEDED(rc) && !inserted)
        return 0;
    memset(tids, 0, sizeof(tids));
    rc = AM_GetTitleCount(MEDIATYPE_GAME_CARD, &count);
    pt_log("cart titlecount rc=0x%08lx n=%lu", (unsigned long)rc, (unsigned long)count);
    if (R_FAILED(rc) || count == 0)
        return 0;
    if (count > 8)
        count = 8;
    rc = AM_GetTitleList(&nread, MEDIATYPE_GAME_CARD, count, tids);
    pt_log("cart list rc=0x%08lx nread=%lu", (unsigned long)rc, (unsigned long)nread);
    if (R_FAILED(rc) || nread == 0)
        return 0;
    for (i = 0; i < nread; i++) {
        pt_log("cart tid[%lu]=%016llx", (unsigned long)i, (unsigned long long)tids[i]);
        for (j = 0; j < (u32)PT_GAME_COUNT; j++) {
            if (PT_GAMES[j].title_id_u64 == tids[i])
                return tids[i];
        }
    }
    return tids[0];
}

static const char *game_name_for_tid(u64 tid)
{
    int i;
    for (i = 0; i < PT_GAME_COUNT; i++) {
        if (PT_GAMES[i].title_id_u64 == tid)
            return PT_GAMES[i].name;
    }
    return NULL;
}

static int select_save_media(int gi, FS_MediaType *out)
{
    u64 want = PT_GAMES[gi].title_id_u64;
    u64 cart = cart_title_id();
    const char *cname = game_name_for_tid(cart);
    const char *items[2];
    FS_MediaType map[2];
    int n = 0, choice;
    int sd_ok = probe_save(want, MEDIATYPE_SD);
    int cart_ok = (cart == want);

    if (sd_ok) {
        items[n] = "SD card (installed title)";
        map[n++] = MEDIATYPE_SD;
    }
    if (cart_ok) {
        items[n] = "Game cart";
        map[n++] = MEDIATYPE_GAME_CARD;
    }

    if (n == 0) {
        consoleSelect(&bottom);
        consoleClear();
        printf("No save for %s.\n", PT_GAMES[gi].name);
        if (cart && cart != want)
            printf("Game cart is %s.\n", cname ? cname : "another title");
        else if (!cart)
            printf("No game cart detected.\n");
        printf("Install the game on SD or insert the cart,\nthen save once in-game.\n");
        wait_a();
        return -1;
    }
    if (n == 1) {
        *out = map[0];
        printf("Using %s from %s.\n", PT_GAMES[gi].name,
               map[0] == MEDIATYPE_GAME_CARD ? "game cart" : "SD");
        return 0;
    }
    choice = pick_items("Load save from", items, n);
    if (choice < 0)
        return -1;
    *out = map[choice];
    return 0;
}

static int read_save_file(FS_Archive arch, const char *name, uint8_t **out, u64 *out_size)
{
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arch, fsMakePath(PATH_ASCII, name), FS_OPEN_READ, 0);
    u32 got = 0;
    if (R_FAILED(rc))
        return -1;
    FSFILE_GetSize(h, out_size);
    *out = malloc((size_t)*out_size);
    if (!*out) {
        FSFILE_Close(h);
        return -1;
    }
    while (got < (u32)*out_size) {
        u32 chunk = 0;
        u32 want = (u32)*out_size - got;
        if (want > 0x1000)
            want = 0x1000;
        rc = FSFILE_Read(h, &chunk, got, *out + got, want);
        if (R_FAILED(rc) || chunk == 0)
            break;
        got += chunk;
    }
    FSFILE_Close(h);
    pt_log("save read rc=0x%08lx got=%lu/%lu", (unsigned long)rc,
           (unsigned long)got, (unsigned long)*out_size);
    if (R_FAILED(rc) || got != (u32)*out_size) {
        free(*out);
        *out = NULL;
        return -1;
    }
    return 0;
}

/* PKSM: delete NAND anti-savegame-restore value or the game hangs on the 3DS logo.
   3dsx/Rosalina already has full FS; CIA needs SeedDB/CardBoard and we wipe every
   slot packing PKSM and Checkpoint use. */
static Result delete_secure_value_one(u32 slot, u32 packed, u8 *existed)
{
    u64 in = ((u64)slot << 32) | packed;
    *existed = 0;
    return FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &in, sizeof(in), existed, 1);
}

static Result delete_secure_value(u64 title)
{
    u32 low = (u32)title;
    u32 unique = (u32)((title >> 8) & 0xFFFFF);
    u8 variation = (u8)(title & 0xFF);
    u32 packed_pksm = low & 0xFFFFFF00u;
    u32 packed_full = low & 0xFFFFFFu;
    u32 slots[2] = { SECUREVALUE_SLOT_SD, 0 };
    u32 packings[2] = { packed_pksm, packed_full };
    Result last = 0;
    int s, p;
    bool found = false;

    {
        bool exists = false;
        u64 val = 0;
        Result grc = FSUSER_GetSaveDataSecureValue(&exists, &val, SECUREVALUE_SLOT_SD, unique,
                                                   variation);
        pt_log("secure get sd rc=0x%08lx exists=%d val=%llu", (unsigned long)grc, exists ? 1 : 0,
               (unsigned long long)val);
        grc = FSUSER_GetSaveDataSecureValue(&exists, &val, 0, unique, variation);
        pt_log("secure get slot0 rc=0x%08lx exists=%d val=%llu", (unsigned long)grc, exists ? 1 : 0,
               (unsigned long long)val);
    }

    for (s = 0; s < 2; s++) {
        for (p = 0; p < 2; p++) {
            u8 existed = 0;
            last = delete_secure_value_one(slots[s], packings[p], &existed);
            pt_log("secure delete slot=0x%lx pack=0x%08lx rc=0x%08lx existed=%u",
                   (unsigned long)slots[s], (unsigned long)packings[p], (unsigned long)last,
                   (unsigned)existed);
            if (existed)
                found = true;
        }
    }
    pt_log("secure value found=%d tid=%016llx", found ? 1 : 0, (unsigned long long)title);
    return last;
}

static int write_save_file(FS_Archive arch, const char *name, const uint8_t *data, u64 size,
                           u64 title)
{
    Handle h;
    u32 got = 0;
    Result rc = FSUSER_OpenFile(&h, arch, fsMakePath(PATH_ASCII, name), FS_OPEN_WRITE, 0);
    if (R_FAILED(rc)) {
        pt_log("save write open 0x%08lx", (unsigned long)rc);
        return -1;
    }
    while (got < (u32)size) {
        u32 chunk = 0;
        u32 want = (u32)size - got;
        if (want > 0x1000)
            want = 0x1000;
        rc = FSFILE_Write(h, &chunk, got, data + got, want, FS_WRITE_FLUSH);
        if (R_FAILED(rc) || chunk == 0)
            break;
        got += chunk;
    }
    pt_log("save write rc=0x%08lx wrote=%lu/%lu", (unsigned long)rc, (unsigned long)got,
           (unsigned long)size);
    if (R_FAILED(rc) || got != (u32)size) {
        FSFILE_Close(h);
        return -1;
    }
    rc = FSUSER_ControlArchive(arch, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    pt_log("save commit 0x%08lx", (unsigned long)rc);
    FSFILE_Close(h);
    if (R_FAILED(rc))
        return -1;
    rc = delete_secure_value(title);
    if (R_FAILED(rc))
        printf("Anti-restore clear failed (0x%08lx).\n", (unsigned long)rc);
    return 0;
}

static void backup_bytes(const char *id, const uint8_t *data, u64 size)
{
    char path[256];
    FILE *f;
    ensure_dirs();
    snprintf(path, sizeof(path), BACKUP_DIR "/%s.main.bak", id);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(data, 1, (size_t)size, f);
    fclose(f);
}

static int apply_token(const char *token)
{
    if (!token || !token[0])
        return -1;
    snprintf(g_cfg.token, sizeof(g_cfg.token), "%s", token);
    pt_http_set_token(g_cfg.token);
    save_config();
    pt_log("token saved");
    return 0;
}

static int enroll_device(void)
{
    char url[320], token[160], err[256];
    HttpBuffer resp = {0};
    long status = 0;
    url_join(url, sizeof(url), "/api/auth/devices/enroll");
    if (pt_http_request("POST", url, "{\"name\":\"3DS\",\"platform\":\"3ds\"}",
                        "application/json", &resp, &status) != 0) {
        printf("HTTP error talking to %s\n", PT_HOST);
        http_buffer_free(&resp);
        return -1;
    }
    if (status != 200 || !json_get_string(resp.data ? resp.data : "", "token", token, sizeof(token))) {
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Enroll failed (%ld): %s\n", status, err[0] ? err : "no token");
        http_buffer_free(&resp);
        return -1;
    }
    apply_token(token);
    printf("Paired. Token saved.\n");
    http_buffer_free(&resp);
    return 0;
}

static int pair_with_code_file(void)
{
    char code[16] = {0};
    char url[320], body[128], token[160], err[256];
    HttpBuffer resp = {0};
    long status = 0;
    FILE *f = fopen(CONFIG_DIR "/pair.txt", "rb");
    if (!f)
        return 1;
    fread(code, 1, sizeof(code) - 1, f);
    fclose(f);
    trim_inplace(code);
    if (!code[0])
        return 1;
    snprintf(body, sizeof(body), "{\"code\":\"%s\",\"name\":\"3DS\",\"platform\":\"3ds\"}", code);
    url_join(url, sizeof(url), "/api/auth/devices/pair");
    if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0) {
        printf("HTTP error\n");
        http_buffer_free(&resp);
        return -1;
    }
    if (status != 200 || !json_get_string(resp.data ? resp.data : "", "token", token, sizeof(token))) {
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Pair failed (%ld): %s\n", status, err);
        http_buffer_free(&resp);
        return -1;
    }
    apply_token(token);
    printf("Paired from pair.txt. Token saved.\n");
    http_buffer_free(&resp);
    return 0;
}

static void restore_consoles(void)
{
    init_video();
}

static int prompt_text(const char *hint, char *out, size_t n, int password, int max_len)
{
    SwkbdState swkbd;
    SwkbdButton button;
    memset(out, 0, n);
    swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, max_len);
    swkbdSetHintText(&swkbd, hint);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetFeatures(&swkbd, SWKBD_DARKEN_TOP_SCREEN | SWKBD_DEFAULT_QWERTY);
    if (password)
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE);
    button = swkbdInputText(&swkbd, out, n);
    restore_consoles();
    trim_inplace(out);
    return (button == SWKBD_BUTTON_CONFIRM && out[0]) ? 0 : -1;
}

static int pick_items(const char *title, const char **items, int count)
{
    int sel = 0, i, drawn = -1;
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (sel != drawn) {
            consoleSelect(&bottom);
            consoleClear();
            printf("%s\n\n", title);
            for (i = 0; i < count; i++)
                printf("%c %s\n", i == sel ? '>' : ' ', items[i]);
            drawn = sel;
        }
        if (k & KEY_DUP)
            sel = (sel + count - 1) % count;
        if (k & KEY_DDOWN)
            sel = (sel + 1) % count;
        if (k & KEY_B)
            return -1;
        if (k & KEY_A)
            return sel;
        present();
    }
    return -1;
}

static int auth_request(int is_register)
{
    char user[40] = {0}, pass[72] = {0}, pass2[72] = {0};
    char body[400], url[320], token[160], err[256], username[40];
    HttpBuffer resp = {0};
    long status = 0;

    consoleSelect(&bottom);
    consoleClear();
    printf("%s on %s\nSystem keyboard next.\n", is_register ? "Create account" : "Log in", PT_HOST);

    if (prompt_text("Username (3-32: a-z, 0-9, _)", user, sizeof(user), 0, 32) != 0)
        return -1;
    if (prompt_text(is_register ? "New password (8+ characters)" : "Password",
                    pass, sizeof(pass), 1, 64) != 0)
        return -1;
    if (is_register) {
        if (prompt_text("Confirm password", pass2, sizeof(pass2), 1, 64) != 0)
            return -1;
        if (strcmp(pass, pass2) != 0) {
            consoleSelect(&bottom);
            consoleClear();
            printf("Passwords do not match.\n");
            return -1;
        }
        if (strlen(pass) < 8) {
            consoleSelect(&bottom);
            consoleClear();
            printf("Password must be at least 8 characters.\n");
            return -1;
        }
    }

    if (!json_auth_body(body, sizeof(body), user, pass, "3DS", "3ds")) {
        printf("Could not build request.\n");
        return -1;
    }
    url_join(url, sizeof(url), is_register ? "/api/auth/register" : "/api/auth/login");
    consoleSelect(&bottom);
    consoleClear();
    printf("%s...\n", is_register ? "Creating account" : "Logging in");
    if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0) {
        printf("HTTP/TLS error talking to %s\n", PT_HOST);
        printf("If the log says BADCERT_FUTURE, set the 3DS\ndate in System Settings to today.\n");
        printf("See sdmc:/pockettransfer.log\n");
        http_buffer_free(&resp);
        return -1;
    }
    if (status != 200 || !json_get_string(resp.data ? resp.data : "", "token", token, sizeof(token))) {
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Failed (%ld): %s\n", status, err[0] ? err : "no token");
        http_buffer_free(&resp);
        return -1;
    }
    apply_token(token);
    if (!json_get_string(resp.data, "username", username, sizeof(username)))
        snprintf(username, sizeof(username), "%s", user);
    printf("Signed in as %s.\nConsole paired. Token saved.\n", username);
    http_buffer_free(&resp);
    return 0;
}

static int account_flow(void)
{
    const char *items[] = {"Create account", "Log in", "Pair from website", "Cancel"};
    int choice;
    while (aptMainLoop()) {
        choice = pick_items("Pockettransfer account\nHost: " PT_HOST, items, 4);
        pt_log("account menu choice=%d", choice);
        if (choice < 0 || choice == 3)
            return g_cfg.token[0] ? 0 : -1;
        consoleSelect(&bottom);
        consoleClear();
        if (choice == 0 || choice == 1) {
            if (auth_request(choice == 0) == 0) {
                wait_a();
                return 0;
            }
            wait_a();
            continue;
        }
        printf("Pairing via website...\n");
        if (pair_with_code_file() == 0 || enroll_device() == 0) {
            wait_a();
            return 0;
        }
        printf("\nGenerate a pairing code on the website, then retry.\n");
        wait_a();
    }
    return g_cfg.token[0] ? 0 : -1;
}

static int select_game(int platform_3ds)
{
    int i, sel = 0, count = 0, drawn = -1;
    int idx[PT_GAME_COUNT];
    for (i = 0; i < PT_GAME_COUNT; i++) {
        if ((platform_3ds && strcmp(PT_GAMES[i].platform, "3ds") == 0) ||
            (!platform_3ds && strcmp(PT_GAMES[i].platform, "switch") == 0))
            idx[count++] = i;
    }
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (sel != drawn) {
            consoleSelect(&bottom);
            consoleClear();
            printf("Select game (D-Pad, A confirm, B back)\n\n");
            for (i = 0; i < count; i++)
                printf("%c %s\n", i == sel ? '>' : ' ', PT_GAMES[idx[i]].name);
            drawn = sel;
        }
        if (k & KEY_DUP)
            sel = (sel + count - 1) % count;
        if (k & KEY_DDOWN)
            sel = (sel + 1) % count;
        if (k & KEY_B)
            return -1;
        if (k & KEY_A)
            return idx[sel];
        present();
    }
    return -1;
}

static void transfer_flow(void)
{
    int gi = select_game(1);
    FS_Archive arch;
    FS_MediaType media;
    uint8_t *save = NULL;
    u64 save_size = 0;
    char url[400], session[80], err[256], save_path[32];
    HttpBuffer resp = {0};
    long status = 0;
    int box = 0, slot = 0, pokemon_id = 0;
    char body[256];
    uint8_t *patched = NULL;

    if (gi < 0)
        return;
    if (select_save_media(gi, &media) != 0)
        return;
    snprintf(save_path, sizeof(save_path), "/%s", PT_GAMES[gi].primary_save);
    consoleSelect(&bottom);
    consoleClear();
    printf("Opening %s (%s)...\n", PT_GAMES[gi].name,
           media == MEDIATYPE_GAME_CARD ? "game cart" : "SD");
    if (R_FAILED(open_save_media(PT_GAMES[gi].title_id_u64, media, &arch))) {
        printf("Could not mount save.\nInsert the cart or install the title,\nthen save once in-game.\n");
        pt_log("save mount fail %s media=%u", PT_GAMES[gi].id, (unsigned)media);
        wait_a();
        return;
    }
    if (read_save_file(arch, save_path, &save, &save_size) != 0) {
        printf("Failed to read %s\n", save_path);
        FSUSER_CloseArchive(arch);
        wait_a();
        return;
    }
    backup_bytes(PT_GAMES[gi].id, save, save_size);
    printf("Backed up %llu bytes. Uploading...\n", (unsigned long long)save_size);
    url_join(url, sizeof(url), "/api/saves/session");
    if (pt_http_upload(url, "file", "main", save, (size_t)save_size, &resp, &status) != 0 || status != 200) {
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Upload failed (%ld) %s\n", status, err);
        free(save);
        http_buffer_free(&resp);
        FSUSER_CloseArchive(arch);
        wait_a();
        return;
    }
    if (!json_get_string(resp.data, "sessionId", session, sizeof(session))) {
        printf("No sessionId in response\n");
        free(save);
        http_buffer_free(&resp);
        FSUSER_CloseArchive(arch);
        wait_a();
        return;
    }
    http_buffer_free(&resp);
    printf("Session %s\nDeposit: set box/slot then A\nWithdraw: write pokemonId to withdraw.txt, X\nStart: download patched save\nB: cancel\n", session);
    printf("Default box 0 slot 0. Edit sdmc:/3ds/pockettransfer/slot.txt as box,slot[,pokemonId]\n");
    wait_a();
    {
        FILE *sf = fopen(CONFIG_DIR "/slot.txt", "rb");
        if (sf) {
            fscanf(sf, "%d,%d,%d", &box, &slot, &pokemon_id);
            fclose(sf);
        }
    }
    hidScanInput();
    /* deposit then user can withdraw; we offer deposit if pokemon_id==0 */
    if (pokemon_id == 0) {
        snprintf(body, sizeof(body),
                 "{\"sessionId\":\"%s\",\"box\":%d,\"slot\":%d}", session, box, slot);
        url_join(url, sizeof(url), "/api/bank/deposit");
        if (pt_http_request("POST", url, body, "application/json", &resp, &status) == 0 && status == 200) {
            printf("Deposited from box %d slot %d.\n", box, slot);
        } else {
            json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
            printf("Deposit skipped/failed: %s\n", err);
        }
        http_buffer_free(&resp);
    } else {
        snprintf(body, sizeof(body),
                 "{\"sessionId\":\"%s\",\"pokemonId\":%d,\"box\":%d,\"slot\":%d}",
                 session, pokemon_id, box, slot);
        url_join(url, sizeof(url), "/api/bank/withdraw");
        if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0 || status != 200) {
            json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
            printf("Withdraw failed: %s\n", err);
            http_buffer_free(&resp);
            free(save);
            FSUSER_CloseArchive(arch);
            wait_a();
            return;
        }
        http_buffer_free(&resp);
        printf("Withdrew pokemon %d.\n", pokemon_id);
    }

    snprintf(url, sizeof(url), "%s/api/saves/%s/file", g_cfg.host, session);
    if (pt_http_request("GET", url, NULL, NULL, &resp, &status) != 0 || status != 200 || !resp.data) {
        printf("Failed to download patched save\n");
        free(save);
        http_buffer_free(&resp);
        FSUSER_CloseArchive(arch);
        wait_a();
        return;
    }
    patched = (uint8_t *)resp.data;
    if (lite_is_known_size(resp.len)) {
        char reason[64];
        if (!lite_validate_pkm(patched, resp.len, reason, sizeof(reason)))
            printf("Note: response is not a raw PKM (%s); treating as full save.\n", reason);
    }
    printf("Write patched save? A=yes B=no\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) {
            printf("Aborted write.\n");
            break;
        }
        if (hidKeysDown() & KEY_A) {
            if (write_save_file(arch, save_path, patched, resp.len,
                                PT_GAMES[gi].title_id_u64) == 0)
                printf("Save written to %s. Close the game if it was open.\n",
                       media == MEDIATYPE_GAME_CARD ? "game cart" : "SD");
            else
                printf("Write failed.\n");
            break;
        }
        gspWaitForVBlank();
    }
    free(save);
    http_buffer_free(&resp);
    FSUSER_CloseArchive(arch);
    wait_a();
}

int main(int argc, char **argv)
{
    int sel = 0, drawn = -1;
    gfxInitDefault();
    init_video();
    fsInit();
    aptInit();
    g_have_am = R_SUCCEEDED(amInit());
    romfsInit();
    pt_log_init("3ds");
    pt_log("entry %s", envIsHomebrew() ? "3dsx" : "cia");
    pt_log("amInit %s", g_have_am ? "ok" : "fail");
    log_3ds_clock();
    {
        FILE *ca = fopen("romfs:/cacert.pem", "rb");
        pt_log("cacert %s", ca ? "ok" : "MISSING");
        if (ca)
            fclose(ca);
    }
    if (init_net() != 0) {
        consoleSelect(&bottom);
        consoleClear();
        printf("Wi-Fi/socket init failed.\nCheck 3DS internet settings.\nSee sdmc:/pockettransfer.log\n");
        wait_a();
    }
    ensure_dirs();
    load_config();
    pt_log("config token=%s host=%s", g_cfg.token[0] ? "yes" : "no", g_cfg.host);
    pt_http_init("romfs:/cacert.pem");
    if (g_cfg.token[0])
        pt_http_set_token(g_cfg.token);
    else if (account_flow() != 0)
        goto shutdown;

    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (sel != drawn) {
            consoleSelect(&bottom);
            consoleClear();
            printf("Pockettransfer (3DS)\nHost: %s\nToken: %s\n\n", g_cfg.host,
                   g_cfg.token[0] ? "(saved)" : "(none)");
            printf("%c Deposit/withdraw\n%c Account\n%c Quit\n",
                   sel == 0 ? '>' : ' ', sel == 1 ? '>' : ' ', sel == 2 ? '>' : ' ');
            drawn = sel;
        }
        if (k & KEY_DUP)
            sel = (sel + 2) % 3;
        if (k & KEY_DDOWN)
            sel = (sel + 1) % 3;
        if (k & KEY_START || (k & KEY_A && sel == 2))
            break;
        if (k & KEY_A && sel == 0) {
            transfer_flow();
            drawn = -1;
        }
        if (k & KEY_A && sel == 1) {
            account_flow();
            drawn = -1;
        }
        present();
    }

shutdown:
    pt_log_shutdown();
    pt_http_shutdown();
    if (g_have_soc)
        socExit();
    if (g_have_ac)
        acExit();
    if (g_soc) {
        free(g_soc);
        g_soc = NULL;
    }
    romfsExit();
    if (g_have_am)
        amExit();
    fsExit();
    aptExit();
    gfxExit();
    return 0;
}
