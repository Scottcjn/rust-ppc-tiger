/*
 * rust_runtime_stubs.c — Stub implementations for rusty-backup PPC link
 *
 * Provides the ~509 undefined symbols needed to link the transpiled
 * rusty-backup .o files on Mac OS X Tiger (PowerPC G4).
 *
 * Compiled with: gcc -std=c99 -O2 -c rust_runtime_stubs.c
 *
 * Categories:
 *  1. Global type instances (zero-init structs)
 *  2. Core runtime (vec, string, io, etc.)
 *  3. Domain-specific stubs (self_*, build_*, make_*, etc.)
 *  4. External crate stubs (serde, zstd, egui, etc.)
 *  5. Platform-specific stubs (Windows, macOS frameworks)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>

/* ============================================================
 * 1. GLOBAL TYPE INSTANCES
 * These are Rust types that the transpiler emits as global symbols.
 * Each is a zero-initialized 256-byte block (enough for any struct).
 * ============================================================ */

/* Helper: a "fat" zero block for any struct */
typedef struct { char _data[256]; } RustStub;

/* Standard library types */
RustStub Vec = {0};
RustStub String = {0};
RustStub HashMap = {0};
RustStub BTreeMap = {0};
RustStub BTreeSet = {0};
RustStub HashSet = {0};
RustStub VecDeque = {0};
RustStub Box = {0};
RustStub Arc = {0};
RustStub Path = {0};
RustStub PathBuf = {0};
RustStub File = {0};
RustStub BufReader = {0};
RustStub BufWriter = {0};
RustStub Cursor = {0};
RustStub Command = {0};
RustStub OpenOptions = {0};
RustStub CString = {0};
RustStub DefaultHasher = {0};
RustStub SystemTime = {0};
RustStub Layout = {0};
RustStub Default = {0};
RustStub StdCursor = {0};

/* Result/Option variants */
RustStub Ok = {0};
RustStub Err = {0};
RustStub Some = {0};
RustStub Self = {0};
RustStub First = {0};

/* Numeric type markers */
RustStub u16 = {0};
RustStub u32 = {0};
RustStub u64 = {0};
RustStub i64 = {0};

/* Domain-specific filesystem types */
RustStub BtrfsFilesystem = {0};
RustStub BtrfsKey = {0};
RustStub ExtFilesystem = {0};
RustStub FatFilesystem = {0};
RustStub HfsFilesystem = {0};
RustStub HfsPlusFilesystem = {0};
RustStub NtfsFilesystem = {0};
RustStub HfsMasterDirectoryBlock = {0};
RustStub HfsPlusVolumeHeader = {0};
RustStub HfsExtDescriptor = {0};
RustStub BTreeHeader = {0};
RustStub BTreeHeaderRecord = {0};
RustStub BTreeNodeDescriptor = {0};
RustStub ExtentDescriptor = {0};
RustStub FileEntry = {0};
RustStub FileInfo = {0};
RustStub ForkData = {0};
RustStub MbrPartitionEntry = {0};
RustStub ApmPartitionEntry = {0};
RustStub Apm = {0};
RustStub Gpt = {0};
RustStub Guid = {0};
RustStub Mbr = {0};
RustStub Local = {0};
RustStub BigEndian = {0};
RustStub PartitionTable = {0};
RustStub TempDir = {0};
RustStub MultiPartReader = {0};
RustStub PartcloneBlockReader = {0};
RustStub PartcloneCompactReader = {0};
RustStub PartcloneExpandReader = {0};
RustStub CompactStreamReader = {0};
RustStub BitmapReader = {0};
RustStub SectorAlignedWriter = {0};
RustStub CdReader = {0};
RustStub RetryConfig = {0};
RustStub RipProgress = {0};
RustStub ConvertProgress = {0};
RustStub UpdateConfig = {0};
RustStub ValidationResult = {0};
RustStub PreBuilt = {0};
RustStub LogPanel = {0};
RustStub CreateDirectoryOptions = {0};
RustStub CreateFileOptions = {0};

/* Windows types (stubs) */
RustStub HANDLE = {0};
RustStub PSID = {0};
RustStub RemovableMedia = {0};

/* Rust keyword/path globals */
RustStub std = {0};
/* 'crate' is reserved in some contexts, use asm alias */
int crate = 0;
int io = 0;
int fs = 0;
int pub = 0;
int cfg = 0;
int derive = 0;
int into = 0;
int super_ = 0;  /* 'super' is C++ keyword but fine in C */
int reserved = 0;
int allow = 0;
/* 'if' is C keyword — use asm symbol alias */

/* ============================================================
 * 2. CORE RUNTIME - Vec, IO, String, etc.
 * ============================================================ */

/* --- Vec --- */
typedef struct {
    void *ptr;
    int len;
    int cap;
    int elem_size;
} RustVec;

void *vec_new(void) {
    RustVec *v = (RustVec *)calloc(1, sizeof(RustVec));
    v->elem_size = sizeof(void*);
    return v;
}

