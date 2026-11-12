#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nanomodbus.h"
#include "ModbusMap.h"

static uint8_t frame[260];
static size_t frame_size, cursor;
static uint16_t crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0xffff;
    while (size--) {
        crc ^= *data++;
        for (unsigned b=0;b<8;b++) crc=(crc>>1)^((crc&1)?0xa001:0);
    }
    return crc;
}
static int32_t read_bytes(uint8_t *out, uint16_t n, int32_t timeout, void *arg) {
    (void)timeout; (void)arg;
    if (n > frame_size-cursor) n=(uint16_t)(frame_size-cursor);
    memcpy(out,frame+cursor,n); cursor+=n; return n;
}
static int32_t write_bytes(const uint8_t *in, uint16_t n, int32_t timeout, void *arg) {
    (void)timeout; (void)arg;
    assert(n==8 && in[0]==2 && in[1]==4 && in[2]==0 && in[3]==0);
    assert(in[4]==0 && in[5]==80); cursor=0; return n;
}
static void make_frame(unsigned count, uint16_t a, uint16_t b, uint16_t c) {
    memset(frame,0,sizeof(frame)); frame[0]=2; frame[1]=4; frame[2]=(uint8_t)(count*2);
    for (unsigned i=0;i<count;i++) {
        uint16_t value=(uint16_t)(100+i);
        if (i==0 || i==65 || i==67) value=0;
        if (i==1) value=a;
        if (i==66) value=b;
        if (i==68) value=c;
        frame[3+i*2]=(uint8_t)(value>>8); frame[4+i*2]=(uint8_t)value;
    }
    frame_size=3+count*2;
    uint16_t crc=crc16(frame,frame_size);
    frame[frame_size++]=(uint8_t)crc; frame[frame_size++]=(uint8_t)(crc>>8);
}
static uint32_t value_at(const uint16_t *r, unsigned i) {
    return ((uint32_t)r[i]<<16)|r[i+1];
}
int main(void) {
    _Static_assert(MB_ACT_INPUT_COUNT==80,"register count changed");
    _Static_assert(MB_ACT_INPUT_TEMPERATURE_HIGH==0,"NTC1 address changed");
    _Static_assert(MB_ACT_INPUT_NTC2_HIGH==65,"NTC2 address changed");
    _Static_assert(MB_ACT_INPUT_NTC3_HIGH==67,"NTC3 address changed");
    nmbs_platform_conf p; nmbs_platform_conf_create(&p);
    p.transport=NMBS_TRANSPORT_RTU; p.read=read_bytes; p.write=write_bytes;
    nmbs_t client; assert(nmbs_client_create(&client,&p)==NMBS_ERROR_NONE);
    nmbs_set_destination_rtu_address(&client,2);
    struct { uint16_t before[4], r[80], after[4]; } guarded;
    for (unsigned test=0;test<8;test++) {
        memset(&guarded,0xa5,sizeof(guarded));
        make_frame(80,1701,1802,1903);
        if (test==1) make_frame(80,3001,3002,3003);
        if (test==2) make_frame(81,1701,1802,1903);
        if (test==3) make_frame(40,1701,1802,1903);
        if (test==4) frame[frame_size-1]^=1;
        if (test==5) frame[0]=3;
        if (test==6) frame[1]=3;
        if (test==7) frame_size-=5;
        nmbs_error e=nmbs_read_input_registers(&client,0,80,guarded.r);
        for(unsigned i=0;i<4;i++) assert(guarded.before[i]==0xa5a5 && guarded.after[i]==0xa5a5);
        if(test<2) {
            assert(e==NMBS_ERROR_NONE);
            assert(value_at(guarded.r,0)==(test?3001:1701));
            assert(value_at(guarded.r,65)==(test?3002:1802));
            assert(value_at(guarded.r,67)==(test?3003:1903));
        } else assert(e!=NMBS_ERROR_NONE);
    }
    puts("PASS: real nanoModbus FC04 80-register decode, NTC addresses, canaries, CRC/length/unit/function/truncation rejection");
}
