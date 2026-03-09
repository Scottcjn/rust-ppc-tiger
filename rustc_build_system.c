/*
 * Rust Build System for PowerPC Tiger/Leopard
 * ============================================
 *
 * Cargo-compatible build orchestration with vendor manifest support.
 * Handles 1000+ crate dependencies via build_manifest.json from cargo_fetch.py.
 *
 * Two modes:
 *   1. Simple:   Parse Cargo.toml directly (small projects)
 *   2. Manifest: Read build_manifest.json from vendored deps (large projects)
 *
 * Part of rust-ppc-tiger — Elyan Labs
 * Opus 4.6 + Sophia Elya
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* ============================================================
 * CONFIGURATION — limits bumped for real-world projects
 * ============================================================ */

#define MAX_CRATES      2048
#define MAX_DEPS         128
#define MAX_SOURCES     1024
#define MAX_PATH_LEN     512
#define MAX_LINE_LEN    4096
#define MAX_NAME_LEN     128

typedef struct {
    char target[64];        /* powerpc-apple-darwin8 (Tiger) */
    char opt_level[8];      /* 0, 1, 2, 3, s, z */
    int debug_info;
    int lto;
    char cpu[32];           /* 7450, 970 */
    int altivec;
    char sysroot[256];
    char linker[256];
    char vendor_dir[MAX_PATH_LEN];
    int use_manifest;       /* 1 = read build_manifest.json */
    int dry_run;            /* 1 = print commands, don't execute */
    int verbose;
} BuildConfig;

typedef struct {
    char name[MAX_NAME_LEN];
    char version[32];
    char path[MAX_PATH_LEN];
    char** dependencies;
    int dep_count;
    int dep_capacity;
    char** source_files;
    int source_count;
    int source_capacity;
    int is_lib;
    int is_bin;
    int skip;               /* 1 = platform-filtered, don't compile */
} Crate;

typedef struct {
    Crate* crates;
    int crate_count;
    int crate_capacity;
    BuildConfig config;
    char output_dir[MAX_PATH_LEN];
    char project_dir[MAX_PATH_LEN];
} BuildContext;

/* ============================================================
 * MEMORY MANAGEMENT
 * ============================================================ */

static Crate* crate_new(void) {
    Crate* c = calloc(1, sizeof(Crate));
    c->dep_capacity = 16;
    c->dependencies = calloc(c->dep_capacity, sizeof(char*));
    c->source_capacity = 64;
    c->source_files = calloc(c->source_capacity, sizeof(char*));
    return c;
}

static void crate_add_dep(Crate* c, const char* dep) {
    if (c->dep_count >= c->dep_capacity) {
        c->dep_capacity *= 2;
        c->dependencies = realloc(c->dependencies, c->dep_capacity * sizeof(char*));
    }
    c->dependencies[c->dep_count++] = strdup(dep);
}

static void crate_add_source(Crate* c, const char* path) {
    if (c->source_count >= c->source_capacity) {
        c->source_capacity *= 2;
        c->source_files = realloc(c->source_files, c->source_capacity * sizeof(char*));
    }
    c->source_files[c->source_count++] = strdup(path);
}

static BuildContext* ctx_new(void) {
    BuildContext* ctx = calloc(1, sizeof(BuildContext));
    ctx->crate_capacity = 256;
    ctx->crates = calloc(ctx->crate_capacity, sizeof(Crate));
    return ctx;
}

static Crate* ctx_add_crate(BuildContext* ctx) {
    if (ctx->crate_count >= ctx->crate_capacity) {
        ctx->crate_capacity *= 2;
        ctx->crates = realloc(ctx->crates, ctx->crate_capacity * sizeof(Crate));
        memset(&ctx->crates[ctx->crate_count], 0,
               (ctx->crate_capacity - ctx->crate_count) * sizeof(Crate));
    }
    Crate* c = &ctx->crates[ctx->crate_count++];
    c->dep_capacity = 16;
    c->dependencies = calloc(c->dep_capacity, sizeof(char*));
    c->source_capacity = 64;
    c->source_files = calloc(c->source_capacity, sizeof(char*));
    return c;
}