void vec_push(void *vec, void *item) {
    RustVec *v = (RustVec *)vec;
    if (!v) return;
    if (v->len >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->ptr = realloc(v->ptr, v->cap * sizeof(void*));
    }
    ((void**)v->ptr)[v->len++] = item;
}

void vec_drop(void *vec) {
    RustVec *v = (RustVec *)vec;
    if (v) {
        free(v->ptr);
        free(v);
    }
}

void queue_push_back(void *queue, void *item) { vec_push(queue, item); }

/* --- IO Operations --- */

int reader_read_exact(void *reader, void *buf, int len) {
    int fd = reader ? *(int*)reader : -1;
    if (fd < 0) return -1;
    int total = 0;
    while (total < len) {
        int n = read(fd, (char*)buf + total, len - total);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

int reader_seek(void *reader, long offset) {
    int fd = reader ? *(int*)reader : -1;
    if (fd < 0) return -1;
    return lseek(fd, offset, SEEK_SET) >= 0 ? 0 : -1;
}

int reader_read_to_end(void *reader, void *buf) {
    (void)reader; (void)buf;
    return 0; /* stub */
}

/* Generic IO wrappers — all delegate to read/write/lseek on fd */
#define MAKE_READ_EXACT(name) \
    int name(void *obj, void *buf, int len) { return reader_read_exact(obj, buf, len); }
#define MAKE_SEEK(name) \
    int name(void *obj, long offset) { return reader_seek(obj, offset); }
#define MAKE_WRITE_ALL(name) \
    int name(void *obj, void *buf, int len) { \
        int fd = obj ? *(int*)obj : -1; \
        if (fd < 0) return -1; \
        int total = 0; \
        while (total < len) { \
            int n = write(fd, (char*)buf + total, len - total); \
            if (n <= 0) return -1; \
            total += n; \
        } \
        return 0; \
    }
#define MAKE_FLUSH(name) \
    int name(void *obj) { (void)obj; return 0; }

MAKE_WRITE_ALL(writer_write_all)
MAKE_FLUSH(writer_flush)
MAKE_SEEK(writer_seek)
void *writer_get_mut(void *writer) { return writer; }

MAKE_READ_EXACT(file_read_exact)
MAKE_SEEK(file_seek)
MAKE_WRITE_ALL(file_write_all)
MAKE_FLUSH(file_flush)

MAKE_READ_EXACT(cursor_read_exact)
MAKE_SEEK(cursor_seek)

MAKE_READ_EXACT(device_read_exact)
MAKE_SEEK(device_seek)
MAKE_WRITE_ALL(device_write_all)
MAKE_FLUSH(device_flush)

MAKE_READ_EXACT(source_read_exact)
MAKE_SEEK(source_seek)

MAKE_READ_EXACT(data_read_exact)
MAKE_READ_EXACT(r_read_exact)

MAKE_WRITE_ALL(w_write_all)
MAKE_FLUSH(w_flush)
int w_write_u8(void *w, unsigned char byte) {
    return writer_write_all(w, &byte, 1);
}

MAKE_WRITE_ALL(h_write)
MAKE_WRITE_ALL(out_write_all)
MAKE_FLUSH(out_flush)
MAKE_WRITE_ALL(bin_file_write_all)
MAKE_FLUSH(bin_file_flush)
MAKE_WRITE_ALL(pad_writer_write_all)
MAKE_FLUSH(pad_writer_flush)
MAKE_WRITE_ALL(temp_writer_write_all)
MAKE_FLUSH(temp_writer_flush)
MAKE_WRITE_ALL(tmp_write_all)
MAKE_FLUSH(tmp_flush)
MAKE_WRITE_ALL(target_write_all)
MAKE_FLUSH(target_flush)
MAKE_SEEK(target_seek)
MAKE_FLUSH(output_flush)
MAKE_WRITE_ALL(encoder_write_all)

int encoder_compress(void *enc, void *in, int len) { (void)enc; (void)in; (void)len; return 0; }
int encoder_finish(void *enc) { (void)enc; return 0; }
int inner_seek(void *inner, long offset) { return reader_seek(inner, offset); }
int format_write_header(void *fmt, void *hdr) { (void)fmt; (void)hdr; return 0; }
int header_write(void *hdr, void *w) { (void)hdr; (void)w; return 0; }

/* --- Map/Collection Operations --- */

void map_insert(void *map, void *key, void *val) { (void)map; (void)key; (void)val; }
void blocks_insert(void *blocks, int idx, void *block) { (void)blocks; (void)idx; (void)block; }
void blocks_into_iter(void *blocks) { (void)blocks; }
void blocks_extend(void *blocks, void *other) { (void)blocks; (void)other; }
void ebr_data_insert(void *data, int idx, void *item) { (void)data; (void)idx; (void)item; }
void directory_clusters_insert(void *dirs, int idx, void *item) { (void)dirs; (void)idx; (void)item; }
void existing_insert(void *existing, void *key, void *val) { (void)existing; (void)key; (void)val; }
void indirect_patches_insert(void *patches, void *key, void *val) { (void)patches; (void)key; (void)val; }
void metadata_blocks_set_insert(void *set, void *item) { (void)set; (void)item; }
void used_blocks_insert(void *set, void *item) { (void)set; (void)item; }
void used_blocks_remove(void *set, void *item) { (void)set; (void)item; }
void records_insert(void *records, int idx, void *item) { (void)records; (void)idx; (void)item; }
void relocations_insert(void *relocs, void *key, void *val) { (void)relocs; (void)key; (void)val; }
void visited_dirs_insert(void *dirs, void *item) { (void)dirs; (void)item; }
void old_to_new_insert(void *map, void *key, void *val) { (void)map; (void)key; (void)val; }
void methods_insert(void *methods, void *key, void *val) { (void)methods; (void)key; (void)val; }
void offsets_remove(void *offsets, int idx) { (void)offsets; (void)idx; }

/* --- Extend/Resize/Truncate Operations --- */

void data_extend(void *data, void *src, int len) { (void)data; (void)src; (void)len; }
void data_extend_from_slice(void *data, void *slice, int len) { (void)data; (void)slice; (void)len; }
void data_resize(void *data, int new_len) { (void)data; (void)new_len; }
void data_truncate(void *data, int new_len) { (void)data; (void)new_len; }
void bitmap_data_resize(void *bm, int len) { (void)bm; (void)len; }
void bitmap_data_truncate(void *bm, int len) { (void)bm; (void)len; }
void buf_extend_from_slice(void *buf, void *slice, int len) { (void)buf; (void)slice; (void)len; }
void buf_fill(void *buf, unsigned char val, int len) { if (buf) memset(buf, val, len); }
void chunk_resize(void *chunk, int len) { (void)chunk; (void)len; }
void target_resize(void *target, int len) { (void)target; (void)len; }
void bitmap_resize(void *bm, int len) { (void)bm; (void)len; }
void disk_resize(void *disk, int len) { (void)disk; (void)len; }
void fat_data_resize(void *fat, int len) { (void)fat; (void)len; }
void gdt_backup_resize(void *gdt, int len) { (void)gdt; (void)len; }
void gdt_buf_truncate(void *gdt, int len) { (void)gdt; (void)len; }
void gdt_padded_resize(void *gdt, int len) { (void)gdt; (void)len; }
void result_truncate(void *result, int len) { (void)result; (void)len; }
void name_chars_truncate(void *name, int len) { (void)name; (void)len; }
void errors_clear(void *errors) { (void)errors; }
void lfn_parts_clear(void *lfn) { (void)lfn; }

/* Extend-from-slice family */
#define MAKE_EXTEND(name) \
    void name(void *dst, void *slice, int len) { (void)dst; (void)slice; (void)len; }

MAKE_EXTEND(entries_extend_from_slice)
MAKE_EXTEND(bitmap_extend_from_slice)
MAKE_EXTEND(key_extend_from_slice)
MAKE_EXTEND(rec_extend_from_slice)
MAKE_EXTEND(rec1_extend_from_slice)
MAKE_EXTEND(rec2_extend_from_slice)
MAKE_EXTEND(index_record_extend_from_slice)
MAKE_EXTEND(key_record_extend_from_slice)
MAKE_EXTEND(thread_record_extend_from_slice)
MAKE_EXTEND(actual_thread_record_extend_from_slice)
MAKE_EXTEND(all_extend_from_slice)
MAKE_EXTEND(all_blocks_extend)
MAKE_EXTEND(built_image_extend_from_slice)
MAKE_EXTEND(output_extend_from_slice)
MAKE_EXTEND(extent_blocks_extend)
MAKE_EXTEND(out_extend)
MAKE_EXTEND(out_extend_from_slice)
MAKE_EXTEND(block_copy_from_slice)
MAKE_EXTEND(bytes_copy_from_slice)

void record_copy_within(void *rec, int from, int to, int len) {
    (void)rec; (void)from; (void)to; (void)len;
}
void indx_copy_within(void *indx, int from, int to, int len) {
    (void)indx; (void)from; (void)to; (void)len;
}
void source_data_fill(void *src, unsigned char val, int len) { (void)src; (void)val; (void)len; }
void cache_fill_to(void *cache, int new_len) { (void)cache; (void)new_len; }
void image_append(void *img, void *data, int len) { (void)img; (void)data; (void)len; }

/* Push string operations */
void out_push_str(void *out, const char *s) { (void)out; (void)s; }
void hex_text_push_str(void *hex, const char *s) { (void)hex; (void)s; }
void result_push_str(void *result, const char *s) { (void)result; (void)s; }

/* --- Sort Operations (all no-ops for initial link) --- */

void entries_sort_by(void *entries, void *cmp) { (void)entries; (void)cmp; }
void files_sort_by(void *files, void *cmp) { (void)files; (void)cmp; }
void devices_sort_by(void *devices, void *cmp) { (void)devices; (void)cmp; }
void partitions_sort_by(void *parts, void *cmp) { (void)parts; (void)cmp; }
void leaf_nodes_sort_by(void *nodes, void *cmp) { (void)nodes; (void)cmp; }
void chunk_map_sort_by_key(void *map) { (void)map; }
void merged_sort_by_key(void *merged) { (void)merged; }
void phys_ranges_sort_by_key(void *ranges) { (void)ranges; }
void all_blocks_sort_by_key(void *blocks) { (void)blocks; }
void sorted_sort_by_key(void *sorted) { (void)sorted; }
void sorted_parts_sort_by_key(void *parts) { (void)parts; }
void backward_plans_sort_by_key(void *plans) { (void)plans; }
void logical_pms_sort_by_key(void *pms) { (void)pms; }
int b_is_dir_cmp(void *a, void *b) { (void)a; (void)b; return 0; }
int upper_a_cmp(void *a, void *b) { (void)a; (void)b; return 0; }

/* ============================================================
 * 3. RUNTIME FUNCTIONS
 * ============================================================ */

void rust_assert(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "ASSERTION FAILED: %s\n", msg ? msg : "(unknown)");
        abort();
    }
}

