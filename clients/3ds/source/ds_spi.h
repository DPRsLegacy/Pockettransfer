#ifndef POCKETTRANSFER_DS_SPI_H
#define POCKETTRANSFER_DS_SPI_H

#include <3ds.h>
#include <stdint.h>

/* Identify an inserted TWL (DS) cart by 3-letter game code. Returns PT_GAMES index or -1. */
int ds_cart_game_index(char *code_out, int code_len);

int ds_spi_read(const char *nds3, uint8_t **out, u32 *size);
int ds_spi_write(const uint8_t *data, u32 size);
void ds_spi_end(void);

#endif
