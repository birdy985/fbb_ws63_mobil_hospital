#ifndef AD8232_BSP_H
#define AD8232_BSP_H

#include <stdint.h>
#include "errcode.h"

errcode_t ad8232_init(void);
errcode_t ad8232_read_mv(uint16_t *voltage_mv);

#endif