void rust_println(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[INFO] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[WARN] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void log_cb(const char *msg) {
    fprintf(stderr, "[CB] %s\n", msg ? msg : "");
}

/* 'log' conflicts with C math log() - this is Rust's log!() macro */
void log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[LOG] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void drop(void *ptr) { free(ptr); }

void *alloc_arc(int size) {
    /* Simple ref-counted block: [refcount:4][data:size] */
    void *p = calloc(1, size + 4);
    if (p) *(int*)p = 1; /* refcount = 1 */
    return p;
}

void progress_lock(void) { /* no-op mutex stub */ }
void progress_cb(int current, int total) { (void)current; (void)total; }

long long now_as_nanos(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000000LL + (long long)tv.tv_usec * 1000LL;
}

void vr_error(void *vr, const char *msg) {
    (void)vr;
    fprintf(stderr, "[VALIDATION ERROR] %s\n", msg ? msg : "");
}

void vr_warn(void *vr, const char *msg) {
    (void)vr;
    fprintf(stderr, "[VALIDATION WARN] %s\n", msg ? msg : "");
}

void new_status(void *status, const char *msg) { (void)status; (void)msg; }

/* ============================================================
 * 4. FILESYSTEM OPERATIONS
 * ============================================================ */

void *fs_create_file(const char *path) {
    if (!path) return NULL;
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return NULL;
    int *p = (int*)malloc(sizeof(int));
    *p = fd;
    return p;
}

void *fs_create_directory(const char *path) {
    if (!path) return NULL;
    mkdir(path, 0755);
    return (void*)path;
}

int fs_delete_entry(const char *path) {
    if (!path) return -1;
    if (unlink(path) == 0) return 0;
    return rmdir(path);
}

int fs_delete_recursive(const char *path) {
    /* Simple recursive delete */
    struct stat st;
    if (!path || stat(path, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *ent;
        char child[1024];
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            fs_delete_recursive(child);
        }
        closedir(d);
        return rmdir(path);
    }
    return unlink(path);
}

int fs_set_permissions(const char *path, int mode) {
    if (!path) return -1;
    return chmod(path, (mode_t)mode);
}

void perms_set_mode(void *perms, int mode) { (void)perms; (void)mode; }

static char _join_buf[2048];

void *folder_join(const char *base, const char *child) {
    snprintf(_join_buf, sizeof(_join_buf), "%s/%s", base ? base : "", child ? child : "");
    return _join_buf;
}

void *parent_join(const char *base, const char *child) {
    return folder_join(base, child);
}

const char *path_file_name(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

const char *path_strip_prefix(const char *path, const char *prefix) {
    if (!path || !prefix) return path;
    int plen = strlen(prefix);
    if (strncmp(path, prefix, plen) == 0) {
        if (path[plen] == '/') return path + plen + 1;
        if (path[plen] == '\0') return "";
    }
    return path;
}

int name_eq_ignore_ascii_case(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

int name_starts_with(const char *name, const char *prefix) {
    if (!name || !prefix) return 0;
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

const char *name_trimmed_to_string(const char *name) { return name; }
void *name_chars(const char *name) { return (void*)name; }

int lower_contains(const char *haystack, const char *needle) {
    (void)haystack; (void)needle;
    return 0; /* simplified */
}

const char *result_chars(void *result) { return result ? (const char*)result : ""; }
int chars_next(void *chars) { (void)chars; return 0; }
void *create_iter(void *collection) { return collection; }
void *lines_join(void *lines, const char *sep) { (void)lines; (void)sep; return (void*)""; }

/* ============================================================
 * 5. BITMAP OPERATIONS
 * ============================================================ */

void *bitmap(int size) {
    int bytes = (size + 7) / 8;
    return calloc(1, bytes);
}

int bitmap_set_bit(void *bm, int bit) {
    if (!bm) return -1;
    ((unsigned char*)bm)[bit / 8] |= (1 << (bit % 8));
    return 0;
}

int bitmap_clear_bit(void *bm, int bit) {
    if (!bm) return -1;
    ((unsigned char*)bm)[bit / 8] &= ~(1 << (bit % 8));
    return 0;
}

int bitmap_set_bit_be(void *bm, int bit) {
    if (!bm) return -1;
    ((unsigned char*)bm)[bit / 8] |= (0x80 >> (bit % 8));
    return 0;
}

int bitmap_clear_bit_be(void *bm, int bit) {
    if (!bm) return -1;
    ((unsigned char*)bm)[bit / 8] &= ~(0x80 >> (bit % 8));
    return 0;
}

int bitmap_test_bit_be(void *bm, int bit) {
    if (!bm) return 0;
    return (((unsigned char*)bm)[bit / 8] & (0x80 >> (bit % 8))) ? 1 : 0;
}

int bitmap_find_clear_run_be(void *bm, int count) {
    (void)bm; (void)count;
    return -1; /* not found */
}

int bitmap_find_set_bit_be(void *bm, int start) {
    (void)bm; (void)start;
    return -1; /* not found */
}

/* ============================================================
 * 6. DOMAIN-SPECIFIC STUBS (self_* methods)
 * All return 0 (success) or do nothing.
 * ============================================================ */

#define STUB_VOID_1(name) void name(void *self) { (void)self; }
#define STUB_VOID_2(name) void name(void *self, void *a) { (void)self; (void)a; }
#define STUB_VOID_3(name) void name(void *self, void *a, void *b) { (void)self; (void)a; (void)b; }
#define STUB_INT_1(name) int name(void *self) { (void)self; return 0; }
#define STUB_INT_2(name) int name(void *self, void *a) { (void)self; (void)a; return 0; }
#define STUB_INT_3(name) int name(void *self, void *a, void *b) { (void)self; (void)a; (void)b; return 0; }
#define STUB_PTR_1(name) void *name(void *self) { (void)self; return NULL; }
#define STUB_PTR_2(name) void *name(void *self, void *a) { (void)self; (void)a; return NULL; }

/* Filesystem self_* methods */
STUB_INT_2(self_read_block)
STUB_INT_2(self_write_block)
STUB_INT_2(self_read_inode_data)
STUB_INT_2(self_write_inode_raw)
STUB_INT_2(self_write_superblock)
STUB_INT_2(self_write_group_descriptor)
STUB_INT_2(self_patch_inode_block_field)
STUB_INT_2(self_add_block_to_inode)
STUB_INT_2(self_add_block_to_extent_inode)
STUB_INT_2(self_add_dir_entry)
STUB_INT_2(self_remove_dir_entry)
STUB_INT_2(self_free_inode)
STUB_INT_1(self_free_blocks_list)
STUB_INT_1(self_inode_data_blocks)
STUB_INT_2(self_update_inode_links)
STUB_INT_2(self_update_inode_size)
STUB_INT_2(self_read_extent_tree)
STUB_INT_2(self_read_extent_node)
STUB_INT_2(self_collect_items_in_range)
STUB_INT_2(self_find_item_ge)

/* HFS/HFS+ methods */
STUB_INT_2(self_find_catalog_record)
STUB_INT_2(self_find_catalog_record_by_name)
STUB_INT_2(self_find_file_by_id)
STUB_INT_2(self_insert_catalog_record)
STUB_INT_2(self_remove_catalog_record)
STUB_INT_2(self_update_parent_valence)
STUB_INT_1(self_write_catalog)
STUB_INT_1(self_write_mdb)
STUB_INT_1(self_write_volume_bitmap)
STUB_INT_1(self_write_allocation_bitmap)
STUB_INT_1(self_write_volume_header)
STUB_INT_1(self_write_volume_header_to_disk)
STUB_INT_1(self_do_sync_metadata)
STUB_INT_1(self_ensure_bitmap)
STUB_INT_2(self_free_extent_blocks)
STUB_INT_2(self_free_fork_blocks)
STUB_INT_2(self_free_blocks)
STUB_PTR_2(self_lookup_folder_name)

/* FAT methods */
STUB_INT_2(self_load_cluster)
STUB_INT_2(self_write_fat_entry)
STUB_INT_2(self_write_fat_entry_disk)
STUB_INT_1(self_update_fsinfo)
STUB_INT_1(self_sector_offset)
STUB_INT_2(self_add_to_directory)
STUB_INT_2(self_add_entry_to_directory)
STUB_INT_2(self_remove_entry_from_directory)
STUB_INT_2(self_parse_directory)
STUB_INT_2(self_read_cluster_chain)
STUB_INT_1(self_recalculate_boot_checksum)
STUB_INT_1(self_sync_metadata)
STUB_INT_2(self_write_cluster_data)
STUB_INT_2(self_write_bitmap)
STUB_INT_2(self_free_cluster_chain_rw)

/* NTFS methods */
STUB_INT_2(self_free_mft_record)
STUB_INT_2(self_free_volume_clusters)
STUB_INT_2(self_insert_index_entry)
STUB_INT_2(self_remove_index_entry)
STUB_INT_2(self_list_directory_entries)
STUB_INT_2(self_parse_index_allocation_entries)
STUB_INT_2(self_parse_index_entry_list)
STUB_INT_2(self_parse_index_root_entries)
STUB_INT_2(self_update_resident_attr_value)
STUB_INT_2(self_write_data_to_runs)
STUB_INT_1(self_write_mft_bitmap)
STUB_INT_1(self_write_mft_record)

/* Btrfs methods */
STUB_INT_1(self_read_chunk_tree)
STUB_INT_2(self_read_file_data)
STUB_INT_2(self_decompress_to_block)
STUB_INT_2(self_load_current_block)

/* IO/cache methods */
STUB_INT_2(self_ensure_cached)
STUB_INT_2(self_ensure_hunk)
STUB_INT_1(self_flush_padded)
STUB_INT_1(self_flush_sectors)

/* Dir methods */
STUB_INT_2(self_clear_dir_entry)
STUB_INT_2(self_write_dir_entry)
STUB_INT_1(self_free_dir_blocks)
STUB_INT_1(self_free_file_blocks)
STUB_INT_1(self_free_block)
STUB_INT_1(self_update_dir_file_count)
STUB_INT_1(self_write_bitmap_to_disk)

/* GUI/App methods (all stubs since no GUI on Tiger) */
STUB_INT_2(self_delete_entry)
STUB_INT_2(self_delete_recursive)
STUB_VOID_1(self_close)
STUB_VOID_2(self_add)
STUB_VOID_2(self_add_file_dialog)
STUB_VOID_2(self_add_host_file)
STUB_VOID_2(self_add_host_paths)
STUB_VOID_1(self_clear_results)
STUB_VOID_1(self_create_new_folder)
STUB_VOID_1(self_handle_dropped_files)
STUB_VOID_2(self_invalidate_cache_for)
STUB_VOID_2(self_load_directory)
STUB_VOID_1(self_perform_delete)
STUB_VOID_1(self_poll_extraction)
STUB_VOID_1(self_poll_progress)
STUB_VOID_2(self_start_archive_compress)
STUB_VOID_2(self_start_rip_to_chd)
STUB_VOID_2(self_update_backup_name)
STUB_VOID_2(self_open_browse_clonezilla)
STUB_VOID_2(self_load_clonezilla_backup_metadata)
STUB_VOID_2(self_load_native_backup_metadata)

/* ============================================================
 * 7. BUILD/MAKE/PARSE/PATCH/RESIZE/EXPORT STUBS
 * ============================================================ */

/* Build functions */
void *build_appledouble(void *a) { (void)a; return NULL; }
void *build_macbinary(void *a) { (void)a; return NULL; }
void *build_minimal_mbr(void) { return calloc(1, 512); }
void *build_file_entry_bytes(void *a) { (void)a; return NULL; }
void *build_subdir_entry_bytes(void *a) { (void)a; return NULL; }
void *build_subdir_header_bytes(void *a) { (void)a; return NULL; }
void *build_twomg_header(void *a) { (void)a; return NULL; }
void *build_vhd_footer(void *a) { (void)a; return NULL; }
void *build_ebr_chain(void *a) { (void)a; return NULL; }

/* Make functions */
void *make_disk_image(void *a) { (void)a; return NULL; }
void *make_exfat_vbr(void *a) { (void)a; return NULL; }
void *make_exfat_vbr_custom(void *a) { (void)a; return NULL; }
void *make_header(void *a) { (void)a; return NULL; }
void *make_mbr_bytes(void *a) { (void)a; return NULL; }
void *make_mbr_with_chs(void *a) { (void)a; return NULL; }
void *make_ntfs_vbr(void *a) { (void)a; return NULL; }
void *make_volume_key_block(void *a) { (void)a; return NULL; }

/* Parse functions */
int parse_exfat_vbr(void *a) { (void)a; return 0; }
int parse_koly(void *a) { (void)a; return 0; }
int parse_vbr(void *a) { (void)a; return 0; }

/* Patch functions */
int patch_bpb_hidden_sectors(void *a, void *b) { (void)a; (void)b; return 0; }
int patch_bpb_total_sectors(void *a, void *b) { (void)a; (void)b; return 0; }
int patch_exfat_hidden_sectors(void *a, void *b) { (void)a; (void)b; return 0; }
int patch_hfs_hidden_sectors(void *a, void *b) { (void)a; (void)b; return 0; }
int patch_hfsplus_hidden_sectors(void *a, void *b) { (void)a; (void)b; return 0; }
int patch_mbr_entries(void *a, void *b) { (void)a; (void)b; return 0; }
int patch_ntfs_hidden_sectors(void *a, void *b) { (void)a; (void)b; return 0; }

/* Resize functions */
int resize_btrfs_in_place(void *a) { (void)a; return 0; }
int resize_exfat_in_place(void *a) { (void)a; return 0; }
int resize_ext_in_place(void *a) { (void)a; return 0; }
int resize_fat_in_place(void *a) { (void)a; return 0; }
int resize_hfs_in_place(void *a) { (void)a; return 0; }
int resize_hfsplus_in_place(void *a) { (void)a; return 0; }
int resize_ntfs_in_place(void *a) { (void)a; return 0; }
int apply_resize(void *a) { (void)a; return 0; }

/* Export functions */
int export_mbr(void *a) { (void)a; return 0; }
int export_mbr_min(void *a) { (void)a; return 0; }
int export_whole_disk_vhd(void *a) { (void)a; return 0; }

/* Detect functions */
int detect_vhd(void *a) { (void)a; return 0; }
int detect_partition_fs_type(void *a) { (void)a; return 0; }

/* ============================================================
 * 8. MISC DOMAIN FUNCTIONS
 * ============================================================ */

int lfn_checksum(void *a) { (void)a; return 0; }
int encode_fourcc(void *a) { (void)a; return 0; }
void *mac_roman_to_utf8(void *a) { (void)a; return (void*)""; }
int mark_extents(void *a) { (void)a; return 0; }
int node_fill(void *a) { (void)a; return 0; }
int btree_free_node(void *a) { (void)a; return 0; }
int btree_remove_record(void *a) { (void)a; return 0; }
void *btrfs_chunk(void *a) { (void)a; return NULL; }
int by_group_entry(void *a) { (void)a; return 0; }
int bs_max(int a, int b) { return a > b ? a : b; }
int count_min(int a, int b) { return a < b ? a : b; }
int eof_min(int a, int b) { return a < b ? a : b; }
int io_width(void *a) { (void)a; return 80; }
void *generation(void *a) { (void)a; return NULL; }
void *reverse_get(void *a) { (void)a; return NULL; }
void *key_extract_fn(void *a) { (void)a; return NULL; }
int unix_file_type(void *a) { (void)a; return 0; }
void *unix_entry_from_inode(void *a) { (void)a; return NULL; }
int sanitized_split(void *a) { (void)a; return 0; }
int get_export_size(void *a) { (void)a; return 0; }
void *output_path(void *a) { (void)a; return (void*)"/tmp/output"; }
void *ext_to_partition(void *a) { (void)a; return NULL; }
int open_disc_filesystem(void *a) { (void)a; return 0; }
int reconstruct_disk_from_backup(void *a) { (void)a; return 0; }
int stream_with_split(void *a) { (void)a; return 0; }
int write_zeros(void *a, int len) { (void)a; (void)len; return 0; }
void *image_head_v2(void *a) { (void)a; return NULL; }
void *read_fork_data(void *a) { (void)a; return NULL; }
void *read_index_block(void *a) { (void)a; return NULL; }
void *hunk_read_hunk_in(void *a) { (void)a; return NULL; }
int data_fork_serialize(void *a) { (void)a; return 0; }
int fork_serialize(void *a) { (void)a; return 0; }
int new_rsrc_serialize(void *a) { (void)a; return 0; }
int rsrc_fork_serialize(void *a) { (void)a; return 0; }
int write_bpb(void *a) { (void)a; return 0; }
int write_hfs_fork_data(void *a) { (void)a; return 0; }
int decompress_to_writer(void *a) { (void)a; return 0; }
int serializer_serialize_str(void *a, void *b) { (void)a; (void)b; return 0; }
void *fs_read_chunk_tree(void *a) { (void)a; return NULL; }
void *temp_read_chunk_tree(void *a) { (void)a; return NULL; }
int fs_scan_root_directory_metadata(void *a) { (void)a; return 0; }
int fs_set_blessed_folder(void *a) { (void)a; return 0; }
int fs_last_data_byte(void *a) { (void)a; return 0; }
int fs_free_block(void *a) { (void)a; return 0; }
void *hfs_common(void *a) { (void)a; return NULL; }
int s_encode_utf16(void *a) { (void)a; return 0; }

/* CRC32C - simple implementation */
unsigned int crc32c(unsigned int crc, const void *buf, int len) {
    const unsigned char *p = (const unsigned char *)buf;
    crc = ~crc;
    while (len-- > 0) {
        crc ^= *p++;
        int i;
        for (i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1));
        }
    }
    return ~crc;
}

/* ============================================================
 * 9. GUI/egui STUBS (all no-ops)
 * ============================================================ */

int egui = 0;

void ui_add(void *ui, void *widget) { (void)ui; (void)widget; }
void ui_add_enabled_ui(void *ui, int enabled) { (void)ui; (void)enabled; }
void ui_add_space(void *ui, int space) { (void)ui; (void)space; }
void ui_end_row(void *ui) { (void)ui; }
void ui_heading(void *ui, const char *text) { (void)ui; (void)text; }
void ui_horizontal(void *ui, void *cb) { (void)ui; (void)cb; }
void ui_label(void *ui, const char *text) { (void)ui; (void)text; }
void ui_radio_value(void *ui, void *val, int v, const char *text) { (void)ui; (void)val; (void)v; (void)text; }
void ui_separator(void *ui) { (void)ui; }
void ui_vertical_centered(void *ui, void *cb) { (void)ui; (void)cb; }
void ui_checkbox(void *ui, void *val, const char *text) { (void)ui; (void)val; (void)text; }
void ui_colored_label(void *ui, void *color, const char *text) { (void)ui; (void)color; (void)text; }
void ui_group(void *ui, void *title, void *cb) { (void)ui; (void)title; (void)cb; }
void ui_spinner(void *ui) { (void)ui; }
void ui_with_layout(void *ui, void *layout, void *cb) { (void)ui; (void)layout; (void)cb; }
int ctx_copy_text(void *ctx, const char *text) { (void)ctx; (void)text; return 0; }
int btn_on_hover_text(void *btn, const char *text) { (void)btn; (void)text; return 0; }

/* ============================================================
 * 10. EXTERNAL CRATE STUBS
 * ============================================================ */

int serde_json = 0;
int serde = 0;
int zstd = 0;
int zeekstd = 0;
int reqwest = 0;
int chd = 0;
int tempfile = 0;
int opticaldiscs = 0;
int clonezilla = 0;
int rbformats = 0;
int rusty_backup = 0;
int config_dir_map = 0;

/* ============================================================
 * 11. WINDOWS API STUBS
 * ============================================================ */

int CreateFileW(void *a, int b, int c, void *d, int e, int f, void *g) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; return -1;
}
int DeviceIoControl(void *a, int b, void *c, int d, void *e, int f, void *g, void *h) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; return 0;
}
int ShellExecuteW(void *a, void *b, void *c, void *d, void *e, int f) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return 0;
}
int CheckTokenMembership(void *a, void *b, void *c) { (void)a;(void)b;(void)c; return 0; }

