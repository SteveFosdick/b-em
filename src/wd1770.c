/*B-em v2.2 by Tom Walker
  1770 FDC emulation*/
#include <stdio.h>
#include <stdlib.h>
#include "b-em.h"
#include "6502.h"
#include "wd1770.h"
#include "disc.h"
#include "model.h"

#define ABS(x) (((x)>0)?(x):-(x))

void wd1770_callback();
void wd1770_data(uint8_t dat);
void wd1770_spindown();
void wd1770_finishread();
void wd1770_notfound();
void wd1770_datacrcerror();
void wd1770_headercrcerror();
void wd1770_writeprotect();
int  wd1770_getdata(int last);

struct
{
    uint8_t command, sector, track, status, data;
    uint8_t ctrl;
    int curside,curtrack;
    int density;
    int written;
    int stepdir;
} wd1770;

int byte;

void wd1770_reset()
{
    nmi = 0;
    wd1770.status = 0;
    motorspin = 0;
//  bem_debug("Reset 1770\n");
    fdc_time = 0;
    if (WD1770)
    {
        fdc_callback       = wd1770_callback;
        fdc_data           = wd1770_data;
        fdc_spindown       = wd1770_spindown;
        fdc_finishread     = wd1770_finishread;
        fdc_notfound       = wd1770_notfound;
        fdc_datacrcerror   = wd1770_datacrcerror;
        fdc_headercrcerror = wd1770_headercrcerror;
        fdc_writeprotect   = wd1770_writeprotect;
        fdc_getdata        = wd1770_getdata;
    }
    motorspin = 45000;
}

void wd1770_spinup()
{
    wd1770.status |= 0x80;
    motoron = 1;
    motorspin = 0;
}

void wd1770_spindown()
{
    wd1770.status &= ~0x80;
    motoron = 0;
}

void wd1770_setspindown()
{
    motorspin = 45000;
}

#define track0 (wd1770.curtrack ? 4 : 0)

