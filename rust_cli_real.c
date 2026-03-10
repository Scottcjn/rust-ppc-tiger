/*
 * rust_cli_real.c — Real CLI implementations for rusty-backup PPC
 *
 * Provides working implementations of:
 *   - std_env_args()    — wraps argc/argv into a Vec-like structure
 *   - print_usage()     — prints actual help text
 *   - flag_value()      — extracts --flag value from args
 *   - has_flag()        — checks if flag present
 *   - main()            — real entry point that dispatches commands
 *
 * On Tiger, we use _NSGetArgc()/_NSGetArgv() from crt_externs.h
 * to access command-line arguments.
 *
 * This file REPLACES the transpiled cli.o in the link.
 * Compiled: gcc -std=c99 -O2 -c rust_cli_real.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* macOS provides these to access argc/argv from anywhere */
#ifdef __APPLE__
#include <crt_externs.h>
#define get_argc() (*_NSGetArgc())
#define get_argv() (*_NSGetArgv())
#else
/* Fallback: store from main */
static int g_argc = 0;
static char **g_argv = NULL;
#define get_argc() g_argc
#define get_argv() g_argv
#endif

/* ============================================================
 * Vec-like structure for args
 * Layout matches our RustVec: { ptr, len, cap, elem_size }
 * ============================================================ */
typedef struct {
    char **items;
    int len;
    int cap;
    int elem_size;
} ArgVec;

/* Forward declarations from runtime stubs */
extern void rust_runtime_init(void);
extern void rust_runtime_cleanup(void);
extern void vec_drop(void *v);
extern void rust_println(const char *s);

/* ============================================================
 * std_env_args — return a Vec of command-line argument strings
 * ============================================================ */
void *std_env_args(void) {
    int argc = get_argc();
    char **argv = get_argv();

    ArgVec *v = (ArgVec *)calloc(1, sizeof(ArgVec));
    if (!v) return NULL;

    v->items = (char **)malloc(sizeof(char *) * (argc + 1));
    if (!v->items) { free(v); return NULL; }

    for (int i = 0; i < argc; i++) {
        v->items[i] = argv[i]; /* point to original strings, no copy needed */
    }
    v->len = argc;
    v->cap = argc + 1;
    v->elem_size = sizeof(char *);

    return v;
}

/* Also provide env_args (unsanitized path version) */
void *env_args(void) {
    return std_env_args();
}

/* ============================================================
 * print_usage — actual rusty-backup help text
 * ============================================================ */
void print_usage(const char *prog) {
    const char *name = prog;
    /* Extract just the filename from path */
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
        "  rip            Rip an optical disc to ISO or BIN/CUE\n"
        "  help           Show this help message\n"
        "\n"
        "Run '%s <COMMAND> --help' for per-command options.\n",
        name, name);
}

/* ============================================================
 * flag_value — extract --key value or --key=value from args
 * Returns NULL if not found.
 * ============================================================ */
const char *flag_value(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0 && i + 1 < argc) {
            return argv[i + 1];
        }
        /* --key=value form */
        int flen = strlen(flag);
        if (strncmp(argv[i], flag, flen) == 0 && argv[i][flen] == '=') {
            return &argv[i][flen + 1];
        }
    }
    return NULL;
}

/* ============================================================
 * has_flag — check if flag is present in args
 * ============================================================ */
int has_flag(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], flag) == 0) return 1;
    }
    return 0;
}

/* ============================================================
 * cmd_list_devices — enumerate available disks (stub for now)
 * ============================================================ */
static int cmd_list_devices(void) {
    printf("Scanning for disk devices...\n\n");
    printf("  %-20s %-10s %-8s %s\n", "DEVICE", "SIZE", "BUS", "MOUNT");
    printf("  %-20s %-10s %-8s %s\n", "------", "----", "---", "-----");

    /* On macOS, we'd enumerate /dev/disk* */
    /* For now, show what we can detect */
#ifdef __APPLE__
    FILE *fp = popen("ls /dev/disk[0-9]* 2>/dev/null | head -20", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            /* Trim newline */
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            printf("  %-20s %-10s %-8s %s\n", line, "?", "?", "?");
        }
        pclose(fp);
    }
