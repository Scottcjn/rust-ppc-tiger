/*
 * Modern Rust Compiler for PowerPC - Simplified
 * C89 compatible for old GCC
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_VARS 100

typedef struct {
    char name[64];
    char type[32];  // "i32", "bool", "mut i32", etc
    int reg;
    int is_mut;
} Variable;

Variable vars[MAX_VARS];
int var_count = 0;
int next_reg = 14;
int string_count = 0;

void reset_compiler() {
    var_count = 0;
    next_reg = 14;
    string_count = 0;
}

int find_var(const char *name) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int create_var(const char *name, const char *type, int is_mut) {
    int idx = find_var(name);
    if (idx >= 0) {
        fprintf(stderr, "error: variable '%s' already defined\n", name);
        exit(1);
    }
    
    strcpy(vars[var_count].name, name);
    strcpy(vars[var_count].type, type);
    vars[var_count].is_mut = is_mut;
    vars[var_count].reg = next_reg++;
    return var_count++;
}

void emit_println_simple(FILE *out, const char *msg) {
    fprintf(out, "\n.data\n");
    fprintf(out, ".align 2\n");
    fprintf(out, "str_%d:\n", string_count);
    fprintf(out, "    .asciz \"%s\\n\"\n", msg);
    fprintf(out, "\n.text\n");
    
    fprintf(out, "    ; println!(\"%s\")\n", msg);
    fprintf(out, "    lis r3, ha16(str_%d)\n", string_count);
    fprintf(out, "    ori r3, r3, lo16(str_%d)\n", string_count);
    fprintf(out, "    bl _printf$stub\n");
    
    string_count++;
}

void emit_println_value(FILE *out, const char *fmt, int reg) {
    fprintf(out, "\n.data\n");
    fprintf(out, ".align 2\n");
    fprintf(out, "fmt_%d:\n", string_count);
    
    /* Convert {} to %d */
    char buf[256];
    int i, j = 0;
    for (i = 0; fmt[i]; i++) {
        if (fmt[i] == '{' && fmt[i+1] == '}') {
            buf[j++] = '%';
            buf[j++] = 'd';
            i++;
        } else {
            buf[j++] = fmt[i];
        }
    }
    buf[j] = '\0';
    
    fprintf(out, "    .asciz \"%s\\n\"\n", buf);
    fprintf(out, "\n.text\n");
    
    fprintf(out, "    lis r3, ha16(fmt_%d)\n", string_count);
    fprintf(out, "    ori r3, r3, lo16(fmt_%d)\n", string_count);
    fprintf(out, "    mr r4, r%d\n", reg);
    fprintf(out, "    bl _printf$stub\n");
    
    string_count++;
}