/* ============================================================
 * SIMPLE JSON PARSER (for build_manifest.json)
 *
 * Minimal recursive-descent parser. Handles the specific
 * structure of our manifest — not a general JSON parser.
 * ============================================================ */

typedef struct {
    const char* data;
    int pos;
    int len;
} JsonParser;

static void jp_skip_ws(JsonParser* jp) {
    while (jp->pos < jp->len) {
        char c = jp->data[jp->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            jp->pos++;
        else break;
    }
}

static int jp_match(JsonParser* jp, char c) {
    jp_skip_ws(jp);
    if (jp->pos < jp->len && jp->data[jp->pos] == c) {
        jp->pos++;
        return 1;
    }
    return 0;
}

/* Parse a JSON string, returns malloc'd copy */
static char* jp_string(JsonParser* jp) {
    jp_skip_ws(jp);
    if (jp->pos >= jp->len || jp->data[jp->pos] != '"') return NULL;
    jp->pos++; /* skip opening " */

    int start = jp->pos;
    while (jp->pos < jp->len && jp->data[jp->pos] != '"') {
        if (jp->data[jp->pos] == '\\') jp->pos++; /* skip escape */
        jp->pos++;
    }

    int slen = jp->pos - start;
    char* s = malloc(slen + 1);
    memcpy(s, jp->data + start, slen);
    s[slen] = '\0';

    if (jp->pos < jp->len) jp->pos++; /* skip closing " */
    return s;
}

/* Parse a JSON number (integer only) */
static int jp_number(JsonParser* jp) {
    jp_skip_ws(jp);
    int val = 0;
    int neg = 0;
    if (jp->pos < jp->len && jp->data[jp->pos] == '-') { neg = 1; jp->pos++; }
    while (jp->pos < jp->len && jp->data[jp->pos] >= '0' && jp->data[jp->pos] <= '9') {
        val = val * 10 + (jp->data[jp->pos] - '0');
        jp->pos++;
    }
    return neg ? -val : val;
}

/* Parse a JSON boolean */
static int jp_bool(JsonParser* jp) {
    jp_skip_ws(jp);
    if (jp->pos + 4 <= jp->len && strncmp(jp->data + jp->pos, "true", 4) == 0) {
        jp->pos += 4;
        return 1;
    }
    if (jp->pos + 5 <= jp->len && strncmp(jp->data + jp->pos, "false", 5) == 0) {
        jp->pos += 5;
        return 0;
    }
    return 0;
}

/* Skip a JSON value (any type) */
static void jp_skip_value(JsonParser* jp) {
    jp_skip_ws(jp);
    if (jp->pos >= jp->len) return;

    char c = jp->data[jp->pos];
    if (c == '"') { free(jp_string(jp)); }
    else if (c == '{') {
        jp->pos++;
        while (!jp_match(jp, '}')) {
            free(jp_string(jp));
            jp_match(jp, ':');
            jp_skip_value(jp);
            jp_match(jp, ',');
        }
    }
    else if (c == '[') {
        jp->pos++;
        while (!jp_match(jp, ']')) {
            jp_skip_value(jp);
            jp_match(jp, ',');
        }
    }
    else if (c == 't' || c == 'f') { jp_bool(jp); }
    else if (c == 'n') { jp->pos += 4; } /* null */
    else { jp_number(jp); }
}

/* ============================================================
 * BUILD MANIFEST PARSER
 * ============================================================ */

static int load_manifest(BuildContext* ctx, const char* manifest_path) {
    FILE* f = fopen(manifest_path, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open manifest: %s\n", manifest_path);
        return 0;
    }

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = malloc(fsize + 1);
    fread(data, 1, fsize, f);
    data[fsize] = '\0';
    fclose(f);

    JsonParser jp = { data, 0, (int)fsize };

    /* Parse top-level object */
    if (!jp_match(&jp, '{')) { free(data); return 0; }

    while (!jp_match(&jp, '}')) {
        char* key = jp_string(&jp);
        jp_match(&jp, ':');

        if (key && strcmp(key, "vendor_dir") == 0) {
            char* val = jp_string(&jp);
            if (val) { strncpy(ctx->config.vendor_dir, val, MAX_PATH_LEN-1); free(val); }
        }
        else if (key && strcmp(key, "crates") == 0) {
            /* Parse crates array */
            if (!jp_match(&jp, '[')) { free(key); break; }

            while (!jp_match(&jp, ']')) {
                Crate* crate = ctx_add_crate(ctx);

                /* Parse crate object */
                if (!jp_match(&jp, '{')) break;

                while (!jp_match(&jp, '}')) {
                    char* ckey = jp_string(&jp);
                    jp_match(&jp, ':');

                    if (!ckey) { jp_skip_value(&jp); jp_match(&jp, ','); continue; }

                    if (strcmp(ckey, "name") == 0) {
                        char* v = jp_string(&jp);
                        if (v) { strncpy(crate->name, v, MAX_NAME_LEN-1); free(v); }
                    }
                    else if (strcmp(ckey, "version") == 0) {
                        char* v = jp_string(&jp);
                        if (v) { strncpy(crate->version, v, 31); free(v); }
                    }
                    else if (strcmp(ckey, "path") == 0) {
                        char* v = jp_string(&jp);
                        if (v) { strncpy(crate->path, v, MAX_PATH_LEN-1); free(v); }
                    }
                    else if (strcmp(ckey, "is_lib") == 0) {
                        crate->is_lib = jp_bool(&jp);
                    }
                    else if (strcmp(ckey, "is_bin") == 0) {
                        crate->is_bin = jp_bool(&jp);
                    }
                    else if (strcmp(ckey, "dependencies") == 0) {
                        /* Parse string array */
                        if (jp_match(&jp, '[')) {
                            while (!jp_match(&jp, ']')) {
                                char* dep = jp_string(&jp);
                                if (dep) { crate_add_dep(crate, dep); free(dep); }
                                jp_match(&jp, ',');
                            }
                        }
                    }
                    else if (strcmp(ckey, "source_files") == 0) {
                        /* Parse string array */
                        if (jp_match(&jp, '[')) {
                            while (!jp_match(&jp, ']')) {
                                char* sf = jp_string(&jp);
                                if (sf) {
                                    /* Build full path: crate.path + source_file */
                                    char full[MAX_PATH_LEN];
                                    snprintf(full, sizeof(full), "%s/%s", crate->path, sf);
                                    crate_add_source(crate, full);
                                    free(sf);
                                }
                                jp_match(&jp, ',');
                            }
                        }
                    }
                    else {
                        jp_skip_value(&jp);
                    }

                    free(ckey);
                    jp_match(&jp, ',');
                }
                jp_match(&jp, ',');
            }
        }
        else {
            jp_skip_value(&jp);
        }

        free(key);
        jp_match(&jp, ',');
    }

    free(data);
    return 1;
}

/* ============================================================
 * CARGO.TOML PARSING (legacy — small projects)
 * ============================================================ */

int parse_cargo_toml(const char* path, Crate* crate) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;

    char line[MAX_LINE_LEN];
    char section[128] = "";

    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] == '\0' || line[0] == '#') continue;

        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(section, line + 1, sizeof(section)-1);
            }
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = line;
        char* value = eq + 1;

        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t' || *value == '"') value++;
        char* end = key + strlen(key) - 1;
        while (end > key && (*end == ' ' || *end == '\t')) *end-- = '\0';
        end = value + strlen(value) - 1;
        while (end > value && (*end == ' ' || *end == '\t' || *end == '"')) *end-- = '\0';

        if (strcmp(section, "package") == 0) {
            if (strcmp(key, "name") == 0) strncpy(crate->name, value, MAX_NAME_LEN-1);
            if (strcmp(key, "version") == 0) strncpy(crate->version, value, 31);
        }
        else if (strcmp(section, "lib") == 0) {
            crate->is_lib = 1;
        }
        else if (strncmp(section, "bin", 3) == 0) {
            crate->is_bin = 1;
        }
        else if (strcmp(section, "dependencies") == 0) {
            crate_add_dep(crate, key);
        }
    }

    fclose(f);
    return 1;
}

