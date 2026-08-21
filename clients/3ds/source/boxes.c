#include "boxes.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "jsonutil.h"
#include "log.h"
#include "ui.h"

#define MAX_BANK_BOXES 40

typedef struct {
    char name[32];
    UiPoke slots[UI_BOX_SLOTS];
} BankBoxMem;

static void parse_slot(const char *s, size_t n, UiPoke *p)
{
    int empty = 0;
    memset(p, 0, sizeof(*p));
    json_get_bool_n(s, n, "empty", &empty);
    json_get_int_n(s, n, "id", &p->id);
    json_get_int_n(s, n, "species", &p->species);
    json_get_int_n(s, n, "level", &p->level);
    json_get_int_n(s, n, "slot", &p->slot);
    json_get_bool_n(s, n, "isShiny", &p->shiny);
    json_get_bool_n(s, n, "locked", &p->locked);
    json_get_int_n(s, n, "form", &p->form);
    json_get_int_n(s, n, "gender", &p->gender);
    json_get_int_n(s, n, "tid", &p->tid);
    json_get_int_n(s, n, "ivHp", &p->iv[0]);
    json_get_int_n(s, n, "ivAtk", &p->iv[1]);
    json_get_int_n(s, n, "ivDef", &p->iv[2]);
    json_get_int_n(s, n, "ivSpa", &p->iv[3]);
    json_get_int_n(s, n, "ivSpd", &p->iv[4]);
    json_get_int_n(s, n, "ivSpe", &p->iv[5]);
    json_get_string_n(s, n, "speciesName", p->species_name, sizeof(p->species_name));
    json_get_string_n(s, n, "originalTrainer", p->ot, sizeof(p->ot));
    json_get_string_n(s, n, "nature", p->nature, sizeof(p->nature));
    json_get_string_n(s, n, "type1", p->type1, sizeof(p->type1));
    json_get_string_n(s, n, "type2", p->type2, sizeof(p->type2));
    json_get_string_n(s, n, "metDate", p->met, sizeof(p->met));
    if (!json_get_string_n(s, n, "nickname", p->name, sizeof(p->name))) {
        if (p->species_name[0])
            snprintf(p->name, sizeof(p->name), "%s", p->species_name);
        else
            json_get_string_n(s, n, "speciesName", p->name, sizeof(p->name));
    }
    if (empty) {
        int slot = p->slot;
        memset(p, 0, sizeof(*p));
        p->slot = slot;
        return;
    }
    p->occupied = p->species > 0;
}

static void fill_slots(const char *arr, UiPoke *slots)
{
    int i;
    const char *a, *b;
    memset(slots, 0, sizeof(UiPoke) * UI_BOX_SLOTS);
    for (i = 0; i < UI_BOX_SLOTS; i++)
        slots[i].slot = i;
    if (!arr)
        return;
    for (i = 0; json_array_nth_object(arr, i, &a, &b); i++) {
        UiPoke tmp;
        int idx;
        parse_slot(a, (size_t)(b - a), &tmp);
        idx = tmp.slot;
        if (idx < 0 || idx >= UI_BOX_SLOTS)
            idx = i;
        if (idx >= UI_BOX_SLOTS)
            break;
        tmp.slot = idx;
        slots[idx] = tmp;
        if (i > 64)
            break;
    }
}

static int http_err(HttpBuffer *resp, const char *fallback, char *err, size_t n)
{
    err[0] = 0;
    if (resp->data)
        json_get_string(resp->data, "error", err, n);
    if (!err[0])
        snprintf(err, n, "%s", fallback);
    return -1;
}

