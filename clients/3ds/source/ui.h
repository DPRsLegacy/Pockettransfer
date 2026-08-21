#ifndef POCKETTRANSFER_UI_H
#define POCKETTRANSFER_UI_H

#define UI_BOX_SLOTS 30
#define UI_BOX_COLS 6
#define UI_BOX_ROWS 5

#define UI_BOX_NONE 0
#define UI_BOX_DEPOSIT 1
#define UI_BOX_WITHDRAW 2
#define UI_BOX_WRITE 3
#define UI_BOX_BACK 4
#define UI_BOX_QUIT 5

typedef struct {
    int occupied;
    int locked;
    int shiny;
    int id;
    int species;
    int form;
    int level;
    int slot;
    int gender; /* 0 male, 1 female, 2 none */
    int tid;
    int iv[6];
    char name[20];
    char species_name[16];
    char ot[16];
    char nature[12];
    char type1[12];
    char type2[12];
    char met[12];
} UiPoke;

typedef struct {
    int mode; /* 0 deposit, 1 withdraw */
    int pane; /* 0 save (bottom), 1 bank (bottom) */
    int dirty;
    int edits;
    int save_box, save_boxes, save_cur;
    int bank_box, bank_boxes, bank_cur;
    int picked_id;
    int browse; /* 1 = bank only, no save */
    char save_label[64];
    char bank_label[64];
    UiPoke save[UI_BOX_SLOTS];
    UiPoke bank[UI_BOX_SLOTS];
} UiBoxView;

void ui_init(void);
void ui_exit(void);
void ui_after_swkbd(void);

void ui_set_chrome(const char *host, const char *account, const char *status);

void ui_busy(const char *title, const char *body);
int ui_alert(const char *title, const char *body);
int ui_confirm(const char *title, const char *body);
int ui_pick(const char *title, const char **items, int count);
int ui_pick_icons(const char *title, const char **items, const int *icons, int count);

int ui_boxes_poll(UiBoxView *st);
void ui_boxes_draw(const UiBoxView *st);

#endif
