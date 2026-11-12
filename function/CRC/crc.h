#ifndef __CRC__H
#define __CRC__H

#include <stdint.h>
#include <stddef.h>
//crc计算函数
uint16_t CRC16_Calc(uint8_t *data,uint32_t len);

#endif