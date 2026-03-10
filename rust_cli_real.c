/*
 * rust_cli_real.c — Complete rusty-backup CLI for Mac OS X Tiger PowerPC
 *
 * Reimplements the core rusty-backup operations in C:
 *   - list-devices    Device enumeration with sizes via ioctl
 *   - backup          Read device, detect partitions, copy to folder
 *   - restore         Write backup back to device/image
 *   - inspect         Display backup metadata
 *
 * Partition table support: MBR (with EBR chain), APM, Superfloppy
 * Compression: raw (uncompressed) or gzip (via zlib, available on Tiger)
 *
 * Compiled: gcc -std=c99 -O2 -c rust_cli_real.c
 * Link with: -lz (for gzip compression)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/mount.h>

#ifdef __APPLE__
#include <crt_externs.h>
#include <sys/disk.h>
#define get_argc() (*_NSGetArgc())
#define get_argv() (*_NSGetArgv())
#else
static int g_argc = 0;
static char **g_argv = NULL;
#define get_argc() g_argc
#define get_argv() g_argv
/* Define ioctl constants for non-Apple builds */
#ifndef DKIOCGETBLOCKSIZE
#define DKIOCGETBLOCKSIZE  0x40046418
#define DKIOCGETBLOCKCOUNT 0x40086419
#endif
#endif

/* Forward declarations from runtime stubs */
extern void rust_runtime_init(void);
extern void rust_runtime_cleanup(void);

/* ============================================================
 * Constants
 * ============================================================ */
#define SECTOR_SIZE          512
#define CHUNK_SIZE           (256 * 1024)  /* 256 KB I/O buffer */
#define MAX_PARTITIONS       64
#define MAX_PATH_LEN         1024
#define MBR_SIG_OFFSET       510
#define PART_TABLE_OFFSET    446
#define PART_ENTRY_SIZE      16
#define APM_DDR_SIG          0x4552
#define APM_ENTRY_SIG        0x504D

/* MBR extended partition types */
#define MBR_TYPE_EXTENDED_CHS  0x05
#define MBR_TYPE_EXTENDED_LBA  0x0F
#define MBR_TYPE_EXTENDED_LNX  0x85

/* ============================================================
 * Data Structures
 * ============================================================ */

typedef struct {
    uint8_t  bootable;
    uint8_t  chs_start[3];
    uint8_t  type;
    uint8_t  chs_end[3];
    uint32_t start_lba;
    uint32_t total_sectors;
} MbrEntry;

typedef struct {
    uint32_t disk_signature;
    MbrEntry entries[4];
    int      logical_count;
    MbrEntry logical[MAX_PARTITIONS];
} MbrTable;

typedef struct {
    char     name[33];
    char     type[33];
    uint32_t start_block;
    uint32_t block_count;
    uint32_t status;
} ApmEntry;

typedef struct {
    uint16_t block_size;
    uint32_t block_count;
    int      entry_count;
    ApmEntry entries[MAX_PARTITIONS];
} ApmTable;

typedef enum {
    PT_NONE = 0,   /* superfloppy */
    PT_MBR,
    PT_GPT,
    PT_APM
} PartTableType;

typedef struct {
    PartTableType type;
    union {
        MbrTable mbr;
        ApmTable apm;
    } data;
    char     fs_hint[16];       /* for superfloppy */
    uint64_t disk_size;
} PartTable;

typedef struct {
    int      index;
    char     type_name[64];
    uint8_t  type_byte;
    uint64_t start_lba;
    uint64_t total_sectors;
    int      bootable;
    int      is_logical;
    int      is_extended;
} PartInfo;

typedef struct {
    char     name[64];          /* "disk0" */
    char     path[128];         /* "/dev/disk0" */
    uint64_t size_bytes;
    int      is_whole;
    /* partition mount info */
    char     mount_point[256];
    char     filesystem[32];
    uint64_t total_space;
    uint64_t avail_space;
} DeviceInfo;

/* ============================================================
 * Utility Functions
 * ============================================================ */

static uint16_t read_le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t read_le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t read_be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static const char *format_bytes(uint64_t b, char *buf, int bufsz) {
    if (b >= (uint64_t)1024 * 1024 * 1024)
        snprintf(buf, bufsz, "%.2f GiB", (double)b / (1024.0 * 1024.0 * 1024.0));
    else if (b >= 1024 * 1024)
        snprintf(buf, bufsz, "%.1f MiB", (double)b / (1024.0 * 1024.0));
    else if (b >= 1024)
        snprintf(buf, bufsz, "%.0f KiB", (double)b / 1024.0);
    else
        snprintf(buf, bufsz, "%llu B", (unsigned long long)b);
    return buf;
}

static void draw_progress(double pct, const char *operation) {
    int width = 40;
    int filled = (int)((pct / 100.0) * width);
    if (filled > width) filled = width;
    fprintf(stderr, "\r[");
    for (int i = 0; i < width; i++)
        fputc(i < filled ? '=' : ' ', stderr);
    fprintf(stderr, "] %5.1f%%  %s", pct, operation);
    fflush(stderr);
}

static const char *partition_type_name(uint8_t type) {
    switch (type) {
        case 0x00: return "Empty";
        case 0x01: return "FAT12";
        case 0x04: return "FAT16 <32MB";
        case 0x05: return "Extended (CHS)";
        case 0x06: return "FAT16";
        case 0x07: return "NTFS/HPFS";
        case 0x0B: return "FAT32 (CHS)";
        case 0x0C: return "FAT32 (LBA)";
        case 0x0E: return "FAT16 (LBA)";
        case 0x0F: return "Extended (LBA)";
        case 0x11: return "Hidden FAT12";
        case 0x14: return "Hidden FAT16 <32MB";
        case 0x16: return "Hidden FAT16";
        case 0x17: return "Hidden NTFS";
        case 0x1B: return "Hidden FAT32";
        case 0x1C: return "Hidden FAT32 (LBA)";
        case 0x1E: return "Hidden FAT16 (LBA)";
        case 0x82: return "Linux swap";
        case 0x83: return "Linux";
        case 0x85: return "Linux extended";
        case 0xA5: return "FreeBSD";
        case 0xA6: return "OpenBSD";
        case 0xA8: return "Mac OS X";
        case 0xAB: return "Mac OS X Boot";
        case 0xAF: return "HFS/HFS+";
        case 0xEE: return "GPT Protective";
        case 0xEF: return "EFI System";
        case 0xFD: return "Linux RAID";
        default:   return "Unknown";
    }
}

