#include <3ds.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "games.h"
#include "http.h"
#include "jsonutil.h"
#include "lite_check.h"

#define CONFIG_DIR "sdmc:/3ds/pockettransfer"
#define CONFIG_PATH CONFIG_DIR "/config.json"
#define BACKUP_DIR CONFIG_DIR "/backups"

typedef struct {
    char host[256];
    char token[160];
} Config;

static Config g_cfg;
static PrintConsole top, bottom;

static void ensure_dirs(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(CONFIG_DIR, 0777);
    mkdir(BACKUP_DIR, 0777);
}

static void load_config(void)
{
    FILE *f = fopen(CONFIG_PATH, "rb");
    char buf[1024];
    size_t n;
    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.host, sizeof(g_cfg.host), "https://bank.saltbox.cc");
    if (!f)
        return;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    json_get_string(buf, "host", g_cfg.host, sizeof(g_cfg.host));
    json_get_string(buf, "token", g_cfg.token, sizeof(g_cfg.token));
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

static void wait_a(void)
{
    printf("Press A to continue.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_A)
            break;
        gspWaitForVBlank();
    }
}

static int url_join(char *out, size_t n, const char *path)
{
    size_t hlen = strlen(g_cfg.host);
    if (hlen && g_cfg.host[hlen - 1] == '/')
        g_cfg.host[hlen - 1] = 0;
    return snprintf(out, n, "%s%s", g_cfg.host, path);
}

static Result open_save(u64 title, FS_Archive *arch)
{
    u32 path_sd[3] = {MEDIATYPE_SD, (u32)title, (u32)(title >> 32)};
    u32 path_card[3] = {MEDIATYPE_GAME_CARD, (u32)title, (u32)(title >> 32)};
    Result rc = FSUSER_OpenArchive(arch, ARCHIVE_USER_SAVEDATA,
                                   (FS_Path){PATH_BINARY, sizeof(path_sd), path_sd});
    if (R_SUCCEEDED(rc))
        return rc;
    return FSUSER_OpenArchive(arch, ARCHIVE_USER_SAVEDATA,
                              (FS_Path){PATH_BINARY, sizeof(path_card), path_card});
}

static int read_save_file(FS_Archive arch, const char *name, uint8_t **out, u64 *out_size)
{
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arch, fsMakePath(PATH_ASCII, name), FS_OPEN_READ, 0);
    u32 read = 0;
    if (R_FAILED(rc))
        return -1;
    FSFILE_GetSize(h, out_size);
    *out = malloc((size_t)*out_size);
    if (!*out) {
        FSFILE_Close(h);
        return -1;
    }
    rc = FSFILE_Read(h, &read, 0, *out, (u32)*out_size);
    FSFILE_Close(h);
    return R_FAILED(rc) ? -1 : 0;
}

static int write_save_file(FS_Archive arch, const char *name, const uint8_t *data, u64 size)
{
    Handle h;
    u32 written = 0;
    Result rc = FSUSER_OpenFile(&h, arch, fsMakePath(PATH_ASCII, name),
                                FS_OPEN_WRITE | FS_OPEN_CREATE, 0);
    if (R_FAILED(rc))
        return -1;
    FSFILE_SetSize(h, size);
    rc = FSFILE_Write(h, &written, 0, data, (u32)size, FS_WRITE_FLUSH);
    FSFILE_Close(h);
    return R_FAILED(rc) ? -1 : 0;
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

static void pair_device(void)
{
    char code[16] = {0};
    char url[320], body[128], token[160], err[256];
    HttpBuffer resp = {0};
    long status = 0;
    consoleSelect(&bottom);
    consoleClear();
    printf("Enter 8-character pairing code from the website.\n");
    /* Homebrew has no full keyboard here: read from config overlay file. */
    printf("Write the code into:\n  sdmc:/3ds/pockettransfer/pair.txt\nthen press A.\n");
    wait_a();
    FILE *f = fopen(CONFIG_DIR "/pair.txt", "rb");
    if (!f) {
        printf("pair.txt missing.\n");
        wait_a();
        return;
    }
    fread(code, 1, sizeof(code) - 1, f);
    fclose(f);
    snprintf(body, sizeof(body), "{\"code\":\"%s\",\"name\":\"3DS\",\"platform\":\"3ds\"}", code);
    url_join(url, sizeof(url), "/api/auth/devices/pair");
    if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0) {
        printf("HTTP error\n");
        wait_a();
        return;
    }
    if (status != 200 || !json_get_string(resp.data ? resp.data : "", "token", token, sizeof(token))) {
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Pair failed (%ld): %s\n", status, err);
        http_buffer_free(&resp);
        wait_a();
        return;
    }
    snprintf(g_cfg.token, sizeof(g_cfg.token), "%s", token);
    pt_http_set_token(g_cfg.token);
    save_config();
    printf("Paired. Token stored.\n");
    http_buffer_free(&resp);
    wait_a();
}

