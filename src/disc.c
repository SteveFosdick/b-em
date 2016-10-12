/*B-em v2.2 by Tom Walker
  Disc support*/

#include <allegro.h>
#include <stdio.h>
#include "b-em.h"

#include "fdi.h"
#include "sdf.h"

#include "disc.h"

#include "ddnoise.h"

DRIVE drives[2];

int curdrive = 0;

char discfns[2][260] = {"", ""};
int defaultwriteprot = 0;
int writeprot[NUM_DRIVES], fwriteprot[NUM_DRIVES];

int fdc_time;
int disc_time;

int motorspin;
int motoron;

void (*fdc_callback)();
void (*fdc_data)(uint8_t dat);
void (*fdc_spindown)();
void (*fdc_finishread)();
void (*fdc_notfound)();
void (*fdc_datacrcerror)();
void (*fdc_headercrcerror)();
void (*fdc_writeprotect)();
int  (*fdc_getdata)(int last);

void disc_load(int drive, char *fn)
{
    const char *p;

    setejecttext(drive, "");
    if (!fn) return;
    p = get_extension(fn);
    if (!p) return;
    setejecttext(drive, fn);
    if (strcasecmp(p, "fdi") == 0) {
        bem_debugf("Loading %i: %s as FDI\n");
        fdi_load(drive, fn);
    } else {
        bem_debugf("Loading %i: %s as SDF\n");
        sdf_load(drive, fn);
    }
}

void disc_new(int drive, char *fn)
{
    FILE *f;

    char *p = get_extension(fn);
    if (p == NULL) {
        bem_error("The filename needs an extension to identify the format");
        return;
    }
    if (!strcasecmp(p, "ADF")) {
        if ((f = fopen(fn, "wb"))) {
            fseek(f, 0, SEEK_SET);
            putc(7, f);
            fseek(f, 0xFD, SEEK_SET);
            putc(5, f); putc(0, f); putc(0xC, f); putc(0xF9, f); putc(0x04, f);
            fseek(f, 0x1FB, SEEK_SET);
            putc(0x88,f); putc(0x39,f); putc(0,f); putc(3,f); putc(0xC1,f);
            putc(0, f); putc('H', f); putc('u', f); putc('g', f); putc('o', f);
            fseek(f, 0x6CC, SEEK_SET);
            putc(0x24, f);
            fseek(f, 0x6D6, SEEK_SET);
            putc(2, f); putc(0, f); putc(0, f); putc(0x24, f);
            fseek(f, 0x6FB, SEEK_SET);
            putc('H', f); putc('u', f); putc('g', f); putc('o', f);
            fclose(f);
            disc_load(drive, fn);
        } else
            bem_errorf("Unable to open disk image %s for writing: %s", fn, strerror(errno));
    } else if (!strcasecmp(p, "ADL")) {
        if ((f = fopen(fn, "wb"))) {
            fseek(f, 0, SEEK_SET);
            putc(7, f);
            fseek(f, 0xFD, SEEK_SET);
            putc(0xA, f); putc(0, f); putc(0x11, f); putc(0xF9, f); putc(0x09, f);
            fseek(f, 0x1FB, SEEK_SET);
            putc(0x01, f); putc(0x84, f); putc(0, f); putc(3, f); putc(0x8A, f);
            putc(0, f); putc('H', f); putc('u', f); putc('g', f); putc('o', f);
            fseek(f, 0x6CC, SEEK_SET);
            putc(0x24, f);
            fseek(f, 0x6D6, SEEK_SET);
            putc(2, f); putc(0, f); putc(0, f); putc(0x24, f);
            fseek(f, 0x6FB, SEEK_SET);
            putc('H', f); putc('u', f); putc('g', f); putc('o', f);
            fclose(f);
            disc_load(drive, fn);
        } else
            bem_errorf("Unable to open disk image %s for writing: %s", fn, strerror(errno));
    } else
        bem_errorf("Creating new disks of format %s not supported", p);
}

void disc_close(int drive)
{
    if (drives[0].close) drives[0].close(drive);
}

int disc_notfound=0;

void disc_init()
{
    drives[0].poll = drives[1].poll = 0;
    drives[0].seek = drives[1].seek = 0;
    drives[0].readsector = drives[1].readsector = 0;
    curdrive = 0;
}

void disc_poll()
{
    if (drives[curdrive].poll)
        drives[curdrive].poll();
    if (disc_notfound)
    {
        disc_notfound--;
        if (!disc_notfound)
            fdc_notfound();
    }
}

int oldtrack[2] = {0, 0};

void disc_seek(int drive, int track)
{
    if (drives[drive].seek)
        drives[drive].seek(drive, track);
    ddnoise_seek(track - oldtrack[drive]);
    oldtrack[drive] = track;
}

void disc_readsector(int drive, int sector, int track, int side, int density)
{
    if (drives[drive].readsector)
        drives[drive].readsector(drive, sector, track, side, density);
    else
        disc_notfound = 10000;
}

void disc_writesector(int drive, int sector, int track, int side, int density)
{
    if (drives[drive].writesector)
        drives[drive].writesector(drive, sector, track, side, density);
    else
        disc_notfound = 10000;
}

void disc_readaddress(int drive, int track, int side, int density)
{
    if (drives[drive].readaddress)
        drives[drive].readaddress(drive, track, side, density);
    else
        disc_notfound = 10000;
}

void disc_format(int drive, int track, int side, int density)
{
    if (drives[drive].format)
        drives[drive].format(drive, track, side, density);
    else
        disc_notfound = 10000;
}

void disc_abort(int drive)
{
    if (drives[drive].abort)
        drives[drive].abort(drive);
    else
        disc_notfound = 10000;
}
