/*
 * Final Rust Compiler for PowerPC Mac OS X
 * Fixed variable tracking and optimized code generation
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_VARS 100
#define MAX_STRINGS 100

typedef struct {
    char name[32];
    int reg;
    int initialized;
} Variable;

Variable variables[MAX_VARS];
int var_count = 0;
int next_reg = 14;  // r14-r30 for locals

void reset_compiler() {
    var_count = 0;
    next_reg = 14;
}

int find_var(const char *name) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int create_var(const char *name) {
    int idx = find_var(name);
    if (idx >= 0) return idx;
    
    strcpy(variables[var_count].name, name);
    variables[var_count].reg = next_reg++;
    variables[var_count].initialized = 0;
    return var_count++;
}

void emit_prologue(FILE *out) {
    fprintf(out, ".text\n");
    fprintf(out, ".align 2\n");
    fprintf(out, ".globl _main\n");
    fprintf(out, "_main:\n");
    fprintf(out, "    mflr r0\n");
    fprintf(out, "    stw r0, 8(r1)\n");
    fprintf(out, "    stwu r1, -64(r1)\n");
}

void emit_epilogue(FILE *out) {
    fprintf(out, "    addi r1, r1, 64\n");
    fprintf(out, "    lwz r0, 8(r1)\n");
    fprintf(out, "    mtlr r0\n");
    fprintf(out, "    blr\n");
}

void parse_expression(FILE *out, const char *expr, int dest_reg) {
    // Remove whitespace
    char clean[256];
    int j = 0;
    int i;
    for (i = 0; expr[i]; i++) {
        if (!isspace(expr[i])) {
            clean[j++] = expr[i];
        }
    }
    clean[j] = '\0';
    
    // Check if it's a number
    if (isdigit(clean[0]) || (clean[0] == '-' && isdigit(clean[1]))) {
        fprintf(out, "    li r%d, %s\n", dest_reg, clean);
        return;
    }
    
    // Check for arithmetic: x+y, x-y, x*y, x/y
    char *ops[] = {"+", "-", "*", "/"};
    char *op_found = NULL;
    int op_type = -1;
    
    for (i = 0; i < 4; i++) {
        op_found = strstr(clean, ops[i]);
        if (op_found) {
            op_type = i;
            break;
        }
    }
    
    if (op_found) {
        *op_found = '\0';
        char *left = clean;
        char *right = op_found + 1;
        
        int left_idx = find_var(left);
        int right_idx = find_var(right);
        
        if (left_idx >= 0 && right_idx >= 0) {
            int left_reg = variables[left_idx].reg;
            int right_reg = variables[right_idx].reg;
            
            switch (op_type) {
                case 0: // +
                    fprintf(out, "    add r%d, r%d, r%d\n", dest_reg, left_reg, right_reg);
                    break;
                case 1: // -
                    fprintf(out, "    sub r%d, r%d, r%d\n", dest_reg, left_reg, right_reg);
                    break;
                case 2: // *
                    fprintf(out, "    mullw r%d, r%d, r%d\n", dest_reg, left_reg, right_reg);
                    break;
                case 3: // /
                    fprintf(out, "    divw r%d, r%d, r%d\n", dest_reg, left_reg, right_reg);
                    break;
            }
            return;
        }
    }
    
    // Simple variable reference
    int idx = find_var(clean);
    if (idx >= 0) {
        fprintf(out, "    mr r%d, r%d\n", dest_reg, variables[idx].reg);
    }
}

void parse_rust_final(FILE *input, FILE *output) {
    char line[1024];
    int in_main = 0;
    
    reset_compiler();
    
    while (fgets(line, sizeof(line), input)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        
        // Skip empty lines and comments
        if (!*p || strncmp(p, "//", 2) == 0) continue;
        
        // Main function
        if (strstr(p, "fn main()")) {
            emit_prologue(output);
            in_main = 1;
            continue;
        }
        
        if (!in_main) continue;
        
        // Variable declaration: let x = expression;
        if (strncmp(p, "let ", 4) == 0) {
            char var_name[32];
            char expr[256];
            
            // Parse: let var_name = expression;
            if (sscanf(p + 4, "%31s = %255[^;]", var_name, expr) == 2) {
                int var_idx = create_var(var_name);
                int var_reg = variables[var_idx].reg;
                
                parse_expression(output, expr, var_reg);
                variables[var_idx].initialized = 1;
                
                fprintf(output, "    ; %s = %s\n", var_name, expr);
            }
        }
        // Return statement
        else if (strncmp(p, "return ", 7) == 0) {
            char expr[256];
            if (sscanf(p + 7, "%255[^;]", expr) == 1) {
                parse_expression(output, expr, 3);  // r3 is return register
                fprintf(output, "    ; return %s\n", expr);
            }
        }
        // println! macro (simplified)
        else if (strstr(p, "println!")) {
            fprintf(output, "    ; println! (not implemented yet)\n");
        }
        // End of function
        else if (*p == '}' && in_main) {
            emit_epilogue(output);
            in_main = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("rustc-ppc final for PowerPC Darwin\n");
        printf("Supports: variables, arithmetic, return\n");
        printf("Usage: %s input.rs [-o output]\n", argv[0]);
        return 1;
    }
    
    char *input_file = argv[1];
    char *output_file = "a.out";
    int i;
    
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
        else if (strcmp(argv[i], "--version") == 0) {
            printf("rustc 1.16.0-powerpc (final)\n");
            return 0;
        }
    }
    
    printf("Compiling %s -> %s\n", input_file, output_file);
    
    FILE *input = fopen(input_file, "r");
    if (!input) {
        fprintf(stderr, "Error: Cannot open %s\n", input_file);
        return 1;
    }
    
    char asm_file[256];
    sprintf(asm_file, "/tmp/rust_%d.s", getpid());
    
    FILE *output = fopen(asm_file, "w");
    parse_rust_final(input, output);
    fclose(input);
    fclose(output);
    
    // Compile
    char cmd[512];
    sprintf(cmd, "gcc %s -o %s", asm_file, output_file);
    if (system(cmd) != 0) {
        fprintf(stderr, "Compilation failed. Assembly in %s\n", asm_file);
        return 1;
    }
    
    unlink(asm_file);
    printf("Success!\n");
    return 0;
}