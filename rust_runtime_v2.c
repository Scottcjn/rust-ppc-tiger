/* rust_runtime_v2.c — Stubs for sanitized path symbols */
/* Compiled: gcc -std=c99 -O2 -c rust_runtime_v2.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dirent.h>

/* Forward declarations */
unsigned short u16_from_be_bytes(void* b);
unsigned short u16_from_le_bytes(void* b);
unsigned int u32_from_be_bytes(void* b);
unsigned int u32_from_le_bytes(void* b);
unsigned int u32_from_ne_bytes(void* b);
unsigned long long u64_from_be_bytes(void* b);
unsigned long long u64_from_le_bytes(void* b);
long long i64_from_ne_bytes(void* b);
void* File_open(const char* path);
void* File_create(const char* path);
void* OpenOptions_new(void);
void* SystemTime_now(void);
void* HashMap_new(void);
void* HashSet_new(void);
void* BTreeSet_new(void);
void* Vec_with_capacity(int cap);
extern unsigned int crc32c(unsigned int, const void*, int);

void* ApmPartitionEntry_parse(void) { return NULL; }
void* Arc_clone(void* a) { return a; }
void* BTreeHeader_read(void) { return NULL; }
void* BTreeMap_new(void) { return calloc(1,64); }
void* BTreeSet_new(void) { return calloc(1,64); }
short BigEndian_read_i16(void* b) { return (short)u16_from_be_bytes(b); }
unsigned short BigEndian_read_u16(void* b) { return u16_from_be_bytes(b); }
unsigned int BigEndian_read_u32(void* b) { return u32_from_be_bytes(b); }
void* BitmapReader_new(void) { return NULL; }
void* Box_into_raw(void* b) { return b; }
void* BufReader_new(void* i) { return i; }
void* BufWriter_new(void* i) { return i; }
void* CFRunLoop_current(void) { return NULL; }
void* CFString_from_static_str(void) { return NULL; }
void* CFString_from_str(void) { return NULL; }
void* CString_new(const char* s) { if(!s)return NULL; char* p=(char*)malloc(strlen(s)+2); strcpy(p,s); return p; }
void* CdReader_open(void) { return NULL; }
void* Command_new(const char* p) { (void)p; return calloc(1,32); }
void* CompactStreamReader_new(void) { return NULL; }
void* CreateDirectoryOptions_default(void) { return NULL; }
void* CreateFileOptions_default(void) { return NULL; }
void* Cursor_new(void* d) { return d; }
void* DADisk_from_bsd_name(void) { return NULL; }
void* DefaultHasher_new(void) { return calloc(1,16); }
void* Default_default(void) { return NULL; }
void* FileEntry_new_directory(void) { return NULL; }
void* FileEntry_new_file(void) { return NULL; }
void* FileEntry_root(void) { return NULL; }
void* File_create(const char* p) { if(!p)return NULL; int fd=open(p,O_CREAT|O_WRONLY|O_TRUNC,0644); if(fd<0)return NULL; int* r=(int*)malloc(sizeof(int)); *r=fd; return r; }
void* File_open(const char* p) { if(!p)return NULL; int fd=open(p,O_RDONLY); if(fd<0)return NULL; int* r=(int*)malloc(sizeof(int)); *r=fd; return r; }
void* Gpt_build_protective_mbr(void) { return NULL; }
void* Gpt_compute_header_crc32(void) { return NULL; }
void* Gpt_parse_header(void) { return NULL; }
void* Gpt_parse_one_entry(void) { return NULL; }
void* HashMap_new(void) { return calloc(1,64); }
void* HashMap_with_capacity(int cap) { (void)cap; return calloc(1,64); }
void* HashSet_new(void) { return calloc(1,64); }
void* HfsExtDescriptor_parse(void) { return NULL; }
void* Layout_from_size_align(int s, int a) { (void)s;(void)a; return calloc(1,16); }
void* LogPanel_default(void) { return NULL; }
void* MbrPartitionEntry_parse(void) { return NULL; }
void* OpenOptions_new(void) { return calloc(1,16); }
void* PartcloneBlockReader_new(void) { return NULL; }
void* PartitionTable_Mbr(void) { return NULL; }
void* PartitionTable_detect(void) { return NULL; }
void* PathBuf_from(const char* s) { if(!s)return NULL; char* p=(char*)malloc(strlen(s)+1); strcpy(p,s); return p; }
void* Path_new(const char* s) { return (void*)s; }
void* RetryConfig_default(void) { return NULL; }
void* SectorAlignedWriter_new(void) { return NULL; }
void* Self_build_catalog_key(void) { return NULL; }
void* Self_build_dir_entries(void) { return NULL; }
void* Self_build_dir_record(void) { return NULL; }
void* Self_build_entry_set(void) { return NULL; }
void* Self_build_file_record(void) { return NULL; }
void* Self_build_folder_record(void) { return NULL; }
void* Self_entry_set_checksum(void) { return NULL; }
void* Self_generate_short_name(void) { return NULL; }
void* Self_name_hash(void) { return NULL; }
void* Self_parse_entries(void) { return NULL; }
void* Self_parse_header(void) { return NULL; }
void* Self_parse_one_entry(void) { return NULL; }
void* Self_parse_total_blocks(void) { return NULL; }
void* Self_upcase_char(void) { return NULL; }
void* StdCursor_new(void* d) { return d; }
void* String_deserialize(void* de) { (void)de; return calloc(1,16); }
void* String_from_utf16_lossy(void* b) { return b; }
void* String_from_utf8_lossy(void* b) { return b; }
void* String_new(void) { return calloc(1,16); }
void* String_with_capacity(int cap) { return Vec_with_capacity(cap); }
void* SystemTime_now(void) { struct timeval* tv=(struct timeval*)malloc(sizeof(struct timeval)); gettimeofday(tv,NULL); return tv; }
void* UpdateConfig_load(void) { return NULL; }
void* ValidationResult_default(void) { return NULL; }
void* VecDeque_new(void) { return calloc(1,32); }
void* Vec_new(void) { return calloc(1,16); }
void* Vec_with_capacity(int cap) { void* v=calloc(1,16); if(v&&cap>0){((void**)v)[0]=malloc(cap*sizeof(void*));((int*)v)[2]=0;((int*)v)[3]=cap;} return v; }
void* chd_Chd_open(void) { return NULL; }
void* clonezilla_metadata_load(void) { return NULL; }
void* convert_to_chd(void) { return NULL; }
void* crate_fs_hfs_HfsFilesystem_open(void) { return NULL; }
void* crate_os_open_source_for_reading(void) { return NULL; }
void* crate_partition_format_size(void) { return NULL; }
unsigned int crc32c_crc32c(unsigned int c, void* b, int l) { return crc32c(c,b,l); }
void* device_enumerate_devices(void) { return NULL; }
void* egui_ProgressBar_new(void) { return NULL; }
/* env_args provided by rust_cli_real.c */
void* env_current_exe(void) { return NULL; }
void* fs_File_create(const char* p) { return File_create(p); }
void* fs_OpenOptions_new(void) { return OpenOptions_new(); }
void* fs_metadata(const char* p) { (void)p; return calloc(1,64); }
void* fs_read_to_string(const char* p) { (void)p; return calloc(1,16); }
void* hfs_common_encode_fourcc(void) { return NULL; }
void* hfs_common_hfs_now(void) { return NULL; }
long long i64_from_ne_bytes(void* b) { long long v; memcpy(&v,b,8); return v; }
void* io_BufReader_new(void* i) { return i; }
void* io_BufWriter_new(void* i) { return i; }
void* opticaldiscs_BinCueSectorReader_open(void) { return NULL; }
void* opticaldiscs_DiscFormat_from_path(void) { return NULL; }
void* opticaldiscs_bincue_parse_cue_tracks(void) { return NULL; }
void* ptr_null_mut(void) { return NULL; }
void* rbformats_detect_image_format(void) { return NULL; }
void* reqwest_blocking_Client_builder(void) { return NULL; }
void* rfd_FileDialog_new(void) { return NULL; }
void* rusty_backup_rbformats_compress_file_to_archive(void) { return NULL; }
void* rusty_backup_rbformats_decompress_partition_to_file(void) { return NULL; }
void* rusty_backup_rbformats_export_export_clonezilla_disk(void) { return NULL; }
void* serde_json_from_reader(void) { return NULL; }
void* serde_json_from_str(void) { return NULL; }
void* serde_json_to_string(void) { return NULL; }
void* serde_json_to_string_pretty(void) { return NULL; }
void* std_array_from_fn(void* f) { (void)f; return calloc(1,64); }
void* std_collections_BTreeSet_new(void) { return BTreeSet_new(); }
void* std_collections_HashMap_new(void) { return HashMap_new(); }
void* std_collections_HashSet_new(void) { return HashSet_new(); }
/* std_env_args provided by rust_cli_real.c */
void* std_env_current_exe(void) { return NULL; }
void* std_env_temp_dir(void) { static char t[]="/tmp"; return t; }
void* std_env_var(const char* n) { return (void*)getenv(n?n:""); }
void* std_fs_File_create(const char* p) { return File_create(p); }
void* std_fs_File_open(const char* p) { return File_open(p); }
void* std_fs_OpenOptions_new(void) { return OpenOptions_new(); }
void* std_fs_metadata(const char* p) { (void)p; return calloc(1,64); }
void* std_fs_read(const char* p) { (void)p; return calloc(1,16); }
void* std_fs_read_dir(const char* p) { (void)p; return NULL; }
void* std_fs_read_to_string(const char* p) { (void)p; return calloc(1,16); }
void* std_io_BufReader_new(void* i) { return i; }
void* std_io_Cursor_new(void* d) { return d; }
void* std_io_Error_last_os_error(void) { return NULL; }
int std_io_Seek_seek(void* o, long off) { (void)o;(void)off; return 0; }
void* std_mem_zeroed(void) { return calloc(1,256); }
int std_process_id(void) { return (int)getpid(); }
void* std_str_from_utf8(void* b) { return b; }
void* std_time_SystemTime_now(void) { return SystemTime_now(); }
void* super_file_dialog(void) { return NULL; }
void* super_hfs_fsck_repair_hfs(void) { return NULL; }
void* super_super_compress_partition(void) { return NULL; }
void* tempfile_NamedTempFile_new(void) { return NULL; }
void* tempfile_TempDir_new(void) { return NULL; }
void* tempfile_tempdir(void) { return NULL; }
void* thread_spawn(void* f) { (void)f; return NULL; }
unsigned short u16_from_be_bytes(void* b) { unsigned char* p=(unsigned char*)b; return (p[0]<<8)|p[1]; }
unsigned short u16_from_le_bytes(void* b) { unsigned char* p=(unsigned char*)b; return p[0]|(p[1]<<8); }
unsigned int u32_from_be_bytes(void* b) { unsigned char* p=(unsigned char*)b; return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }
unsigned int u32_from_le_bytes(void* b) { unsigned char* p=(unsigned char*)b; return p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24); }
unsigned int u32_from_ne_bytes(void* b) { unsigned int v; memcpy(&v,b,4); return v; }
unsigned long long u64_from_be_bytes(void* b) { unsigned char* p=(unsigned char*)b; return ((unsigned long long)p[0]<<56)|((unsigned long long)p[1]<<48)|((unsigned long long)p[2]<<40)|((unsigned long long)p[3]<<32)|((unsigned long long)p[4]<<24)|((unsigned long long)p[5]<<16)|((unsigned long long)p[6]<<8)|(unsigned long long)p[7]; }
unsigned long long u64_from_le_bytes(void* b) { unsigned char* p=(unsigned char*)b; return (unsigned long long)p[0]|((unsigned long long)p[1]<<8)|((unsigned long long)p[2]<<16)|((unsigned long long)p[3]<<24)|((unsigned long long)p[4]<<32)|((unsigned long long)p[5]<<40)|((unsigned long long)p[6]<<48)|((unsigned long long)p[7]<<56); }
void* zeekstd_EncodeOptions_new(void) { return NULL; }
void* zstd_Decoder_new(void) { return NULL; }
void* zstd_Encoder_new(void) { return NULL; }
void* zstd_decode_all(void) { return NULL; }
void* zstd_stream_read_Decoder_new(void) { return NULL; }
