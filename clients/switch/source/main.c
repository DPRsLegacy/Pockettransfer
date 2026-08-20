#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <dirent.h>

#include "games.h"
#include "http.h"
#include "jsonutil.h"
#include "lite_check.h"
#include "log.h"

#define CONFIG_DIR "sdmc:/switch/pockettransfer"
#define CONFIG_PATH CONFIG_DIR "/config.json"
#define BACKUP_DIR CONFIG_DIR "/backups"
#define PT_HOST "https://bank.saltbox.cc"

typedef struct {
    char host[256];
    char token[160];
} Config;

static Config g_cfg;
static PadState g_pad;

static int button_down(u64 mask)
{
    padUpdate(&g_pad);
    return (int)(padGetButtonsDown(&g_pad) & mask);
}

static void wait_a(void)
{
    printf("Press A to continue.\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
        if (button_down(HidNpadButton_A))
            break;
    }
}

static void ensure_dirs(void)
{
    mkdir("sdmc:/switch", 0777);
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
    snprintf(url, sizeof(url), "%s/api/auth/devices/enroll", g_cfg.host);
    if (pt_http_request("POST", url, "{\"name\":\"Switch\",\"platform\":\"switch\"}",
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
    char url[320], body[160], token[160], err[256];
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
    snprintf(body, sizeof(body), "{\"code\":\"%s\",\"name\":\"Switch\",\"platform\":\"switch\"}", code);
    snprintf(url, sizeof(url), "%s/api/auth/devices/pair", g_cfg.host);
    if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0 ||
        !json_get_string(resp.data ? resp.data : "", "token", token, sizeof(token))) {
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

static int prompt_text(const char *header, const char *guide, char *out, size_t n, int password, u32 max_len)
{
    SwkbdConfig kbd;
    Result rc;
    memset(out, 0, n);
    rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc))
        return -1;
    if (password)
        swkbdConfigMakePresetPassword(&kbd);
    else
        swkbdConfigMakePresetDefault(&kbd);
    if (header)
        swkbdConfigSetHeaderText(&kbd, header);
    if (guide)
        swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetOkButtonText(&kbd, "OK");
    swkbdConfigSetStringLenMax(&kbd, max_len);
    rc = swkbdShow(&kbd, out, n);
    swkbdClose(&kbd);
    trim_inplace(out);
    return (R_SUCCEEDED(rc) && out[0]) ? 0 : -1;
}

static int pick_items(const char *title, const char **items, int count)
{
    int sel = 0, i;
    while (appletMainLoop()) {
        consoleClear();
        printf("%s\n\n", title);
        for (i = 0; i < count; i++)
            printf("%c %s\n", i == sel ? '>' : ' ', items[i]);
        consoleUpdate(NULL);
        if (button_down(HidNpadButton_Up))
            sel = (sel + count - 1) % count;
        if (button_down(HidNpadButton_Down))
            sel = (sel + 1) % count;
        if (button_down(HidNpadButton_B))
            return -1;
        if (button_down(HidNpadButton_A))
            return sel;
    }
    return -1;
}

static int auth_request(int is_register)
{
    char user[40] = {0}, pass[72] = {0}, pass2[72] = {0};
    char body[400], url[320], token[160], err[256], username[40];
    HttpBuffer resp = {0};
    long status = 0;

    printf("%s on %s\nSystem keyboard next.\n", is_register ? "Create account" : "Log in", PT_HOST);
    consoleUpdate(NULL);

    if (prompt_text("Username", "3-32 chars: a-z, 0-9, _", user, sizeof(user), 0, 32) != 0)
        return -1;
    if (prompt_text("Password", is_register ? "8+ characters" : "Your password",
                    pass, sizeof(pass), 1, 64) != 0)
        return -1;
    if (is_register) {
        if (prompt_text("Confirm password", "Type it again", pass2, sizeof(pass2), 1, 64) != 0)
            return -1;
        if (strcmp(pass, pass2) != 0) {
            printf("Passwords do not match.\n");
            return -1;
        }
        if (strlen(pass) < 8) {
            printf("Password must be at least 8 characters.\n");
            return -1;
        }
    }

    if (!json_auth_body(body, sizeof(body), user, pass, "Switch", "switch")) {
        printf("Could not build request.\n");
        return -1;
    }
    snprintf(url, sizeof(url), "%s%s", g_cfg.host, is_register ? "/api/auth/register" : "/api/auth/login");
    printf("%s...\n", is_register ? "Creating account" : "Logging in");
    consoleUpdate(NULL);
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
    while (appletMainLoop()) {
        choice = pick_items("Pockettransfer account\nHost: " PT_HOST, items, 4);
        if (choice < 0 || choice == 3)
            return g_cfg.token[0] ? 0 : -1;
        if (choice == 0 || choice == 1) {
            if (auth_request(choice == 0) == 0) {
                wait_a();
                return 0;
            }
            wait_a();
            continue;
        }
        printf("Pairing via website...\n");
        consoleUpdate(NULL);
        if (pair_with_code_file() == 0 || enroll_device() == 0) {
            wait_a();
            return 0;
        }
        printf("\nGenerate a pairing code on the website, then retry.\n");
        wait_a();
    }
    return g_cfg.token[0] ? 0 : -1;
}

static int select_switch_game(void)
{
    int i, sel = 0, count = 0, idx[PT_GAME_COUNT];
    for (i = 0; i < PT_GAME_COUNT; i++) {
        if (strcmp(PT_GAMES[i].platform, "switch") == 0)
            idx[count++] = i;
    }
    while (appletMainLoop()) {
        consoleClear();
        printf("Select game (D-Pad, A confirm, B back)\n\n");
        for (i = 0; i < count; i++)
            printf("%c %s\n", i == sel ? '>' : ' ', PT_GAMES[idx[i]].name);
        consoleUpdate(NULL);
        if (button_down(HidNpadButton_Up))
            sel = (sel + count - 1) % count;
        if (button_down(HidNpadButton_Down))
            sel = (sel + 1) % count;
        if (button_down(HidNpadButton_B))
            return -1;
        if (button_down(HidNpadButton_A))
            return idx[sel];
    }
    return -1;
}

static int read_file(const char *path, uint8_t **out, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    *out = malloc((size_t)sz);
    if (!*out) {
        fclose(f);
        return -1;
    }
    *len = fread(*out, 1, (size_t)sz, f);
    fclose(f);
    return 0;
}

static void transfer_flow(void)
{
    int gi = select_switch_game();
    AccountUid uid;
    Result rc;
    uint8_t *save = NULL;
    size_t save_len = 0;
    char url[400], session[80], err[256], body[256];
    HttpBuffer resp = {0};
    long status = 0;
    int box = 0, slot = 0, pokemon_id = 0;
    FILE *sf;

    if (gi < 0)
        return;

    rc = accountInitialize(AccountServiceType_Application);
    if (R_FAILED(rc)) {
        printf("accountInitialize failed: 0x%x — close the game first.\n", rc);
        wait_a();
        return;
    }
    rc = accountGetPreselectedUser(&uid);
    accountExit();
    if (R_FAILED(rc)) {
        printf("No user selected.\n");
        wait_a();
        return;
    }

    rc = fsdevMountSaveData("save", PT_GAMES[gi].title_id_u64, uid);
    if (R_FAILED(rc)) {
        printf("fsdevMountSaveData failed 0x%x. Fully close the Pokémon game (homebrew overlay / title takeover), then retry.\n", rc);
        pt_log("save mount fail %s rc=0x%x", PT_GAMES[gi].id, rc);
        wait_a();
        return;
    }

    if (read_file("save:/main", &save, &save_len) != 0) {
        printf("Could not read save:/main\n");
        fsdevUnmountDevice("save");
        wait_a();
        return;
    }

    ensure_dirs();
    {
        char bak[256];
        FILE *b;
        snprintf(bak, sizeof(bak), BACKUP_DIR "/%s.main.bak", PT_GAMES[gi].id);
        b = fopen(bak, "wb");
        if (b) {
            fwrite(save, 1, save_len, b);
            fclose(b);
        }
    }

    printf("Uploading %zu bytes from %s...\n", save_len, PT_GAMES[gi].name);
    consoleUpdate(NULL);
    snprintf(url, sizeof(url), "%s/api/saves/session", g_cfg.host);
    if (pt_http_upload(url, "file", "main", save, save_len, &resp, &status) != 0 || status != 200 ||
        !json_get_string(resp.data ? resp.data : "", "sessionId", session, sizeof(session))) {
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Upload failed (%ld) %s\n", status, err);
        free(save);
        http_buffer_free(&resp);
        fsdevUnmountDevice("save");
        wait_a();
        return;
    }
    http_buffer_free(&resp);
    printf("Session %s\nEdit %s/slot.txt as box,slot[,pokemonId]\nA continue\n", session, CONFIG_DIR);
    wait_a();
    sf = fopen(CONFIG_DIR "/slot.txt", "rb");
    if (sf) {
        fscanf(sf, "%d,%d,%d", &box, &slot, &pokemon_id);
        fclose(sf);
    }

    if (pokemon_id == 0) {
        snprintf(body, sizeof(body), "{\"sessionId\":\"%s\",\"box\":%d,\"slot\":%d}", session, box, slot);
        snprintf(url, sizeof(url), "%s/api/bank/deposit", g_cfg.host);
        pt_http_request("POST", url, body, "application/json", &resp, &status);
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Deposit status %ld %s\n", status, err);
        http_buffer_free(&resp);
    } else {
        snprintf(body, sizeof(body),
                 "{\"sessionId\":\"%s\",\"pokemonId\":%d,\"box\":%d,\"slot\":%d}",
                 session, pokemon_id, box, slot);
        snprintf(url, sizeof(url), "%s/api/bank/withdraw", g_cfg.host);
        if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0 || status != 200) {
            json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
            printf("Withdraw failed: %s\n", err);
            http_buffer_free(&resp);
            free(save);
            fsdevUnmountDevice("save");
            wait_a();
            return;
        }
        http_buffer_free(&resp);
    }

    snprintf(url, sizeof(url), "%s/api/saves/%s/file", g_cfg.host, session);
    if (pt_http_request("GET", url, NULL, NULL, &resp, &status) != 0 || status != 200 || !resp.data) {
        printf("Patched save download failed\n");
        free(save);
        http_buffer_free(&resp);
        fsdevUnmountDevice("save");
        wait_a();
        return;
    }

    if (lite_is_known_size(resp.len)) {
        char reason[64];
        lite_validate_pkm((const uint8_t *)resp.data, resp.len, reason, sizeof(reason));
    }

    printf("Write save and commit? A=yes B=no\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
        if (button_down(HidNpadButton_B))
            break;
        if (button_down(HidNpadButton_A)) {
            FILE *out = fopen("save:/main", "wb");
            if (!out) {
                printf("Cannot open save:/main for write\n");
                break;
            }
            fwrite(resp.data, 1, resp.len, out);
            fclose(out);
            rc = fsdevCommitDevice("save");
            printf("Commit result 0x%x\n", rc);
            break;
        }
    }

    free(save);
    http_buffer_free(&resp);
    fsdevUnmountDevice("save");
    wait_a();
}