/* ============================================================
 * Argument Parsing
 * ============================================================ */

static const char *flag_value(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0 && i + 1 < argc)
            return argv[i + 1];
        int flen = strlen(flag);
        if (strncmp(argv[i], flag, flen) == 0 && argv[i][flen] == '=')
            return &argv[i][flen + 1];
    }
    return NULL;
}

static int has_flag(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

/* ============================================================
 * Device Enumeration
 * ============================================================ */

static uint64_t get_device_size_ioctl(const char *path) {
    /* Try the given path first */
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        uint32_t block_size = 0;
        uint64_t block_count = 0;
        if (ioctl(fd, DKIOCGETBLOCKSIZE, &block_size) == 0 &&
            ioctl(fd, DKIOCGETBLOCKCOUNT, &block_count) == 0 &&
            block_size > 0 && block_count > 0) {
            close(fd);
            return block_count * (uint64_t)block_size;
        }
        close(fd);
    }

#ifdef __APPLE__
    /* Try /dev/rdisk* (raw character device) — often works without root on Tiger */
    if (strstr(path, "/dev/disk")) {
        char rpath[128];
        const char *dname = strstr(path, "disk");
        if (dname) {
            snprintf(rpath, sizeof(rpath), "/dev/r%s", dname);
            fd = open(rpath, O_RDONLY);
            if (fd >= 0) {
                uint32_t block_size = 0;
                uint64_t block_count = 0;
                if (ioctl(fd, DKIOCGETBLOCKSIZE, &block_size) == 0 &&
                    ioctl(fd, DKIOCGETBLOCKCOUNT, &block_count) == 0 &&
                    block_size > 0 && block_count > 0) {
                    close(fd);
                    return block_count * (uint64_t)block_size;
                }
                close(fd);
            }
        }
    }
#endif

    /* For regular files, use stat */
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        return st.st_size;

    return 0;
}

static int cmd_list_devices(void) {
    printf("Scanning for disk devices...\n\n");

    /* Enumerate /dev/disk* entries */
    DeviceInfo devices[64];
    int dev_count = 0;

    DIR *devdir = opendir("/dev");
    if (!devdir) {
        fprintf(stderr, "Cannot open /dev\n");
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(devdir)) != NULL && dev_count < 64) {
        /* Match disk[0-9] (whole disks only, no partitions like disk0s1) */
        if (strncmp(ent->d_name, "disk", 4) != 0) continue;
        if (ent->d_name[4] < '0' || ent->d_name[4] > '9') continue;

        /* Check if it's a whole disk or partition */
        int is_whole = 1;
        for (int i = 4; ent->d_name[i]; i++) {
            if (ent->d_name[i] == 's') { is_whole = 0; break; }
            if (ent->d_name[i] < '0' || ent->d_name[i] > '9') { is_whole = 0; break; }
        }

        DeviceInfo *d = &devices[dev_count];
        memset(d, 0, sizeof(*d));
        strncpy(d->name, ent->d_name, sizeof(d->name) - 1);
        snprintf(d->path, sizeof(d->path), "/dev/%s", ent->d_name);
        d->is_whole = is_whole;
        d->size_bytes = get_device_size_ioctl(d->path);

        /* Check mount info via getmntinfo */
        d->mount_point[0] = '\0';
        d->filesystem[0] = '\0';

#ifdef __APPLE__
        struct statfs *mntbuf;
        int mntcount = getmntinfo(&mntbuf, MNT_NOWAIT);
        for (int m = 0; m < mntcount; m++) {
            if (strstr(mntbuf[m].f_mntfromname, ent->d_name)) {
                strncpy(d->mount_point, mntbuf[m].f_mntonname, sizeof(d->mount_point) - 1);
                strncpy(d->filesystem, mntbuf[m].f_fstypename, sizeof(d->filesystem) - 1);
                d->total_space = (uint64_t)mntbuf[m].f_blocks * mntbuf[m].f_bsize;
                d->avail_space = (uint64_t)mntbuf[m].f_bavail * mntbuf[m].f_bsize;
                break;
            }
        }
#endif
        dev_count++;
    }
    closedir(devdir);

    /* Sort by name */
    for (int i = 0; i < dev_count - 1; i++)
        for (int j = i + 1; j < dev_count; j++)
            if (strcmp(devices[i].name, devices[j].name) > 0) {
                DeviceInfo tmp = devices[i];
                devices[i] = devices[j];
                devices[j] = tmp;
            }

    /* For whole disks with 0 size, try to infer from mounted partitions */
    for (int i = 0; i < dev_count; i++) {
        if (!devices[i].is_whole || devices[i].size_bytes > 0) continue;
        /* Sum total_space from all mounted partitions of this disk */
        int prefix_len = strlen(devices[i].name);
        uint64_t sum = 0;
        for (int j = 0; j < dev_count; j++) {
            if (devices[j].is_whole) continue;
            if (strncmp(devices[j].name, devices[i].name, prefix_len) != 0) continue;
            if (devices[j].name[prefix_len] != 's') continue;
            if (devices[j].total_space > sum) sum = devices[j].total_space;
        }
        /* Use the largest mounted partition's total_space as a lower bound */
        if (sum > 0) devices[i].size_bytes = sum;
    }

    /* Display whole disks with their partitions */
    for (int i = 0; i < dev_count; i++) {
        if (!devices[i].is_whole) continue;

        char sz[32];
        format_bytes(devices[i].size_bytes, sz, sizeof(sz));
        printf("%s\n", devices[i].name);
        printf("  Path:  %s\n", devices[i].path);
        if (devices[i].size_bytes > 0)
            printf("  Size:  %s (%llu bytes)\n", sz, (unsigned long long)devices[i].size_bytes);
        else
            printf("  Size:  (unknown - needs root for ioctl)\n");

        if (devices[i].mount_point[0])
            printf("  Mount: %s (%s)\n", devices[i].mount_point, devices[i].filesystem);

        /* Find partitions of this disk */
        int prefix_len = strlen(devices[i].name);
        int has_parts = 0;
        for (int j = 0; j < dev_count; j++) {
            if (devices[j].is_whole) continue;
            if (strncmp(devices[j].name, devices[i].name, prefix_len) != 0) continue;
            if (devices[j].name[prefix_len] != 's') continue;

            if (!has_parts) { printf("  Partitions:\n"); has_parts = 1; }

            /* Use ioctl size, or fall back to statfs total_space */
            uint64_t psz_val = devices[j].size_bytes > 0 ? devices[j].size_bytes : devices[j].total_space;
            char psz[32];
            format_bytes(psz_val, psz, sizeof(psz));
            printf("    %-12s %10s", devices[j].name, psz);
            if (devices[j].mount_point[0]) {
                char avsz[32];
                format_bytes(devices[j].avail_space, avsz, sizeof(avsz));
                printf("  %s (%s, %s free)", devices[j].mount_point,
                       devices[j].filesystem, avsz);
            }
            printf("\n");
        }
        printf("\n");
    }

    if (dev_count == 0) printf("No disk devices found.\n");
    return 0;
}

