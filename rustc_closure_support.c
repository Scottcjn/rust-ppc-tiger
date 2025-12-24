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
    int closure_offset;
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

void compile_rust(char* source) {
    pos = source;
    
    printf("; PowerPC Rust Compiler - Closure Support\n");
    printf("; Supports: Closures with variable capture\n\n");
    
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
            if (*pos == '=') {
                pos++;
                skip_whitespace();
                
                if (*pos == '|') {
                    /* Parse closure: let add = |x| x + captured_var; */
                    pos++; /* Skip | */
                    
                    char params[128] = {0};
                    int param_count = 0;
                    
                    /* Parse parameters */
                    while (*pos && *pos != '|') {
                        skip_whitespace();
                        char param[32] = {0};
                        parse_string(param, sizeof(param));
                        
                        if (param_count > 0) strcat(params, ",");
                        strcat(params, param);
                        param_count++;
                        
                        skip_whitespace();
                        if (*pos == ',') pos++;
                    }
                    
                    if (*pos == '|') pos++;
                    skip_whitespace();
                    
                    /* Create closure structure */
                    strcpy(closures[closure_count].name, var_name);
                    strcpy(closures[closure_count].params, params);
                    closures[closure_count].closure_offset = stack_offset;
                    closures[closure_count].capture_count = 0;
                    
                    /* Parse closure body to find captured variables */
                    char* body_start = pos;
                    char captured[256] = {0};
                    int capture_offset = stack_offset;
                    
                    /* For simplicity, assume closure body is: param + captured_var */
                    while (*pos && *pos != ';') {
                        if (isalpha(*pos)) {
                            char var_ref[64] = {0};
                            parse_string(var_ref, sizeof(var_ref));
                            
                            /* Check if it's a captured variable (not a parameter) */
                            if (strstr(params, var_ref) == NULL) {
                                Variable* captured_var = get_var(var_ref);
                                if (captured_var) {
                                    printf("    ; Capture %s for closure %s\n", var_ref, var_name);
                                    printf("    lwz r14, %d(r1)   ; load %s\n", captured_var->offset, var_ref);
                                    printf("    stw r14, %d(r1)   ; store captured %s\n", capture_offset, var_ref);
                                    
                                    if (closures[closure_count].capture_count > 0) strcat(captured, ",");
                                    strcat(captured, var_ref);
                                    closures[closure_count].capture_count++;
                                    capture_offset += 4;
                                }
                            }
                        } else {
                            pos++;
                        }
                    }
                    
                    strcpy(closures[closure_count].captured_vars, captured);
                    
                    /* Store closure as a function pointer + captured environment */
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "closure");
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 4 + (closures[closure_count].capture_count * 4);
                    var_count++;
                    
                    printf("    ; Closure %s created\n", var_name);
                    printf("    lis r14, ha16(Lclosure_%s)\n", var_name);
                    printf("    la r14, lo16(Lclosure_%s)(r14)\n", var_name);
                    printf("    stw r14, %d(r1)   ; store function ptr\n", stack_offset);
                    
                    stack_offset += 4 + (closures[closure_count].capture_count * 4);
                    closure_count++;
                    
                } else if (strncmp(pos, "Vec::new()", 10) == 0) {
                    pos += 10;
                    
                    strcpy(vars[var_count].name, var_name);
                    strcpy(vars[var_count].type, "vec");
                    vars[var_count].offset = stack_offset;
                    vars[var_count].size = 12;
                    var_count++;
                    
                    printf("    ; Vec::new() for %s\n", var_name);
                    printf("    li r14, 0\n");
                    printf("    stw r14, %d(r1)  ; ptr\n", stack_offset);
                    printf("    stw r14, %d(r1)  ; len\n", stack_offset + 4);
                    printf("    stw r14, %d(r1)  ; cap\n", stack_offset + 8);
                    
                    stack_offset += 12;
                    
                } else {
                    /* Check for closure call: let result = closure(arg); */
                    char func_name[64] = {0};
                    parse_string(func_name, sizeof(func_name));
                    
                    if (*pos == '(') {
                        pos++;
                        skip_whitespace();
                        
                        /* Check if it's a closure call */
                        Closure* closure = NULL;
                        int i;
                        for (i = 0; i < closure_count; i++) {
                            if (strcmp(closures[i].name, func_name) == 0) {
                                closure = &closures[i];
                                break;
                            }
                        }
                        
                        if (closure) {
                            int arg_value = parse_number();
                            
                            printf("    ; Call closure %s(%d)\n", func_name, arg_value);
                            printf("    li r3, %d         ; argument\n", arg_value);
                            
                            /* Load captured variables */
                            Variable* closure_var = get_var(func_name);
                            if (closure_var && closure->capture_count > 0) {
                                printf("    lwz r4, %d(r1)   ; load captured value\n", closure_var->offset + 4);
                            }
                            
                            printf("    lwz r14, %d(r1)   ; load function ptr\n", closure_var->offset);
                            printf("    mtctr r14\n");
                            printf("    bctrl             ; call closure\n");
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
                            /* Regular number assignment */
                            pos = func_name;
                            int value = parse_number();
                            printf("    li r14, %d\n", value);
                            printf("    stw r14, %d(r1)  ; %s = %d\n", stack_offset, var_name, value);
                            
                            strcpy(vars[var_count].name, var_name);
                            strcpy(vars[var_count].type, "i32");
                            vars[var_count].offset = stack_offset;
                            vars[var_count].size = 4;
                            var_count++;
                            stack_offset += 4;
                        }
                    } else {
                        /* Regular assignment */
                        pos = func_name;
                        int value = parse_number();
                        printf("    li r14, %d\n", value);
                        printf("    stw r14, %d(r1)  ; %s\n", stack_offset, var_name);
                        
                        strcpy(vars[var_count].name, var_name);
                        strcpy(vars[var_count].type, "i32");
                        vars[var_count].offset = stack_offset;
                        vars[var_count].size = 4;
                        var_count++;
                        stack_offset += 4;
                    }
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
    
    /* Generate closure functions */
    int i;
    for (i = 0; i < closure_count; i++) {
        printf("\n.align 2\n");
        printf("Lclosure_%s:\n", closures[i].name);
        printf("    ; Closure body: %s + captured\n", closures[i].params);
        printf("    ; r3 = parameter, r4 = captured value\n");
        printf("    add r3, r3, r4    ; param + captured\n");
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