/* ============================================================
 * SOURCE FILE DISCOVERY
 * ============================================================ */

void find_rust_files(const char* dir, Crate* crate) {
    DIR* d = opendir(dir);
    if (!d) return;

    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            find_rust_files(path, crate);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(entry->d_name);
            if (len > 3 && strcmp(entry->d_name + len - 3, ".rs") == 0) {
                crate_add_source(crate, path);
            }
        }
    }

    closedir(d);
}

/* ============================================================
 * DEPENDENCY RESOLUTION (scales to 2048 crates)
 * ============================================================ */

typedef struct {
    char name[MAX_NAME_LEN];
    int index;
    int visited;
    int in_stack;
} DepNode;

static DepNode* dep_nodes = NULL;
static int* build_order = NULL;

void topo_sort_visit(DepNode* nodes, int* order, int* order_idx,
                     int node_idx, int node_count, Crate* crates) {
    if (nodes[node_idx].visited) return;
    if (nodes[node_idx].in_stack) {
        /* Circular dependency — skip silently for large dep trees */
        return;
    }

    nodes[node_idx].in_stack = 1;

    Crate* crate = &crates[node_idx];
    for (int i = 0; i < crate->dep_count; i++) {
        for (int j = 0; j < node_count; j++) {
            if (strcmp(nodes[j].name, crate->dependencies[i]) == 0) {
                topo_sort_visit(nodes, order, order_idx, j, node_count, crates);
                break; /* found the dep, no need to keep searching */
            }
        }
    }

    nodes[node_idx].in_stack = 0;
    nodes[node_idx].visited = 1;
    order[(*order_idx)++] = node_idx;
}