/* ============================================================
 * 12. macOS FRAMEWORK STUBS (IOKit, DiskArbitration, CoreFoundation)
 * ============================================================ */

void *CFRunLoop = NULL;
void *CFString = NULL;

void *IOServiceMatching(const char *name) { (void)name; return NULL; }
int IOServiceGetMatchingServices(void *master, void *matching, void *iter) {
    (void)master;(void)matching;(void)iter; return -1;
}
void *IOIteratorNext(void *iter) { (void)iter; return NULL; }
void IOObjectRelease(void *obj) { (void)obj; }
int IORegistryEntryCreateCFProperties(void *entry, void *props, void *alloc, int opts) {
    (void)entry;(void)props;(void)alloc;(void)opts; return -1;
}
long long cf_num_as_i64(void *num) { (void)num; return 0; }
const char *cf_url_to_file_path(void *url) { (void)url; return ""; }
int disk_unmount(void *disk) { (void)disk; return 0; }
void rl_stop(void *rl) { (void)rl; }
void session_schedule_with_run_loop(void *session, void *rl) { (void)session; (void)rl; }
void *DADisk = NULL;

/* ============================================================
 * 13. LAZY STATIC / ONCE STUBS
 * ============================================================ */

void *MAP_get_or_init(void) { return NULL; }
void *HFS_TYPES_get_or_init(void) { return NULL; }

