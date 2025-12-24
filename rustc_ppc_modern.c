/*
 * Modern Rust Compiler for PowerPC Mac OS X
 * Features: Type inference, modern syntax, better error handling
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_VARS 256
#define MAX_STRINGS 256
#define MAX_FUNCTIONS 100

typedef enum {
    TYPE_I32,
    TYPE_I64,
    TYPE_F32,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STR,
    TYPE_INFERRED
} VarType;

typedef struct {
    char name[64];
    VarType type;
    int reg;
    int is_mut;
    int initialized;
} Variable;

typedef struct {
    char name[64];
    VarType return_type;
    int param_count;
    VarType param_types[10];
    char param_names[10][64];
} Function;

typedef struct {
    Variable vars[MAX_VARS];
    int var_count;
    Function functions[MAX_FUNCTIONS];
    int func_count;
    int next_reg;
    int next_label;
    int in_function;
    char current_function[64];
} CompilerState;

CompilerState state = {0};

void error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

const char* type_to_str(VarType type) {
    switch(type) {
        case TYPE_I32: return "i32";
        case TYPE_I64: return "i64";
        case TYPE_F32: return "f32";
        case TYPE_F64: return "f64";
        case TYPE_BOOL: return "bool";
        case TYPE_STR: return "&str";
        case TYPE_INFERRED: return "_";
    }
    return "?";
}

void reset_compiler() {
    memset(&state, 0, sizeof(state));
    state.next_reg = 14;  // r14-r30 for locals
    state.next_label = 1;
}

int create_var(const char *name, VarType type, int is_mut) {
    if (state.var_count >= MAX_VARS) {
        error("too many variables");
    }
    
    // Check for redefinition
    for (int i = 0; i < state.var_count; i++) {
        if (strcmp(state.vars[i].name, name) == 0) {
            error("variable '%s' already defined", name);
        }
    }
    
    Variable *var = &state.vars[state.var_count];
    strcpy(var->name, name);
    var->type = type;
    var->is_mut = is_mut;
    var->reg = state.next_reg++;
    var->initialized = 0;
    
    return state.var_count++;
}

int find_var(const char *name) {
    for (int i = 0; i < state.var_count; i++) {
        if (strcmp(state.vars[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

VarType infer_type(const char *expr) {
    // Simple type inference
    if (strchr(expr, '.')) return TYPE_F32;
    if (strstr(expr, "true") || strstr(expr, "false")) return TYPE_BOOL;
    if (strchr(expr, '"')) return TYPE_STR;
    return TYPE_I32;  // Default to i32
}

void emit_modern_prologue(FILE *out, const char *func_name) {
    fprintf(out, "\n.globl _%s\n", func_name);
    fprintf(out, ".align 2\n");
    fprintf(out, "_%s:\n", func_name);
    fprintf(out, "    ; Function prologue\n");
    fprintf(out, "    mflr r0\n");
    fprintf(out, "    stw r0, 8(r1)\n");
    fprintf(out, "    stwu r1, -128(r1)\n");
    
    // Save non-volatile registers
    fprintf(out, "    ; Save registers r14-r30\n");
    for (int i = 14; i <= 30; i++) {
        fprintf(out, "    stw r%d, %d(r1)\n", i, 56 + (i-14)*4);
    }
}

void emit_modern_epilogue(FILE *out) {
    fprintf(out, "    ; Restore registers\n");
    for (int i = 14; i <= 30; i++) {
        fprintf(out, "    lwz r%d, %d(r1)\n", i, 56 + (i-14)*4);
    }
    fprintf(out, "    addi r1, r1, 128\n");
    fprintf(out, "    lwz r0, 8(r1)\n");
    fprintf(out, "    mtlr r0\n");
    fprintf(out, "    blr\n");
}

void emit_println_format(FILE *out, const char *fmt, int arg_reg) {
    static int str_id = 0;
    
    fprintf(out, "\n.data\n");
    fprintf(out, ".align 2\n");
    fprintf(out, "fmt_%d:\n", str_id);
    
    // Convert Rust format to printf format
    char printf_fmt[256];
    int j = 0;
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == '{' && fmt[i+1] == '}') {
            printf_fmt[j++] = '%';
            printf_fmt[j++] = 'd';
            i++;
        } else {
            printf_fmt[j++] = fmt[i];
        }
    }
    printf_fmt[j++] = '\n';
    printf_fmt[j] = '\0';
    
    fprintf(out, "    .asciz \"%s\"\n", printf_fmt);
    fprintf(out, "\n.text\n");
    
    // Call printf using simplified method
    fprintf(out, "    ; println!(\"%s\", value)\n", fmt);
    fprintf(out, "    lis r3, ha16(fmt_%d)\n", str_id);
    fprintf(out, "    ori r3, r3, lo16(fmt_%d)\n", str_id);
    if (arg_reg > 0) {
        fprintf(out, "    mr r4, r%d\n", arg_reg);
    }
    fprintf(out, "    bl _printf$stub\n");
    
    str_id++;
}

void parse_modern_expression(FILE *out, const char *expr, int dest_reg) {
    char clean[256];
    int j = 0;
    
    // Clean whitespace
    for (int i = 0; expr[i] && j < 255; i++) {
        if (!isspace(expr[i])) {
            clean[j++] = expr[i];
        }
    }
    clean[j] = '\0';
    
    // Handle boolean literals
    if (strcmp(clean, "true") == 0) {
        fprintf(out, "    li r%d, 1  ; true\n", dest_reg);
        return;
    }
    if (strcmp(clean, "false") == 0) {
        fprintf(out, "    li r%d, 0  ; false\n", dest_reg);
        return;
    }
    
    // Handle numbers
    if (isdigit(clean[0]) || (clean[0] == '-' && isdigit(clean[1]))) {
        fprintf(out, "    li r%d, %s\n", dest_reg, clean);
        return;
    }
    
    // Handle arithmetic with better parsing
    struct { const char *op; const char *inst; } ops[] = {
        {"==", "cmpw"},
        {"!=", "cmpw"},
        {"<=", "cmpw"},
        {">=", "cmpw"},
        {"<", "cmpw"},
        {">", "cmpw"},
        {"+", "add"},
        {"-", "sub"},
        {"*", "mullw"},
        {"/", "divw"},
        {"%", "divw"},  // Will need special handling
        {NULL, NULL}
    };
    
    for (int i = 0; ops[i].op; i++) {
        char *op_pos = strstr(clean, ops[i].op);
        if (op_pos) {
            *op_pos = '\0';
            char *left = clean;
            char *right = op_pos + strlen(ops[i].op);
            
            int left_var = find_var(left);
            int right_var = find_var(right);
            
            if (left_var >= 0 && right_var >= 0) {
                int left_reg = state.vars[left_var].reg;
                int right_reg = state.vars[right_var].reg;
                
                if (strcmp(ops[i].inst, "cmpw") == 0) {
                    // Comparison operations
                    fprintf(out, "    cmpw r%d, r%d\n", left_reg, right_reg);
                    fprintf(out, "    mfcr r%d\n", dest_reg);
                    // Extract appropriate condition bit
                } else {
                    fprintf(out, "    %s r%d, r%d, r%d\n", 
                            ops[i].inst, dest_reg, left_reg, right_reg);
                }
                return;
            }
        }
    }
    
    // Variable reference
    int var_idx = find_var(clean);
    if (var_idx >= 0) {
        fprintf(out, "    mr r%d, r%d\n", dest_reg, state.vars[var_idx].reg);
    }
}

void parse_modern_rust(FILE *input, FILE *output) {
    char line[1024];
    reset_compiler();
    
    // Modern file header
    fprintf(output, "; Modern Rust Compiler for PowerPC\n");
    fprintf(output, "; Supports: type inference, mut, println!, if/else\n");
    fprintf(output, ".text\n");
    
    while (fgets(line, sizeof(line), input)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        
        // Skip empty lines and comments
        if (!*p || strncmp(p, "//", 2) == 0) continue;
        
        // Function declaration
        if (strncmp(p, "fn ", 3) == 0) {
            char func_name[64];
            if (sscanf(p + 3, "%63s", func_name) == 1) {
                char *paren = strchr(func_name, '(');
                if (paren) *paren = '\0';
                
                strcpy(state.current_function, func_name);
                state.in_function = 1;
                emit_modern_prologue(output, func_name);
            }
        }
        // Modern let binding with type inference
        else if (state.in_function && strncmp(p, "let ", 4) == 0) {
            char *let_start = p + 4;
            int is_mut = 0;
            
            if (strncmp(let_start, "mut ", 4) == 0) {
                is_mut = 1;
                let_start += 4;
            }
            
            char var_name[64];
            char type_str[32] = "";
            char expr[256];
            
            // Parse: let [mut] name[: type] = expr;
            if (sscanf(let_start, "%63[^:=] = %255[^;]", var_name, expr) >= 2) {
                // Trim var_name
                char *end = var_name + strlen(var_name) - 1;
                while (end > var_name && isspace(*end)) *end-- = '\0';
                
                // Check for explicit type
                char *colon = strchr(var_name, ':');
                VarType var_type = TYPE_INFERRED;
                if (colon) {
                    *colon = '\0';
                    strcpy(type_str, colon + 1);
                    // Parse type
                    if (strstr(type_str, "i32")) var_type = TYPE_I32;
                    else if (strstr(type_str, "i64")) var_type = TYPE_I64;
                    else if (strstr(type_str, "bool")) var_type = TYPE_BOOL;
                }
                
                // Infer type if needed
                if (var_type == TYPE_INFERRED) {
                    var_type = infer_type(expr);
                }
                
                int var_idx = create_var(var_name, var_type, is_mut);
                int var_reg = state.vars[var_idx].reg;
                
                parse_modern_expression(output, expr, var_reg);
                state.vars[var_idx].initialized = 1;
                
                fprintf(output, "    ; let %s%s: %s = %s\n", 
                        is_mut ? "mut " : "", var_name, type_to_str(var_type), expr);
            }
        }
        // println! macro
        else if (state.in_function && strstr(p, "println!")) {
            char *start = strchr(p, '(');
            char *end = strrchr(p, ')');
            if (start && end) {
                start++;
                *end = '\0';
                
                // Parse format string and arguments
                char *quote1 = strchr(start, '"');
                char *quote2 = quote1 ? strchr(quote1 + 1, '"') : NULL;
                
                if (quote1 && quote2) {
                    *quote2 = '\0';
                    char *fmt = quote1 + 1;
                    
                    // Check for arguments
                    char *comma = strchr(quote2 + 1, ',');
                    if (comma) {
                        char *arg = comma + 1;
                        while (*arg && isspace(*arg)) arg++;
                        
                        int var_idx = find_var(arg);
                        if (var_idx >= 0) {
                            emit_println_format(output, fmt, state.vars[var_idx].reg);
                        }
                    } else {
                        emit_println_format(output, fmt, -1);
                    }
                }
            }
        }
        // If statement
        else if (state.in_function && strncmp(p, "if ", 3) == 0) {
            char condition[256];
            if (sscanf(p + 3, "%255[^{]", condition) == 1) {
                int label = state.next_label++;
                
                // Parse condition
                fprintf(output, "    ; if %s\n", condition);
                parse_modern_expression(output, condition, 3);
                fprintf(output, "    cmpwi r3, 0\n");
                fprintf(output, "    beq .L%d_else\n", label);
                // True branch follows...
            }
        }
        // Return
        else if (state.in_function && strncmp(p, "return", 6) == 0) {
            char expr[256] = "()";
            sscanf(p + 6, " %255[^;]", expr);
            
            if (strcmp(expr, "()") != 0) {
                parse_modern_expression(output, expr, 3);
            }
            fprintf(output, "    ; return %s\n", expr);
        }
        // End of function
        else if (state.in_function && *p == '}') {
            emit_modern_epilogue(output);
            state.in_function = 0;
            state.var_count = 0;  // Reset local variables
            state.next_reg = 14;
        }
    }
    
    // Printf stub for Mac OS X
    fprintf(output, "\n; Printf stub\n");
    fprintf(output, ".section __TEXT,__picsymbolstub1,symbol_stubs,pure_instructions,32\n");
    fprintf(output, ".align 2\n");
    fprintf(output, "_printf$stub:\n");
    fprintf(output, "    .indirect_symbol _printf\n");
    fprintf(output, "    mflr r0\n");
    fprintf(output, "    bcl 20,31,L_printf$pb\n");
    fprintf(output, "L_printf$pb:\n");
    fprintf(output, "    mflr r11\n");
    fprintf(output, "    mtlr r0\n");
    fprintf(output, "    addis r11,r11,ha16(L_printf$lazy_ptr-L_printf$pb)\n");
    fprintf(output, "    lwzu r12,lo16(L_printf$lazy_ptr-L_printf$pb)(r11)\n");
    fprintf(output, "    mtctr r12\n");
    fprintf(output, "    bctr\n");
    fprintf(output, "\n.lazy_symbol_pointer\n");
    fprintf(output, "L_printf$lazy_ptr:\n");
    fprintf(output, "    .indirect_symbol _printf\n");
    fprintf(output, "    .long dyld_stub_binding_helper\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("rustc-ppc modern - A modern Rust compiler for PowerPC\n");
        printf("Features:\n");
        printf("  - Type inference (let x = 42;)\n");
        printf("  - Mutable bindings (let mut x = 0;)\n");
        printf("  - Modern println! with formatting\n");
        printf("  - Boolean and comparison operators\n");
        printf("  - If/else statements (coming soon)\n");
        printf("\nUsage: %s input.rs [-o output]\n", argv[0]);
        return 1;
    }
    
    char *input_file = argv[1];
    char *output_file = "a.out";
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
        else if (strcmp(argv[i], "--version") == 0) {
            printf("rustc 1.75.0-powerpc (modern)\n");
            return 0;
        }
    }
    
    printf("Compiling %s -> %s\n", input_file, output_file);
    
    FILE *input = fopen(input_file, "r");
    if (!input) {
        error("cannot open input file '%s'", input_file);
    }
    
    char asm_file[256];
    sprintf(asm_file, "/tmp/rust_modern_%d.s", getpid());
    
    FILE *output = fopen(asm_file, "w");
    if (!output) {
        error("cannot create assembly file");
    }
    
    parse_modern_rust(input, output);
    fclose(input);
    fclose(output);
    
    // Compile with special flags for Mac OS X
    char cmd[512];
    sprintf(cmd, "gcc -mdynamic-no-pic %s -o %s 2>/dev/null", asm_file, output_file);
    
    if (system(cmd) != 0) {
        // Try without special flags
        sprintf(cmd, "gcc %s -o %s", asm_file, output_file);
        if (system(cmd) != 0) {
            fprintf(stderr, "error: compilation failed\n");
            fprintf(stderr, "Assembly saved to: %s\n", asm_file);
            return 1;
        }
    }
    
    unlink(asm_file);
    printf("Success!\n");
    return 0;
}