void resolve_build_order(BuildContext* ctx, int* order) {
    int n = ctx->crate_count;
    dep_nodes = calloc(n, sizeof(DepNode));
    int order_idx = 0;

    for (int i = 0; i < n; i++) {
        strncpy(dep_nodes[i].name, ctx->crates[i].name, MAX_NAME_LEN-1);
        dep_nodes[i].index = i;
        dep_nodes[i].visited = 0;
        dep_nodes[i].in_stack = 0;
    }

    for (int i = 0; i < n; i++) {
        topo_sort_visit(dep_nodes, order, &order_idx, i, n, ctx->crates);
    }

    free(dep_nodes);
    dep_nodes = NULL;
}

/* ============================================================
 * COMPILATION
 * ============================================================ */

void compile_crate(BuildContext* ctx, Crate* crate) {
    if (crate->skip) {
        if (ctx->config.verbose)
            printf("; SKIP: %s (platform-filtered)\n", crate->name);
        return;
    }

    if (crate->source_count == 0) {
        if (ctx->config.verbose)
            printf("; SKIP: %s (no source files)\n", crate->name);
        return;
    }

    printf("; Compiling crate: %s v%s (%d files)\n",
           crate->name, crate->version, crate->source_count);

    /* Create output directory */
    char crate_out[MAX_PATH_LEN];
    snprintf(crate_out, sizeof(crate_out), "%s/obj/%s", ctx->output_dir, crate->name);

    char mkdir_cmd[MAX_PATH_LEN + 16];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", crate_out);
    if (!ctx->config.dry_run) system(mkdir_cmd);

    for (int i = 0; i < crate->source_count; i++) {
        char* src = crate->source_files[i];

        /* Derive object file name from source path */
        const char* basename = strrchr(src, '/');
        basename = basename ? basename + 1 : src;

        char obj_name[256];
        strncpy(obj_name, basename, sizeof(obj_name)-1);
        /* Replace .rs with .o */
        char* dot = strrchr(obj_name, '.');
        if (dot) strcpy(dot, ".o");

        char asm_path[MAX_PATH_LEN];
        char obj_path[MAX_PATH_LEN];
        snprintf(asm_path, sizeof(asm_path), "%s/%s", crate_out, basename);
        /* Replace .rs extension */
        dot = strrchr(asm_path, '.');
        if (dot) strcpy(dot, ".s");

        snprintf(obj_path, sizeof(obj_path), "%s/%s", crate_out, obj_name);

        /* Compile: .rs → .s */
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                "./rustc_ppc %s -o %s "
                "-C target-cpu=%s "
                "-C opt-level=%s "
                "%s %s",
                src, asm_path,
                ctx->config.cpu,
                ctx->config.opt_level,
                ctx->config.altivec ? "-C target-feature=+altivec" : "",
                ctx->config.debug_info ? "-g" : "");

        if (ctx->config.verbose || ctx->config.dry_run)
            printf(";   $ %s\n", cmd);

        if (!ctx->config.dry_run) {
            int ret = system(cmd);
            if (ret != 0) {
                fprintf(stderr, "Error: Compilation failed for %s\n", src);
                continue;
            }
        }

        /* Assemble: .s → .o */
        snprintf(cmd, sizeof(cmd), "as -o %s %s", obj_path, asm_path);
        if (ctx->config.verbose || ctx->config.dry_run)
            printf(";   $ %s\n", cmd);
        if (!ctx->config.dry_run) system(cmd);
    }

    /* Archive library crates: .o → .a */
    if (crate->is_lib) {
        char archive[MAX_PATH_LEN];
        snprintf(archive, sizeof(archive), "%s/lib/lib%s.a", ctx->output_dir, crate->name);

        /* Ensure lib dir exists */
        char libdir[MAX_PATH_LEN];
        snprintf(libdir, sizeof(libdir), "%s/lib", ctx->output_dir);
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", libdir);
        if (!ctx->config.dry_run) system(mkdir_cmd);

        char ar_cmd[4096];
        int len = snprintf(ar_cmd, sizeof(ar_cmd), "ar rcs %s", archive);

        /* Add all .o files from this crate */
        for (int i = 0; i < crate->source_count && len < (int)sizeof(ar_cmd) - MAX_PATH_LEN; i++) {
            const char* basename = strrchr(crate->source_files[i], '/');
            basename = basename ? basename + 1 : crate->source_files[i];
            char obj_name[256];
            strncpy(obj_name, basename, sizeof(obj_name)-1);
            char* dot = strrchr(obj_name, '.');
            if (dot) strcpy(dot, ".o");

            len += snprintf(ar_cmd + len, sizeof(ar_cmd) - len,
                           " %s/%s", crate_out, obj_name);
        }

        if (ctx->config.verbose || ctx->config.dry_run)
            printf(";   $ %s\n", ar_cmd);
        if (!ctx->config.dry_run) system(ar_cmd);
    }
}

