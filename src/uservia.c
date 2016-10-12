/*B-em v2.1 by Tom Walker
  User VIA + Master 512 mouse emulation*/

#include <stdio.h>
#include <allegro.h>
#include "b-em.h"
#include "via.h"
#include "uservia.h"
#include "model.h"
#include "compact_joystick.h"
#include "mouse.h"

VIA uservia;

uint8_t lpt_dac;

void uservia_set_ca1(int level)
{
        via_set_ca1(&uservia, level);
}
void uservia_set_ca2(int level)
{
        via_set_ca2(&uservia, level);
}
void uservia_set_cb1(int level)
{
        via_set_cb1(&uservia, level);
}
void uservia_set_cb2(int level)
{
        via_set_cb2(&uservia, level);
}

void uservia_write_portA(uint8_t val)
{
        lpt_dac = val; /*Printer port - no printer, just 8-bit DAC*/
}

static FILE *upfp;
static int  upbit;
static uint8_t upbyte;

void uservia_write_portB(uint8_t val)
{
    /*User port - nothing emulated*/

    if (upfp == NULL)
        upfp = fopen("userport.dat", "wb");
    if (upfp != NULL) {
        val &= 0x04;
        switch(upbit) {
            case 0:
                if (val == 0) { // start bit;
                    upbit = 1;
                    upbyte = 0;
                }
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
                upbyte >>= 1;
                if (val)
                    upbyte |= 0x80;
                upbit++;
                break;
            case 9:
                putc(upbyte, upfp);
                fflush(upfp);
                upbit = 0;
        }
    }
}


uint8_t uservia_read_portA()
{
        return 0xff; /*Printer port - read only*/
}

uint8_t uservia_read_portB()
{
        return 0x00; /*User port - nothing emulated*/
        if (curtube == 3 || mouse_amx) return mouse_portb;
        if (compactcmos) return compact_joystick_read();
}

void uservia_write(uint16_t addr, uint8_t val)
{
        via_write(&uservia, addr, val);
}

uint8_t uservia_read(uint16_t addr)
{
        return via_read(&uservia, addr);
}

void uservia_updatetimers()
{
        via_updatetimers(&uservia);
}

void uservia_reset()
{
        via_reset(&uservia);

        uservia.read_portA = uservia_read_portA;
        uservia.read_portB = uservia_read_portB;

        uservia.write_portA = uservia_write_portA;
        uservia.write_portB = uservia_write_portB;

        uservia.intnum = 2;
}

void dumpuservia()
{
        bem_debugf("T1 = %04X %04X T2 = %04X %04X\n",uservia.t1c,uservia.t1l,uservia.t2c,uservia.t2l);
        bem_debugf("%02X %02X  %02X %02X\n",uservia.ifr,uservia.ier,uservia.pcr,uservia.acr);
}

void uservia_savestate(FILE *f)
{
        via_savestate(&uservia, f);
}

void uservia_loadstate(FILE *f)
{
        via_loadstate(&uservia, f);
}
