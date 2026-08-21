#include "ds_spi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games.h"
#include "log.h"

#define SPI_CMD_RDSR 0x05
#define SPI_CMD_WREN 0x06
#define SPI_CMD_READ 0x03
#define SPI_FLASH_RDID 0x9F
#define SPI_FLASH_PW 0x0A
#define SPI_FLG_WIP 0x01
#define SPI_FLG_WEL 0x02

#define CHIP_NONE 0
#define CHIP_FLASH_256 1
#define CHIP_FLASH_512 2
#define CHIP_FLASH_IR256 3
#define CHIP_FLASH_IR512 4

static int g_pxi;
static int g_chip;
static int g_ir;

static int chip_is_ir(int chip)
{
    return chip == CHIP_FLASH_IR256 || chip == CHIP_FLASH_IR512;
}

static u32 chip_capacity(int chip)
{
    switch (chip) {
    case CHIP_FLASH_256:
    case CHIP_FLASH_IR256:
        return 256 * 1024;
    case CHIP_FLASH_512:
    case CHIP_FLASH_IR512:
        return 512 * 1024;
    default:
        return 0;
    }
}

static Result spi_xfer(const void *cmd, u32 cmd_len, void *answer, u32 answer_len, const void *data,
                       u32 data_len)
{
    u64 zero = 0;
    u8 transfer = pxiDevMakeTransferOption(BAUDRATE_4MHZ, BUSMODE_1BIT);
    u8 transfer_ir = pxiDevMakeTransferOption(BAUDRATE_1MHZ, BUSMODE_1BIT);
    u64 wait = pxiDevMakeWaitOperation(WAIT_NONE, DEASSERT_NONE, 0);
    PXIDEV_SPIBuffer header = {&zero, g_ir ? 1U : 0U, g_ir ? transfer_ir : transfer, wait};
    PXIDEV_SPIBuffer cmd_buf = {(void *)cmd, cmd_len, transfer, wait};
    PXIDEV_SPIBuffer ans_buf = {answer, answer_len, transfer, wait};
    PXIDEV_SPIBuffer data_buf = {(void *)data, data_len, transfer, wait};
    PXIDEV_SPIBuffer none = {NULL, 0, transfer, wait};
    PXIDEV_SPIBuffer footer = {&zero, 0, transfer, wait};
    return PXIDEV_SPIMultiWriteRead(&header, &cmd_buf, &ans_buf, &data_buf, &none, &footer);
}

static Result spi_wait_idle(void)
{
    u8 cmd = SPI_CMD_RDSR, sr = 0;
    int i;
    Result rc;
    for (i = 0; i < 20000; i++) {
        rc = spi_xfer(&cmd, 1, &sr, 1, NULL, 0);
        if (R_FAILED(rc))
            return rc;
        if (!(sr & SPI_FLG_WIP))
            return 0;
        svcSleepThread(500000LL);
    }
    return -1;
}

static Result spi_wren(void)
{
    u8 cmd = SPI_CMD_WREN, sr = 0;
    int i;
    Result rc = spi_xfer(&cmd, 1, NULL, 0, NULL, 0);
    if (R_FAILED(rc))
        return rc;
    cmd = SPI_CMD_RDSR;
    for (i = 0; i < 2000; i++) {
        rc = spi_xfer(&cmd, 1, &sr, 1, NULL, 0);
        if (R_FAILED(rc))
            return rc;
        if (sr & SPI_FLG_WEL)
            return 0;
        svcSleepThread(100000LL);
    }
    return -1;
}

static Result spi_jedec(u32 *id, u8 *sr)
{
    u8 cmd = SPI_FLASH_RDID, buf[3] = {0}, reg = 0;
    Result rc = spi_wait_idle();
    if (R_FAILED(rc))
        return rc;
    rc = spi_xfer(&cmd, 1, buf, 3, NULL, 0);
    if (R_FAILED(rc))
        return rc;
    cmd = SPI_CMD_RDSR;
    rc = spi_xfer(&cmd, 1, &reg, 1, NULL, 0);
    if (R_FAILED(rc))
        return rc;
    if (id)
        *id = ((u32)buf[0] << 16) | ((u32)buf[1] << 8) | buf[2];
    if (sr)
        *sr = reg;
    return 0;
}

static int chip_from_jedec(u32 jedec, int ir)
{
    static const u32 list[] = {0x204012, 0x621600, 0x204013, 0x621100, 0x204014, 0x202017};
    int i;
    if (ir) {
        if (jedec == list[0] || jedec == list[1])
            return CHIP_FLASH_IR256;
        return CHIP_FLASH_IR512;
    }
    if (jedec == 0x204017)
        return CHIP_NONE;
    for (i = 0; i < 6; i++) {
        if (jedec == list[i])
            return (i < 2) ? CHIP_FLASH_256 : CHIP_FLASH_512;
    }
    /* Pokémon gen 4/5 retail is 512KB FLASH. */
    if (jedec && jedec != 0x00FFFFFFu)
        return CHIP_FLASH_512;
    return CHIP_NONE;
}

static int nds_wants_ir(const char *nds3)
{
    return nds3 && (strcmp(nds3, "IPK") == 0 || strcmp(nds3, "IPG") == 0 || strcmp(nds3, "IRE") == 0 ||
                    strcmp(nds3, "IRD") == 0);
}

