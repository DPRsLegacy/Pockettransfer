#ifndef POCKETTRANSFER_GAMES_H
#define POCKETTRANSFER_GAMES_H

#include <stdint.h>

#define PT_ARCH_SAVE 0
#define PT_ARCH_EXT 1
#define PT_ARCH_DS 2

typedef struct {
    const char *id;
    const char *name;
    const char *platform;
    const char *title_id;
    uint64_t title_id_u64;
    int format;
    const char *primary_save;
    int icon; /* 3DS HOME icon index, or -1 */
    int archive;
    const char *nds3; /* 3-letter NDS game code, or "" */
    uint64_t extra_tids[8];
} PtGame;

static inline int pt_game_has_tid(const PtGame *g, uint64_t tid)
{
    int i;
    if (!g || !tid)
        return 0;
    if (g->title_id_u64 == tid)
        return 1;
    for (i = 0; i < 8; i++) {
        if (g->extra_tids[i] == 0)
            break;
        if (g->extra_tids[i] == tid)
            return 1;
    }
    return 0;
}

static inline int pt_game_collect_tids(const PtGame *g, uint64_t *out, int max)
{
    int n = 0, i;
    if (!g || !out || max <= 0)
        return 0;
    if (g->title_id_u64 && n < max)
        out[n++] = g->title_id_u64;
    for (i = 0; i < 8 && n < max; i++) {
        if (g->extra_tids[i] == 0)
            break;
        out[n++] = g->extra_tids[i];
    }
    return n;
}