void link_binary(BuildContext* ctx, Crate* crate) {
    printf("; Linking: %s\n", crate->name);

    /* Build link command */
    char cmd[65536]; /* Large buffer for many -l flags */
    int len = 0;

    len += snprintf(cmd + len, sizeof(cmd) - len,
            "%s -o %s/%s ",
            ctx->config.linker[0] ? ctx->config.linker : "gcc",
            ctx->output_dir,
            crate->name);

    /* Add main crate object files */
    char crate_out[MAX_PATH_LEN];
    snprintf(crate_out, sizeof(crate_out), "%s/obj/%s", ctx->output_dir, crate->name);

    for (int i = 0; i < crate->source_count; i++) {
        const char* basename = strrchr(crate->source_files[i], '/');
        basename = basename ? basename + 1 : crate->source_files[i];
        char obj_name[256];
        strncpy(obj_name, basename, sizeof(obj_name)-1);
        char* dot = strrchr(obj_name, '.');
        if (dot) strcpy(dot, ".o");

        len += snprintf(cmd + len, sizeof(cmd) - len,
                "%s/%s ", crate_out, obj_name);
    }

    /* Add library search path */
    len += snprintf(cmd + len, sizeof(cmd) - len,
            "-L%s/lib ", ctx->output_dir);

    /* Add dependency libraries (from build order) */
    for (int i = 0; i < crate->dep_count; i++) {
        len += snprintf(cmd + len, sizeof(cmd) - len,
                "-l%s ", crate->dependencies[i]);
    }

    /* Tiger/Leopard specific */
    const char* sdk = ctx->config.sysroot[0] ? ctx->config.sysroot :
                      "/Developer/SDKs/MacOSX10.4u.sdk/usr";
    len += snprintf(cmd + len, sizeof(cmd) - len,
            "-L%s/lib -lSystem -lc ", sdk);

    /* AltiVec library if enabled */
    if (ctx->config.altivec) {
        len += snprintf(cmd + len, sizeof(cmd) - len,
                "-framework Accelerate ");
    }

    /* Tiger linking flags */
    len += snprintf(cmd + len, sizeof(cmd) - len,
            "-arch ppc -mmacosx-version-min=10.4 ");

    if (ctx->config.verbose || ctx->config.dry_run)
        printf(";   $ %s\n", cmd);
    if (!ctx->config.dry_run) {
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "Error: Linking failed for %s\n", crate->name);
        }
    }
}

