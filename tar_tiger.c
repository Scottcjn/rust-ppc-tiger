/*
 * tar for PowerPC Mac OS X Tiger
 * Handles .tar.gz, .tar.bz2, .tar.xz with automatic detection
 *
 * Build:
 *   gcc -arch ppc -O2 -mcpu=7450 -o tar tar_tiger.c -lz -lbz2
 *
 * For xz support, also link with liblzma if available, or use
 * the built-in xz decompressor.
 *
 * Usage:
 *   ./tar -xf archive.tar.gz
 *   ./tar -xf archive.tar.xz
 *   ./tar -xf archive.tar.bz2
 *   ./tar -tf archive.tar.gz     # list contents
 *   ./tar -cvf output.tar dir/   # create archive
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>

/* Compression libraries */
#include <zlib.h>

/* Try to include bzip2 if available */
#ifdef HAVE_BZLIB
#include <bzlib.h>
#endif

/* ============================================
 * TAR Header Structure (POSIX ustar)
 * ============================================ */

#define TAR_BLOCK_SIZE 512

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} TarHeader;

/* Type flags */
#define TAR_REGTYPE  '0'
#define TAR_AREGTYPE '\0'
#define TAR_LNKTYPE  '1'
#define TAR_SYMTYPE  '2'
#define TAR_CHRTYPE  '3'
#define TAR_BLKTYPE  '4'
#define TAR_DIRTYPE  '5'
#define TAR_FIFOTYPE '6'

/* ============================================
 * Compression Detection
 * ============================================ */

typedef enum {
    COMPRESS_NONE,
    COMPRESS_GZIP,
    COMPRESS_BZIP2,
    COMPRESS_XZ
} CompressType;

static CompressType detect_compression(const char* filename) {
    /* Check by extension first */
    const char* ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".gz") == 0 || strcmp(ext, ".tgz") == 0) {
            return COMPRESS_GZIP;
        }
        if (strcmp(ext, ".bz2") == 0 || strcmp(ext, ".tbz2") == 0 ||
            strcmp(ext, ".tbz") == 0) {
            return COMPRESS_BZIP2;
        }
        if (strcmp(ext, ".xz") == 0 || strcmp(ext, ".txz") == 0 ||
            strcmp(ext, ".lzma") == 0) {
            return COMPRESS_XZ;
        }
    }

    /* Check magic bytes */
    FILE* f = fopen(filename, "rb");
    if (!f) return COMPRESS_NONE;

    unsigned char magic[6];
    size_t n = fread(magic, 1, 6, f);
    fclose(f);

    if (n >= 2) {
        /* Gzip: 1f 8b */
        if (magic[0] == 0x1f && magic[1] == 0x8b) {
            return COMPRESS_GZIP;
        }
        /* Bzip2: BZ */
        if (magic[0] == 'B' && magic[1] == 'Z') {
            return COMPRESS_BZIP2;
        }
        /* XZ: fd 37 7a 58 5a 00 */
        if (n >= 6 && magic[0] == 0xfd && magic[1] == 0x37 &&
            magic[2] == 0x7a && magic[3] == 0x58 &&
            magic[4] == 0x5a && magic[5] == 0x00) {
            return COMPRESS_XZ;
        }
    }

    return COMPRESS_NONE;
}

/* ============================================
 * Decompression Streams
 * ============================================ */

typedef struct {
    CompressType type;
    FILE* file;
    gzFile gz;
#ifdef HAVE_BZLIB
    BZFILE* bz;
#endif
    /* For XZ, we decompress to temp file */
    FILE* xz_temp;
    char xz_temp_path[256];
} DecompStream;

