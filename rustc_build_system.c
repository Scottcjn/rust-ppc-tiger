/*
 * Rust Build System for PowerPC Tiger/Leopard
 *
 * Cargo-compatible build orchestration
 * Handles dependencies, compilation order, linking
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* ============================================================
 * CONFIGURATION
 * ============================================================ */

typedef struct {
    char target[64];        /* powerpc-apple-darwin8 (Tiger) */
    char opt_level[8];      /* 0, 1, 2, 3, s, z */
    int debug_info;
    int lto;
    char cpu[32];           /* 7450, 970 */
    int altivec;
    char sysroot[256];
    char linker[256];
} BuildConfig;

typedef struct {
    char name[64];
    char version[32];
    char path[256];
    char* dependencies[32];
    int dep_count;
    char* source_files[256];
    int source_count;
    int is_lib;
    int is_bin;
} Crate;

typedef struct {
    Crate crates[64];
    int crate_count;
    BuildConfig config;
    char output_dir[256];
} BuildContext;

/* ============================================================
 * CARGO.TOML PARSING
 * ============================================================ */

int parse_cargo_toml(const char* path, Crate* crate) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;

    char line[1024];
    char section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        /* Remove newline */
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#') continue;

        /* Section header */
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strcpy(section, line + 1);
            }
            continue;
        }

        /* Key = value */
        char* eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = line;
        char* value = eq + 1;

        /* Trim whitespace */
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t' || *value == '"') value++;
        char* end = key + strlen(key) - 1;
        while (end > key && (*end == ' ' || *end == '\t')) *end-- = '\0';
        end = value + strlen(value) - 1;
        while (end > value && (*end == ' ' || *end == '\t' || *end == '"')) *end-- = '\0';

        if (strcmp(section, "package") == 0) {
            if (strcmp(key, "name") == 0) strcpy(crate->name, value);
            if (strcmp(key, "version") == 0) strcpy(crate->version, value);
        }
        else if (strcmp(section, "lib") == 0) {
            crate->is_lib = 1;
        }
        else if (strncmp(section, "bin", 3) == 0) {
            crate->is_bin = 1;
        }
        else if (strcmp(section, "dependencies") == 0) {
            crate->dependencies[crate->dep_count] = strdup(key);
            crate->dep_count++;
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

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            find_rust_files(path, crate);
        } else if (S_ISREG(st.st_mode)) {
            size_t len = strlen(entry->d_name);
            if (len > 3 && strcmp(entry->d_name + len - 3, ".rs") == 0) {
                crate->source_files[crate->source_count] = strdup(path);
                crate->source_count++;
            }
        }
    }

    closedir(d);
}

/* ============================================================
 * DEPENDENCY RESOLUTION
 * ============================================================ */

typedef struct {
    char name[64];
    int index;
    int visited;
    int in_stack;
} DepNode;

void topo_sort_visit(DepNode* nodes, int* order, int* order_idx,
                     int node_idx, int node_count, Crate* crates) {
    if (nodes[node_idx].visited) return;
    if (nodes[node_idx].in_stack) {
        fprintf(stderr, "Error: Circular dependency involving %s\n",
                nodes[node_idx].name);
        return;
    }

    nodes[node_idx].in_stack = 1;

    /* Visit dependencies first */
    Crate* crate = &crates[node_idx];
    for (int i = 0; i < crate->dep_count; i++) {
        for (int j = 0; j < node_count; j++) {
            if (strcmp(nodes[j].name, crate->dependencies[i]) == 0) {
                topo_sort_visit(nodes, order, order_idx, j, node_count, crates);
            }
        }
    }

    nodes[node_idx].in_stack = 0;
    nodes[node_idx].visited = 1;
    order[(*order_idx)++] = node_idx;
}

void resolve_build_order(BuildContext* ctx, int* order) {
    DepNode nodes[64];
    int order_idx = 0;

    for (int i = 0; i < ctx->crate_count; i++) {
        strcpy(nodes[i].name, ctx->crates[i].name);
        nodes[i].index = i;
        nodes[i].visited = 0;
        nodes[i].in_stack = 0;
    }

    for (int i = 0; i < ctx->crate_count; i++) {
        topo_sort_visit(nodes, order, &order_idx, i, ctx->crate_count, ctx->crates);
    }
}

/* ============================================================
 * COMPILATION
 * ============================================================ */

void compile_crate(BuildContext* ctx, Crate* crate) {
    printf("; Compiling crate: %s v%s\n", crate->name, crate->version);

    for (int i = 0; i < crate->source_count; i++) {
        char* src = crate->source_files[i];
        char obj[512];
        snprintf(obj, sizeof(obj), "%s/%s.o", ctx->output_dir, crate->name);

        printf(";   %s -> %s\n", src, obj);

        /* In real impl, would invoke rustc_ppc here */
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                "./rustc_ppc %s -o %s.s "
                "-C target-cpu=%s "
                "-C opt-level=%s "
                "%s %s",
                src,
                crate->name,
                ctx->config.cpu,
                ctx->config.opt_level,
                ctx->config.altivec ? "-C target-feature=+altivec" : "",
                ctx->config.debug_info ? "-g" : "");

        printf(";   $ %s\n", cmd);

        /* Assemble */
        snprintf(cmd, sizeof(cmd),
                "as -o %s/%s.o %s.s",
                ctx->output_dir, crate->name, crate->name);
        printf(";   $ %s\n", cmd);
    }
}