static void write_1770(uint16_t addr, uint8_t val)
{
    switch (addr & 0x03)
    {
        case 0: // Command register
            if (wd1770.status & 1 && (val >> 4) != 0xD) {
                bem_debugf("wd1770: command %02x rejected\n", val);
                return;
            }
            wd1770.command = val;
            if ((val >> 4) != 0xD)/* && !(val&8)) */
                wd1770_spinup();
            switch (val >> 4)
            {
                case 0x0: /*Restore*/
                    bem_debug("wd1770: restore\n");
                    wd1770.curtrack = 0;
                    wd1770.status = 0x80 | 0x21 | track0;
                    disc_seek(curdrive, 0);
                    break;

                case 0x1: /*Seek*/
                    bem_debugf("wd1770: seek track=%02d\n", wd1770.track);
                    wd1770.curtrack = wd1770.data;
                    wd1770.status = 0x80 | 0x21 | track0;
                    disc_seek(curdrive, wd1770.curtrack);
                    break;

                case 0x2:
                case 0x3: /*Step*/
                    bem_debug("wd1770: step\n");
                    wd1770.status = 0x80 | 0x21 | track0;
                    wd1770.curtrack += wd1770.stepdir;
                    if (wd1770.curtrack < 0)
                        wd1770.curtrack = 0;
                    disc_seek(curdrive, wd1770.curtrack);
                    break;

                case 0x4:
                case 0x5: /*Step in*/
                    bem_debug("wd1770: step in\n");
                    wd1770.status = 0x80 | 0x21 | track0;
                    wd1770.curtrack++;
                    disc_seek(curdrive, wd1770.curtrack);
                    wd1770.stepdir = 1;
                    break;

                case 0x6:
                case 0x7: /*Step out*/
                    bem_debug("wd1770: step out\n");
                    wd1770.status = 0x80 | 0x21 | track0;
                    wd1770.curtrack--;
                    if (wd1770.curtrack < 0)
                        wd1770.curtrack = 0;
                    disc_seek(curdrive, wd1770.curtrack);
                    wd1770.stepdir = -1;
                    break;

                case 0x8: /*Read sector*/
                    bem_debugf("wd1770: read sector drive=%d side=%d track=%d sector=%d dens=%d\n", curdrive, wd1770.curside, wd1770.track, wd1770.sector, wd1770.density);
                    wd1770.status = 0x80 | 0x1;
                    disc_readsector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, wd1770.density);
                    byte = 0;
                    break;

                case 0xA:
                    bem_debugf("wd1770: write sector drive=%d side=%d track=%d sector=%d dens=%d\n", curdrive, wd1770.curside, wd1770.track, wd1770.sector, wd1770.density);
                    wd1770.status = 0x80 | 0x1;
                    disc_writesector(curdrive, wd1770.sector, wd1770.track, wd1770.curside, wd1770.density);
                    byte = 0;
                    nmi |= 2;
                    wd1770.status |= 2;
                    //Carlo Concari: wait for first data byte before starting sector write
                    wd1770.written = 0;

                case 0xC: /*Read address*/
                    bem_debugf("wd1770: read address side=%d track=%d dens=%d\n", wd1770.curside, wd1770.track, wd1770.density);
                    wd1770.status = 0x80 | 0x1;
                    disc_readaddress(curdrive, wd1770.track, wd1770.curside, wd1770.density);
                    byte = 0;
                    break;

                case 0xD: /*Force interrupt*/
                    bem_debug("wd1770: force interrupt\n");
                    fdc_time = 0;
                    if (wd1770.status & 0x01)
                        wd1770.status &= ~1;
                    else
                        wd1770.status = 0x80 | track0;
                    nmi = (val & 8) && (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER) ? 1 : 0;
                    wd1770_setspindown();
                    break;

                case 0xF: /*Write track*/
                    bem_debugf("wd1770: write track side=%d track=%d dens=%d\n", wd1770.curside, wd1770.track, wd1770.density);
                    wd1770.status = 0x80 | 0x1;
                    disc_format(curdrive, wd1770.track, wd1770.curside, wd1770.density);
                    break;

                default:
                    bem_debugf("wd1770: bad 1770 command %02X\n",val);
                    fdc_time = 0;
                    if (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER)
                        nmi = 1;
                    wd1770.status = 0x90;
                    wd1770_spindown();
                    break;
            }
            break;
        case 1: // Track register
            bem_debugf("wd1770: write track register, track=%02x\n", val);
            wd1770.track = val;
            break;

        case 2: // Sector register.
            bem_debugf("wd1770: write sector register, sector=%02x\n", val);
            wd1770.sector = val;
            break;

        case 3: // data register
            nmi &= ~2;
            wd1770.status &= ~2;
            wd1770.data = val;
            wd1770.written = 1;
    }
}

static void write_ctrl_acorn(uint8_t val)
{
    bem_debugf("wd1770: write acorn-style ctrl %02X\n", val);
    if (val & 0x20)
        wd1770_reset();
    wd1770.ctrl = val;
    curdrive = (val & 0x02) ? 1 : 0;
    wd1770.curside =  (wd1770.ctrl & 0x04) ? 1 : 0;
    wd1770.density = !(wd1770.ctrl & 0x08);
}

static void write_ctrl_master(uint8_t val)
{
    bem_debugf("wd1770: write master-style ctrl %02X\n", val);
    if (val & 0x04)
        wd1770_reset();
    wd1770.ctrl = val;
    curdrive = (val & 2) ? 1 : 0;
    wd1770.curside =  (wd1770.ctrl & 0x10) ? 1 : 0;
    wd1770.density = !(wd1770.ctrl & 0x20);
}

static void write_ctrl_stl(uint8_t val)
{
    bem_debugf("wd1770: write solidisk-style ctrl %02X\n", val);
    wd1770.ctrl = val;
    curdrive = (val & 0x01);
    wd1770.curside =  (wd1770.ctrl & 0x02) ? 1 : 0;
    wd1770.density = !(wd1770.ctrl & 0x04);
}

void wd1770_write(uint16_t addr, uint8_t val)
{
    switch (WD1770)
    {
        case WD1770_ACORN:
            if (addr & 0x0004)
                write_1770(addr, val);
            else
                write_ctrl_acorn(val);
            break;
        case WD1770_MASTER:
            if (addr & 0x0008)
                write_1770(addr, val);
            else
                write_ctrl_master(val);
            break;
        case WD1770_SOLIDISK:
            if (addr & 0x0004)
                write_ctrl_stl(val);
            else
                write_1770(addr, val);
            break;
        default:
            bem_warnf("unrecognised WD1770 board %d\n", WD1770);
    }
}