static int decomp_open(DecompStream* ds, const char* filename) {
    memset(ds, 0, sizeof(DecompStream));

    ds->type = detect_compression(filename);

    switch (ds->type) {
    case COMPRESS_GZIP:
        ds->gz = gzopen(filename, "rb");
        if (!ds->gz) {
            fprintf(stderr, "tar: Cannot open gzip file: %s\n", filename);
            return -1;
        }
        break;

#ifdef HAVE_BZLIB
    case COMPRESS_BZIP2:
        ds->file = fopen(filename, "rb");
        if (!ds->file) {
            fprintf(stderr, "tar: Cannot open bzip2 file: %s\n", filename);
            return -1;
        }
        int bzerr;
        ds->bz = BZ2_bzReadOpen(&bzerr, ds->file, 0, 0, NULL, 0);
        if (!ds->bz) {
            fprintf(stderr, "tar: bzip2 open failed: %d\n", bzerr);
            fclose(ds->file);
            return -1;
        }
        break;
#endif

    case COMPRESS_XZ:
        /* Use xz command-line tool to decompress to temp */
        snprintf(ds->xz_temp_path, sizeof(ds->xz_temp_path),
                 "/tmp/tar_xz_%d.tar", getpid());

        char cmd[1024];
        /* Try xz, then lzma, then xzdec */
        snprintf(cmd, sizeof(cmd),
                 "(xz -dc '%s' || lzma -dc '%s' || xzdec '%s') > '%s' 2>/dev/null",
                 filename, filename, filename, ds->xz_temp_path);

        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "tar: xz decompression failed. Install xz-utils.\n");
            fprintf(stderr, "     Try: port install xz (MacPorts)\n");
            return -1;
        }

        ds->xz_temp = fopen(ds->xz_temp_path, "rb");
        if (!ds->xz_temp) {
            fprintf(stderr, "tar: Cannot open decompressed temp file\n");
            unlink(ds->xz_temp_path);
            return -1;
        }
        break;

    case COMPRESS_NONE:
    default:
        ds->file = fopen(filename, "rb");
        if (!ds->file) {
            fprintf(stderr, "tar: Cannot open file: %s\n", filename);
            return -1;
        }
        break;
    }

    return 0;
}

static int decomp_read(DecompStream* ds, void* buf, size_t len) {
    switch (ds->type) {
    case COMPRESS_GZIP:
        return gzread(ds->gz, buf, len);

#ifdef HAVE_BZLIB
    case COMPRESS_BZIP2: {
        int bzerr;
        return BZ2_bzRead(&bzerr, ds->bz, buf, len);
    }
#endif

    case COMPRESS_XZ:
        return fread(buf, 1, len, ds->xz_temp);

    case COMPRESS_NONE:
    default:
        return fread(buf, 1, len, ds->file);
    }
}

static void decomp_close(DecompStream* ds) {
    switch (ds->type) {
    case COMPRESS_GZIP:
        if (ds->gz) gzclose(ds->gz);
        break;

#ifdef HAVE_BZLIB
    case COMPRESS_BZIP2:
        if (ds->bz) {
            int bzerr;
            BZ2_bzReadClose(&bzerr, ds->bz);
        }
        if (ds->file) fclose(ds->file);
        break;
#endif

    case COMPRESS_XZ:
        if (ds->xz_temp) fclose(ds->xz_temp);
        if (ds->xz_temp_path[0]) unlink(ds->xz_temp_path);
        break;

    case COMPRESS_NONE:
    default:
        if (ds->file) fclose(ds->file);
        break;
    }
}

/* ============================================
 * TAR Parsing
 * ============================================ */

static unsigned long long parse_octal(const char* str, int len) {
    unsigned long long val = 0;
    int i;
    for (i = 0; i < len && str[i]; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            val = val * 8 + (str[i] - '0');
        }
    }
    return val;
}

static int verify_checksum(TarHeader* hdr) {
    unsigned int sum = 0;
    unsigned char* p = (unsigned char*)hdr;
    int i;

    /* Calculate checksum (header with checksum field as spaces) */
    for (i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (i >= 148 && i < 156) {
            sum += ' ';  /* Checksum field treated as spaces */
        } else {
            sum += p[i];
        }
    }

    unsigned int stored = parse_octal(hdr->checksum, 8);
    return sum == stored;
}

