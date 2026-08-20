#ifndef POCKETTRANSFER_LITE_CHECK_H
#define POCKETTRANSFER_LITE_CHECK_H

#include <stddef.h>
#include <stdint.h>

int lite_is_known_size(size_t size);
int lite_validate_pkm(const uint8_t *data, size_t size, char *err, size_t err_len);

#endif
