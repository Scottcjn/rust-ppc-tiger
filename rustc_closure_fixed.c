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

typedef struct {
    char name[64];
    char params[128];
    char captured_vars[256];
    int capture_count;
    char body[512];
} Closure;

Variable vars[100];
Closure closures[20];
int var_count = 0;
int closure_count = 0;
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

void parse_closure_body(char* body, int max_len) {
    int i = 0;
    int depth = 0;
    
    /* Skip to opening brace or expression */
    while (*pos && isspace(*pos)) pos++;
    
    if (*pos == '{') {
        /* Block body */
        pos++; /* Skip '{' */
        depth = 1;
        
        while (*pos && depth > 0 && i < max_len - 1) {
            if (*pos == '{') depth++;
            else if (*pos == '}') depth--;
            
            if (depth > 0) {
                body[i++] = *pos;
            }
            pos++;
        }
    } else {
        /* Expression body */
        while (*pos && *pos != ';' && *pos != ',' && *pos != ')' && i < max_len - 1) {
            body[i++] = *pos++;
        }
    }
    
    body[i] = '\0';
}

void compile_rust(char* source) {
    pos = source;
    
    printf("; PowerPC Rust Compiler - Fixed Closure Support\n");
    printf("; Supports: Closures with captured variables\n\n");
    
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
                
                if (*pos == '|') {
                    /* Closure definition */
                    pos++; /* Skip '|' */
                    
                    strcpy(closures[closure_count].name, var_name);
                    closures[closure_count].capture_count = 0;
                    
                    /* Parse parameters */
                    char params[128] = {0};
                    int param_idx = 0;
                    while (*pos && *pos != '|' && param_idx < 127) {
                        params[param_idx++] = *pos++;
                    }
                    params[param_idx] = '\0';
                    strcpy(closures[closure_count].params, params);
                    
                    if (*pos == '|') pos++; /* Skip closing '|' */
                    skip_whitespace();
                    
                    /* Parse closure body */
                    char body[512] = {0};
                    parse_closure_body(body, sizeof(body));
                    strcpy(closures[closure_count].body, body);
                    
                    /* Analyze captured variables */
                    char* body_scan = body;
                    while (*body_scan) {
                        if (isalpha(*body_scan) || *body_scan == '_') {
                            char var_check[64] = {0};
                            int idx = 0;
                            while ((isalnum(*body_scan) || *body_scan == '_') && idx < 63) {
                                var_check[idx++] = *body_scan++;
                            }
                            var_check[idx] = '\0';
                            
                            /* Check if it's a captured variable */
                            Variable* var = get_var(var_check);
                            if (var && strstr(params, var_check) == NULL) {
                                /* It's a capture! */
                                if (closures[closure_count].capture_count == 0) {
                                    strcpy(closures[closure_count].captured_vars, var_check);
                                } else {
                                    strcat(closures[closure_count].captured_vars, ",");
                                    strcat(closures[closure_count].captured_vars, var_check);
                                }
                                closures[closure_count].capture_count++;
                            }
                        } else {
                            body_scan++;
                        }
                    }
                    
                    printf("    ; Closure %s captures: %s\n", var_name, 
                           closures[closure_count].capture_count > 0 ? 
                           closures[closure_count].captured_vars : "nothing");
                    
                    /* Store closure reference */
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "closure");
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 8; /* Function ptr + capture ptr */
                    var_count++;
                    
                    printf("    lis r14, ha16(Lclosure_%s)\n", var_name);
                    printf("    la r14, lo16(Lclosure_%s)(r14)\n", var_name);
                    printf("    stw r14, %d(r1)   ; closure function ptr\n", stack_offset);
                    
                    /* Store captured values */
                    if (closures[closure_count].capture_count > 0) {
                        Variable* cap_var = get_var(closures[closure_count].captured_vars);
                        if (cap_var) {
                            printf("    lwz r15, %d(r1)   ; load captured %s\n", 
                                   cap_var->offset, cap_var->name);
                            printf("    stw r15, %d(r1)   ; store capture\n", stack_offset + 4);
                        }
                    }
                    
                    stack_offset += 8;
                    closure_count++;
                    
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
            
            char expr[128] = {0};
            int expr_idx = 0;
            
            /* Parse return expression */
            while (*pos && *pos != ';' && expr_idx < 127) {
                expr[expr_idx++] = *pos++;
            }
            expr[expr_idx] = '\0';
            
            /* Check if it's a closure call */
            char* paren = strchr(expr, '(');
            if (paren) {
                *paren = '\0';
                char* closure_name = expr;
                char* arg = paren + 1;
                char* close_paren = strchr(arg, ')');
                if (close_paren) *close_paren = '\0';
                
                Variable* closure_var = get_var(closure_name);
                if (closure_var && strcmp(closure_var->type, "closure") == 0) {
                    int arg_val = atoi(arg);
                    
                    printf("    ; Call closure %s(%d)\n", closure_name, arg_val);
                    printf("    li r3, %d         ; argument\n", arg_val);
                    printf("    lwz r4, %d(r1)    ; load capture\n", closure_var->offset + 4);
                    printf("    lwz r12, %d(r1)   ; load closure function\n", closure_var->offset);
                    printf("    mtctr r12\n");
                    printf("    bctrl             ; call closure\n");
                }
            } else {
                /* Regular return */
                int value = atoi(expr);
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
    
    /* Generate closure functions */
    int i;
    for (i = 0; i < closure_count; i++) {
        printf("\n.align 2\n");
        printf("Lclosure_%s:\n", closures[i].name);
        printf("    ; Parameters: %s\n", closures[i].params);
        printf("    ; Body: %s\n", closures[i].body);
        printf("    ; r3 = parameter, r4 = captured value\n");
        
        /* Simple implementation - just add for now */
        if (strstr(closures[i].body, "+")) {
            printf("    add r3, r3, r4    ; param + captured\n");
        }
        
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