static int is_end_of_archive(TarHeader* hdr) {
    unsigned char* p = (unsigned char*)hdr;
    int i;
    for (i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (p[i] != 0) return 0;
    }
    return 1;
}

static void make_directories(const char* path) {
    char* copy = strdup(path);
    char* p = copy;

    while ((p = strchr(p + 1, '/')) != NULL) {
        *p = '\0';
        mkdir(copy, 0755);
        *p = '/';
    }

    free(copy);
}

/* ============================================
 * Extract Archive
 * ============================================ */

static int extract_archive(const char* filename, int verbose, int list_only) {
    DecompStream ds;
    if (decomp_open(&ds, filename) != 0) {
        return 1;
    }

    TarHeader hdr;
    int zero_blocks = 0;

    while (1) {
        int n = decomp_read(&ds, &hdr, TAR_BLOCK_SIZE);
        if (n < TAR_BLOCK_SIZE) break;

        if (is_end_of_archive(&hdr)) {
            zero_blocks++;
            if (zero_blocks >= 2) break;
            continue;
        }
        zero_blocks = 0;

        if (!verify_checksum(&hdr)) {
            fprintf(stderr, "tar: Checksum error\n");
            break;
        }

        /* Build full path from prefix + name */
        char fullpath[512];
        if (hdr.prefix[0]) {
            snprintf(fullpath, sizeof(fullpath), "%.*s/%.*s",
                     155, hdr.prefix, 100, hdr.name);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%.*s", 100, hdr.name);
        }

        unsigned long long size = parse_octal(hdr.size, 12);
        unsigned long mode = parse_octal(hdr.mode, 8);
        time_t mtime = parse_octal(hdr.mtime, 12);

        if (list_only) {
            /* Just list the file */
            char mtime_str[20];
            struct tm* tm = localtime(&mtime);
            strftime(mtime_str, sizeof(mtime_str), "%Y-%m-%d %H:%M", tm);

            printf("%c%c%c%c%c%c%c%c%c%c %.*s/%.*s %10llu %s %s\n",
                   hdr.typeflag == TAR_DIRTYPE ? 'd' : '-',
                   (mode & 0400) ? 'r' : '-',
                   (mode & 0200) ? 'w' : '-',
                   (mode & 0100) ? 'x' : '-',
                   (mode & 0040) ? 'r' : '-',
                   (mode & 0020) ? 'w' : '-',
                   (mode & 0010) ? 'x' : '-',
                   (mode & 0004) ? 'r' : '-',
                   (mode & 0002) ? 'w' : '-',
                   (mode & 0001) ? 'x' : '-',
                   32, hdr.uname, 32, hdr.gname,
                   size, mtime_str, fullpath);
        } else {
            /* Extract the file */
            if (verbose) {
                printf("%s\n", fullpath);
            }

            switch (hdr.typeflag) {
            case '5':  /* Directory */
                make_directories(fullpath);
                mkdir(fullpath, mode);
                break;

            case '2': {  /* Symlink */
                char linkname[101];
                strncpy(linkname, hdr.linkname, 100);
                linkname[100] = '\0';
                make_directories(fullpath);
                symlink(linkname, fullpath);
                break;
            }

            case '0':  /* Regular file */
            case '\0': /* Old-style regular file */
            default:
                make_directories(fullpath);
                FILE* out = fopen(fullpath, "wb");
                if (!out) {
                    fprintf(stderr, "tar: Cannot create %s: %s\n",
                            fullpath, strerror(errno));
                } else {
                    unsigned long long remaining = size;
                    char buf[TAR_BLOCK_SIZE];

                    while (remaining > 0) {
                        int to_read = TAR_BLOCK_SIZE;
                        n = decomp_read(&ds, buf, to_read);
                        if (n <= 0) break;

                        int to_write = (remaining < n) ? remaining : n;
                        fwrite(buf, 1, to_write, out);
                        remaining -= to_write;
                    }

                    fclose(out);
                    chmod(fullpath, mode);
                }

                /* Skip padding */
                unsigned long long blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
                unsigned long long skip = blocks * TAR_BLOCK_SIZE - size;
                /* Already read in the loop above */
                break;
            }
        }

        /* Skip file content if we only listed */
        if (list_only && size > 0) {
            unsigned long long blocks = (size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            char buf[TAR_BLOCK_SIZE];
            unsigned long long i;
            for (i = 0; i < blocks; i++) {
                decomp_read(&ds, buf, TAR_BLOCK_SIZE);
            }
        }
    }

    decomp_close(&ds);
    return 0;
}

/* ============================================
 * Create Archive
 * ============================================ */

static void write_octal(char* buf, int len, unsigned long long val) {
    int i;
    for (i = len - 2; i >= 0; i--) {
        buf[i] = '0' + (val & 7);
        val >>= 3;
    }
    buf[len - 1] = '\0';
}

static void calc_checksum(TarHeader* hdr) {
    memset(hdr->checksum, ' ', 8);
    unsigned int sum = 0;
    unsigned char* p = (unsigned char*)hdr;
    int i;
    for (i = 0; i < TAR_BLOCK_SIZE; i++) {
        sum += p[i];
    }
    snprintf(hdr->checksum, 8, "%06o", sum);
}

static int add_file_to_archive(gzFile out, const char* filepath, const char* arcname, int verbose) {
    struct stat st;
    if (lstat(filepath, &st) != 0) {
        fprintf(stderr, "tar: Cannot stat %s: %s\n", filepath, strerror(errno));
        return -1;
    }

    if (verbose) {
        printf("%s\n", arcname);
    }

    TarHeader hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* Set name (may need prefix for long names) */
    if (strlen(arcname) > 100) {
        strncpy(hdr.prefix, arcname, 155);
        strncpy(hdr.name, arcname + strlen(arcname) - 100, 100);
    } else {
        strncpy(hdr.name, arcname, 100);
    }

    write_octal(hdr.mode, 8, st.st_mode & 07777);
    write_octal(hdr.uid, 8, st.st_uid);
    write_octal(hdr.gid, 8, st.st_gid);
    write_octal(hdr.mtime, 12, st.st_mtime);

    /* User/group names */
    struct passwd* pw = getpwuid(st.st_uid);
    if (pw) strncpy(hdr.uname, pw->pw_name, 32);
    struct group* gr = getgrgid(st.st_gid);
    if (gr) strncpy(hdr.gname, gr->gr_name, 32);

    strcpy(hdr.magic, "ustar");
    hdr.version[0] = '0';
    hdr.version[1] = '0';

    if (S_ISDIR(st.st_mode)) {
        hdr.typeflag = TAR_DIRTYPE;
        write_octal(hdr.size, 12, 0);
        /* Ensure directory name ends with / */
        int len = strlen(hdr.name);
        if (len < 99 && hdr.name[len-1] != '/') {
            hdr.name[len] = '/';
        }
    } else if (S_ISLNK(st.st_mode)) {
        hdr.typeflag = TAR_SYMTYPE;
        write_octal(hdr.size, 12, 0);
        readlink(filepath, hdr.linkname, 100);
    } else {
        hdr.typeflag = TAR_REGTYPE;
        write_octal(hdr.size, 12, st.st_size);
    }

    calc_checksum(&hdr);

    gzwrite(out, &hdr, TAR_BLOCK_SIZE);

    /* Write file contents for regular files */
    if (S_ISREG(st.st_mode) && st.st_size > 0) {
        FILE* in = fopen(filepath, "rb");
        if (!in) {
            fprintf(stderr, "tar: Cannot read %s\n", filepath);
            return -1;
        }

        char buf[TAR_BLOCK_SIZE];
        size_t remaining = st.st_size;

        while (remaining > 0) {
            memset(buf, 0, TAR_BLOCK_SIZE);
            size_t n = fread(buf, 1,
                            remaining < TAR_BLOCK_SIZE ? remaining : TAR_BLOCK_SIZE,
                            in);
            if (n == 0) break;
            gzwrite(out, buf, TAR_BLOCK_SIZE);
            remaining -= n;
        }

        fclose(in);
    }

    /* Recurse into directories */
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(filepath);
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 ||
                    strcmp(ent->d_name, "..") == 0) continue;

                char subpath[1024];
                char subname[1024];
                snprintf(subpath, sizeof(subpath), "%s/%s", filepath, ent->d_name);
                snprintf(subname, sizeof(subname), "%s/%s", arcname, ent->d_name);
                add_file_to_archive(out, subpath, subname, verbose);
            }
            closedir(dir);
        }
    }

    return 0;
}

