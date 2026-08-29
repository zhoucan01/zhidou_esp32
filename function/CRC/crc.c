#include "crc.h"

#include <stdint.h>
#include <stddef.h>

uint16_t CRC16_Calc(uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i;
    
    while(len >= 4)
    {
        // 小端序组合，与STM32硬件一致
        uint32_t word = (uint32_t)data[0] | 
                        ((uint32_t)data[1] << 8) | 
                        ((uint32_t)data[2] << 16) | 
                        ((uint32_t)data[3] << 24);
        
        crc ^= word;
        
        for(i = 0; i < 32; i++)
        {
            if(crc & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc <<= 1;
        }
        
        data += 4;
        len -= 4;
    }
    
    if(len > 0)
    {
        uint32_t word = 0;
        for(i = 0; i < len; i++)
            word |= (uint32_t)data[i] << (i * 8);
        
        crc ^= word;
        
        for(i = 0; i < 32; i++)
        {
            if(crc & 0x80000000)
                crc = (crc << 1) ^ 0x04C11DB7;
            else
                crc <<= 1;
        }
    }
    
    return (uint16_t)(crc & 0xFFFF);
}