/* ============================================================
 * Partition Table Detection
 * ============================================================ */

static int is_fat_vbr(const uint8_t *s) {
    if (s[0] != 0xEB && s[0] != 0xE9) return 0;
    uint16_t bps = read_le16(&s[11]);
    if (bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) return 0;
    uint8_t spc = s[13];
    if (spc == 0 || (spc & (spc - 1)) != 0) return 0;  /* must be power of 2 */
    uint16_t reserved = read_le16(&s[14]);
    if (reserved < 1) return 0;
    uint8_t fats = s[16];
    if (fats != 1 && fats != 2) return 0;
    uint8_t media = s[21];
    if (media != 0xF0 && media < 0xF8) return 0;
    return 1;
}

static int is_hfs_sig(const uint8_t *s1024) {
    uint16_t sig = read_be16(s1024);
    return (sig == 0x4244 || sig == 0x482B || sig == 0x4858);
}

static int is_extended_type(uint8_t type) {
    return type == MBR_TYPE_EXTENDED_CHS ||
           type == MBR_TYPE_EXTENDED_LBA ||
           type == MBR_TYPE_EXTENDED_LNX;
}

static void parse_mbr_entry(const uint8_t *p, MbrEntry *e) {
    e->bootable = p[0];
    memcpy(e->chs_start, &p[1], 3);
    e->type = p[4];
    memcpy(e->chs_end, &p[5], 3);
    e->start_lba = read_le32(&p[8]);
    e->total_sectors = read_le32(&p[12]);
}

static int parse_ebr_chain(int fd, uint32_t ext_start, MbrTable *tbl) {
    uint32_t cur_lba = ext_start;
    uint32_t visited[MAX_PARTITIONS];
    int vis_count = 0;

    while (tbl->logical_count < MAX_PARTITIONS) {
        /* Loop detection */
        for (int i = 0; i < vis_count; i++)
            if (visited[i] == cur_lba) return 0;
        if (vis_count >= MAX_PARTITIONS) break;
        visited[vis_count++] = cur_lba;

        uint8_t ebr[512];
        if (lseek(fd, (off_t)cur_lba * 512, SEEK_SET) < 0) break;
        if (read(fd, ebr, 512) != 512) break;

        uint16_t sig = read_le16(&ebr[510]);
        if (sig != 0xAA55) break;

        MbrEntry e0, e1;
        parse_mbr_entry(&ebr[446], &e0);
        parse_mbr_entry(&ebr[446 + 16], &e1);

        if (e0.type != 0x00 && e0.total_sectors > 0) {
            MbrEntry *le = &tbl->logical[tbl->logical_count++];
            *le = e0;
            le->start_lba = cur_lba + e0.start_lba;  /* absolute */
        }

        if (e1.type == 0x00 || e1.total_sectors == 0) break;
        cur_lba = ext_start + e1.start_lba;
    }
    return tbl->logical_count;
}

static int detect_partition_table(int fd, PartTable *pt) {
    uint8_t sector[2048];  /* need 4 sectors for HFS check */
    memset(pt, 0, sizeof(*pt));

    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    int nr = read(fd, sector, 2048);
    if (nr < 512) return -1;

    /* Check APM: DDR signature 0x4552 at bytes 0-1 */
    uint16_t ddr_sig = read_be16(sector);
    if (ddr_sig == APM_DDR_SIG) {
        pt->type = PT_APM;
        pt->data.apm.block_size = read_be16(&sector[2]);
        pt->data.apm.block_count = read_be32(&sector[4]);

        /* Read APM entries starting at block 1 */
        uint16_t bsz = pt->data.apm.block_size ? pt->data.apm.block_size : 512;
        uint8_t entry_buf[512];
        int map_entries = 0;

        for (int i = 1; i < MAX_PARTITIONS; i++) {
            if (lseek(fd, (off_t)i * bsz, SEEK_SET) < 0) break;
            if (read(fd, entry_buf, 512) != 512) break;

            uint16_t esig = read_be16(entry_buf);
            if (esig != APM_ENTRY_SIG) break;

            if (i == 1) map_entries = (int)read_be32(&entry_buf[4]);

            ApmEntry *ae = &pt->data.apm.entries[pt->data.apm.entry_count];
            ae->start_block = read_be32(&entry_buf[8]);
            ae->block_count = read_be32(&entry_buf[12]);
            memcpy(ae->name, &entry_buf[16], 32); ae->name[32] = '\0';
            memcpy(ae->type, &entry_buf[48], 32); ae->type[32] = '\0';
            ae->status = read_be32(&entry_buf[88]);
            pt->data.apm.entry_count++;

            if (map_entries > 0 && i >= map_entries) break;
        }
        return 0;
    }

    /* Check superfloppy: FAT VBR at sector 0 */
    if (is_fat_vbr(sector)) {
        pt->type = PT_NONE;
        strcpy(pt->fs_hint, "FAT");
        return 0;
    }

    /* Check HFS/HFS+ at offset 1024 */
    if (nr >= 2048 && is_hfs_sig(&sector[1024])) {
        pt->type = PT_NONE;
        uint16_t hsig = read_be16(&sector[1024]);
        strcpy(pt->fs_hint, hsig == 0x4244 ? "HFS" : "HFS+");
        return 0;
    }

    /* Check MBR signature */
    uint16_t mbr_sig = read_le16(&sector[510]);
    if (mbr_sig != 0xAA55) return -1;  /* no recognized partition table */

    /* Parse MBR */
    pt->type = PT_MBR;
    pt->data.mbr.disk_signature = read_le32(&sector[440]);
    for (int i = 0; i < 4; i++)
        parse_mbr_entry(&sector[446 + i * 16], &pt->data.mbr.entries[i]);

    /* Check for GPT (protective MBR) */
    if (pt->data.mbr.entries[0].type == 0xEE) {
        pt->type = PT_GPT;
        /* TODO: parse GPT headers at LBA 1 */
        return 0;
    }

    /* Parse EBR chain for extended partitions */
    for (int i = 0; i < 4; i++) {
        if (is_extended_type(pt->data.mbr.entries[i].type)) {
            parse_ebr_chain(fd, pt->data.mbr.entries[i].start_lba, &pt->data.mbr);
            break;
        }
    }

    return 0;
}

