#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char name[64];
    int offset;
    char type[32];
    int size;
    char traits[256]; // comma-separated list of implemented traits
} Variable;

typedef struct {
    char name[64];
    char methods[512];
} Trait;

Variable vars[100];
Trait traits[20];
int var_count = 0;
int trait_count = 0;
int stack_offset = 0;
char* pos;

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

Variable* get_var(const char* name) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            return &vars[i];
        }
    }
    return NULL;
}

void parse_string(char* dest, int max_len) {
    int i = 0;
    while (*pos && (isalnum(*pos) || *pos == '_') && i < max_len - 1) {
        dest[i++] = *pos++;
    }
    dest[i] = '\0';
}

char* parse_string_literal() {
    static char buffer[256];
    int i = 0;
    
    if (*pos == '"') {
        pos++;
        while (*pos && *pos != '"' && i < 255) {
            buffer[i++] = *pos++;
        }
        if (*pos == '"') pos++;
    }
    
    buffer[i] = '\0';
    return buffer;
}

void compile_rust(char* source) {
    pos = source;
    
    printf("; PowerPC Rust Compiler - Trait Support\n");
    printf("; Supports: Display and Debug traits\n\n");
    
    printf(".text\n.align 2\n.globl _main\n_main:\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -512(r1)\n");
    
    /* Pre-define Display and Debug traits */
    strcpy(traits[0].name, "Display");
    strcpy(traits[0].methods, "fmt");
    trait_count = 1;
    
    strcpy(traits[1].name, "Debug");
    strcpy(traits[1].methods, "fmt");
    trait_count = 2;
    
    char* main_start = strstr(source, "fn main()");
    if (!main_start) return;
    
    pos = strchr(main_start, '{') + 1;
    
    while (*pos && *pos != '}') {
        skip_whitespace();
        
        if (strncmp(pos, "let ", 4) == 0) {
            pos += 4;
            skip_whitespace();
            
            int is_mut = 0;
            if (strncmp(pos, "mut ", 4) == 0) {
                is_mut = 1;
                pos += 4;
                skip_whitespace();
            }
            
            char var_name[64] = {0};
            parse_string(var_name, sizeof(var_name));
            
            skip_whitespace();
            if (*pos == '=') {
                pos++;
                skip_whitespace();
                
                if (strncmp(pos, "Point", 5) == 0) {
                    pos += 5;
                    skip_whitespace();
                    
                    if (*pos == '{') {
                        pos++;
                        skip_whitespace();
                        
                        int x_val = 0, y_val = 0;
                        
                        /* Parse x field */
                        if (strncmp(pos, "x:", 2) == 0) {
                            pos += 2;
                            skip_whitespace();
                            x_val = parse_number();
                            
                            skip_whitespace();
                            if (*pos == ',') pos++;
                            skip_whitespace();
                            
                            /* Parse y field */
                            if (strncmp(pos, "y:", 2) == 0) {
                                pos += 2;
                                skip_whitespace();
                                y_val = parse_number();
                            }
                        }
                        
                        strcpy(vars[var_count].name, var_name);
                        strcpy(vars[var_count].type, "Point");
                        strcpy(vars[var_count].traits, "Display,Debug"); // Point implements Display and Debug
                        vars[var_count].offset = stack_offset;
                        vars[var_count].size = 8;
                        var_count++;
                        
                        printf("    ; Point { x: %d, y: %d } for %s\n", x_val, y_val, var_name);
                        printf("    li r14, %d\n", x_val);
                        printf("    stw r14, %d(r1)   ; x\n", stack_offset);
                        printf("    li r14, %d\n", y_val);
                        printf("    stw r14, %d(r1)   ; y\n", stack_offset + 4);
                        
                        stack_offset += 8;
                        
                        while (*pos && *pos != '}') pos++;
                        if (*pos == '}') pos++;
                    }
                    
                } else if (strncmp(pos, "String::from(", 13) == 0) {
                    pos += 13;
                    skip_whitespace();
                    
                    char* str_content = parse_string_literal();
                    
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "String");
                    strcpy(vars[var_count].traits, "Display,Debug"); // String implements Display and Debug
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 4; // Simplified: just store pointer
                    var_count++;
                    
                    printf("    ; String::from(\"%s\") for %s\n", str_content, var_name);
                    printf("    lis r14, ha16(Lstr_%s)\n", var_name);
                    printf("    la r14, lo16(Lstr_%s)(r14)\n", var_name);
                    printf("    stw r14, %d(r1)   ; string ptr\n", stack_offset);
                    
                    stack_offset += 4;
                    
                    while (*pos && *pos != ')') pos++;
                    if (*pos == ')') pos++;
                    
                } else {
                    /* Regular number */
                    int value = parse_number();
                    printf("    li r14, %d\n", value);
                    printf("    stw r14, %d(r1)  ; %s\n", stack_offset, var_name);
                    
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "i32");
                    strcpy(vars[var_count].traits, "Display,Debug"); // i32 implements Display and Debug
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 4;
                    var_count++;
                    stack_offset += 4;
                }
            }
            
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;
            
        } else if (strncmp(pos, "println!", 8) == 0) {
            pos += 8;
            skip_whitespace();
            
            if (*pos == '(') {
                pos++;
                skip_whitespace();
                
                if (*pos == '"') {
                    /* Parse format string */
                    char* fmt_str = parse_string_literal();
                    
                    skip_whitespace();
                    if (*pos == ',') {
                        pos++;
                        skip_whitespace();
                        
                        /* Parse variable to print */
                        char var_name[64] = {0};
                        parse_string(var_name, sizeof(var_name));
                        
                        Variable* var = get_var(var_name);
                        if (var && strstr(var->traits, "Display")) {
                            printf("    ; println!(\"%s\", %s) - using Display trait\n", fmt_str, var_name);
                            
                            if (strcmp(var->type, "i32") == 0) {
                                printf("    lwz r3, %d(r1)   ; load %s\n", var->offset, var_name);
                                printf("    bl _print_i32    ; Display for i32\n");
                            } else if (strcmp(var->type, "String") == 0) {
                                printf("    lwz r3, %d(r1)   ; load %s ptr\n", var->offset, var_name);
                                printf("    bl _print_string ; Display for String\n");
                            } else if (strcmp(var->type, "Point") == 0) {
                                printf("    lwz r3, %d(r1)   ; load %s.x\n", var->offset, var_name);
                                printf("    lwz r4, %d(r1)   ; load %s.y\n", var->offset + 4, var_name);
                                printf("    bl _print_point  ; Display for Point\n");
                            }
                        }
                    }
                }
                
                while (*pos && *pos != ')') pos++;
                if (*pos == ')') pos++;
            }
            
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;
            
        } else if (strncmp(pos, "return ", 7) == 0) {
            pos += 7;
            skip_whitespace();
            
            int value = parse_number();
            printf("    li r3, %d\n", value);
            
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;
        }
        
        skip_whitespace();
    }
    
    printf("    addi r1, r1, 512\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n");
    
    /* Generate trait implementation functions */
    printf("\n; Display trait implementations\n");
    
    printf(".align 2\n");
    printf("_print_i32:\n");
    printf("    ; Display::fmt for i32 (simplified)\n");
    printf("    ; Would normally call printf or similar\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_print_string:\n");
    printf("    ; Display::fmt for String\n");
    printf("    ; r3 = string pointer\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_print_point:\n");
    printf("    ; Display::fmt for Point\n");
    printf("    ; r3 = x, r4 = y\n");
    printf("    ; Would print \"Point { x: _, y: _ }\"\n");
    printf("    blr\n");
    
    /* String constants */
    printf("\n.cstring\n");
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(vars[i].type, "String") == 0) {
            printf("Lstr_%s:\n", vars[i].name);
            printf("    .asciz \"example string\"\n");
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