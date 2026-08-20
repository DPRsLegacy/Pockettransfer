#ifndef POCKETTRANSFER_GAMES_H
#define POCKETTRANSFER_GAMES_H

#include <stdint.h>

typedef struct {
    const char *id;
    const char *name;
    const char *platform;
    const char *title_id;
    uint64_t title_id_u64;
    int format;
    const char *primary_save;
} PtGame;

static const PtGame PT_GAMES[] = {
    {"x", "Pokémon X", "3ds", "0004000000055D00", 0x0004000000055D00ULL, 6, "main"},
    {"y", "Pokémon Y", "3ds", "0004000000055E00", 0x0004000000055E00ULL, 6, "main"},
    {"or", "Pokémon Omega Ruby", "3ds", "000400000011C400", 0x000400000011C400ULL, 6, "main"},
    {"as", "Pokémon Alpha Sapphire", "3ds", "000400000011C500", 0x000400000011C500ULL, 6, "main"},
    {"sun", "Pokémon Sun", "3ds", "0004000000164800", 0x0004000000164800ULL, 7, "main"},
    {"moon", "Pokémon Moon", "3ds", "0004000000175E00", 0x0004000000175E00ULL, 7, "main"},
    {"us", "Pokémon Ultra Sun", "3ds", "00040000001B5000", 0x00040000001B5000ULL, 7, "main"},
    {"um", "Pokémon Ultra Moon", "3ds", "00040000001B5100", 0x00040000001B5100ULL, 7, "main"},
    {"sw", "Pokémon Sword", "switch", "0100ABF008968000", 0x0100ABF008968000ULL, 8, "main"},
    {"sh", "Pokémon Shield", "switch", "01008DB008C2C000", 0x01008DB008C2C000ULL, 8, "main"},
    {"bd", "Pokémon Brilliant Diamond", "switch", "0100000011D90000", 0x0100000011D90000ULL, 8, "main"},
    {"sp", "Pokémon Shining Pearl", "switch", "010018E011D92000", 0x010018E011D92000ULL, 8, "main"},
    {"pla", "Pokémon Legends: Arceus", "switch", "01001F5010DFA000", 0x01001F5010DFA000ULL, 8, "main"},
    {"sl", "Pokémon Scarlet", "switch", "0100A3D008C5C000", 0x0100A3D008C5C000ULL, 9, "main"},
    {"vl", "Pokémon Violet", "switch", "01008F600124C000", 0x01008F600124C000ULL, 9, "main"},
};

#define PT_GAME_COUNT (int)(sizeof(PT_GAMES) / sizeof(PT_GAMES[0]))

#endif
