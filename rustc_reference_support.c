#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char name[64];
    int offset;
    char type[32];
    int size;
    int is_ref;        // 0 = owned, 1 = immutable ref, 2 = mutable ref
    char ref_to[64];   // name of variable this references
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
    
    printf("; PowerPC Rust Compiler - References & Borrowing\n");
    printf("; Supports: &T (immutable ref), &mut T (mutable ref)\n\n");
    
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
            
            int is_mut = 0;
            if (strncmp(pos, "mut ", 4) == 0) {
                is_mut = 1;
                pos += 4;
                skip_whitespace();
            }
            
            char var_name[64] = {0};
            parse_string(var_name, sizeof(var_name));
            
            skip_whitespace();
            if (*pos == ':') {
                /* Type annotation like: let r: &i32 = &x; */
                pos++;
                skip_whitespace();
                
                int ref_type = 0; // 0 = owned, 1 = &T, 2 = &mut T
                if (*pos == '&') {
                    pos++;
                    if (strncmp(pos, "mut ", 4) == 0) {
                        ref_type = 2;
                        pos += 4;
                    } else {
                        ref_type = 1;
                    }
                }
                
                /* Skip type name */
                while (*pos && !isspace(*pos) && *pos != '=') pos++;
            }
            
            skip_whitespace();
            if (*pos == '=') {
                pos++;
                skip_whitespace();
                
                if (*pos == '&') {
                    /* Creating a reference: let r = &x; or let r = &mut x; */
                    pos++;
                    
                    int ref_type = 1; // immutable by default
                    if (strncmp(pos, "mut ", 4) == 0) {
                        ref_type = 2;
                        pos += 4;
                        skip_whitespace();
                    }
                    
                    char ref_target[64] = {0};
                    parse_string(ref_target, sizeof(ref_target));
                    
                    Variable* target = get_var(ref_target);
                    if (target) {
                        strcpy(vars[var_count].name, var_name);
                        strcpy(vars[var_count].type, "ref");
                        vars[var_count].offset = stack_offset;
                        vars[var_count].size = 4; // pointer size
                        vars[var_count].is_ref = ref_type;
                        strcpy(vars[var_count].ref_to, ref_target);
                        var_count++;
                        
                        printf("    ; %s = &%s%s\n", var_name, 
                               ref_type == 2 ? "mut " : "", ref_target);
                        printf("    la r14, %d(r1)    ; get address of %s\n", 
                               target->offset, ref_target);
                        printf("    stw r14, %d(r1)   ; store as %s\n", 
                               stack_offset, var_name);
                        
                        stack_offset += 4;
                    }
                    
                } else if (*pos == '*') {
                    /* Dereferencing: let y = *r; */
                    pos++;
                    
                    char ref_name[64] = {0};
                    parse_string(ref_name, sizeof(ref_name));
                    
                    Variable* ref_var = get_var(ref_name);
                    if (ref_var && ref_var->is_ref) {
                        strcpy(vars[var_count].name, var_name);
                        strcpy(vars[var_count].type, "i32");
                        vars[var_count].offset = stack_offset;
                        vars[var_count].size = 4;
                        vars[var_count].is_ref = 0;
                        var_count++;
                        
                        printf("    ; %s = *%s (dereference)\n", var_name, ref_name);
                        printf("    lwz r14, %d(r1)   ; load pointer %s\n", 
                               ref_var->offset, ref_name);
                        printf("    lwz r15, 0(r14)   ; dereference\n");
                        printf("    stw r15, %d(r1)   ; store as %s\n", 
                               stack_offset, var_name);
                        
                        stack_offset += 4;
                    }
                    
                } else {
                    /* Regular value */
                    int value = parse_number();
                    
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "i32");
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 4;
                    vars[var_count].is_ref = 0;
                    var_count++;
                    
                    printf("    li r14, %d\n", value);
                    printf("    stw r14, %d(r1)  ; %s = %d\n", stack_offset, var_name, value);
                    
                    stack_offset += 4;
                }
            }
            
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;
            
        } else if (*pos == '*') {
            /* Assignment through mutable reference: *r = value; */
            pos++;
            
            char ref_name[64] = {0};
            parse_string(ref_name, sizeof(ref_name));
            
            skip_whitespace();
            if (*pos == '=') {
                pos++;
                skip_whitespace();
                
                int value = parse_number();
                
                Variable* ref_var = get_var(ref_name);
                if (ref_var && ref_var->is_ref == 2) { // mutable ref
                    printf("    ; *%s = %d (assign through mut ref)\n", ref_name, value);
                    printf("    lwz r14, %d(r1)   ; load pointer %s\n", 
                           ref_var->offset, ref_name);
                    printf("    li r15, %d\n", value);
                    printf("    stw r15, 0(r14)   ; store through pointer\n");
                }
            }
            
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;
            
        } else if (strncmp(pos, "return ", 7) == 0) {
            pos += 7;
            skip_whitespace();
            
            if (*pos == '*') {
                /* Return dereferenced value */
                pos++;
                char ref_name[64] = {0};
                parse_string(ref_name, sizeof(ref_name));
                
                Variable* ref_var = get_var(ref_name);
                if (ref_var && ref_var->is_ref) {
                    printf("    lwz r14, %d(r1)   ; load pointer %s\n", 
                           ref_var->offset, ref_name);
                    printf("    lwz r3, 0(r14)    ; dereference and return\n");
                }
            } else {
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