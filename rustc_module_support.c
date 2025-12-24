#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char name[64];
    int offset;
    char type[32];
    int size;
    char module[64];  // Module this belongs to
} Variable;

typedef struct {
    char name[64];
    char module[64];
    int is_public;
} Function;

typedef struct {
    char name[64];
    int is_public;
    char items[1024]; // Space-separated list of public items
} Module;

Variable vars[100];
Function functions[50];
Module modules[20];
int var_count = 0;
int func_count = 0;
int module_count = 0;
int stack_offset = 0;
char* pos;
char current_module[64] = "main";

void skip_whitespace() {
    while (*pos && isspace(*pos)) pos++;
}

int parse_number() {
    int num = 0;
    int sign = 1;
    
    if (*pos == '-') {
        sign = -1;
        pos++;
    }
    
    while (*pos && isdigit(*pos)) {
        num = num * 10 + (*pos - '0');
        pos++;
    }
    return num * sign;
}

void parse_string(char* dest, int max_len) {
    int i = 0;
    while (*pos && (isalnum(*pos) || *pos == '_') && i < max_len - 1) {
        dest[i++] = *pos++;
    }
    dest[i] = '\0';
}

void parse_path(char* dest, int max_len) {
    int i = 0;
    while (*pos && (isalnum(*pos) || *pos == '_' || *pos == ':') && i < max_len - 1) {
        dest[i++] = *pos++;
    }
    dest[i] = '\0';
}

Variable* get_var(const char* name) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            if (strcmp(vars[i].module, current_module) == 0 ||
                strcmp(vars[i].module, "main") == 0) {
                return &vars[i];
            }
        }
    }
    return NULL;
}