void parse_modern_rust(FILE *input, FILE *output) {
    char line[1024];
    int in_main = 0;
    
    reset_compiler();
    
    fprintf(output, "; Modern Rust for PowerPC\n");
    fprintf(output, ".text\n");
    fprintf(output, ".align 2\n\n");
    
    while (fgets(line, sizeof(line), input)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        
        if (!*p || strncmp(p, "//", 2) == 0) continue;
        
        /* Main function */
        if (strstr(p, "fn main()")) {
            fprintf(output, ".globl _main\n");
            fprintf(output, "_main:\n");
            fprintf(output, "    mflr r0\n");
            fprintf(output, "    stw r0, 8(r1)\n");
            fprintf(output, "    stwu r1, -96(r1)\n");
            in_main = 1;
        }
        /* Modern let binding */
        else if (in_main && strncmp(p, "let ", 4) == 0) {
            char var[64], expr[256];
            int is_mut = 0;
            char *start = p + 4;
            
            /* Check for mut */
            if (strncmp(start, "mut ", 4) == 0) {
                is_mut = 1;
                start += 4;
            }
            
            /* Parse variable and expression */
            if (sscanf(start, "%63s = %255[^;]", var, expr) == 2) {
                /* Remove type annotation if present */
                char *colon = strchr(var, ':');
                if (colon) *colon = '\0';
                
                /* Infer type from value */
                char type[32] = "i32";
                if (strstr(expr, "true") || strstr(expr, "false")) {
                    strcpy(type, "bool");
                } else if (strchr(expr, '.')) {
                    strcpy(type, "f32");
                }
                
                int idx = create_var(var, type, is_mut);
                int reg = vars[idx].reg;
                
                /* Generate code based on expression */
                if (strcmp(expr, "true") == 0) {
                    fprintf(output, "    li r%d, 1  ; %s = true\n", reg, var);
                } else if (strcmp(expr, "false") == 0) {
                    fprintf(output, "    li r%d, 0  ; %s = false\n", reg, var);
                } else if (isdigit(expr[0])) {
                    fprintf(output, "    li r%d, %s  ; %s = %s\n", reg, expr, var, expr);
                } else {
                    /* Handle x + y */
                    char v1[32], op, v2[32];
                    if (sscanf(expr, "%s %c %s", v1, &op, v2) == 3) {
                        int r1 = vars[find_var(v1)].reg;
                        int r2 = vars[find_var(v2)].reg;
                        
                        if (op == '+') {
                            fprintf(output, "    add r%d, r%d, r%d  ; %s = %s\n", 
                                    reg, r1, r2, var, expr);
                        }
                    }
                }
            }
        }
        /* Variable assignment (for mut vars) */
        else if (in_main && strchr(p, '=') && !strstr(p, "let")) {
            char var[64], expr[256];
            if (sscanf(p, "%63s = %255[^;]", var, expr) == 2) {
                int idx = find_var(var);
                if (idx >= 0 && vars[idx].is_mut) {
                    int reg = vars[idx].reg;
                    /* Simple case: assign from another var */
                    char v1[32], op, v2[32];
                    if (sscanf(expr, "%s %c %s", v1, &op, v2) == 3) {
                        int r1 = vars[find_var(v1)].reg;
                        int r2 = vars[find_var(v2)].reg;
                        if (op == '+') {
                            fprintf(output, "    add r%d, r%d, r%d  ; %s = %s\n", 
                                    reg, r1, r2, var, expr);
                        }
                    }
                }
            }
        }
        /* println! macro */
        else if (in_main && strstr(p, "println!")) {
            char *start = strchr(p, '(');
            char *end = strrchr(p, ')');
            if (start && end) {
                *end = '\0';
                start++;
                
                /* Check if it's a simple string */
                if (*start == '"') {
                    char *q1 = start + 1;
                    char *q2 = strchr(q1, '"');
                    if (q2) {
                        *q2 = '\0';
                        
                        /* Check for format with variable */
                        char *comma = strchr(q2 + 1, ',');
                        if (comma && strstr(q1, "{}")) {
                            char var[64];
                            sscanf(comma + 1, " %63s", var);
                            int idx = find_var(var);
                            if (idx >= 0) {
                                emit_println_value(output, q1, vars[idx].reg);
                            }
                        } else {
                            emit_println_simple(output, q1);
                        }
                    }
                }
            }
        }
        /* Return */
        else if (in_main && strncmp(p, "return ", 7) == 0) {
            char expr[256];
            if (sscanf(p + 7, "%255[^;]", expr) == 1) {
                int idx = find_var(expr);
                if (idx >= 0) {
                    fprintf(output, "    mr r3, r%d  ; return %s\n", vars[idx].reg, expr);
                } else if (isdigit(expr[0])) {
                    fprintf(output, "    li r3, %s  ; return %s\n", expr, expr);
                }
            }
        }
        /* End of main */
        else if (in_main && *p == '}') {
            fprintf(output, "    addi r1, r1, 96\n");
            fprintf(output, "    lwz r0, 8(r1)\n");
            fprintf(output, "    mtlr r0\n");
            fprintf(output, "    blr\n");
            in_main = 0;
        }
    }
    
    /* Printf stub */
    fprintf(output, "\n.section __TEXT,__picsymbolstub1,symbol_stubs,pure_instructions,32\n");
    fprintf(output, ".align 2\n");
    fprintf(output, "_printf$stub:\n");
    fprintf(output, "    .indirect_symbol _printf\n");
    fprintf(output, "    mflr r0\n");
    fprintf(output, "    bcl 20,31,L0$_printf\n");
    fprintf(output, "L0$_printf:\n");
    fprintf(output, "    mflr r11\n");
    fprintf(output, "    mtlr r0\n");
    fprintf(output, "    addis r11,r11,ha16(L_printf$lazy-L0$_printf)\n");
    fprintf(output, "    lwzu r12,lo16(L_printf$lazy-L0$_printf)(r11)\n");
    fprintf(output, "    mtctr r12\n");
    fprintf(output, "    bctr\n");
    fprintf(output, "\n.lazy_symbol_pointer\n");
    fprintf(output, "L_printf$lazy:\n");
    fprintf(output, "    .indirect_symbol _printf\n");
    fprintf(output, "    .long dyld_stub_binding_helper\n");
}

int main(int argc, char *argv[]) {
    char *input_file;
    char *output_file = "a.out";
    int i;
    
    if (argc < 2) {
        printf("rustc-modern for PowerPC\n");
        printf("Features: type inference, mut, println!\n");
        printf("Usage: %s input.rs [-o output]\n", argv[0]);
        return 1;
    }
    
    input_file = argv[1];
    
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }
    
    printf("Compiling %s -> %s\n", input_file, output_file);
    
    FILE *input = fopen(input_file, "r");
    if (!input) {
        fprintf(stderr, "Error: Cannot open %s\n", input_file);
        return 1;
    }
    
    char asm_file[256];
    sprintf(asm_file, "/tmp/rust_mod_%d.s", getpid());
    
    FILE *output = fopen(asm_file, "w");
    parse_modern_rust(input, output);
    fclose(input);
    fclose(output);
    
    char cmd[512];
    sprintf(cmd, "gcc -mdynamic-no-pic %s -o %s 2>/dev/null || gcc %s -o %s", 
            asm_file, output_file, asm_file, output_file);
    
    if (system(cmd) != 0) {
        fprintf(stderr, "Compilation failed\n");
        return 1;
    }
    
    unlink(asm_file);
    printf("Success!\n");
    return 0;
}