static int select_game(int platform_3ds)
{
    int i, sel = 0, count = 0;
    int idx[PT_GAME_COUNT];
    for (i = 0; i < PT_GAME_COUNT; i++) {
        if ((platform_3ds && strcmp(PT_GAMES[i].platform, "3ds") == 0) ||
            (!platform_3ds && strcmp(PT_GAMES[i].platform, "switch") == 0))
            idx[count++] = i;
    }
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        consoleSelect(&top);
        consoleClear();
        printf("Select game (D-Pad, A confirm, B back)\n\n");
        for (i = 0; i < count; i++)
            printf("%c %s\n", i == sel ? '>' : ' ', PT_GAMES[idx[i]].name);
        if (k & KEY_DUP)
            sel = (sel + count - 1) % count;
        if (k & KEY_DDOWN)
            sel = (sel + 1) % count;
        if (k & KEY_B)
            return -1;
        if (k & KEY_A)
            return idx[sel];
        gspWaitForVBlank();
    }
    return -1;
}

static void transfer_flow(void)
{
    int gi = select_game(1);
    FS_Archive arch;
    uint8_t *save = NULL;
    u64 save_size = 0;
    char url[400], session[80], err[256];
    HttpBuffer resp = {0};
    long status = 0;
    int box = 0, slot = 0, pokemon_id = 0;
    char body[256];
    uint8_t *patched = NULL;

    if (gi < 0)
        return;
    consoleSelect(&bottom);
    consoleClear();
    printf("Opening %s...\n", PT_GAMES[gi].name);
    if (R_FAILED(open_save(PT_GAMES[gi].title_id_u64, &arch))) {
        printf("Could not mount save. Is the title installed?\n");
        wait_a();
        return;
    }
    if (read_save_file(arch, "/main", &save, &save_size) != 0) {
        printf("Failed to read /main\n");
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
            if (write_save_file(arch, "/main", patched, resp.len) == 0)
                printf("Save written. Close the game if it was open next time.\n");
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
    int sel = 0;
    gfxInitDefault();
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);
    fsInit();
    aptInit();
    romfsInit();
    ensure_dirs();
    load_config();
    pt_http_init("romfs:/cacert.pem");
    if (g_cfg.token[0])
        pt_http_set_token(g_cfg.token);

    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        consoleSelect(&top);
        consoleClear();
        printf("Pockettransfer (3DS)\nHost: %s\nToken: %s\n\n", g_cfg.host,
               g_cfg.token[0] ? "(saved)" : "(none)");
        printf("%c Pair device\n%c Deposit/withdraw\n%c Quit\n",
               sel == 0 ? '>' : ' ', sel == 1 ? '>' : ' ', sel == 2 ? '>' : ' ');
        if (k & KEY_DUP)
            sel = (sel + 2) % 3;
        if (k & KEY_DDOWN)
            sel = (sel + 1) % 3;
        if (k & KEY_START || (k & KEY_A && sel == 2))
            break;
        if (k & KEY_A && sel == 0)
            pair_device();
        if (k & KEY_A && sel == 1)
            transfer_flow();
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    pt_http_shutdown();
    romfsExit();
    fsExit();
    aptExit();
    gfxExit();
    return 0;
}