void compile_rust(char* source) {
    pos = source;
    
    printf("; PowerPC Rust Compiler - Module System\n");
    printf("; Supports: mod, pub, use statements\n\n");
    
    printf(".text\n");
    
    /* First pass - find modules */
    while (*pos) {
        skip_whitespace();
        
        if (strncmp(pos, "mod ", 4) == 0) {
            pos += 4;
            skip_whitespace();
            
            char mod_name[64] = {0};
            parse_string(mod_name, sizeof(mod_name));
            
            strcpy(modules[module_count].name, mod_name);
            modules[module_count].is_public = 0;
            module_count++;
            
            printf("; Module: %s\n", mod_name);
            
            /* Skip to end of line or block */
            while (*pos && *pos != '\n' && *pos != '{' && *pos != ';') pos++;
            if (*pos == '{') {
                /* Module block */
                int depth = 1;
                pos++;
                while (*pos && depth > 0) {
                    if (*pos == '{') depth++;
                    else if (*pos == '}') depth--;
                    pos++;
                }
            } else if (*pos == ';') {
                pos++;
            }
        } else {
            pos++;
        }
    }
    
    /* Reset for second pass */
    pos = source;
    strcpy(current_module, "main");
    
    /* Second pass - compile functions */
    while (*pos) {
        skip_whitespace();
        
        if (strncmp(pos, "mod ", 4) == 0) {
            pos += 4;
            skip_whitespace();
            
            char mod_name[64] = {0};
            parse_string(mod_name, sizeof(mod_name));
            strcpy(current_module, mod_name);
            
            /* Skip module handling for now */
            while (*pos && *pos != '\n' && *pos != '{' && *pos != ';') pos++;
            if (*pos == ';') pos++;
            
        } else if (strncmp(pos, "pub ", 4) == 0) {
            pos += 4;
            skip_whitespace();
            
            if (strncmp(pos, "fn ", 3) == 0) {
                pos += 3;
                skip_whitespace();
                
                char func_name[64] = {0};
                parse_string(func_name, sizeof(func_name));
                
                printf("\n.align 2\n");
                printf(".globl _%s_%s\n", current_module, func_name);
                printf("_%s_%s:\n", current_module, func_name);
                
                strcpy(functions[func_count].name, func_name);
                strcpy(functions[func_count].module, current_module);
                functions[func_count].is_public = 1;
                func_count++;
                
                /* Skip to function body */
                while (*pos && *pos != '{') pos++;
                if (*pos == '{') {
                    pos++;
                    printf("    mflr r0\n");
                    printf("    stw r0, 8(r1)\n");
                    printf("    stwu r1, -256(r1)\n");
                    
                    /* Simple function body */
                    while (*pos && *pos != '}') {
                        skip_whitespace();
                        
                        if (strncmp(pos, "return ", 7) == 0) {
                            pos += 7;
                            int val = parse_number();
                            printf("    li r3, %d\n", val);
                            while (*pos && *pos != ';') pos++;
                            if (*pos == ';') pos++;
                        } else {
                            pos++;
                        }
                    }
                    
                    printf("    addi r1, r1, 256\n");
                    printf("    lwz r0, 8(r1)\n");
                    printf("    mtlr r0\n");
                    printf("    blr\n");
                    
                    if (*pos == '}') pos++;
                }
            }
            
        } else if (strncmp(pos, "fn main()", 9) == 0) {
            pos += 9;
            strcpy(current_module, "main");
            
            printf("\n.align 2\n.globl _main\n_main:\n");
            printf("    mflr r0\n");
            printf("    stw r0, 8(r1)\n");
            printf("    stwu r1, -512(r1)\n");
            
            /* Skip to function body */
            while (*pos && *pos != '{') pos++;
            if (*pos == '{') pos++;
            
            while (*pos && *pos != '}') {
                skip_whitespace();
                
                if (strncmp(pos, "use ", 4) == 0) {
                    pos += 4;
                    skip_whitespace();
                    
                    char path[128] = {0};
                    parse_path(path, sizeof(path));
                    
                    printf("    ; use %s\n", path);
                    
                    while (*pos && *pos != ';') pos++;
                    if (*pos == ';') pos++;
                    
                } else if (strncmp(pos, "let ", 4) == 0) {
                    pos += 4;
                    skip_whitespace();
                    
                    char var_name[64] = {0};
                    parse_string(var_name, sizeof(var_name));
                    
                    skip_whitespace();
                    if (*pos == '=') {
                        pos++;
                        skip_whitespace();
                        
                        /* Check for module function call */
                        char call_path[128] = {0};
                        int path_idx = 0;
                        char* start = pos;
                        
                        while (*pos && (isalnum(*pos) || *pos == '_' || *pos == ':')) {
                            call_path[path_idx++] = *pos++;
                        }
                        call_path[path_idx] = '\0';
                        
                        if (*pos == '(') {
                            /* It's a function call */
                            pos++; /* Skip '(' */
                            skip_whitespace();
                            if (*pos == ')') {
                                pos++; /* Skip ')' */
                                
                                /* Parse module::function */
                                char* separator = strstr(call_path, "::");
                                if (separator) {
                                    *separator = '\0';
                                    char* mod_name = call_path;
                                    char* func_name = separator + 2;
                                    
                                    printf("    ; %s = %s::%s()\n", var_name, mod_name, func_name);
                                    printf("    bl _%s_%s\n", mod_name, func_name);
                                    printf("    stw r3, %d(r1)   ; store result as %s\n", 
                                           stack_offset, var_name);
                                    
                                    strcpy(vars[var_count].name, var_name);
                                    strcpy(vars[var_count].type, "i32");
                                    strcpy(vars[var_count].module, current_module);
                                    vars[var_count].offset = stack_offset;
                                    vars[var_count].size = 4;
                                    var_count++;
                                    stack_offset += 4;
                                }
                            }
                        } else {
                            /* Regular assignment */
                            pos = start;
                            int value = parse_number();
                            
                            printf("    li r14, %d\n", value);
                            printf("    stw r14, %d(r1)  ; %s = %d\n", stack_offset, var_name, value);
                            
                            strcpy(vars[var_count].name, var_name);
                            strcpy(vars[var_count].type, "i32");
                            strcpy(vars[var_count].module, current_module);
                            vars[var_count].offset = stack_offset;
                            vars[var_count].size = 4;
                            var_count++;
                            stack_offset += 4;
                        }
                    }
                    
                    while (*pos && *pos != ';') pos++;
                    if (*pos == ';') pos++;
                    
                } else if (strncmp(pos, "return ", 7) == 0) {
                    pos += 7;
                    skip_whitespace();
                    
                    char var_name[64] = {0};
                    parse_string(var_name, sizeof(var_name));
                    
                    Variable* var = get_var(var_name);
                    if (var) {
                        printf("    lwz r3, %d(r1)    ; return %s\n", var->offset, var_name);
                    } else {
                        pos -= strlen(var_name);
                        int value = parse_number();
                        printf("    li r3, %d\n", value);
                    }
                    
                    while (*pos && *pos != ';') pos++;
                    if (*pos == ';') pos++;
                } else {
                    pos++;
                }
                
                skip_whitespace();
            }
            
            printf("    addi r1, r1, 512\n");
            printf("    lwz r0, 8(r1)\n");
            printf("    mtlr r0\n");
            printf("    blr\n");
            
            if (*pos == '}') pos++;
            
        } else {
            pos++;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <file.rs>\n", argv[0]);
        return 1;
    }
    
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        perror("Cannot open file");
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = 0;
    fclose(f);
    
    compile_rust(source);
    free(source);
    
    return 0;
}