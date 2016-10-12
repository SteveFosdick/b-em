/*
 * B-EM SDF - Simple Disk Formats
 *
 * This is a module to handle the various disk image formats where
 * the sectors that comprise the disk image are stored in the file
 * in a logical order and without ID headers.
 *
 * It understands enough of the filing systems on Acorn to be able
 * to work out the geometry including the sector size, the number
 * of sectors per track etc. and whether the sides are interleaved
 * as used in DSD images or sequential as in SSD.  It can handle
 * double-density images that are not ADFS, i.e. are one of the
 * non-Acorn DFS.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "b-em.h"
#include "disc.h"

typedef enum {
    SIDES_NA,
    SIDES_SINGLE,
    SIDES_SEQUENTIAL,
    SIDES_INTERLEAVED
} sides_t;

typedef enum {
    DENS_NA,
    DENS_SINGLE,
    DENS_DOUBLE,
    DENS_QUAD
} density_t;

typedef struct {
    const char *name;
    sides_t    sides;
    density_t  density;
    uint16_t   size_in_sectors;
    uint8_t    tracks;
    uint8_t    sectors_per_track;
    uint16_t   sector_size;
} geometry_t;

static const geometry_t adfs_new_formats[] = {
    { "Acorn ADFS F", SIDES_INTERLEAVED, DENS_QUAD,  1600, 80, 10, 1024 },
    { "Acorn ADFS D", SIDES_INTERLEAVED, DENS_DOUBLE, 800, 80,  5, 1024 },
    { NULL,           SIDES_NA,          DENS_NA,       0,  0,  0,    0 }
};

static const geometry_t adfs_old_formats[] = {
    { "Acorn ADFS L", SIDES_INTERLEAVED, DENS_DOUBLE, 2560, 80, 16,  256 },
    { "Acorn ADFS M", SIDES_SINGLE,      DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Acorn ADFS S", SIDES_SINGLE,      DENS_DOUBLE,  640, 40, 16,  256 },
    { NULL,           SIDES_NA,          DENS_NA,        0,  0,  0,    0 }
};

static const geometry_t dfs_formats[] = {
    { "Watford/Opus DDFS", SIDES_INTERLEAVED, DENS_DOUBLE, 1440, 80, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SEQUENTIAL,  DENS_DOUBLE, 1440, 80, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SINGLE,      DENS_DOUBLE, 1440, 80, 18,  256 },
    { "Watford/Opus DDFS", SIDES_INTERLEAVED, DENS_DOUBLE,  720, 40, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SEQUENTIAL,  DENS_DOUBLE,  720, 40, 18,  256 },
    { "Watford/Opus DDFS", SIDES_SINGLE,      DENS_DOUBLE,  720, 40, 18,  256 },

    { "Solidisk DDFS",     SIDES_INTERLEAVED, DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Solidisk DDFS",     SIDES_SEQUENTIAL,  DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Solidisk DDFS",     SIDES_SINGLE,      DENS_DOUBLE, 1280, 80, 16,  256 },
    { "Solidisk DDFS",     SIDES_INTERLEAVED, DENS_DOUBLE,  640, 40, 16,  256 },
    { "Solidisk DDFS",     SIDES_SEQUENTIAL,  DENS_DOUBLE,  640, 40, 16,  256 },
    { "Solidisk DDFS",     SIDES_SINGLE,      DENS_DOUBLE,  640, 40, 16,  256 },

    { "Acorn DFS",         SIDES_INTERLEAVED, DENS_SINGLE, 800, 80, 10, 256 },
    { "Acorn DFS",         SIDES_SEQUENTIAL,  DENS_SINGLE, 800, 80, 10, 256 },
    { "Acorn DFS",         SIDES_SINGLE,      DENS_SINGLE, 800, 80, 10, 256 },
    { "Acorn DFS",         SIDES_INTERLEAVED, DENS_SINGLE, 400, 40, 10, 256 },
    { "Acorn DFS",         SIDES_SEQUENTIAL,  DENS_SINGLE, 400, 40, 10, 256 },
    { "Acorn DFS",         SIDES_SINGLE,      DENS_SINGLE, 400, 40, 10, 256 },
    { NULL,                SIDES_NA,          DENS_NA,       0,  0,  0,   0 }
};

static int check_id(FILE *fp, long posn, const char *id) {
    int ch;

    if (fseek(fp, posn, SEEK_SET) == -1)
        return 0;
    while ((ch = *id++))
        if (ch != getc(fp))
            return 0;
    return 1;
}

static const geometry_t *try_adfs_new(FILE *fp) {
    long size;
    const geometry_t *ptr;

    if (check_id(fp, 0x401, "Nick") || check_id(fp, 0x801, "Nick")) {
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        for (ptr = adfs_new_formats; ptr->name; ptr++)
            if (size == (ptr->size_in_sectors * ptr->sector_size))
                return ptr;
    }
    return NULL;
}

static const geometry_t *try_adfs_old(FILE *fp) {
    uint32_t sects;
    const geometry_t *ptr;

    if (check_id(fp, 0x201, "Hugo") && check_id(fp, 0x6fb, "Hugo")) {
        fseek(fp, 0xfc, SEEK_SET);
        sects = getc(fp) | (getc(fp) << 8) | (getc(fp) << 16);
        for (ptr = adfs_old_formats; ptr->name; ptr++)
            if (sects == ptr->size_in_sectors)
                return ptr;
    }
    return NULL;
}

static const geometry_t *try_dfs(FILE *fp) {
    uint32_t sects0, sects2, side2_off, track_bytes;
    const geometry_t *ptr;

    fseek(fp, 0x106, SEEK_SET);
    sects0 = ((getc(fp) & 3) << 8) + getc(fp);
    for (ptr = dfs_formats; ptr->name; ptr++) {
        if (sects0 == ptr->size_in_sectors) {
            if (ptr->sides == SIDES_SINGLE)
                return ptr;
            side2_off = track_bytes = ptr->sectors_per_track * ptr->sector_size;
            if (ptr->sides == SIDES_SEQUENTIAL)
                side2_off = ptr->tracks * track_bytes;
            if (fseek(fp, side2_off+0x106, SEEK_SET) == 0) {
                sects2 = ((getc(fp) & 3) << 8) + getc(fp);
                if (sects2 == sects0)
                    return ptr;
            }
        }
    }
    return NULL;
}

static void info_msg(int drive, const char *fn, const geometry_t *geo) {
    const char *sides;
    const char *dens;

    switch(geo->sides) {
        case SIDES_SINGLE:
            sides = "single-sided";
            break;
        case SIDES_SEQUENTIAL:
            sides = "doubled-sides, sequential";
            break;
        case SIDES_INTERLEAVED:
            sides = "double-sided, interleaved";
            break;
        default:
            sides = "unknown";
    }
    switch(geo->density) {
        case DENS_QUAD:
            dens = "quad";
            break;
        case DENS_DOUBLE:
            dens = "double";
            break;
        case DENS_SINGLE:
            dens = "single";
            break;
        default:
            dens = "unknown";
    }
    bem_debugf("Loaded drive %d with %s, format %s, %s, %d tracks, %s, %d %d byte sectors/track",
               drive, fn, geo->name, sides, geo->tracks, dens, geo->sectors_per_track, geo->sector_size);
}

static const geometry_t *geometry[NUM_DRIVES];
static FILE *sdf_fp[NUM_DRIVES];
static uint8_t current_track[NUM_DRIVES];

typedef enum {
    ST_IDLE,
    ST_NOTFOUND,
    ST_READSECTOR,
    ST_WRITESECTOR,
    ST_READ_ADDR0,
    ST_READ_ADDR1,
    ST_READ_ADDR2,
    ST_READ_ADDR3,
    ST_READ_ADDR4,
    ST_READ_ADDR5,
    ST_READ_ADDR6,
    ST_FORMAT
} state_t;

state_t state = ST_IDLE;

static uint16_t count = 0;

static int     sdf_time;
static uint8_t sdf_drive;
static uint8_t sdf_side;
static uint8_t sdf_track;
static uint8_t sdf_sector;

static void sdf_close(int drive) {
    if (drive < NUM_DRIVES) {
        geometry[drive] = NULL;
        if (sdf_fp[drive]) {
            fclose(sdf_fp[drive]);
            sdf_fp[drive] = NULL;
        }
    }
}

static void sdf_seek(int drive, int track) {
    if (drive < NUM_DRIVES)
        current_track[drive] = track;
}

static void io_seek(const geometry_t *geo, uint8_t drive, uint8_t sector, uint8_t track, uint8_t side) {
    uint32_t track_bytes, offset;

    track_bytes = geo->sectors_per_track * geo->sector_size;
    if (side == 0) {
        offset = track * track_bytes;
        if (geo->sides == SIDES_INTERLEAVED)
            offset *= 2;
    } else {
        if (geo->sides == SIDES_SEQUENTIAL)
            offset = (track + geo->tracks) * track_bytes;
        else
            offset = (track * 2 + 1) * track_bytes;
    }
    offset += sector * geo->sector_size;
    bem_debugf("sdf: seeking to %d bytes\n", offset);
    fseek(sdf_fp[drive], offset, SEEK_SET);
}

static void sdf_readsector(int drive, int sector, int track, int side, int density)
{
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (track == current_track[drive] && track >= 0 && track < geo->tracks) {
                        if (sector >= 0 && sector < geo->sectors_per_track ) {
                            if (side == 0 || geo->sides != SIDES_SINGLE) {
                                io_seek(geo, drive, sector, track, side);
                                count = geo->sector_size;
                                sdf_drive = drive;
                                state = ST_READSECTOR;
                                return;
                            }
                        }
                    }
                }
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void sdf_writesector(int drive, int sector, int track, int side, int density)
{
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (track == current_track[drive] && track >= 0 && track < geo->tracks) {
                        if (sector >= 0 && sector < geo->sectors_per_track ) {
                            if (side == 0 || geo->sides != SIDES_SINGLE) {
                                io_seek(geo, drive, sector, track, side);
                                count = geo->sector_size;
                                sdf_drive = drive;
                                sdf_side = side;
                                sdf_track = track;
                                sdf_sector = sector;
                                sdf_time = -20;
                                state = ST_WRITESECTOR;
                                return;
                            }
                        }
                    }
                }
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void sdf_readaddress(int drive, int track, int side, int density) {
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (track == current_track[drive] && track >= 0 && track < geo->tracks) {
                        if (side == 0 || geo->sides != SIDES_SINGLE) {
                            sdf_drive = drive;
                            sdf_side = side;
                            sdf_track = track;
                            state = ST_READ_ADDR0;
                            return;
                        }
                    }
                }
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void sdf_format(int drive, int track, int side, int density) {
    const geometry_t *geo;

    if (state == ST_IDLE) {
        if (drive < NUM_DRIVES) {
            if ((geo = geometry[drive])) {
                if ((!density && geo->density == DENS_SINGLE) || (density && geo->density == DENS_DOUBLE)) {
                    if (track == current_track[drive] && track >= 0 && track < geo->tracks) {
                        if (side == 0 || geo->sides != SIDES_SINGLE) {
                            io_seek(geo, drive, 0, track, side);
                            sdf_drive = drive;
                            sdf_side = side;
                            sdf_track = track;
                            sdf_sector = 0;
                            state = ST_FORMAT;
                        }
                    }
                }
            }
        }
        count = 500;
        state = ST_NOTFOUND;
    }
}

static void sdf_poll() {
    int c;

    if (++sdf_time <= 16)
        return;
    sdf_time = 0;

    switch(state) {
        case ST_IDLE:
            break;

        case ST_NOTFOUND:
            if (--count == 0) {
                fdc_notfound();
                state = ST_IDLE;
            }
            break;

        case ST_READSECTOR:
            fdc_data(getc(sdf_fp[sdf_drive]));
            if (--count == 0) {
                fdc_finishread();
                state = ST_IDLE;
            }
            break;

        case ST_WRITESECTOR:
            if (writeprot[sdf_drive]) {
                bem_debug("sdf: poll, write protected during write sector\n");
                fdc_writeprotect();
                state = ST_IDLE;
                break;
            }
            c = fdc_getdata(--count == 0);
            if (c == -1) {
                bem_warn("sdf: data underrun on write");
                count++;
            } else {
                putc(c, sdf_fp[sdf_drive]);
                if (count == 0) {
                    fdc_finishread();
                    state = ST_IDLE;
                }
            }
            break;

        case ST_READ_ADDR0:
            fdc_data(sdf_track);
            state = ST_READ_ADDR1;
            break;

        case ST_READ_ADDR1:
            fdc_data(sdf_side);
            state = ST_READ_ADDR2;
            break;

        case ST_READ_ADDR2:
            fdc_data(sdf_sector);
            state = ST_READ_ADDR3;
            break;

        case ST_READ_ADDR3:
            fdc_data(1);
            state = ST_READ_ADDR4;
            break;

        case ST_READ_ADDR4:
            fdc_data(0);
            state = ST_READ_ADDR5;
            break;

        case ST_READ_ADDR5:
            fdc_data(0);
            state = ST_READ_ADDR6;
            break;

        case ST_READ_ADDR6:
            fdc_finishread();
            sdf_sector++;
            if (sdf_sector == geometry[sdf_drive]->sectors_per_track)
                sdf_sector = 0;
            break;

        case ST_FORMAT:
            if (writeprot[sdf_drive]) {
                bem_debug("sdf: poll, write protected during write track\n");
                fdc_writeprotect();
                state = ST_IDLE;
                break;
            }
            if (--count == 0)
                if (++sdf_sector >= geometry[sdf_drive]->sectors_per_track)
                    break;
            putc(0, sdf_fp[sdf_drive]);
            break;
    }
}

static void sdf_abort(void) {
    state = ST_IDLE;
}

void sdf_load(int drive, const char *fn) {
    FILE *fp;
    const geometry_t *geo;

    writeprot[drive] = 0;
    if ((fp = fopen(fn, "rb+")) == NULL) {
        if ((fp = fopen(fn, "rb")) == NULL) {
            bem_errorf("Unable to open file '%s' for reading - %s", fn, strerror(errno));
            return;
        }
        writeprot[drive] = 1;
    }
    if ((geo = try_adfs_new(fp)) == NULL) {
        if ((geo = try_adfs_old(fp)) == NULL) {
            if ((geo = try_dfs(fp)) == NULL) {
                bem_errorf("Unable to determine geometry for %s", fn);
                fclose(fp);
                return;
            }
        }
    }
    sdf_fp[drive] = fp;
    info_msg(drive, fn, geo);
    geometry[drive] = geo;
    drives[drive].close       = sdf_close;
    drives[drive].seek        = sdf_seek;
    drives[drive].readsector  = sdf_readsector;
    drives[drive].writesector = sdf_writesector;
    drives[drive].readaddress = sdf_readaddress;
    drives[drive].poll        = sdf_poll;
    drives[drive].format      = sdf_format;
    drives[drive].abort       = sdf_abort;
}