void link_binary(BuildContext* ctx, Crate* crate) {
    printf("; Linking: %s\n", crate->name);

    char cmd[2048];
    int len = 0;

    len += snprintf(cmd + len, sizeof(cmd) - len,
            "%s -o %s/%s ",
            ctx->config.linker[0] ? ctx->config.linker : "gcc",
            ctx->output_dir,
            crate->name);

    /* Add object files */
    len += snprintf(cmd + len, sizeof(cmd) - len,
            "%s/%s.o ", ctx->output_dir, crate->name);

    /* Add dependency libraries */
    for (int i = 0; i < crate->dep_count; i++) {
        len += snprintf(cmd + len, sizeof(cmd) - len,
                "-l%s ", crate->dependencies[i]);
    }

    /* Tiger/Leopard specific */
    len += snprintf(cmd + len, sizeof(cmd) - len,
            "-L%s/lib -lSystem -lc ",
            ctx->config.sysroot[0] ? ctx->config.sysroot :
            "/Developer/SDKs/MacOSX10.4u.sdk/usr");

    /* AltiVec library if enabled */
    if (ctx->config.altivec) {
        len += snprintf(cmd + len, sizeof(cmd) - len,
                "-framework Accelerate ");
    }

    printf(";   $ %s\n", cmd);
}

/* ============================================================
 * MAIN BUILD DRIVER
 * ============================================================ */

void build_project(const char* project_dir) {
    BuildContext ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Default config for Tiger on G4 */
    strcpy(ctx.config.target, "powerpc-apple-darwin8");
    strcpy(ctx.config.opt_level, "3");
    strcpy(ctx.config.cpu, "7450");
    ctx.config.altivec = 1;
    ctx.config.debug_info = 0;
    strcpy(ctx.output_dir, "target/powerpc-apple-darwin8/release");

    /* Find Cargo.toml */
    char toml_path[512];
    snprintf(toml_path, sizeof(toml_path), "%s/Cargo.toml", project_dir);

    Crate* main_crate = &ctx.crates[ctx.crate_count++];
    if (!parse_cargo_toml(toml_path, main_crate)) {
        fprintf(stderr, "Error: Cannot read %s\n", toml_path);
        return;
    }

    strcpy(main_crate->path, project_dir);

    /* Find source files */
    char src_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src", project_dir);
    find_rust_files(src_dir, main_crate);

    printf("; =====================================================\n");
    printf("; Rust Build for Tiger/Leopard PowerPC\n");
    printf("; =====================================================\n");
    printf("; Project: %s v%s\n", main_crate->name, main_crate->version);
    printf("; Target: %s\n", ctx.config.target);
    printf("; CPU: %s, AltiVec: %s\n",
           ctx.config.cpu, ctx.config.altivec ? "yes" : "no");
    printf("; Sources: %d files\n", main_crate->source_count);
    printf("; Dependencies: %d\n", main_crate->dep_count);
    printf("; =====================================================\n\n");

    /* Resolve build order */
    int order[64];
    resolve_build_order(&ctx, order);

    /* Compile each crate */
    for (int i = 0; i < ctx.crate_count; i++) {
        compile_crate(&ctx, &ctx.crates[order[i]]);
    }

    /* Link if it's a binary */
    if (main_crate->is_bin || !main_crate->is_lib) {
        link_binary(&ctx, main_crate);
    }

    printf("\n; Build complete!\n");
}

/* ============================================================
 * TIGER-SPECIFIC TOOLCHAIN
 * ============================================================ */

void emit_tiger_toolchain() {
    printf("; Tiger/Leopard Rust Toolchain\n\n");

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
    printf(";   gcc -o myapp main.o -isysroot /Developer/SDKs/MacOSX10.4u.sdk\n");
}

/* ============================================================
 * MAKEFILE GENERATION
 * ============================================================ */

void generate_makefile(const char* project_name) {
    printf("# Makefile for %s (Tiger/Leopard PowerPC)\n\n", project_name);

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
    printf("LIBS = -lSystem -lc\n\n");

    printf("# Output\n");
    printf("BUILD_DIR = target/$(TARGET)/release\n");
    printf("BIN = %s\n\n", project_name);

    printf("# Source files\n");
    printf("SOURCES = $(wildcard src/*.rs)\n");
    printf("OBJECTS = $(patsubst src/%%.rs,$(BUILD_DIR)/%%.o,$(SOURCES))\n\n");

    printf("# Rules\n");
    printf("all: $(BUILD_DIR)/$(BIN)\n\n");

    printf("$(BUILD_DIR):\n");
    printf("\tmkdir -p $@\n\n");

    printf("$(BUILD_DIR)/%%.s: src/%%.rs | $(BUILD_DIR)\n");
    printf("\t$(RUSTC) $< -o $@ $(RUSTFLAGS)\n\n");

    printf("$(BUILD_DIR)/%%.o: $(BUILD_DIR)/%%.s\n");
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
        printf("Rust Build System for Tiger/Leopard PowerPC\n\n");
        printf("Usage:\n");
        printf("  %s build [path]     Build project\n", argv[0]);
        printf("  %s toolchain        Show toolchain info\n", argv[0]);
        printf("  %s makefile [name]  Generate Makefile\n", argv[0]);
        printf("  %s --demo           Run demonstration\n", argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "build") == 0) {
        build_project(argc > 2 ? argv[2] : ".");
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
        generate_makefile("firefox");
    }

    return 0;
}