static const PtGame PT_GAMES[] = {
    {"x", "Pokémon X", "3ds", "0004000000055D00", 0x0004000000055D00ULL, 6, "main", 0, PT_ARCH_SAVE, "", {0}},
    {"y", "Pokémon Y", "3ds", "0004000000055E00", 0x0004000000055E00ULL, 6, "main", 1, PT_ARCH_SAVE, "", {0}},
    {"or", "Pokémon Omega Ruby", "3ds", "000400000011C400", 0x000400000011C400ULL, 6, "main", 2, PT_ARCH_SAVE, "", {0}},
    {"as", "Pokémon Alpha Sapphire", "3ds", "000400000011C500", 0x000400000011C500ULL, 6, "main", 3, PT_ARCH_SAVE, "", {0}},
    {"sun", "Pokémon Sun", "3ds", "0004000000164800", 0x0004000000164800ULL, 7, "main", 4, PT_ARCH_SAVE, "", {0}},
    {"moon", "Pokémon Moon", "3ds", "0004000000175E00", 0x0004000000175E00ULL, 7, "main", 5, PT_ARCH_SAVE, "", {0}},
    {"us", "Pokémon Ultra Sun", "3ds", "00040000001B5000", 0x00040000001B5000ULL, 7, "main", 6, PT_ARCH_SAVE, "", {0}},
    {"um", "Pokémon Ultra Moon", "3ds", "00040000001B5100", 0x00040000001B5100ULL, 7, "main", 7, PT_ARCH_SAVE, "", {0}},

    {"red", "Pokémon Red", "3ds", "0004000000171000", 0x0004000000171000ULL, 1, "sav.dat", 8, PT_ARCH_EXT, "",
     {0x0004000000170C00ULL, 0x0004000000171300ULL, 0x0004000000171600ULL, 0x0004000000171900ULL, 0x0004000000171C00ULL}},
    {"blue", "Pokémon Blue", "3ds", "0004000000171100", 0x0004000000171100ULL, 1, "sav.dat", 9, PT_ARCH_EXT, "",
     {0x0004000000170E00ULL, 0x0004000000171400ULL, 0x0004000000171700ULL, 0x0004000000171A00ULL, 0x0004000000171D00ULL}},
    {"yellow", "Pokémon Yellow", "3ds", "0004000000171200", 0x0004000000171200ULL, 1, "sav.dat", 10, PT_ARCH_EXT, "",
     {0x0004000000170F00ULL, 0x0004000000171500ULL, 0x0004000000171800ULL, 0x0004000000171B00ULL, 0x0004000000171E00ULL}},
    {"green", "Pokémon Green", "3ds", "0004000000170D00", 0x0004000000170D00ULL, 1, "sav.dat", 11, PT_ARCH_EXT, "", {0}},
    {"gold", "Pokémon Gold", "3ds", "0004000000172600", 0x0004000000172600ULL, 2, "sav.dat", 12, PT_ARCH_EXT, "",
     {0x0004000000172300ULL, 0x0004000000172900ULL, 0x0004000000172C00ULL, 0x0004000000172F00ULL, 0x0004000000173200ULL,
      0x0004000000173500ULL}},
    {"silver", "Pokémon Silver", "3ds", "0004000000172700", 0x0004000000172700ULL, 2, "sav.dat", 13, PT_ARCH_EXT, "",
     {0x0004000000172400ULL, 0x0004000000172A00ULL, 0x0004000000172D00ULL, 0x0004000000173000ULL, 0x0004000000173300ULL,
      0x0004000000173600ULL}},
    {"crystal", "Pokémon Crystal", "3ds", "0004000000172800", 0x0004000000172800ULL, 2, "sav.dat", 14, PT_ARCH_EXT, "",
     {0x0004000000172500ULL, 0x0004000000172B00ULL, 0x0004000000172E00ULL, 0x0004000000173100ULL, 0x0004000000173400ULL}},

    {"dp", "Pokémon Diamond", "3ds", "DS-ADA", 0, 4, "pokemon.sav", 15, PT_ARCH_DS, "ADA", {0}},
    {"pearl", "Pokémon Pearl", "3ds", "DS-APA", 0, 4, "pokemon.sav", 16, PT_ARCH_DS, "APA", {0}},
    {"pt", "Pokémon Platinum", "3ds", "DS-CPU", 0, 4, "pokemon.sav", 17, PT_ARCH_DS, "CPU", {0}},
    {"hg", "Pokémon HeartGold", "3ds", "DS-IPK", 0, 4, "pokemon.sav", 18, PT_ARCH_DS, "IPK", {0}},
    {"ss", "Pokémon SoulSilver", "3ds", "DS-IPG", 0, 4, "pokemon.sav", 19, PT_ARCH_DS, "IPG", {0}},
    {"bl", "Pokémon Black", "3ds", "DS-IRB", 0, 5, "pokemon.sav", 20, PT_ARCH_DS, "IRB", {0}},
    {"wh", "Pokémon White", "3ds", "DS-IRA", 0, 5, "pokemon.sav", 21, PT_ARCH_DS, "IRA", {0}},
    {"b2", "Pokémon Black 2", "3ds", "DS-IRE", 0, 5, "pokemon.sav", 22, PT_ARCH_DS, "IRE", {0}},
    {"w2", "Pokémon White 2", "3ds", "DS-IRD", 0, 5, "pokemon.sav", 23, PT_ARCH_DS, "IRD", {0}},

    {"sw", "Pokémon Sword", "switch", "0100ABF008968000", 0x0100ABF008968000ULL, 8, "main", -1, PT_ARCH_SAVE, "", {0}},
    {"sh", "Pokémon Shield", "switch", "01008DB008C2C000", 0x01008DB008C2C000ULL, 8, "main", -1, PT_ARCH_SAVE, "", {0}},
    {"bd", "Pokémon Brilliant Diamond", "switch", "0100000011D90000", 0x0100000011D90000ULL, 8, "main", -1, PT_ARCH_SAVE, "", {0}},
    {"sp", "Pokémon Shining Pearl", "switch", "010018E011D92000", 0x010018E011D92000ULL, 8, "main", -1, PT_ARCH_SAVE, "", {0}},
    {"pla", "Pokémon Legends: Arceus", "switch", "01001F5010DFA000", 0x01001F5010DFA000ULL, 8, "main", -1, PT_ARCH_SAVE, "", {0}},
    {"sl", "Pokémon Scarlet", "switch", "0100A3D008C5C000", 0x0100A3D008C5C000ULL, 9, "main", -1, PT_ARCH_SAVE, "", {0}},
    {"vl", "Pokémon Violet", "switch", "01008F600124C000", 0x01008F600124C000ULL, 9, "main", -1, PT_ARCH_SAVE, "", {0}},
};

#define PT_GAME_COUNT (int)(sizeof(PT_GAMES) / sizeof(PT_GAMES[0]))

#endif