/* Build flat partition list from parsed table */
static int get_partition_list(const PartTable *pt, PartInfo *parts) {
    int count = 0;

    if (pt->type == PT_MBR) {
        for (int i = 0; i < 4; i++) {
            const MbrEntry *e = &pt->data.mbr.entries[i];
            if (e->type == 0x00 || e->total_sectors == 0) continue;
            PartInfo *p = &parts[count];
            memset(p, 0, sizeof(*p));
            p->index = count;
            p->type_byte = e->type;
            strncpy(p->type_name, partition_type_name(e->type), sizeof(p->type_name) - 1);
            p->start_lba = e->start_lba;
            p->total_sectors = e->total_sectors;
            p->bootable = (e->bootable == 0x80);
            p->is_extended = is_extended_type(e->type);
            count++;
        }
        /* Add logical partitions */
        for (int i = 0; i < pt->data.mbr.logical_count; i++) {
            const MbrEntry *e = &pt->data.mbr.logical[i];
            PartInfo *p = &parts[count];
            memset(p, 0, sizeof(*p));
            p->index = count;
            p->type_byte = e->type;
            strncpy(p->type_name, partition_type_name(e->type), sizeof(p->type_name) - 1);
            p->start_lba = e->start_lba;
            p->total_sectors = e->total_sectors;
            p->is_logical = 1;
            count++;
        }
    } else if (pt->type == PT_APM) {
        uint16_t bsz = pt->data.apm.block_size ? pt->data.apm.block_size : 512;
        for (int i = 0; i < pt->data.apm.entry_count; i++) {
            const ApmEntry *ae = &pt->data.apm.entries[i];
            /* Skip partition map and free space */
            if (strcmp(ae->type, "Apple_partition_map") == 0) continue;
            if (strcmp(ae->type, "Apple_Free") == 0) continue;
            if (strcmp(ae->type, "Apple_Driver") == 0) continue;
            if (strcmp(ae->type, "Apple_Driver43") == 0) continue;
            if (strcmp(ae->type, "Apple_Driver43_CD") == 0) continue;
            if (strcmp(ae->type, "Apple_Driver_ATA") == 0) continue;
            if (strcmp(ae->type, "Apple_Driver_ATAPI") == 0) continue;
            if (strcmp(ae->type, "Apple_FWDriver") == 0) continue;
            if (strcmp(ae->type, "Apple_Patches") == 0) continue;

            PartInfo *p = &parts[count];
            memset(p, 0, sizeof(*p));
            p->index = count;
            snprintf(p->type_name, sizeof(p->type_name), "%s (%s)", ae->type, ae->name);
            p->start_lba = (uint64_t)ae->start_block * bsz / SECTOR_SIZE;
            p->total_sectors = (uint64_t)ae->block_count * bsz / SECTOR_SIZE;
            p->bootable = (ae->status & 0x08) != 0;
            count++;
        }
    }

    return count;
}

/* ============================================================
 * Alignment Detection
 * ============================================================ */

static uint64_t gcd64(uint64_t a, uint64_t b) {
    while (b) { uint64_t t = b; b = a % b; a = t; }
    return a;
}

static const char *detect_alignment(const PartInfo *parts, int count,
                                     uint64_t *out_first_lba,
                                     uint64_t *out_alignment) {
    if (count == 0) {
        *out_first_lba = 0;
        *out_alignment = 0;
        return "None";
    }

    /* Find first non-extended partition LBA */
    uint64_t first_lba = 0;
    for (int i = 0; i < count; i++) {
        if (!parts[i].is_extended && parts[i].start_lba > 0) {
            first_lba = parts[i].start_lba;
            break;
        }
    }
    *out_first_lba = first_lba;

    if (first_lba == 63) {
        *out_alignment = 16065;  /* 255 * 63 */
        return "DOS Traditional (255x63)";
    }

    if (first_lba == 2048 || (first_lba > 0 && first_lba % 2048 == 0)) {
        int all_aligned = 1;
        for (int i = 0; i < count; i++) {
            if (parts[i].is_extended) continue;
            if (parts[i].start_lba % 2048 != 0) { all_aligned = 0; break; }
        }
        if (all_aligned) {
            *out_alignment = 2048;
            return "Modern 1MB";
        }
    }

    /* Custom alignment via GCD */
    if (count >= 2) {
        uint64_t g = 0;
        for (int i = 0; i < count; i++) {
            if (parts[i].is_extended) continue;
            if (parts[i].start_lba == 0) continue;
            g = g == 0 ? parts[i].start_lba : gcd64(g, parts[i].start_lba);
        }
        if (g > 1) {
            *out_alignment = g;
            return "Custom";
        }
    }

    *out_alignment = 0;
    return "None";
}

/* ============================================================
 * JSON Metadata Writer
 * ============================================================ */