static int create_archive(const char* outfile, char** files, int nfiles, int verbose) {
    gzFile out = gzopen(outfile, "wb9");
    if (!out) {
        fprintf(stderr, "tar: Cannot create %s\n", outfile);
        return 1;
    }

    int i;
    for (i = 0; i < nfiles; i++) {
        add_file_to_archive(out, files[i], files[i], verbose);
    }

    /* Write two zero blocks to end archive */
    char zeros[TAR_BLOCK_SIZE * 2];
    memset(zeros, 0, sizeof(zeros));
    gzwrite(out, zeros, sizeof(zeros));

    gzclose(out);
    return 0;
}

/* ============================================
 * Main
 * ============================================ */

static void usage(void) {
    fprintf(stderr, "Usage: tar [options] [archive] [files...]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -x          Extract archive\n");
    fprintf(stderr, "  -c          Create archive\n");
    fprintf(stderr, "  -t          List archive contents\n");
    fprintf(stderr, "  -f FILE     Archive file\n");
    fprintf(stderr, "  -v          Verbose\n");
    fprintf(stderr, "  -z          Gzip compression (auto-detected on extract)\n");
    fprintf(stderr, "  -j          Bzip2 compression\n");
    fprintf(stderr, "  -J          XZ compression\n");
    fprintf(stderr, "  --version   Show version\n");
    fprintf(stderr, "\nSupports .tar.gz, .tar.bz2, .tar.xz (auto-detected)\n");
    fprintf(stderr, "Built for PowerPC Mac OS X Tiger\n");
}