/* ============================================================
 * MAIN BUILD DRIVER
 * ============================================================ */

void build_project(BuildContext* ctx, const char* project_dir) {
    strncpy(ctx->project_dir, project_dir, MAX_PATH_LEN-1);

    /* Default config for Tiger on G4 */
    strcpy(ctx->config.target, "powerpc-apple-darwin8");
    strcpy(ctx->config.opt_level, "3");
    strcpy(ctx->config.cpu, "7450");
    ctx->config.altivec = 1;
    ctx->config.debug_info = 0;
    snprintf(ctx->output_dir, sizeof(ctx->output_dir),
             "%s/target/powerpc-apple-darwin8/release", project_dir);

    /* Try manifest first (from cargo_fetch.py) */
    char manifest_path[MAX_PATH_LEN];

    if (ctx->config.vendor_dir[0]) {
        snprintf(manifest_path, sizeof(manifest_path),
                 "%s/build_manifest.json", ctx->config.vendor_dir);
    } else {
        snprintf(manifest_path, sizeof(manifest_path),
                 "%s/vendor/build_manifest.json", project_dir);
    }

    if (access(manifest_path, F_OK) == 0) {
        printf("; Loading build manifest: %s\n", manifest_path);
        if (!load_manifest(ctx, manifest_path)) {
            fprintf(stderr, "Error: Failed to parse manifest\n");
            return;
        }
        ctx->config.use_manifest = 1;
        printf("; Loaded %d crates from manifest\n", ctx->crate_count);
    } else {
        /* Fall back to Cargo.toml parsing */
        char toml_path[MAX_PATH_LEN];
        snprintf(toml_path, sizeof(toml_path), "%s/Cargo.toml", project_dir);

        Crate* main_crate = ctx_add_crate(ctx);
        if (!parse_cargo_toml(toml_path, main_crate)) {
            fprintf(stderr, "Error: Cannot read %s\n", toml_path);
            return;
        }

        strncpy(main_crate->path, project_dir, MAX_PATH_LEN-1);

        char src_dir[MAX_PATH_LEN];
        snprintf(src_dir, sizeof(src_dir), "%s/src", project_dir);
        find_rust_files(src_dir, main_crate);
    }

    /* Print build info */
    printf("; =====================================================\n");
    printf("; Rust Build for Tiger/Leopard PowerPC\n");
    printf("; =====================================================\n");
    if (ctx->crate_count > 0) {
        printf("; Project: %s v%s\n", ctx->crates[0].name, ctx->crates[0].version);
    }
    printf("; Target:  %s\n", ctx->config.target);
    printf("; CPU:     %s, AltiVec: %s\n",
           ctx->config.cpu, ctx->config.altivec ? "yes" : "no");
    printf("; Crates:  %d\n", ctx->crate_count);

    int total_sources = 0;
    for (int i = 0; i < ctx->crate_count; i++)
        total_sources += ctx->crates[i].source_count;
    printf("; Sources: %d files\n", total_sources);

    if (ctx->config.dry_run) printf("; Mode:    DRY RUN\n");
    printf("; =====================================================\n\n");

    /* Create output directory */
    char mkdir_cmd[MAX_PATH_LEN + 16];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s/obj %s/lib",
             ctx->output_dir, ctx->output_dir);
    if (!ctx->config.dry_run) system(mkdir_cmd);

    /* Resolve build order */
    build_order = calloc(ctx->crate_count, sizeof(int));
    resolve_build_order(ctx, build_order);

    /* Compile each crate in dependency order */
    int compiled = 0, skipped = 0;
    for (int i = 0; i < ctx->crate_count; i++) {
        Crate* c = &ctx->crates[build_order[i]];
        if (c->skip || c->source_count == 0) {
            skipped++;
            continue;
        }
        compile_crate(ctx, c);
        compiled++;
    }

    /* Find the binary crate and link */
    for (int i = ctx->crate_count - 1; i >= 0; i--) {
        Crate* c = &ctx->crates[build_order[i]];
        if (c->is_bin || (!c->is_lib && c->source_count > 0)) {
            link_binary(ctx, c);
            break;
        }
    }

    printf("\n; =====================================================\n");
    printf("; Build complete! Compiled: %d, Skipped: %d\n", compiled, skipped);
    printf("; =====================================================\n");

    free(build_order);
    build_order = NULL;
}