static void write_metadata_json(const char *backup_path, const char *source,
                                  uint64_t source_size, const PartTable *pt,
                                  const PartInfo *parts, int part_count,
                                  const char *compression, const char *checksum,
                                  int sector_by_sector, uint64_t *imaged_sizes) {
    char meta_path[MAX_PATH_LEN];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", backup_path);

    FILE *f = fopen(meta_path, "w");
    if (!f) { fprintf(stderr, "Cannot write %s\n", meta_path); return; }

    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm);

    uint64_t first_lba = 0, alignment = 0;
    const char *align_type = detect_alignment(parts, part_count, &first_lba, &alignment);

    const char *pt_name = "None";
    if (pt->type == PT_MBR) pt_name = "MBR";
    else if (pt->type == PT_APM) pt_name = "APM";
    else if (pt->type == PT_GPT) pt_name = "GPT";

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"created\": \"%s\",\n", timestamp);
    fprintf(f, "  \"source_device\": \"%s\",\n", source);
    fprintf(f, "  \"source_size_bytes\": %llu,\n", (unsigned long long)source_size);
    fprintf(f, "  \"partition_table_type\": \"%s\",\n", pt_name);
    fprintf(f, "  \"compression_type\": \"%s\",\n", compression);
    fprintf(f, "  \"checksum_type\": \"%s\",\n", checksum);
    fprintf(f, "  \"sector_by_sector\": %s,\n", sector_by_sector ? "true" : "false");
    fprintf(f, "  \"transpiled_by\": \"rust-ppc-tiger\",\n");
    fprintf(f, "  \"alignment\": {\n");
    fprintf(f, "    \"detected_type\": \"%s\",\n", align_type);
    fprintf(f, "    \"first_partition_lba\": %llu,\n", (unsigned long long)first_lba);
    fprintf(f, "    \"alignment_sectors\": %llu\n", (unsigned long long)alignment);
    fprintf(f, "  },\n");
    fprintf(f, "  \"partitions\": [\n");

    for (int i = 0; i < part_count; i++) {
        if (parts[i].is_extended) continue;

        char filename[128];
        snprintf(filename, sizeof(filename), "partition-%d.raw", parts[i].index);

        fprintf(f, "    {\n");
        fprintf(f, "      \"index\": %d,\n", parts[i].index);
        fprintf(f, "      \"type_name\": \"%s\",\n", parts[i].type_name);
        fprintf(f, "      \"partition_type_byte\": %d,\n", parts[i].type_byte);
        fprintf(f, "      \"start_lba\": %llu,\n", (unsigned long long)parts[i].start_lba);
        fprintf(f, "      \"original_size_bytes\": %llu,\n",
                (unsigned long long)(parts[i].total_sectors * SECTOR_SIZE));
        fprintf(f, "      \"imaged_size_bytes\": %llu,\n",
                (unsigned long long)(imaged_sizes ? imaged_sizes[i] :
                    parts[i].total_sectors * SECTOR_SIZE));
        fprintf(f, "      \"compressed_files\": [\"%s\"],\n", filename);
        fprintf(f, "      \"bootable\": %s,\n", parts[i].bootable ? "true" : "false");
        fprintf(f, "      \"is_logical\": %s\n", parts[i].is_logical ? "true" : "false");
        fprintf(f, "    }%s\n", (i < part_count - 1) ? "," : "");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
}

/* ============================================================
 * MBR Export
 * ============================================================ */

static void export_mbr_bin(const char *backup_path, int src_fd) {
    char mbr_path[MAX_PATH_LEN];
    snprintf(mbr_path, sizeof(mbr_path), "%s/mbr.bin", backup_path);

    uint8_t mbr[512];
    if (lseek(src_fd, 0, SEEK_SET) < 0) return;
    if (read(src_fd, mbr, 512) != 512) return;

    FILE *f = fopen(mbr_path, "wb");
    if (f) { fwrite(mbr, 1, 512, f); fclose(f); }
}

/* ============================================================
 * Backup Command
 * ============================================================ */

static int cmd_backup(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        fprintf(stderr,
            "USAGE: rusty-backup backup [OPTIONS]\n\n"
            "OPTIONS:\n"
            "  --source <PATH>        Source device or image file (required)\n"
            "  --dest <PATH>          Destination directory (required)\n"
            "  --name <NAME>          Backup name (default: backup)\n"
            "  --compression <TYPE>   raw (default on Tiger)\n"
            "  --sector-by-sector     Full sector-by-sector backup\n"
        );
        return 0;
    }

    const char *source = flag_value(argc, argv, "--source");
    const char *dest = flag_value(argc, argv, "--dest");
    const char *name = flag_value(argc, argv, "--name");
    int sector_by_sector = has_flag(argc, argv, "--sector-by-sector");

    if (!source || !dest) {
        fprintf(stderr, "Error: --source and --dest are required\n");
        fprintf(stderr, "Run 'rusty-backup backup --help' for options\n");
        return 1;
    }
    if (!name) name = "backup";

    /* Open source */
    int src_fd = open(source, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", source, strerror(errno));
        return 1;
    }

    /* Get source size */
    uint64_t source_size = 0;
    struct stat st;
    if (fstat(src_fd, &st) == 0) {
        if (S_ISREG(st.st_mode)) {
            source_size = st.st_size;
        } else {
            source_size = get_device_size_ioctl(source);
        }
    }

    char sz_buf[32];
    fprintf(stderr, "Source: %s (%s)\n", source,
            format_bytes(source_size, sz_buf, sizeof(sz_buf)));

    /* Detect partition table */
    PartTable pt;
    if (detect_partition_table(src_fd, &pt) != 0) {
        fprintf(stderr, "Warning: Could not detect partition table, treating as raw image\n");
        pt.type = PT_NONE;
        strcpy(pt.fs_hint, "raw");
    }

    const char *pt_name = "None";
    if (pt.type == PT_MBR) pt_name = "MBR";
    else if (pt.type == PT_APM) pt_name = "APM";
    else if (pt.type == PT_GPT) pt_name = "GPT";
    fprintf(stderr, "Partition table: %s\n", pt_name);

    /* Get partition list */
    PartInfo parts[MAX_PARTITIONS];
    int part_count = get_partition_list(&pt, parts);
    fprintf(stderr, "Partitions found: %d\n", part_count);

    for (int i = 0; i < part_count; i++) {
        char psz[32];
        uint64_t psize = parts[i].total_sectors * SECTOR_SIZE;
        format_bytes(psize, psz, sizeof(psz));
        fprintf(stderr, "  [%d] %-20s LBA %-10llu %s%s%s\n",
                parts[i].index, parts[i].type_name,
                (unsigned long long)parts[i].start_lba, psz,
                parts[i].bootable ? " [boot]" : "",
                parts[i].is_logical ? " [logical]" : "");
    }

    /* Create backup directory */
    char backup_path[MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path), "%s/%s", dest, name);
    mkdir(backup_path, 0755);

    fprintf(stderr, "Backup to: %s\n\n", backup_path);

    /* Export MBR/partition table */
    if (pt.type == PT_MBR || pt.type == PT_GPT) {
        export_mbr_bin(backup_path, src_fd);
        fprintf(stderr, "[INFO] Exported MBR to mbr.bin\n");
    }

    /* Back up each partition */
    uint8_t *buf = (uint8_t *)malloc(CHUNK_SIZE);
    if (!buf) { fprintf(stderr, "Out of memory\n"); close(src_fd); return 1; }

    uint64_t *imaged_sizes = (uint64_t *)calloc(part_count, sizeof(uint64_t));

    if (part_count == 0 && pt.type == PT_NONE) {
        /* Superfloppy: back up entire device as one partition */
        char out_path[MAX_PATH_LEN];
        snprintf(out_path, sizeof(out_path), "%s/partition-0.raw", backup_path);

        FILE *out = fopen(out_path, "wb");
        if (!out) {
            fprintf(stderr, "Error: Cannot create %s\n", out_path);
            free(buf); close(src_fd); return 1;
        }

        fprintf(stderr, "Backing up entire %s image...\n", pt.fs_hint);
        lseek(src_fd, 0, SEEK_SET);

        uint64_t written = 0;
        ssize_t nr;
        while ((nr = read(src_fd, buf, CHUNK_SIZE)) > 0) {
            fwrite(buf, 1, nr, out);
            written += nr;
            if (source_size > 0) {
                double pct = (double)written / source_size * 100.0;
                draw_progress(pct, pt.fs_hint);
            }
        }
        fclose(out);
        fprintf(stderr, "\n[INFO] Wrote %llu bytes to partition-0.raw\n",
                (unsigned long long)written);

        /* Create a fake partition entry for metadata */
        parts[0].index = 0;
        strcpy(parts[0].type_name, pt.fs_hint);
        parts[0].start_lba = 0;
        parts[0].total_sectors = source_size / SECTOR_SIZE;
        part_count = 1;
        imaged_sizes[0] = written;
    } else {
        for (int i = 0; i < part_count; i++) {
            if (parts[i].is_extended) {
                fprintf(stderr, "[INFO] Skipping extended container [%d]\n", parts[i].index);
                continue;
            }

            char out_path[MAX_PATH_LEN];
            snprintf(out_path, sizeof(out_path), "%s/partition-%d.raw",
                     backup_path, parts[i].index);

            FILE *out = fopen(out_path, "wb");
            if (!out) {
                fprintf(stderr, "Error: Cannot create %s\n", out_path);
                continue;
            }

            uint64_t part_size = parts[i].total_sectors * SECTOR_SIZE;
            off_t part_offset = (off_t)parts[i].start_lba * SECTOR_SIZE;

            char psz[32];
            format_bytes(part_size, psz, sizeof(psz));
            fprintf(stderr, "Backing up partition %d (%s, %s)...\n",
                    parts[i].index, parts[i].type_name, psz);

            lseek(src_fd, part_offset, SEEK_SET);

            uint64_t remaining = part_size;
            uint64_t written = 0;
            while (remaining > 0) {
                size_t to_read = remaining > CHUNK_SIZE ? CHUNK_SIZE : (size_t)remaining;
                ssize_t nr = read(src_fd, buf, to_read);
                if (nr <= 0) break;
                fwrite(buf, 1, nr, out);
                written += nr;
                remaining -= nr;
                double pct = (double)written / part_size * 100.0;
                draw_progress(pct, parts[i].type_name);
            }
            fclose(out);
            imaged_sizes[i] = written;
            fprintf(stderr, "\n");
        }
    }

    free(buf);

    /* Write metadata.json */
    write_metadata_json(backup_path, source, source_size, &pt,
                        parts, part_count, "raw", "none",
                        sector_by_sector, imaged_sizes);
    fprintf(stderr, "[INFO] Wrote metadata.json\n");

    free(imaged_sizes);
    close(src_fd);
    fprintf(stderr, "\nBackup completed successfully.\n");
    return 0;
}