/* ============================================================
 * 14. MISC SYMBOLS THAT ARE RUST KEYWORDS USED AS GLOBALS
 * ============================================================ */

/* Some of these are Rust path segments that the transpiler
 * incorrectly emits as symbols. They're just zero globals. */
int env = 0;
int ptr = 0;
int rfd = 0;
int device = 0;
int convert = 0;
int us = 0;

/* command_extend is likely Command::args extension */
void command_extend(void *cmd, void *args) { (void)cmd; (void)args; }

/* Missing efs_* filesystem operations */
int efs_create_directory(void *a) { (void)a; return 0; }
int efs_create_file(void *a) { (void)a; return 0; }
int efs_delete_entry(void *a) { (void)a; return 0; }
int efs_delete_recursive(void *a) { (void)a; return 0; }
int efs_sync_metadata(void *a) { (void)a; return 0; }

/* Device enumeration */
void *enumerate_devices(void) { return NULL; }

/* Threading stub */
void *thread(void *func) { (void)func; return NULL; }

/* ============================================================
 * ASM ALIASES for C reserved words
 * The transpiler may emit _if, _super as symbols.
 * We use inline asm to define these.
 * ============================================================ */

/* For 'if' — can't use as C identifier, so we use a C-compatible name
 * and export it via the linker. We'll handle this with a separate .s file. */

/* For 'super' — same approach */

/* These will be in rust_reserved.s */

/* ============================================================
 * 15. MAIN ENTRY POINT (if needed)
 * The transpiler generates _main in the cli.o, but just in case:
 * ============================================================ */

/* Entry not provided here — cli.o has main() */