static int load_bank(const char *host, const char *session, BankBoxMem *bank, int *count)
{
    char url[480], err[256];
    HttpBuffer resp = {0};
    long status = 0;
    const char *obj, *obj_end, *slots;
    int i;

    if (session && session[0])
        snprintf(url, sizeof(url), "%s/api/bank/boxes?sessionId=%s", host, session);
    else
        snprintf(url, sizeof(url), "%s/api/bank/boxes", host);
    ui_busy("Bank", "Loading boxes...");
    if (pt_http_request("GET", url, NULL, NULL, &resp, &status) != 0 || status != 200 ||
        !resp.data) {
        http_err(&resp, "Could not load bank boxes.", err, sizeof(err));
        ui_alert("Bank failed", err);
        http_buffer_free(&resp);
        return -1;
    }

    memset(bank, 0, sizeof(*bank) * MAX_BANK_BOXES);
    *count = 0;
    for (i = 0; i < MAX_BANK_BOXES && json_array_nth_object(resp.data, i, &obj, &obj_end); i++) {
        size_t n = (size_t)(obj_end - obj);
        int idx = i;
        json_get_int_n(obj, n, "index", &idx);
        if (idx < 0 || idx >= MAX_BANK_BOXES)
            idx = i;
        if (!json_get_string_n(obj, n, "name", bank[idx].name, sizeof(bank[idx].name)))
            snprintf(bank[idx].name, sizeof(bank[idx].name), "Box %d", idx + 1);
        slots = json_find_array_n(obj, n, "slots");
        fill_slots(slots, bank[idx].slots);
        if (idx + 1 > *count)
            *count = idx + 1;
    }
    http_buffer_free(&resp);
    if (*count <= 0)
        *count = 1;
    pt_log("bank boxes loaded n=%d", *count);
    return 0;
}

static int load_save_box(const char *host, const char *session, int box, UiBoxView *st)
{
    char url[480], err[256];
    HttpBuffer resp = {0};
    long status = 0;
    const char *slots;
    int got_box = box;

    snprintf(url, sizeof(url), "%s/api/saves/%s/boxes?box=%d", host, session, box);
    ui_busy("Save", "Loading PC box...");
    if (pt_http_request("GET", url, NULL, NULL, &resp, &status) != 0 || status != 200 ||
        !resp.data) {
        http_err(&resp, "Could not load the save box.", err, sizeof(err));
        ui_alert("Save box failed", err);
        http_buffer_free(&resp);
        return -1;
    }
    json_get_int(resp.data, "boxCount", &st->save_boxes);
    json_get_int(resp.data, "box", &got_box);
    st->save_box = got_box;
    if (st->save_boxes <= 0)
        st->save_boxes = 1;
    slots = json_find_array_n(resp.data, resp.len, "slots");
    fill_slots(slots, st->save);
    snprintf(st->save_label, sizeof(st->save_label), "Box %d/%d", st->save_box + 1, st->save_boxes);
    http_buffer_free(&resp);
    return 0;
}

static void apply_bank(UiBoxView *st, BankBoxMem *bank, int n)
{
    int b = st->bank_box;
    if (n <= 0)
        n = 1;
    if (b < 0)
        b = 0;
    if (b >= n)
        b = n - 1;
    st->bank_box = b;
    st->bank_boxes = n;
    memcpy(st->bank, bank[b].slots, sizeof(st->bank));
    snprintf(st->bank_label, sizeof(st->bank_label), "Box %d/%d  %s", b + 1, n, bank[b].name);
}

static int ask_leave(int dirty)
{
    const char *items[] = {"Keep editing", "Write to game", "Leave without writing"};
    int choice;
    if (!dirty)
        return 0;
    choice = ui_pick("Unwritten changes", items, 3);
    if (choice == 1)
        return 1;
    if (choice == 2)
        return 0;
    return -1;
}

static int post_json(const char *url, const char *body, char *err, size_t err_n)
{
    HttpBuffer resp = {0};
    long status = 0;
    if (pt_http_request("POST", url, body, "application/json", &resp, &status) != 0 ||
        status != 200) {
        http_err(&resp, "The bank rejected that move.", err, err_n);
        http_buffer_free(&resp);
        return -1;
    }
    http_buffer_free(&resp);
    return 0;
}

