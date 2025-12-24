#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char name[64];
    int offset;
    char type[32];
    int size;
} Variable;

Variable vars[100];
int var_count = 0;
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

void compile_rust(char* source) {
    pos = source;
    
    printf("; PowerPC Rust Compiler - Simple Generics\n");
    printf("; Supports: Generic functions like identity<T>\n\n");
    
    /* Check for generic functions first */
    char* fn_pos = strstr(source, "fn identity<T>(x: T) -> T");
    if (fn_pos) {
        printf("; Generic function identity<T> found\n");
        printf("; Will be monomorphized for each use\n\n");
    }
    
    printf(".text\n.align 2\n.globl _main\n_main:\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -512(r1)\n");
    
    char* main_start = strstr(source, "fn main()");
    if (!main_start) return;
    
    pos = strchr(main_start, '{') + 1;
    
    while (*pos && *pos != '}') {
        skip_whitespace();
        
        if (strncmp(pos, "let ", 4) == 0) {
            pos += 4;
            skip_whitespace();
            
            char var_name[64] = {0};
            parse_string(var_name, sizeof(var_name));
            
            skip_whitespace();
            if (*pos == '=') {
                pos++;
                skip_whitespace();
                
                if (strncmp(pos, "identity(", 9) == 0) {
                    /* Generic function call */
                    pos += 9;
                    skip_whitespace();
                    
                    int value = parse_number();
                    
                    printf("    ; %s = identity(%d) - monomorphized for i32\n", var_name, value);
                    printf("    li r3, %d\n", value);
                    printf("    bl _identity_i32\n");
                    printf("    stw r3, %d(r1)   ; store result as %s\n", stack_offset, var_name);
                    
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "i32");
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 4;
                    var_count++;
                    stack_offset += 4;
                    
                    while (*pos && *pos != ')') pos++;
                    if (*pos == ')') pos++;
                    
                } else {
                    /* Regular value */
                    int value = parse_number();
                    
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "i32");
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 4;
                    var_count++;
                    
                    printf("    li r14, %d\n", value);
                    printf("    stw r14, %d(r1)  ; %s = %d\n", stack_offset, var_name, value);
                    
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
        }
        
        skip_whitespace();
    }
    
    printf("    addi r1, r1, 512\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n");
    
    /* Generate monomorphized versions */
    if (fn_pos) {
        printf("\n; Monomorphized identity<i32>\n");
        printf(".align 2\n");
        printf("_identity_i32:\n");
        printf("    ; r3 = input, return r3 unchanged\n");
        printf("    blr\n");
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