static uint8_t read_1770(uint16_t addr)
{
    switch (addr & 0x03)
    {
        case 0:
            //bem_debugf("wd1770: status %02X, pc=%04x, nmi=%02x\n",wd1770.status, pc, nmi);
            return wd1770.status;

        case 1:
            return wd1770.track;

        case 2:
            return wd1770.sector;

        case 3:
            nmi &= ~2;
            wd1770.status &= ~2;
            //bem_debugf("wd1770: Read data %02X %04X\n",wd1770.data,pc);
            return wd1770.data;
    }
    bem_debug("wd1770: returning unmapped status\n");
    return 0xfe;
}

uint8_t wd1770_read(uint16_t addr)
{
    //bem_debugf("wd1770_read: addr=%04x, 1770=%d\n", addr, WD1770);
    switch (WD1770)
    {
        case WD1770_ACORN:
            if (addr & 0x0004)
                return read_1770(addr);
            break;
        case WD1770_MASTER:
            if (addr & 0x0008)
                return read_1770(addr);
            break;
        case WD1770_SOLIDISK:
            return read_1770(addr);
    }
    return 0xFE;
}

void wd1770_callback()
{
    bem_debugf("wd1770: FDC callback %02X\n",wd1770.command);
    fdc_time = 0;
    switch (wd1770.command >> 4)
    {
        case 0: /*Restore*/
        case 1: /*Seek*/
        case 3: /*Step*/
        case 5: /*Step in*/
        case 7: /*Step out*/
            wd1770.track = wd1770.curtrack;

        case 2: /*Step*/
        case 4: /*Step in*/
        case 6: /*Step out*/
            wd1770.status = 0x80 | track0;
            wd1770_setspindown();
            if (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER)
                nmi |= 1;
            break;

        case 8: /*Read sector*/
            wd1770.status = 0x80;
            wd1770_setspindown();
            if (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER)
                nmi |= 1;
            break;

        case 0xA: /*Write sector*/
            wd1770.status = 0x80;
            wd1770_setspindown();
            if (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER)
                nmi |= 1;
            break;

        case 0xC: /*Read address*/
            wd1770.status = 0x80;
            wd1770_setspindown();
            if (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER)
                nmi |= 1;
            wd1770.sector = wd1770.track;
            break;

        case 0xF: /*Write track*/
            wd1770.status = 0x80;
            wd1770_setspindown();
            if (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER)
                nmi |= 1;
            break;
    }
}

void wd1770_data(uint8_t dat)
{
    wd1770.data = dat;
    wd1770.status |= 2;
    nmi |= 2;
}

void wd1770_finishread()
{
    fdc_time = 200;
}

void wd1770_notfound()
{
    bem_debug("wd1770: not found\n");
    fdc_time = 0;
    nmi = (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER) ? 1 : 0;
    wd1770.status = 0x90;
    wd1770_spindown();
}

void wd1770_datacrcerror()
{
    // bem_debug("Data CRC\n");
    fdc_time = 0;
    nmi = (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER) ? 1 : 0;
    wd1770.status = 0x88;
    wd1770_spindown();
}

void wd1770_headercrcerror()
{
    // bem_debug("Header CRC\n");
    fdc_time = 0;
    nmi = (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER) ? 1 : 0;
    wd1770.status = 0x98;
    wd1770_spindown();
}

int wd1770_getdata(int last)
{
    // bem_debug("Disc get data\n");
    if (!wd1770.written)
        return -1;
    if (!last)
    {
        nmi |= 2;
        wd1770.status |= 2;
    }
    wd1770.written = 0;
    return wd1770.data;
}

void wd1770_writeprotect()
{
    fdc_time = 0;
    nmi = (WD1770 == WD1770_ACORN || WD1770 == WD1770_MASTER) ? 1 : 0;
    wd1770.status = 0xC0;
    wd1770_spindown();
}