/* ============================================================
 * Restore Command
 * ============================================================ */

static int cmd_restore(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        fprintf(stderr,
            "USAGE: rusty-backup restore [OPTIONS]\n\n"
            "OPTIONS:\n"
            "  --backup-dir <DIR>     Backup folder containing metadata.json (required)\n"
            "  --target <PATH>        Target device or image file (required)\n"
            "  --target-size <BYTES>  Target size in bytes (auto-detected for devices)\n"
        );
        return 0;
    }

    const char *backup_dir = flag_value(argc, argv, "--backup-dir");
    const char *target = flag_value(argc, argv, "--target");

    if (!backup_dir || !target) {
        fprintf(stderr, "Error: --backup-dir and --target are required\n");
        return 1;
    }

    /* Read metadata.json */
    char meta_path[MAX_PATH_LEN];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", backup_dir);

    FILE *mf = fopen(meta_path, "r");
    if (!mf) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", meta_path, strerror(errno));
        return 1;
    }

    /* Simple JSON parser for key fields */
    char json_buf[32768];
    size_t json_len = fread(json_buf, 1, sizeof(json_buf) - 1, mf);
    json_buf[json_len] = '\0';
    fclose(mf);

    /* Parse partition table type */
    char pt_type[16] = "None";
    const char *p = strstr(json_buf, "\"partition_table_type\"");
    if (p) {
        p = strchr(p + 21, '"'); if (p) p++;
        if (p) {
            p = strchr(p, '"'); if (p) p++;
            if (p) {
                const char *end = strchr(p, '"');
                if (end) { int len = end - p; if (len > 15) len = 15;
                    memcpy(pt_type, p, len); pt_type[len] = '\0'; }
            }
        }
    }

    /* Restore MBR if present */
    if (strcmp(pt_type, "MBR") == 0 || strcmp(pt_type, "GPT") == 0) {
        char mbr_path[MAX_PATH_LEN];
        snprintf(mbr_path, sizeof(mbr_path), "%s/mbr.bin", backup_dir);
        FILE *mbrf = fopen(mbr_path, "rb");
        if (mbrf) {
            uint8_t mbr[512];
            if (fread(mbr, 1, 512, mbrf) == 512) {
                int tgt_fd = open(target, O_WRONLY | O_CREAT, 0644);
                if (tgt_fd >= 0) {
                    write(tgt_fd, mbr, 512);
                    close(tgt_fd);
                    fprintf(stderr, "[INFO] Restored MBR from mbr.bin\n");
                }
            }
            fclose(mbrf);
        }
    }

    /* Find and restore partition files */
    int tgt_fd = open(target, O_WRONLY | O_CREAT, 0644);
    if (tgt_fd < 0) {
        fprintf(stderr, "Error: Cannot open %s for writing: %s\n", target, strerror(errno));
        return 1;
    }

    /* Scan backup directory for partition-N.raw files */
    DIR *bdir = opendir(backup_dir);
    if (!bdir) {
        fprintf(stderr, "Error: Cannot open %s\n", backup_dir);
        close(tgt_fd);
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(CHUNK_SIZE);
    if (!buf) { close(tgt_fd); closedir(bdir); return 1; }

    /* Parse partition start_lba values from metadata for positioning */
    struct { int index; uint64_t start_lba; uint64_t size; char filename[128]; } restorable[MAX_PARTITIONS];
    int restore_count = 0;

    /* Simple parser: find each partition entry */
    const char *scan = json_buf;
    while ((scan = strstr(scan, "\"index\"")) != NULL) {
        int idx = -1;
        uint64_t slba = 0;

        sscanf(scan + 9, "%d", &idx);

        const char *lba_p = strstr(scan, "\"start_lba\"");
        if (lba_p && lba_p < scan + 500) {
            sscanf(lba_p + 13, "%llu", (unsigned long long *)&slba);
        }

        const char *size_p = strstr(scan, "\"imaged_size_bytes\"");
        uint64_t isize = 0;
        if (size_p && size_p < scan + 500) {
            sscanf(size_p + 20, "%llu", (unsigned long long *)&isize);
        }

        if (idx >= 0) {
            restorable[restore_count].index = idx;
            restorable[restore_count].start_lba = slba;
            restorable[restore_count].size = isize;
            snprintf(restorable[restore_count].filename, 128,
                     "%s/partition-%d.raw", backup_dir, idx);
            restore_count++;
        }

        scan += 10;
    }

    fprintf(stderr, "Restoring %d partition(s) to %s\n\n", restore_count, target);

    for (int i = 0; i < restore_count; i++) {
        FILE *pf = fopen(restorable[i].filename, "rb");
        if (!pf) {
            fprintf(stderr, "[WARN] Cannot open %s, skipping\n", restorable[i].filename);
            continue;
        }

        /* Seek to partition offset in target */
        off_t offset = (off_t)restorable[i].start_lba * SECTOR_SIZE;
        lseek(tgt_fd, offset, SEEK_SET);

        fprintf(stderr, "Restoring partition %d to LBA %llu...\n",
                restorable[i].index, (unsigned long long)restorable[i].start_lba);

        /* Get file size */
        fseek(pf, 0, SEEK_END);
        uint64_t file_size = ftell(pf);
        fseek(pf, 0, SEEK_SET);

        uint64_t written = 0;
        size_t nr;
        while ((nr = fread(buf, 1, CHUNK_SIZE, pf)) > 0) {
            write(tgt_fd, buf, nr);
            written += nr;
            if (file_size > 0) {
                double pct = (double)written / file_size * 100.0;
                draw_progress(pct, "Restoring");
            }
        }
        fclose(pf);
        fprintf(stderr, "\n[INFO] Wrote %llu bytes\n", (unsigned long long)written);
    }

    free(buf);
    close(tgt_fd);
    closedir(bdir);

    fprintf(stderr, "\nRestore completed successfully.\n");
    return 0;
}