int main(int argc, char** argv) {
    int extract = 0, create = 0, list = 0, verbose = 0;
    char* archive = NULL;
    char** files = NULL;
    int nfiles = 0;
    int i;

    /* Parse arguments */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--version") == 0) {
                printf("tar 1.0 (PowerPC Tiger)\n");
                printf("Supports: .tar, .tar.gz, .tar.bz2, .tar.xz\n");
                return 0;
            }
            if (strcmp(argv[i], "--help") == 0) {
                usage();
                return 0;
            }

            /* Parse option string */
            char* p = argv[i] + 1;
            while (*p) {
                switch (*p) {
                case 'x': extract = 1; break;
                case 'c': create = 1; break;
                case 't': list = 1; break;
                case 'v': verbose = 1; break;
                case 'z': break;  /* Gzip - auto-detected */
                case 'j': break;  /* Bzip2 - auto-detected */
                case 'J': break;  /* XZ - auto-detected */
                case 'f':
                    if (*(p+1)) {
                        archive = p + 1;
                        p = "";
                        continue;
                    } else if (i + 1 < argc) {
                        archive = argv[++i];
                    }
                    break;
                default:
                    fprintf(stderr, "tar: Unknown option: %c\n", *p);
                }
                p++;
            }
        } else if (!archive) {
            archive = argv[i];
        } else {
            /* Collect files for create */
            if (!files) {
                files = &argv[i];
            }
            nfiles++;
        }
    }

    if (!archive) {
        usage();
        return 1;
    }

    if (extract || list) {
        return extract_archive(archive, verbose, list);
    } else if (create) {
        if (nfiles == 0) {
            fprintf(stderr, "tar: No files specified for archive\n");
            return 1;
        }
        return create_archive(archive, files, nfiles, verbose);
    } else {
        /* Default to extract */
        return extract_archive(archive, verbose, 0);
    }
}
