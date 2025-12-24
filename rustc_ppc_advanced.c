/*
 * Advanced Rust Compiler for PowerPC Mac OS X
 * Now with variables, arithmetic, and println!
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_VARS 100
#define MAX_STRINGS 100

typedef struct {
    char name[32];
    int reg;  // PowerPC register (r14-r30 for locals)
} Variable;

typedef struct {
    char content[256];
    int id;
} StringLiteral;

Variable variables[MAX_VARS];
int var_count = 0;
int next_reg = 14;  // r14-r30 for local variables

StringLiteral strings[MAX_STRINGS];
int string_count = 0;

int find_or_create_var(const char *name) {
    int i;
    for (i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return variables[i].reg;
        }
    }
    // Create new variable
    strcpy(variables[var_count].name, name);
    variables[var_count].reg = next_reg++;
    var_count++;
    return variables[var_count-1].reg;
}

void emit_arithmetic(FILE *out, const char *expr) {
    // Simple parser for "x + y" style expressions
    char left[32], op, right[32];
    if (sscanf(expr, "%s %c %s", left, &op, right) == 3) {
        int left_reg = find_or_create_var(left);
        int right_reg = find_or_create_var(right);
        
        switch (op) {
            case '+':
                fprintf(out, "    add r3, r%d, r%d\n", left_reg, right_reg);
                break;
            case '-':
                fprintf(out, "    sub r3, r%d, r%d\n", left_reg, right_reg);
                break;
            case '*':
                fprintf(out, "    mullw r3, r%d, r%d\n", left_reg, right_reg);
                break;
            case '/':
                fprintf(out, "    divw r3, r%d, r%d\n", left_reg, right_reg);
                break;
        }
    } else if (isdigit(expr[0])) {
        fprintf(out, "    li r3, %s\n", expr);
    } else {
        int reg = find_or_create_var(expr);
        fprintf(out, "    mr r3, r%d\n", reg);
    }
}

void parse_rust_advanced(FILE *input, FILE *output) {
    char line[1024];
    int in_main = 0;
    
    // Header
    fprintf(output, "; Advanced Rust Compiler for PowerPC\n");
    fprintf(output, ".text\n");
    fprintf(output, ".align 2\n\n");
    
    while (fgets(line, sizeof(line), input)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        
        // Main function
        if (strstr(p, "fn main()")) {
            fprintf(output, ".globl _main\n");
            fprintf(output, "_main:\n");
            fprintf(output, "    mflr r0\n");
            fprintf(output, "    stw r0, 8(r1)\n");
            fprintf(output, "    stwu r1, -128(r1)\n");  // Larger stack frame
            fprintf(output, "    ; Save registers r14-r30\n");
            int i;
            for (i = 14; i <= 30; i++) {
                fprintf(output, "    stw r%d, %d(r1)\n", i, 56 + (i-14)*4);
            }
            in_main = 1;
            continue;
        }
        
        if (!in_main) continue;
        
        // Variable declaration: let x = 5;
        if (strncmp(p, "let ", 4) == 0) {
            char var_name[32];
            int value;
            if (sscanf(p + 4, "%s = %d", var_name, &value) == 2) {
                int reg = find_or_create_var(var_name);
                fprintf(output, "    li r%d, %d    ; %s = %d\n", reg, value, var_name, value);
            }
            // Handle arithmetic: let sum = x + y;
            else {
                char var_name2[32], expr[64];
                if (sscanf(p + 4, "%s = %[^;]", var_name2, expr) == 2) {
                    emit_arithmetic(output, expr);
                    int reg = find_or_create_var(var_name2);
                    fprintf(output, "    mr r%d, r3    ; %s = result\n", reg, var_name2);
                }
            }
        }
        // println! with format string
        else if (strstr(p, "println!(")) {
            char *quote1 = strchr(p, '"');
            if (quote1) {
                char *quote2 = strchr(quote1 + 1, '"');
                if (quote2) {
                    *quote2 = '\0';
                    strcpy(strings[string_count].content, quote1 + 1);
                    strings[string_count].id = string_count;
                    
                    // Check for {} format specifiers
                    if (strstr(strings[string_count].content, "{}")) {
                        // Find variable name after comma
                        char *comma = strchr(quote2 + 1, ',');
                        if (comma) {
                            char var_name[32];
                            sscanf(comma + 1, " %[^)]", var_name);
                            int reg = find_or_create_var(var_name);
                            
                            // Load format string
                            fprintf(output, "    lis r3, ha16(str_%d)\n", string_count);
                            fprintf(output, "    ori r3, r3, lo16(str_%d)\n", string_count);
                            fprintf(output, "    mr r4, r%d    ; arg = %s\n", reg, var_name);
                            fprintf(output, "    bl _printf\n");
                        }
                    } else {
                        // Simple string
                        fprintf(output, "    lis r3, ha16(str_%d)\n", string_count);
                        fprintf(output, "    ori r3, r3, lo16(str_%d)\n", string_count);
                        fprintf(output, "    bl _printf\n");
                    }
                    string_count++;
                }
            }
        }
        // Return statement
        else if (strncmp(p, "return ", 7) == 0) {
            emit_arithmetic(output, p + 7);
        }
        // End of main
        else if (*p == '}' && in_main) {
            // Restore registers
            int i;
            for (i = 14; i <= 30; i++) {
                fprintf(output, "    lwz r%d, %d(r1)\n", i, 56 + (i-14)*4);
            }
            fprintf(output, "    addi r1, r1, 128\n");
            fprintf(output, "    lwz r0, 8(r1)\n");
            fprintf(output, "    mtlr r0\n");
            fprintf(output, "    blr\n\n");
            in_main = 0;
        }
    }
    
    // Data section with strings
    if (string_count > 0) {
        fprintf(output, "\n.data\n");
        int i;
        for (i = 0; i < string_count; i++) {
            fprintf(output, "str_%d:\n", i);
            // Replace {} with %d for printf
            char *p = strings[i].content;
            fprintf(output, "    .ascii \"");
            while (*p) {
                if (*p == '{' && *(p+1) == '}') {
                    fprintf(output, "%%d");
                    p += 2;
                } else {
                    fputc(*p++, output);
                }
            }
            fprintf(output, "\\n\\0\"\n");
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("rustc-ppc advanced for PowerPC Darwin\n");
        printf("Supports: variables, arithmetic, println!\n");
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
    parse_rust_advanced(input, output);
    fclose(input);
    fclose(output);
    
    // Compile and link
    char cmd[512];
    sprintf(cmd, "gcc %s -o %s", asm_file, output_file);
    if (system(cmd) != 0) {
        fprintf(stderr, "Compilation failed. Assembly saved to %s\n", asm_file);
        return 1;
    }
    
    unlink(asm_file);
    printf("Success!\n");
    return 0;
}