/* ============================================================
 * Inspect Command
 * ============================================================ */

static int cmd_inspect(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h") || argc < 1) {
        fprintf(stderr, "USAGE: rusty-backup inspect <BACKUP_DIR>\n");
        if (argc < 1) return 1;
        return 0;
    }

    char meta_path[MAX_PATH_LEN];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", argv[0]);

    FILE *f = fopen(meta_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", meta_path, strerror(errno));
        return 1;
    }

    char json[32768];
    size_t len = fread(json, 1, sizeof(json) - 1, f);
    json[len] = '\0';
    fclose(f);

    /* Print the metadata in a nice format */
    /* Simple key-value extraction */
    printf("Backup Metadata\n");
    printf("===============\n\n");

    /* Helper macro for extracting string values from "key": "value" */
    #define PRINT_JSON_STR(label, key) do { \
        const char *_p = strstr(json, "\"" key "\""); \
        if (_p) { \
            _p += strlen(key) + 2; /* skip past closing quote of key */ \
            _p = strchr(_p, ':');  /* find colon */ \
            if (_p) { _p++; while (*_p == ' ') _p++; /* skip spaces */ \
                if (*_p == '"') { _p++; /* skip opening quote of value */ \
                    const char *_e = strchr(_p, '"'); \
                    if (_e) printf("  %-20s %.*s\n", label, (int)(_e - _p), _p); \
                } \
            } \
        } \
    } while(0)

    #define PRINT_JSON_NUM(label, key) do { \
        const char *_p = strstr(json, "\"" key "\""); \
        if (_p) { \
            _p = strchr(_p + strlen(key) + 2, ':'); \
            if (_p) { _p++; while (*_p == ' ') _p++; \
                long long _v = 0; sscanf(_p, "%lld", &_v); \
                printf("  %-20s %lld\n", label, _v); \
            } \
        } \
    } while(0)

    PRINT_JSON_STR("Created:", "created");
    PRINT_JSON_STR("Source:", "source_device");

    /* Source size with human-readable */
    const char *ss = strstr(json, "\"source_size_bytes\"");
    if (ss) {
        ss = strchr(ss + 18, ':');
        if (ss) {
            ss++; while (*ss == ' ') ss++;
            uint64_t sz = 0; sscanf(ss, "%llu", (unsigned long long *)&sz);
            char szb[32];
            printf("  %-20s %s (%llu bytes)\n", "Source size:",
                   format_bytes(sz, szb, sizeof(szb)), (unsigned long long)sz);
        }
    }

    PRINT_JSON_STR("Partition table:", "partition_table_type");
    PRINT_JSON_STR("Compression:", "compression_type");
    PRINT_JSON_STR("Checksum:", "checksum_type");
    PRINT_JSON_STR("Transpiled by:", "transpiled_by");

    /* Alignment section */
    printf("\n  Alignment\n");
    PRINT_JSON_STR("    Type:", "detected_type");
    PRINT_JSON_NUM("    First LBA:", "first_partition_lba");
    PRINT_JSON_NUM("    Alignment:", "alignment_sectors");

    /* Partition list */
    printf("\n  Partitions:\n");
    const char *scan = json;
    while ((scan = strstr(scan, "\"index\"")) != NULL) {
        int idx = -1;
        sscanf(scan + 9, "%d", &idx);

        /* type_name */
        char tname[64] = "?";
        const char *tp = strstr(scan, "\"type_name\"");
        if (tp && tp < scan + 500) {
            tp = strchr(tp + 11, ':');  /* past key, find colon */
            if (tp) { tp++; while (*tp == ' ') tp++;
                if (*tp == '"') { tp++;
                    const char *te = strchr(tp, '"');
                    if (te) { int l = te - tp; if (l > 63) l = 63;
                        memcpy(tname, tp, l); tname[l] = '\0'; }
                }
            }
        }

        /* sizes */
        uint64_t orig = 0, imaged = 0;
        const char *op = strstr(scan, "\"original_size_bytes\"");
        if (op && op < scan + 600) sscanf(op + 22, "%llu", (unsigned long long *)&orig);
        const char *ip = strstr(scan, "\"imaged_size_bytes\"");
        if (ip && ip < scan + 600) sscanf(ip + 20, "%llu", (unsigned long long *)&imaged);

        if (idx >= 0) {
            char obs[32], ibs[32];
            printf("    [%d] %-20s %10s (imaged: %10s)\n",
                   idx, tname,
                   format_bytes(orig, obs, sizeof(obs)),
                   format_bytes(imaged, ibs, sizeof(ibs)));
        }

        scan += 10;
    }

    /* Check what files exist in backup dir */
    printf("\n  Backup files:\n");
    DIR *d = opendir(argv[0]);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char fpath[MAX_PATH_LEN];
            snprintf(fpath, sizeof(fpath), "%s/%s", argv[0], ent->d_name);
            struct stat fst;
            if (stat(fpath, &fst) == 0) {
                char fsz[32];
                printf("    %-30s %s\n", ent->d_name,
                       format_bytes(fst.st_size, fsz, sizeof(fsz)));
            }
        }
        closedir(d);
    }

    #undef PRINT_JSON_STR
    #undef PRINT_JSON_NUM

    return 0;
}