int boxes_run(const char *host, const char *session_id)
{
    BankBoxMem *bank;
    UiBoxView st;
    int bank_n = 0, loaded_save = -1, act, box_n;
    char url[480], body[320], err[256];

    if (!host || !session_id || !session_id[0])
        return 0;
    bank = calloc(MAX_BANK_BOXES, sizeof(*bank));
    if (!bank) {
        ui_alert("Out of memory", "Could not allocate bank boxes.");
        return 0;
    }
    memset(&st, 0, sizeof(st));
    st.save_boxes = 1;
    st.bank_boxes = 1;
    if (load_bank(host, session_id, bank, &bank_n) != 0) {
        free(bank);
        return 0;
    }
    apply_bank(&st, bank, bank_n);

    while (aptMainLoop()) {
        if (st.save_box != loaded_save) {
            if (load_save_box(host, session_id, st.save_box, &st) != 0) {
                if (loaded_save < 0) {
                    free(bank);
                    return st.dirty ? 1 : 0;
                }
                st.save_box = loaded_save;
            } else {
                loaded_save = st.save_box;
            }
        }
        apply_bank(&st, bank, bank_n);
        ui_boxes_draw(&st);
        act = ui_boxes_poll(&st);
        if (act == UI_BOX_NONE)
            continue;
        if (act == UI_BOX_QUIT || act == UI_BOX_BACK) {
            box_n = ask_leave(st.dirty);
            if (box_n < 0)
                continue;
            free(bank);
            return box_n;
        }
        if (act == UI_BOX_WRITE) {
            if (!st.dirty) {
                free(bank);
                return 0;
            }
            free(bank);
            return 1;
        }
        if (act == UI_BOX_DEPOSIT) {
            const UiPoke *p = &st.save[st.save_cur];
            snprintf(url, sizeof(url), "%s/api/bank/deposit", host);
            snprintf(body, sizeof(body),
                     "{\"sessionId\":\"%s\",\"box\":%d,\"slot\":%d,\"bankBox\":%d}", session_id,
                     st.save_box, st.save_cur, st.bank_box);
            ui_busy("Deposit", p->name[0] ? p->name : "Moving into the bank...");
            if (post_json(url, body, err, sizeof(err)) != 0) {
                ui_alert("Deposit failed", err);
                continue;
            }
            st.dirty = 1;
            st.edits++;
            st.picked_id = 0;
            loaded_save = -1;
            if (load_bank(host, session_id, bank, &bank_n) != 0)
                continue;
            continue;
        }
        if (act == UI_BOX_WITHDRAW) {
            snprintf(url, sizeof(url), "%s/api/bank/withdraw", host);
            snprintf(body, sizeof(body),
                     "{\"sessionId\":\"%s\",\"pokemonId\":%d,\"box\":%d,\"slot\":%d}", session_id,
                     st.picked_id, st.save_box, st.save_cur);
            ui_busy("Withdraw", "Moving into the save...");
            if (post_json(url, body, err, sizeof(err)) != 0) {
                ui_alert("Withdraw failed", err);
                continue;
            }
            st.dirty = 1;
            st.edits++;
            st.picked_id = 0;
            loaded_save = -1;
            if (load_bank(host, session_id, bank, &bank_n) != 0)
                continue;
            continue;
        }
    }
    free(bank);
    return st.dirty ? 1 : 0;
}

int boxes_browse(const char *host)
{
    BankBoxMem *bank;
    UiBoxView st;
    int bank_n = 0, act;

    if (!host || !host[0])
        return 0;
    bank = calloc(MAX_BANK_BOXES, sizeof(*bank));
    if (!bank) {
        ui_alert("Out of memory", "Could not allocate bank boxes.");
        return 0;
    }
    memset(&st, 0, sizeof(st));
    st.browse = 1;
    st.pane = 1;
    st.bank_boxes = 1;
    if (load_bank(host, NULL, bank, &bank_n) != 0) {
        free(bank);
        return 0;
    }
    apply_bank(&st, bank, bank_n);

    while (aptMainLoop()) {
        apply_bank(&st, bank, bank_n);
        st.pane = 1;
        ui_boxes_draw(&st);
        act = ui_boxes_poll(&st);
        if (act == UI_BOX_NONE)
            continue;
        if (act == UI_BOX_QUIT || act == UI_BOX_BACK)
            break;
    }
    free(bank);
    return 0;
}