/* ============================================================
 * TIGER-SPECIFIC TOOLCHAIN INFO
 * ============================================================ */

void emit_tiger_toolchain(void) {
    printf("; Tiger/Leopard Rust Toolchain (rust-ppc-tiger)\n\n");

    printf("; rustc_ppc - Rust to PowerPC compiler\n");
    printf("; Target triple: powerpc-apple-darwin8 (Tiger)\n");
    printf(";               powerpc-apple-darwin9 (Leopard)\n\n");

    printf("; Compiler flags:\n");
    printf(";   -C target-cpu=7450    # G4 (default)\n");
    printf(";   -C target-cpu=970     # G5\n");
    printf(";   -C target-feature=+altivec\n");
    printf(";   -C opt-level=3        # Maximum optimization\n");
    printf(";   -C lto=thin           # Link-time optimization\n\n");

    printf("; Linker (gcc):\n");
    printf(";   -isysroot /Developer/SDKs/MacOSX10.4u.sdk\n");
    printf(";   -mmacosx-version-min=10.4\n");
    printf(";   -arch ppc             # or ppc64 for G5 64-bit\n\n");

    printf("; Example build:\n");
    printf(";   ./rustc_ppc src/main.rs -o main.s -C target-cpu=7450\n");
    printf(";   as -o main.o main.s\n");
    printf(";   gcc -o myapp main.o -isysroot /Developer/SDKs/MacOSX10.4u.sdk\n\n");

    printf("; Cargo commands:\n");
    printf(";   cargo_ppc fetch    Download dependencies from crates.io\n");
    printf(";   cargo_ppc vendor   Fetch + skip platform-incompatible crates\n");
    printf(";   cargo_ppc deps     Show dependency summary\n");
    printf(";   cargo_ppc build    Compile project\n");
}

/* ============================================================
 * MAKEFILE GENERATION (updated for vendor support)
 * ============================================================ */