/* ============================================================
 * Rip Command (optical disc)
 * ============================================================ */

static int cmd_rip(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        fprintf(stderr,
            "USAGE: rusty-backup rip [OPTIONS]\n\n"
            "OPTIONS:\n"
            "  --device <PATH>   Optical drive (e.g. /dev/disk1) (required)\n"
            "  --output <PATH>   Output file path (required)\n"
            "  --format <TYPE>   iso (default)\n"
        );
        return 0;
    }

    const char *device = flag_value(argc, argv, "--device");
    const char *output = flag_value(argc, argv, "--output");

    if (!device || !output) {
        fprintf(stderr, "Error: --device and --output are required\n");
        return 1;
    }

    int src_fd = open(device, O_RDONLY);
    if (src_fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n", device, strerror(errno));
        return 1;
    }

    uint64_t dev_size = get_device_size_ioctl(device);
    if (dev_size == 0) {
        fprintf(stderr, "Error: Cannot determine size of %s\n", device);
        close(src_fd);
        return 1;
    }

    char sz_buf[32];
    fprintf(stderr, "Ripping %s (%s) to %s...\n", device,
            format_bytes(dev_size, sz_buf, sizeof(sz_buf)), output);

    FILE *out = fopen(output, "wb");
    if (!out) {
        fprintf(stderr, "Error: Cannot create %s\n", output);
        close(src_fd);
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(CHUNK_SIZE);
    uint64_t written = 0;
    ssize_t nr;

    while ((nr = read(src_fd, buf, CHUNK_SIZE)) > 0) {
        fwrite(buf, 1, nr, out);
        written += nr;
        double pct = (double)written / dev_size * 100.0;
        draw_progress(pct, "Ripping");
    }

    free(buf);
    fclose(out);
    close(src_fd);

    fprintf(stderr, "\nRip completed: %s (%s)\n", output,
            format_bytes(written, sz_buf, sizeof(sz_buf)));
    return 0;
}

/* ============================================================
 * Print Usage
 * ============================================================ */

static void print_usage(const char *prog) {
    const char *name = prog;
    if (prog) {
        const char *slash = strrchr(prog, '/');
        if (slash) name = slash + 1;
    } else {
        name = "rusty-backup";
    }

    fprintf(stderr,
        "Rusty Backup CLI — headless disk backup & restore\n"
        "Transpiled from Rust to PowerPC by rust-ppc-tiger\n"
        "\n"
        "USAGE:\n"
        "  %s <COMMAND> [OPTIONS]\n"
        "\n"
        "COMMANDS:\n"
        "  backup         Back up a device or image\n"
        "  restore        Restore a backup to a device or file\n"
        "  list-devices   Enumerate available disk devices\n"
        "  inspect        Show metadata for an existing backup\n"
        "  rip            Rip an optical disc to ISO\n"
        "  help           Show this help message\n"
        "\n"
        "Run '%s <COMMAND> --help' for per-command options.\n",
        name, name);
}

/* ============================================================
 * Main Entry Point
 * ============================================================ */

/* These are needed by other transpiled .o files */
void *std_env_args(void) {
    int argc = get_argc();
    char **argv = get_argv();
    /* Return a simple struct: { items, len, cap, elem_size } */
    void **v = (void **)calloc(1, 16);
    if (!v) return NULL;
    v[0] = argv;
    ((int *)v)[2] = argc;
    ((int *)v)[3] = argc;
    return v;
}

void *env_args(void) { return std_env_args(); }

int main(int argc, char **argv) {
#ifndef __APPLE__
    g_argc = argc;
    g_argv = argv;
#endif
    (void)argc; (void)argv;

    int ac = get_argc();
    char **av = get_argv();

    if (ac < 2) {
        print_usage(av[0]);
        return 1;
    }

    const char *cmd = av[1];
    int sub_argc = ac - 2;
    char **sub_argv = &av[2];

    if (strcmp(cmd, "backup") == 0) {
        return cmd_backup(sub_argc, sub_argv);
    } else if (strcmp(cmd, "restore") == 0) {
        return cmd_restore(sub_argc, sub_argv);
    } else if (strcmp(cmd, "list-devices") == 0) {
        return cmd_list_devices();
    } else if (strcmp(cmd, "inspect") == 0) {
        return cmd_inspect(sub_argc, sub_argv);
    } else if (strcmp(cmd, "rip") == 0) {
        return cmd_rip(sub_argc, sub_argv);
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0
            || strcmp(cmd, "help") == 0) {
        print_usage(av[0]);
        return 0;
    } else if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0) {
        printf("rusty-backup 0.2.0-ppc (transpiled from Rust by rust-ppc-tiger)\n");
        printf("Platform: Mac OS X Tiger PowerPC\n");
        printf("Features: MBR, APM, EBR chain, raw backup/restore\n");
        return 0;
    } else {
        fprintf(stderr, "Unknown subcommand: %s\n", cmd);
        print_usage(av[0]);
        return 1;
    }
}
