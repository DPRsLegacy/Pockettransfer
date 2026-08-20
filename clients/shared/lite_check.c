#include "lite_check.h"

#include <stdio.h>
#include <string.h>

static const uint8_t kPositions[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2},
    {0, 2, 3, 1}, {0, 3, 2, 1}, {1, 0, 2, 3}, {1, 0, 3, 2},
    {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2},
    {2, 3, 0, 1}, {3, 2, 0, 1}, {1, 2, 3, 0}, {1, 3, 2, 0},
    {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

int lite_is_known_size(size_t size)
{
    return size == 232 || size == 328 || size == 344 || size == 360;
}

static void crypt_span(uint8_t *data, size_t size, uint32_t seed)
{
    size_t i;
    for (i = 8; i + 1 < size; i += 2) {
        seed = (uint32_t)(0x41C64E6Du * seed + 0x6073u);
        uint16_t xorv = (uint16_t)(seed >> 16);
        data[i] ^= (uint8_t)xorv;
        data[i + 1] ^= (uint8_t)(xorv >> 8);
    }
}

static void unshuffle(uint8_t *data, size_t size)
{
    uint32_t ec = (uint32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
    int index = (int)((ec >> 13) & 31) % 24;
    int block = (int)((size - 8) / 4);
    uint8_t tmp[512];
    int b;
    if ((size - 8) > sizeof(tmp))
        return;
    memcpy(tmp, data + 8, size - 8);
    for (b = 0; b < 4; b++)
        memcpy(data + 8 + b * block, tmp + kPositions[index][b] * block, (size_t)block);
}

int lite_validate_pkm(const uint8_t *data, size_t size, char *err, size_t err_len)
{
    uint8_t copy[512];
    uint16_t stored, calc = 0;
    uint16_t species;
    size_t i;
    uint32_t seed;

    if (!lite_is_known_size(size) || size > sizeof(copy)) {
        if (err && err_len)
            snprintf(err, err_len, "bad size %zu", size);
        return 0;
    }
    memcpy(copy, data, size);
    seed = (uint32_t)(copy[0] | (copy[1] << 8) | (copy[2] << 16) | (copy[3] << 24));
    crypt_span(copy, size, seed);
    unshuffle(copy, size);

    stored = (uint16_t)(copy[6] | (copy[7] << 8));
    for (i = 8; i + 1 < size; i += 2)
        calc = (uint16_t)(calc + (uint16_t)(copy[i] | (copy[i + 1] << 8)));
    if (stored != calc) {
        if (err && err_len)
            snprintf(err, err_len, "checksum");
        return 0;
    }
    species = (uint16_t)(copy[8] | (copy[9] << 8));
    if (species == 0 || species > 1025) {
        if (err && err_len)
            snprintf(err, err_len, "species");
        return 0;
    }
    if (err && err_len)
        snprintf(err, err_len, "ok");
    return 1;
}