void generate_makefile(const char* project_name) {
    printf("# Makefile for %s (Tiger/Leopard PowerPC)\n", project_name);
    printf("# Generated by rustc_build_system (rust-ppc-tiger)\n\n");

    printf("# Toolchain\n");
    printf("RUSTC = ./rustc_ppc\n");
    printf("AS = as\n");
    printf("CC = gcc\n");
    printf("AR = ar\n\n");

    printf("# Target configuration\n");
    printf("TARGET = powerpc-apple-darwin8\n");
    printf("CPU = 7450\n");
    printf("SDK = /Developer/SDKs/MacOSX10.4u.sdk\n\n");

    printf("# Flags\n");
    printf("RUSTFLAGS = -C target-cpu=$(CPU) -C target-feature=+altivec -C opt-level=3\n");
    printf("ASFLAGS = \n");
    printf("LDFLAGS = -isysroot $(SDK) -mmacosx-version-min=10.4 -arch ppc\n");
    printf("LIBS = -lSystem -lc -framework Accelerate\n\n");

    printf("# Output\n");
    printf("BUILD_DIR = target/$(TARGET)/release\n");
    printf("BIN = %s\n\n", project_name);

    printf("# Source files\n");
    printf("SOURCES = $(wildcard src/*.rs) $(wildcard src/**/*.rs)\n");
    printf("OBJECTS = $(patsubst src/%%.rs,$(BUILD_DIR)/obj/%%.o,$(SOURCES))\n\n");

    printf("# Rules\n");
    printf("all: $(BUILD_DIR)/$(BIN)\n\n");

    printf("$(BUILD_DIR)/obj:\n");
    printf("\tmkdir -p $@\n\n");

    printf("$(BUILD_DIR)/obj/%%.s: src/%%.rs | $(BUILD_DIR)/obj\n");
    printf("\t@mkdir -p $(dir $@)\n");
    printf("\t$(RUSTC) $< -o $@ $(RUSTFLAGS)\n\n");

    printf("$(BUILD_DIR)/obj/%%.o: $(BUILD_DIR)/obj/%%.s\n");
    printf("\t$(AS) $(ASFLAGS) -o $@ $<\n\n");

    printf("$(BUILD_DIR)/$(BIN): $(OBJECTS)\n");
    printf("\t$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)\n\n");

    printf("clean:\n");
    printf("\trm -rf $(BUILD_DIR)\n\n");

    printf(".PHONY: all clean\n");
}

/* ============================================================
 * MAIN
 * ============================================================ */

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Rust Build System for Tiger/Leopard PowerPC (v2.0)\n\n");
        printf("Usage:\n");
        printf("  %s build [path]              Build project\n", argv[0]);
        printf("  %s build [path] --dry-run    Show commands without executing\n", argv[0]);
        printf("  %s build [path] --vendor=DIR Use specific vendor directory\n", argv[0]);
        printf("  %s build [path] --cpu=970    Target G5 instead of G4\n", argv[0]);
        printf("  %s build [path] --verbose    Verbose output\n", argv[0]);
        printf("  %s toolchain                 Show toolchain info\n", argv[0]);
        printf("  %s makefile [name]           Generate Makefile\n", argv[0]);
        printf("  %s --demo                    Run demonstration\n", argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "build") == 0) {
        BuildContext* ctx = ctx_new();
        const char* project_dir = ".";

        /* Parse remaining arguments */
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--dry-run") == 0) {
                ctx->config.dry_run = 1;
            }
            else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
                ctx->config.verbose = 1;
            }
            else if (strncmp(argv[i], "--vendor=", 9) == 0) {
                strncpy(ctx->config.vendor_dir, argv[i] + 9, MAX_PATH_LEN-1);
            }
            else if (strncmp(argv[i], "--cpu=", 6) == 0) {
                strncpy(ctx->config.cpu, argv[i] + 6, 31);
            }
            else if (strcmp(argv[i], "--no-altivec") == 0) {
                ctx->config.altivec = 0;
            }
            else if (strcmp(argv[i], "--debug") == 0) {
                ctx->config.debug_info = 1;
                strcpy(ctx->config.opt_level, "0");
            }
            else if (argv[i][0] != '-') {
                project_dir = argv[i];
            }
        }

        build_project(ctx, project_dir);
        /* Note: not freeing ctx for simplicity — process exits anyway */
    }
    else if (strcmp(argv[1], "toolchain") == 0) {
        emit_tiger_toolchain();
    }
    else if (strcmp(argv[1], "makefile") == 0) {
        generate_makefile(argc > 2 ? argv[2] : "myproject");
    }
    else if (strcmp(argv[1], "--demo") == 0) {
        printf("; === Build System Demo ===\n\n");
        emit_tiger_toolchain();
        printf("\n");
        generate_makefile("rusty-backup-cli");
    }

    return 0;
}
