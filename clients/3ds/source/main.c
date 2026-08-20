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
#define PT_HOST "https://bank.saltbox.cc"

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

static int apply_token(const char *token)
{
    if (!token || !token[0])
        return -1;
    snprintf(g_cfg.token, sizeof(g_cfg.token), "%s", token);
    pt_http_set_token(g_cfg.token);
    save_config();
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
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);
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
    int sel = 0, i;
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        consoleSelect(&top);
        consoleClear();
        printf("%s\n\n", title);
        for (i = 0; i < count; i++)
            printf("%c %s\n", i == sel ? '>' : ' ', items[i]);
        if (k & KEY_DUP)
            sel = (sel + count - 1) % count;
        if (k & KEY_DDOWN)
            sel = (sel + 1) % count;
        if (k & KEY_B)
            return -1;
        if (k & KEY_A)
            return sel;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
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
        printf("HTTP error talking to %s\n", PT_HOST);
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
    else if (account_flow() != 0)
        goto shutdown;

    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        consoleSelect(&top);
        consoleClear();
        printf("Pockettransfer (3DS)\nHost: %s\nToken: %s\n\n", g_cfg.host,
               g_cfg.token[0] ? "(saved)" : "(none)");
        printf("%c Deposit/withdraw\n%c Account\n%c Quit\n",
               sel == 0 ? '>' : ' ', sel == 1 ? '>' : ' ', sel == 2 ? '>' : ' ');
        if (k & KEY_DUP)
            sel = (sel + 2) % 3;
        if (k & KEY_DDOWN)
            sel = (sel + 1) % 3;
        if (k & KEY_START || (k & KEY_A && sel == 2))
            break;
        if (k & KEY_A && sel == 0)
            transfer_flow();
        if (k & KEY_A && sel == 1)
            account_flow();
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

shutdown:
    pt_http_shutdown();
    romfsExit();
    fsExit();
    aptExit();
    gfxExit();
    return 0;
}
