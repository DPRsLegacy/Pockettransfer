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

#define CONFIG_DIR "sdmc:/switch/pockettransfer"
#define CONFIG_PATH CONFIG_DIR "/config.json"
#define BACKUP_DIR CONFIG_DIR "/backups"

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

static void pair_device(void)
{
    char code[16] = {0};
    char url[320], body[160], token[160], err[256];
    HttpBuffer resp = {0};
    long status = 0;
    FILE *f;
    printf("Put pairing code in %s/pair.txt then press A.\n", CONFIG_DIR);
    wait_a();
    f = fopen(CONFIG_DIR "/pair.txt", "rb");
    if (!f) {
        printf("pair.txt missing\n");
        wait_a();
        return;
    }
    fread(code, 1, sizeof(code) - 1, f);
    fclose(f);
    snprintf(body, sizeof(body), "{\"code\":\"%s\",\"name\":\"Switch\",\"platform\":\"switch\"}", code);
    snprintf(url, sizeof(url), "%s/api/auth/devices/pair", g_cfg.host);
    if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0 ||
        !json_get_string(resp.data ? resp.data : "", "token", token, sizeof(token))) {
        json_get_string(resp.data ? resp.data : "", "error", err, sizeof(err));
        printf("Pair failed (%ld): %s\n", status, err);
        http_buffer_free(&resp);
        wait_a();
        return;
    }
    snprintf(g_cfg.token, sizeof(g_cfg.token), "%s", token);
    pt_http_set_token(g_cfg.token);
    save_config();
    printf("Paired.\n");
    http_buffer_free(&resp);
    wait_a();
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
    ensure_dirs();
    load_config();
    pt_http_init("romfs:/cacert.pem");
    if (g_cfg.token[0])
        pt_http_set_token(g_cfg.token);

    while (appletMainLoop()) {
        consoleClear();
        printf("Pockettransfer (Switch)\nHost: %s\nToken: %s\n\n", g_cfg.host,
               g_cfg.token[0] ? "(saved)" : "(none)");
        printf("%c Pair device\n%c Deposit/withdraw\n%c Quit\n",
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
            pair_device();
        if (button_down(HidNpadButton_A) && sel == 1)
            transfer_flow();
    }

    pt_http_shutdown();
    romfsExit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