#else
    printf("  (device enumeration not yet implemented on this platform)\n");
#endif
    return 0;
}

/* ============================================================
 * cmd_backup — backup a device (skeleton)
 * ============================================================ */
static int cmd_backup(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        fprintf(stderr,
            "USAGE: rusty-backup backup [OPTIONS]\n\n"
            "OPTIONS:\n"
            "  --source <PATH>        Source device or image file\n"
            "  --dest <PATH>          Destination directory for backup\n"
            "  --name <NAME>          Backup name (default: auto-generated)\n"
            "  --compression <TYPE>   none, zstd, chd (default: zstd)\n"
            "  --checksum <TYPE>      none, crc32, sha256 (default: sha256)\n"
            "  --sector-by-sector     Full sector-by-sector backup\n"
            "  --split <SIZE_MB>      Split backup into chunks\n"
        );
        return 0;
    }

    const char *source = flag_value(argc, argv, "--source");
    const char *dest = flag_value(argc, argv, "--dest");

    if (!source || !dest) {
        fprintf(stderr, "Error: --source and --dest are required\n");
        fprintf(stderr, "Run 'rusty-backup backup --help' for options\n");
        return 1;
    }

    printf("Backup: %s -> %s\n", source, dest);
    printf("(Backup operation not yet fully implemented in transpiled build)\n");
    return 0;
}

/* ============================================================
 * cmd_restore — restore a backup (skeleton)
 * ============================================================ */
static int cmd_restore(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        fprintf(stderr,
            "USAGE: rusty-backup restore [OPTIONS]\n\n"
            "OPTIONS:\n"
            "  --backup-dir <PATH>    Path to backup directory\n"
            "  --target <PATH>        Target device or image file\n"
            "  --target-size <BYTES>  Override target size\n"
        );
        return 0;
    }

    const char *backup_dir = flag_value(argc, argv, "--backup-dir");
    const char *target = flag_value(argc, argv, "--target");

    if (!backup_dir || !target) {
        fprintf(stderr, "Error: --backup-dir and --target are required\n");
        return 1;
    }

    printf("Restore: %s -> %s\n", backup_dir, target);
    printf("(Restore operation not yet fully implemented in transpiled build)\n");
    return 0;
}

/* ============================================================
 * cmd_inspect — show backup metadata (skeleton)
 * ============================================================ */
static int cmd_inspect(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Error: specify backup directory to inspect\n");
        return 1;
    }
    printf("Inspecting: %s\n", argv[0]);
    printf("(Inspect not yet fully implemented in transpiled build)\n");
    return 0;
}

/* ============================================================
 * cmd_rip — optical disc ripping (skeleton)
 * ============================================================ */
static int cmd_rip(int argc, char **argv) {
    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h")) {
        fprintf(stderr,
            "USAGE: rusty-backup rip [OPTIONS]\n\n"
            "OPTIONS:\n"
            "  --device <PATH>   Optical drive device (e.g. /dev/disk1)\n"
            "  --output <PATH>   Output file path\n"
            "  --format <FMT>    iso or bincue (default: iso)\n"
            "  --eject           Eject disc after ripping\n"
        );
        return 0;
    }

    printf("(Disc ripping not yet implemented in transpiled build)\n");
    return 0;
}

/* ============================================================
 * main — real entry point
 * ============================================================ */
int main(int argc, char **argv) {
#ifndef __APPLE__
    g_argc = argc;
    g_argv = argv;
#endif
    (void)argc; /* we use _NSGetArgc on macOS */
    (void)argv;

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
        printf("rusty-backup 0.1.0-ppc (transpiled from Rust by rust-ppc-tiger)\n");
        return 0;
    } else {
        fprintf(stderr, "Unknown subcommand: %s\n", cmd);
        print_usage(av[0]);
        return 1;
    }
}