static int detect_chip(const char *nds3)
{
    u32 jedec = 0;
    u8 sr = 0;
    int try_ir = nds_wants_ir(nds3);
    int pass;
    for (pass = 0; pass < 2; pass++) {
        g_ir = try_ir;
        if (R_FAILED(spi_jedec(&jedec, &sr)))
            continue;
        pt_log("ds spi jedec=0x%06lx sr=0x%02x ir=%d", (unsigned long)jedec, sr, g_ir);
        if ((sr & 0xFD) == 0x00 && jedec != 0x00FFFFFFu) {
            g_chip = chip_from_jedec(jedec, g_ir);
            if (g_chip != CHIP_NONE) {
                g_ir = chip_is_ir(g_chip);
                return 0;
            }
        }
        try_ir = !try_ir;
    }
    g_chip = CHIP_NONE;
    return -1;
}

static Result spi_read_at(u32 offset, void *data, u32 size)
{
    u8 cmd[4] = {SPI_CMD_READ, (u8)(offset >> 16), (u8)(offset >> 8), (u8)offset};
    Result rc = spi_wait_idle();
    if (R_FAILED(rc))
        return rc;
    return spi_xfer(cmd, 4, data, size, NULL, 0);
}

static Result spi_write_page(u32 offset, const u8 *data, u32 size)
{
    u8 cmd[4] = {SPI_FLASH_PW, (u8)(offset >> 16), (u8)(offset >> 8), (u8)offset};
    Result rc = spi_wait_idle();
    if (R_FAILED(rc))
        return rc;
    rc = spi_wren();
    if (R_FAILED(rc))
        return rc;
    rc = spi_xfer(cmd, 4, NULL, 0, data, size);
    if (R_FAILED(rc))
        return rc;
    return spi_wait_idle();
}

int ds_cart_game_index(char *code_out, int code_len)
{
    u8 hdr[0x3B4];
    char code[5];
    bool inserted = false, powered = false;
    FS_CardType type = CARD_CTR;
    int i;

    if (R_FAILED(FSUSER_CardSlotIsInserted(&inserted)) || !inserted)
        return -1;
    if (R_FAILED(FSUSER_GetCardType(&type)) || type != CARD_TWL)
        return -1;
    FSUSER_CardSlotPowerOn(&powered);
    memset(hdr, 0, sizeof(hdr));
    if (R_FAILED(FSUSER_GetLegacyRomHeader(MEDIATYPE_GAME_CARD, 0, hdr)))
        return -1;
    memcpy(code, hdr + 0x0C, 4);
    code[4] = 0;
    pt_log("twl cart code=%.4s", code);
    if (code_out && code_len > 0)
        snprintf(code_out, (size_t)code_len, "%.4s", code);
    for (i = 0; i < PT_GAME_COUNT; i++) {
        if (PT_GAMES[i].archive == PT_ARCH_DS && PT_GAMES[i].nds3[0] &&
            strncmp(code, PT_GAMES[i].nds3, 3) == 0)
            return i;
    }
    return -1;
}

static int ensure_pxi(const char *nds3)
{
    Result rc;
    bool inserted = false, powered = false;
    if (g_pxi && g_chip != CHIP_NONE)
        return 0;
    if (R_FAILED(FSUSER_CardSlotIsInserted(&inserted)) || !inserted) {
        pt_log("ds spi no cart");
        return -1;
    }
    FSUSER_CardSlotPowerOn(&powered);
    rc = pxiDevInit();
    pt_log("pxiDevInit 0x%08lx", (unsigned long)rc);
    if (R_FAILED(rc))
        return -1;
    g_pxi = 1;
    if (detect_chip(nds3) != 0) {
        pt_log("ds spi chip detect fail");
        return -1;
    }
    pt_log("ds spi chip=%d cap=%lu ir=%d", g_chip, (unsigned long)chip_capacity(g_chip), g_ir);
    return 0;
}

int ds_spi_read(const char *nds3, uint8_t **out, u32 *size)
{
    u32 cap, got = 0;
    uint8_t *buf;
    if (ensure_pxi(nds3) != 0)
        return -1;
    cap = chip_capacity(g_chip);
    if (cap == 0)
        return -1;
    buf = malloc(cap);
    if (!buf)
        return -1;
    while (got < cap) {
        u32 chunk = cap - got;
        if (chunk > 0x1000)
            chunk = 0x1000;
        if (R_FAILED(spi_read_at(got, buf + got, chunk))) {
            free(buf);
            return -1;
        }
        got += chunk;
    }
    *out = buf;
    *size = cap;
    pt_log("ds spi read %lu bytes", (unsigned long)cap);
    return 0;
}

int ds_spi_write(const uint8_t *data, u32 size)
{
    u32 cap, pos = 0;
    if (!g_pxi || g_chip == CHIP_NONE || !data)
        return -1;
    cap = chip_capacity(g_chip);
    if (size > cap)
        size = cap;
    while (pos < size) {
        u32 n = 256 - (pos % 256);
        if (n > size - pos)
            n = size - pos;
        if (R_FAILED(spi_write_page(pos, data + pos, n))) {
            pt_log("ds spi write fail at %lu", (unsigned long)pos);
            return -1;
        }
        pos += n;
    }
    pt_log("ds spi wrote %lu bytes", (unsigned long)size);
    return 0;
}

void ds_spi_end(void)
{
    if (g_pxi) {
        pxiDevExit();
        g_pxi = 0;
    }
    g_chip = CHIP_NONE;
    g_ir = 0;
}