int main(int argc, char **argv)
{
    int sel = 0;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    socketInitializeDefault();
    romfsInit();
    pt_log_init("switch");
    {
        FILE *ca = fopen("romfs:/cacert.pem", "rb");
        pt_log("cacert %s", ca ? "ok" : "MISSING");
        if (ca)
            fclose(ca);
    }
    ensure_dirs();
    load_config();
    pt_log("config token=%s host=%s", g_cfg.token[0] ? "yes" : "no", g_cfg.host);
    pt_http_init("romfs:/cacert.pem");
    if (g_cfg.token[0])
        pt_http_set_token(g_cfg.token);
    else if (account_flow() != 0)
        goto shutdown;

    while (appletMainLoop()) {
        consoleClear();
        printf("Pockettransfer (Switch)\nHost: %s\nToken: %s\n\n", g_cfg.host,
               g_cfg.token[0] ? "(saved)" : "(none)");
        printf("%c Deposit/withdraw\n%c Account\n%c Quit\n",
               sel == 0 ? '>' : ' ', sel == 1 ? '>' : ' ', sel == 2 ? '>' : ' ');
        printf("\nClose Pokémon titles before mounting saves.\n");
        consoleUpdate(NULL);
        if (button_down(HidNpadButton_Up))
            sel = (sel + 2) % 3;
        if (button_down(HidNpadButton_Down))
            sel = (sel + 1) % 3;
        if (button_down(HidNpadButton_Plus) || (button_down(HidNpadButton_A) && sel == 2))
            break;
        if (button_down(HidNpadButton_A) && sel == 0)
            transfer_flow();
        if (button_down(HidNpadButton_A) && sel == 1)
            account_flow();
    }

shutdown:
    pt_log_shutdown();
    pt_http_shutdown();
    romfsExit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
