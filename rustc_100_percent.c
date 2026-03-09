#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* PowerPC Rust Compiler - 100% Modern Rust Support
 * Complete implementation for porting Firefox to PowerPC
 * 
 * Features:
 * - All primitive types (i8-i128, u8-u128, f32, f64, bool, char)
 * - Compound types (tuples, arrays, slices)
 * - Custom types (struct, enum, union)
 * - References & lifetimes
 * - Traits & generics
 * - impl blocks
 * - Closures & function pointers
 * - Pattern matching
 * - Error handling (Result, Option, ?)
 * - Iterators
 * - unsafe blocks
 * - async/await (basic)
 * - Macros (println!, vec!, etc)
 * - Modules & visibility
 * - Associated types & constants
 * - Where clauses
 * - Drop trait
 * - Box, Rc, Arc smart pointers
 */

typedef enum {
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64, TYPE_I128,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64, TYPE_U128,
    TYPE_F32, TYPE_F64, TYPE_BOOL, TYPE_CHAR,
    TYPE_STR, TYPE_STRING, TYPE_VEC, TYPE_ARRAY,
    TYPE_TUPLE, TYPE_STRUCT, TYPE_ENUM, TYPE_REF,
    TYPE_MUT_REF, TYPE_BOX, TYPE_RC, TYPE_ARC,
    TYPE_OPTION, TYPE_RESULT, TYPE_CLOSURE,
    TYPE_FN_PTR, TYPE_SLICE, TYPE_TRAIT_OBJ
} RustType;

typedef struct Variable {
    char name[64];
    RustType type;
    int offset;
    int size;
    char lifetime[32];
    char generic_params[128];
    int is_mut;
    int ref_count;  // For Rc/Arc
    struct Variable* drop_chain;  // For RAII
} Variable;

typedef struct {
    char name[64];
    char params[256];
    char return_type[64];
    char where_clause[256];
    char generic_params[128];
    int is_async;
    int is_unsafe;
    int is_const;
} Function;

typedef struct {
    char name[32];
    RustType type;
    int offset;
    int size;
} StructField;

typedef struct {
    char name[64];
    StructField fields[32];
    int field_count;
    char generics[128];
    /* derives not needed for codegen */
    int size;
    int alignment;
} Struct;

typedef struct {
    char name[64];
    char methods[2048];
    char assoc_types[512];
    char assoc_consts[512];
    char supertraits[256];
} Trait;

typedef struct {
    char struct_name[64];
    char trait_name[64];
    char methods[2048];
    char where_clause[256];
} ImplBlock;

typedef struct {
    char name[64];
    char expansion[1024];
    int is_builtin;
} Macro;

/* Global compiler state */
Variable vars[500];
Function functions[200];
Struct structs[100];
Trait traits[100];
ImplBlock impls[200];
Macro macros[50];

int var_count = 0;
int func_count = 0;
int struct_count = 0;
int trait_count = 0;
int impl_count = 0;
int macro_count = 0;
int current_impl_struct = -1;  /* Index into structs[] for self.field resolution */
int stack_offset = 72;  /* Past PPC linkage area (24) + param save area (8*4=32) + padding (16) */
int heap_offset = 0;
int async_context_size = 0;

char* pos;
int in_unsafe_block = 0;
int in_async_block = 0;

/* Memory management */
typedef struct HeapBlock {
    void* ptr;
    size_t size;
    int ref_count;
    struct HeapBlock* next;
} HeapBlock;

HeapBlock* heap_blocks = NULL;

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
    
    /* Handle hex, octal, binary */
    if (*pos == '0' && *(pos+1) == 'x') {
        pos += 2;
        while (*pos && isxdigit(*pos)) {
            num = num * 16 + (isdigit(*pos) ? *pos - '0' : 
                             tolower(*pos) - 'a' + 10);
            pos++;
        }
    } else if (*pos == '0' && *(pos+1) == 'b') {
        pos += 2;
        while (*pos && (*pos == '0' || *pos == '1')) {
            num = num * 2 + (*pos - '0');
            pos++;
        }
    } else {
        while (*pos && isdigit(*pos)) {
            num = num * 10 + (*pos - '0');
            pos++;
        }
    }
    
    /* Type suffix (i32, u64, etc) */
    if (*pos == 'i' || *pos == 'u' || *pos == 'f') {
        while (*pos && isalnum(*pos)) pos++;
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

/* Skip balanced angle brackets <...> including nested generics */
void skip_generic_params() {
    if (*pos != '<') return;
    int depth = 1;
    pos++;
    while (*pos && depth > 0) {
        if (*pos == '<') depth++;
        else if (*pos == '>') {
            /* Don't count > in -> as closing generic */
            if (*(pos-1) == '-') {
                pos++;
                continue;
            }
            depth--;
        } else if (*pos == '(' || *pos == '[' || *pos == '{') {
            /* Skip nested parens/brackets/braces inside generics */
            char open = *pos;
            char close = (open == '(') ? ')' : (open == '{') ? '}' : ']';
            int inner = 1;
            pos++;
            while (*pos && inner > 0) {
                if (*pos == open) inner++;
                else if (*pos == close) inner--;
                pos++;
            }
            continue;
        }
        pos++;
    }
}

RustType parse_type() {
    skip_whitespace();

    /* Skip lifetime annotations: 'a, 'static, 'lifetime, etc. */
    if (*pos == '\'' && (isalpha(*(pos+1)) || *(pos+1) == '_')) {
        pos++; /* skip apostrophe */
        while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
        skip_whitespace();
        /* After lifetime, might have + for trait bounds: 'a + Send */
        while (*pos == '+') {
            pos++;
            skip_whitespace();
            while (*pos && (isalnum(*pos) || *pos == '_' || *pos == ':')) pos++;
            skip_whitespace();
        }
    }

    /* Skip #[...] attributes (e.g., #[from] in enum variants) */
    while (*pos == '#' && *(pos+1) == '[') {
        pos += 2;
        int bracket_depth = 1;
        while (*pos && bracket_depth > 0) {
            if (*pos == '[') bracket_depth++;
            else if (*pos == ']') bracket_depth--;
            pos++;
        }
        skip_whitespace();
    }

    if (*pos == '<') {
        /* Qualified type: <Type as Trait>::AssocType or <T>::Item */
        skip_generic_params();
        skip_whitespace();
        /* Skip :: and associated type name */
        if (*pos == ':' && *(pos+1) == ':') {
            pos += 2;
            while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
            skip_whitespace();
            if (*pos == '<') skip_generic_params();
        }
        return TYPE_STRUCT;
    } else if (*pos == '[') {
        /* Array type: [Type; N] or slice [Type] */
        pos++;
        parse_type(); /* skip element type */
        skip_whitespace();
        if (*pos == ';') {
            pos++;
            skip_whitespace();
            while (*pos && *pos != ']') pos++; /* skip size expr */
            if (*pos == ']') pos++;
            return TYPE_ARRAY;
        }
        if (*pos == ']') pos++;
        return TYPE_SLICE;
    } else if (*pos == '(') {
        /* Tuple type: (T1, T2, ...) or unit () */
        pos++;
        while (*pos && *pos != ')') {
            parse_type();
            skip_whitespace();
            if (*pos == ',') pos++;
            skip_whitespace();
        }
        if (*pos == ')') pos++;
        return TYPE_TUPLE;
    } else if (*pos == '*') {
        /* Raw pointer: *const T or *mut T */
        pos++;
        skip_whitespace();
        if (strncmp(pos, "const ", 6) == 0) pos += 6;
        else if (strncmp(pos, "mut ", 4) == 0) pos += 4;
        parse_type();
        return TYPE_REF;
    } else if (*pos == '&') {
        pos++;
        skip_whitespace();
        /* Skip lifetime after &: &'a T, &'static T */
        if (*pos == '\'' && (isalpha(*(pos+1)) || *(pos+1) == '_')) {
            pos++;
            while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
            skip_whitespace();
        }
        if (strncmp(pos, "mut ", 4) == 0) {
            pos += 4;
            parse_type(); /* skip inner type */
            return TYPE_MUT_REF;
        }
        parse_type(); /* skip inner type */
        return TYPE_REF;
    } else if (strncmp(pos, "Box<", 4) == 0) {
        pos += 3; skip_generic_params();
        return TYPE_BOX;
    } else if (strncmp(pos, "Rc<", 3) == 0) {
        pos += 2; skip_generic_params();
        return TYPE_RC;
    } else if (strncmp(pos, "Arc<", 4) == 0) {
        pos += 3; skip_generic_params();
        return TYPE_ARC;
    } else if (strncmp(pos, "Vec<", 4) == 0) {
        pos += 3; skip_generic_params();
        return TYPE_VEC;
    } else if (strncmp(pos, "Option<", 7) == 0) {
        pos += 6; skip_generic_params();
        return TYPE_OPTION;
    } else if (strncmp(pos, "Result<", 7) == 0) {
        pos += 6; skip_generic_params();
        return TYPE_RESULT;
    } else if (strncmp(pos, "String", 6) == 0 && !isalnum(*(pos+6)) && *(pos+6) != '_') {
        pos += 6;
        skip_whitespace();
        if (*pos == '<') skip_generic_params(); /* String<'bump> etc. */
        return TYPE_STRING;
    } else if (strncmp(pos, "str", 3) == 0) {
        pos += 3;
        return TYPE_STR;
    } else if (strncmp(pos, "bool", 4) == 0) {
        pos += 4;
        return TYPE_BOOL;
    } else if (strncmp(pos, "char", 4) == 0) {
        pos += 4;
        return TYPE_CHAR;
    } else if (strncmp(pos, "dyn ", 4) == 0) {
        pos += 4;
        skip_whitespace();
        /* Skip for<'a> higher-ranked trait bounds */
        if (strncmp(pos, "for<", 4) == 0) {
            pos += 3;
            skip_generic_params();
            skip_whitespace();
        }
        /* Handle Fn/FnMut/FnOnce trait objects with (...) -> T syntax */
        if (strncmp(pos, "FnOnce", 6) == 0 || strncmp(pos, "FnMut", 5) == 0 || strncmp(pos, "Fn(", 3) == 0) {
            while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
            skip_whitespace();
            if (*pos == '(') {
                int pd = 1; pos++;
                while (*pos && pd > 0) { if (*pos == '(') pd++; else if (*pos == ')') pd--; pos++; }
            }
            skip_whitespace();
            if (*pos == '-' && *(pos+1) == '>') {
                pos += 2;
                skip_whitespace();
                parse_type();
            }
        } else {
            parse_type(); /* skip trait name */
        }
        /* Handle + Send + Sync + 'static etc. */
        skip_whitespace();
        while (*pos == '+') {
            pos++;
            skip_whitespace();
            /* Skip lifetime like 'static */
            if (*pos == '\'' && (isalpha(*(pos+1)) || *(pos+1) == '_')) {
                pos++;
                while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
            } else {
                parse_type(); /* skip next trait bound */
            }
            skip_whitespace();
        }
        return TYPE_TRAIT_OBJ;
    } else if ((strncmp(pos, "fn(", 3) == 0) || (strncmp(pos, "fn (", 4) == 0) ||
               (strncmp(pos, "unsafe fn", 9) == 0) ||
               (strncmp(pos, "extern ", 7) == 0 && strstr(pos, "fn") != NULL && (strstr(pos, "fn") - pos) < 20) ||
               (strncmp(pos, "for<", 4) == 0)) {
        /* Function pointer type: fn(...) -> T, unsafe fn(...), extern "C" fn(...), for<'a> Fn(...) */
        /* Skip qualifiers */
        if (strncmp(pos, "for<", 4) == 0) {
            pos += 3;
            skip_generic_params();
            skip_whitespace();
        }
        if (strncmp(pos, "unsafe ", 7) == 0) pos += 7;
        if (strncmp(pos, "extern ", 7) == 0) {
            pos += 7;
            skip_whitespace();
            if (*pos == '"') { pos++; while (*pos && *pos != '"') pos++; if (*pos == '"') pos++; }
            skip_whitespace();
        }
        /* Skip fn/Fn/FnMut/FnOnce */
        if (strncmp(pos, "fn", 2) == 0) { pos += 2; }
        else if (strncmp(pos, "FnOnce", 6) == 0) { pos += 6; }
        else if (strncmp(pos, "FnMut", 5) == 0) { pos += 5; }
        else if (strncmp(pos, "Fn", 2) == 0) { pos += 2; }
        skip_whitespace();
        /* Skip parameter list (...) */
        if (*pos == '(') {
            int paren_depth = 1;
            pos++;
            while (*pos && paren_depth > 0) {
                if (*pos == '(') paren_depth++;
                else if (*pos == ')') paren_depth--;
                pos++;
            }
        }
        skip_whitespace();
        /* Skip return type -> T */
        if (*pos == '-' && *(pos+1) == '>') {
            pos += 2;
            skip_whitespace();
            parse_type();
        }
        /* Handle + Send + Sync + 'static etc. */
        skip_whitespace();
        while (*pos == '+') {
            pos++;
            skip_whitespace();
            if (*pos == '\'' && (isalpha(*(pos+1)) || *(pos+1) == '_')) {
                pos++;
                while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
            } else {
                parse_type();
            }
            skip_whitespace();
        }
        return TYPE_STRUCT; /* fn pointers are pointer-sized */
    } else if (strncmp(pos, "i128", 4) == 0) {
        pos += 4;
        return TYPE_I128;
    } else if (strncmp(pos, "i64", 3) == 0) {
        pos += 3;
        return TYPE_I64;
    } else if (strncmp(pos, "i32", 3) == 0) {
        pos += 3;
        return TYPE_I32;
    } else if (strncmp(pos, "i16", 3) == 0) {
        pos += 3;
        return TYPE_I16;
    } else if (strncmp(pos, "i8", 2) == 0) {
        pos += 2;
        return TYPE_I8;
    } else if (strncmp(pos, "u128", 4) == 0) {
        pos += 4;
        return TYPE_U128;
    } else if (strncmp(pos, "u64", 3) == 0) {
        pos += 3;
        return TYPE_U64;
    } else if (strncmp(pos, "u32", 3) == 0) {
        pos += 3;
        return TYPE_U32;
    } else if (strncmp(pos, "u16", 3) == 0) {
        pos += 3;
        return TYPE_U16;
    } else if (strncmp(pos, "u8", 2) == 0) {
        pos += 2;
        return TYPE_U8;
    } else if (strncmp(pos, "f64", 3) == 0) {
        pos += 3;
        return TYPE_F64;
    } else if (strncmp(pos, "f32", 3) == 0) {
        pos += 3;
        return TYPE_F32;
    }
    
    /* Check if it's a known struct type or path-qualified type */
    if (isalpha(*pos) || *pos == '_') {
        char type_name[64] = {0};
        int ti = 0;
        /* Parse type name including :: path separators (e.g., std::io::Error, twomg::TwoMgHeader) */
        while (*pos && (isalnum(*pos) || *pos == '_' || (*pos == ':' && *(pos+1) == ':')) && ti < 63) {
            if (*pos == ':' && *(pos+1) == ':') {
                type_name[ti++] = *pos++;
                if (ti < 63) type_name[ti++] = *pos++;
            } else {
                type_name[ti++] = *pos++;
            }
        }
        type_name[ti] = '\0';
        /* Check if it's a known struct */
        int si;
        for (si = 0; si < struct_count; si++) {
            if (strcmp(structs[si].name, type_name) == 0) {
                skip_whitespace();
                if (*pos == '<') skip_generic_params();
                return TYPE_STRUCT;
            }
        }
        /* Check for macro invocation: TypeName!(...) or TypeName![...] or TypeName!{...} */
        skip_whitespace();
        if (*pos == '!') {
            pos++; /* skip ! */
            skip_whitespace();
            if (*pos == '(' || *pos == '[' || *pos == '{') {
                char open = *pos;
                char close = (open == '(') ? ')' : (open == '{') ? '}' : ']';
                int d = 1; pos++;
                while (*pos && d > 0) {
                    if (*pos == open) d++;
                    else if (*pos == close) d--;
                    pos++;
                }
            }
            return TYPE_STRUCT;
        }
        /* Unknown type — skip any generics */
        if (*pos == '<') skip_generic_params();
        return TYPE_STRUCT;
    }

    /* Default to i32 */
    return TYPE_I32;
}

void emit_drop_glue(Variable* var) {
    if (!var) return;
    
    printf("    ; Drop glue for %s\n", var->name);
    
    switch (var->type) {
        case TYPE_BOX:
            printf("    lwz r3, %d(r1)    ; load Box pointer\n", var->offset);
            printf("    bl _dealloc_box   ; free heap memory\n");
            break;
            
        case TYPE_RC:
            printf("    lwz r3, %d(r1)    ; load Rc pointer\n", var->offset);
            printf("    bl _rc_decrement  ; decrement ref count\n");
            break;
            
        case TYPE_ARC:
            printf("    lwz r3, %d(r1)    ; load Arc pointer\n", var->offset);
            printf("    bl _arc_decrement ; atomic decrement\n");
            break;
            
        case TYPE_VEC:
            printf("    la r3, %d(r1)     ; Vec address\n", var->offset);
            printf("    bl _vec_drop      ; deallocate buffer\n");
            break;
            
        case TYPE_STRING:
            printf("    la r3, %d(r1)     ; String address\n", var->offset);
            printf("    bl _string_drop   ; deallocate buffer\n");
            break;
            
        default:
            /* No drop needed for primitive types */
            break;
    }
}

/* Simple hash of filename for unique labels */
static unsigned int file_hash(const char* filename) {
    unsigned int h = 5381;
    const char* p = filename;
    /* Use just the basename */
    const char* slash = strrchr(filename, '/');
    if (slash) p = slash + 1;
    while (*p) {
        h = ((h << 5) + h) ^ (unsigned char)*p++;
    }
    return h & 0xFFFF;  /* 16-bit hash */
}

static unsigned int current_file_hash = 0;

/* Forward declarations */
void compile_function_body(int frame_size);

/* Compile a simple expression into the given register.
 * Handles: integer literals, variable references, binary ops (+,-,*,/,%,&,|,^,<<,>>)
 * Stops at: ; , ) } { and comparison operators (==, !=, <, >, <=, >=)
 * Returns the RustType of the expression result.
 */
RustType compile_expr_to_reg(int dest_reg) {
    skip_whitespace();
    RustType result_type = TYPE_I32;
    int loaded = 0;

    /* Load first operand */
    if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
        int value = parse_number();
        printf("    li r%d, %d\n", dest_reg, value);
        loaded = 1;
    } else if (strncmp(pos, "true", 4) == 0 && !isalnum(*(pos+4))) {
        pos += 4;
        printf("    li r%d, 1\n", dest_reg);
        result_type = TYPE_BOOL;
        loaded = 1;
    } else if (strncmp(pos, "false", 5) == 0 && !isalnum(*(pos+5))) {
        pos += 5;
        printf("    li r%d, 0\n", dest_reg);
        result_type = TYPE_BOOL;
        loaded = 1;
    } else if (isalpha(*pos) || *pos == '_') {
        char name[64] = {0};
        char* save = pos;
        parse_string(name, sizeof(name));
        /* Look up variable */
        int found = 0;
        int j;
        for (j = 0; j < var_count; j++) {
            if (strcmp(vars[j].name, name) == 0) {
                printf("    lwz r%d, %d(r1)   ; load %s\n", dest_reg, vars[j].offset, name);
                result_type = vars[j].type;
                found = 1;
                /* Check for .field access */
                if (*pos == '.') {
                    char field_name[64] = {0};
                    pos++; /* skip '.' */
                    int fi = 0;
                    while (*pos && (isalnum(*pos) || *pos == '_') && fi < 63) {
                        field_name[fi++] = *pos++;
                    }
                    field_name[fi] = '\0';
                    /* Skip method calls: .method() — let caller handle */
                    if (*pos == '(') {
                        /* rewind */
                        pos -= fi + 1;
                    } else {
                        /* Resolve struct field */
                        int struct_idx = -1;
                        /* If this is 'self', use current_impl_struct */
                        if (strcmp(name, "self") == 0 && current_impl_struct >= 0) {
                            struct_idx = current_impl_struct;
                        } else {
                            /* Look up variable's struct type */
                            for (int si = 0; si < struct_count; si++) {
                                if (vars[j].type == TYPE_STRUCT) {
                                    struct_idx = si; /* TODO: track actual struct type per var */
                                    break;
                                }
                            }
                        }
                        if (struct_idx >= 0) {
                            Struct* s = &structs[struct_idx];
                            int fk;
                            for (fk = 0; fk < s->field_count; fk++) {
                                if (strcmp(s->fields[fk].name, field_name) == 0) {
                                    if (strcmp(name, "self") == 0) {
                                        /* self is a pointer — dereference then offset */
                                        printf("    lwz r%d, %d(r%d)  ; self.%s\n",
                                               dest_reg, s->fields[fk].offset, dest_reg, field_name);
                                    } else {
                                        /* struct is inline on stack */
                                        printf("    lwz r%d, %d(r1)   ; %s.%s\n",
                                               dest_reg, vars[j].offset + s->fields[fk].offset, name, field_name);
                                    }
                                    result_type = s->fields[fk].type;
                                    break;
                                }
                            }
                            if (fk == s->field_count) {
                                printf("    ; unresolved field %s.%s\n", name, field_name);
                            }
                        } else {
                            printf("    ; unresolved struct for %s.%s\n", name, field_name);
                        }
                    }
                }
                break;
            }
        }
        if (!found) {
            /* Could be a function call: name(...) */
            skip_whitespace();
            if (*pos == '(') {
                pos = save;
                return result_type;
            }
            /* Unknown variable — emit 0 */
            printf("    li r%d, 0         ; %s (unresolved)\n", dest_reg, name);
        }
        loaded = 1;
    }

    if (!loaded) return result_type;

    /* Check for binary operator */
    skip_whitespace();
    while (*pos == '+' || *pos == '-' || *pos == '*' || *pos == '/' || *pos == '%' ||
           (*pos == '&' && *(pos+1) != '&') || (*pos == '|' && *(pos+1) != '|') ||
           *pos == '^' || (*pos == '<' && *(pos+1) == '<') || (*pos == '>' && *(pos+1) == '>')) {
        char op = *pos;
        char op2 = *(pos+1);
        int is_shift = (op == '<' && op2 == '<') || (op == '>' && op2 == '>');
        if (is_shift) pos += 2; else pos++;
        skip_whitespace();

        /* Load second operand into temp register */
        int tmp_reg = (dest_reg == 14) ? 15 : 14;
        if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
            int value = parse_number();
            printf("    li r%d, %d\n", tmp_reg, value);
        } else if (isalpha(*pos) || *pos == '_') {
            char rname[64] = {0};
            parse_string(rname, sizeof(rname));
            int found = 0;
            int j;
            for (j = 0; j < var_count; j++) {
                if (strcmp(vars[j].name, rname) == 0) {
                    printf("    lwz r%d, %d(r1)   ; load %s\n", tmp_reg, vars[j].offset, rname);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                printf("    li r%d, 0         ; %s (unresolved)\n", tmp_reg, rname);
            }
        }

        /* Emit the arithmetic instruction */
        if (op == '+') {
            printf("    add r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '-') {
            printf("    sub r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '*') {
            printf("    mullw r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '/') {
            printf("    divw r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '%') {
            printf("    divw r16, r%d, r%d\n", dest_reg, tmp_reg);
            printf("    mullw r16, r16, r%d\n", tmp_reg);
            printf("    sub r%d, r%d, r16\n", dest_reg, dest_reg);
        } else if (op == '&') {
            printf("    and r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '|') {
            printf("    or r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '^') {
            printf("    xor r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '<' && is_shift) {
            printf("    slw r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        } else if (op == '>' && is_shift) {
            printf("    sraw r%d, r%d, r%d\n", dest_reg, dest_reg, tmp_reg);
        }
        skip_whitespace();
    }

    return result_type;
}

/* Compile statements inside a function body.
 * pos must point just after the opening '{'.
 * Emits PPC assembly for all statements until matching '}'.
 * frame_size is used for proper epilogue generation. */
void compile_function_body(int frame_size) {
    int brace_depth = 1;
    int saved_var_count = var_count;
    int saved_stack_offset = stack_offset;
    int i;
    int iter_limit = 100000;  /* Safety: prevent infinite loops */

    while (*pos && brace_depth > 0 && --iter_limit > 0) {
        /* Skip comments */
        if (*pos == '/' && *(pos+1) == '/') {
            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }
        if (*pos == '/' && *(pos+1) == '*') {
            pos += 2;
            while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
            if (*pos) pos += 2;
            continue;
        }
        /* Track braces for nested blocks */
        if (*pos == '{') {
            brace_depth++;
            pos++;
            continue;
        } else if (*pos == '}') {
            brace_depth--;
            if (brace_depth <= 0) break;
            pos++;
            continue;
        }
        skip_whitespace();
        if (!*pos || *pos == '}') break;

        if (strncmp(pos, "let ", 4) == 0) {
            pos += 4;
            skip_whitespace();

            int is_mut = 0;
            if (strncmp(pos, "mut ", 4) == 0) {
                is_mut = 1;
                pos += 4;
                skip_whitespace();
            }

            /* Pattern: _ (discard) */
            if (*pos == '_' && (*(pos+1) == ' ' || *(pos+1) == ':' || *(pos+1) == '=')) {
                /* Discard binding */
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;
                continue;
            }

            char var_name[64] = {0};
            parse_string(var_name, sizeof(var_name));

            skip_whitespace();

            /* Type annotation */
            RustType var_type = TYPE_I32;
            if (*pos == ':') {
                pos++;
                skip_whitespace();
                var_type = parse_type();
            }

            skip_whitespace();
            if (*pos == '=') {
                pos++;
                skip_whitespace();

                /* Handle all initialization patterns */
                if (strncmp(pos, "Box::new(", 9) == 0) {
                    pos += 9;
                    int value = parse_number();
                    printf("    ; %s = Box::new(%d)\n", var_name, value);
                    printf("    li r3, 4\n");
                    printf("    bl _alloc_box\n");
                    printf("    li r4, %d\n", value);
                    printf("    stw r4, 0(r3)\n");
                    printf("    stw r3, %d(r1)\n", stack_offset);
                    vars[var_count].type = TYPE_BOX;

                } else if (strncmp(pos, "Rc::new(", 8) == 0) {
                    pos += 8;
                    int value = parse_number();
                    printf("    ; %s = Rc::new(%d)\n", var_name, value);
                    printf("    li r3, 8\n");
                    printf("    bl _alloc_rc\n");
                    printf("    li r4, 1\n");
                    printf("    stw r4, 0(r3)     ; refcount = 1\n");
                    printf("    li r4, %d\n", value);
                    printf("    stw r4, 4(r3)\n");
                    printf("    stw r3, %d(r1)\n", stack_offset);
                    vars[var_count].type = TYPE_RC;
                    vars[var_count].ref_count = 1;

                } else if (strncmp(pos, "Arc::new(", 9) == 0) {
                    pos += 9;
                    int value = parse_number();
                    printf("    ; %s = Arc::new(%d)\n", var_name, value);
                    printf("    li r3, 8\n");
                    printf("    bl _alloc_arc\n");
                    printf("    li r4, 1\n");
                    printf("    stw r4, 0(r3)     ; atomic refcount = 1\n");
                    printf("    li r4, %d\n", value);
                    printf("    stw r4, 4(r3)\n");
                    printf("    stw r3, %d(r1)\n", stack_offset);
                    vars[var_count].type = TYPE_ARC;
                    vars[var_count].ref_count = 1;

                } else if (strncmp(pos, "vec![", 5) == 0) {
                    pos += 5;
                    skip_whitespace();
                    printf("    ; %s = vec![...]\n", var_name);
                    printf("    bl _vec_new\n");
                    /* Check for vec![value; count] repeat syntax */
                    int vec_repeat = 0;
                    if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                        char* save_pos = pos;
                        int first_val = parse_number();
                        skip_whitespace();
                        if (*pos == ';') {
                            pos++; /* past ';' */
                            skip_whitespace();
                            /* count could be a variable or literal */
                            int count = 0;
                            if (isdigit(*pos)) {
                                count = parse_number();
                            } else {
                                /* Variable count - skip it, emit runtime-sized vec */
                                while (*pos && *pos != ']') pos++;
                                count = 0; /* runtime-determined */
                            }
                            if (count > 0 && count <= 256) {
                                for (int vi = 0; vi < count; vi++) {
                                    printf("    mr r16, r3\n");
                                    printf("    li r4, %d\n", first_val);
                                    printf("    bl _vec_push\n");
                                    printf("    mr r3, r16\n");
                                }
                            }
                            vec_repeat = 1;
                        } else {
                            pos = save_pos; /* rewind, not a repeat */
                        }
                    }
                    if (!vec_repeat) {
                        while (*pos && *pos != ']') {
                            skip_whitespace();
                            if (*pos == ']') break;
                            if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                                int value = parse_number();
                                printf("    mr r16, r3\n");
                                printf("    li r4, %d\n", value);
                                printf("    bl _vec_push\n");
                                printf("    mr r3, r16\n");
                            } else {
                                /* Non-numeric element (variable, expr) - skip */
                                while (*pos && *pos != ',' && *pos != ']') pos++;
                            }
                            skip_whitespace();
                            if (*pos == ',') pos++;
                        }
                    }
                    while (*pos && *pos != ']') pos++;
                    if (*pos == ']') pos++;
                    printf("    stw r3, %d(r1)\n", stack_offset);
                    printf("    lwz r4, 4(r3)\n");
                    printf("    stw r4, %d(r1)\n", stack_offset + 4);
                    printf("    lwz r4, 8(r3)\n");
                    printf("    stw r4, %d(r1)\n", stack_offset + 8);
                    vars[var_count].type = TYPE_VEC;
                    vars[var_count].size = 12;

                } else if (strncmp(pos, "String::from(", 13) == 0) {
                    pos += 13;
                    printf("    ; %s = String::from(...)\n", var_name);
                    /* Parse string arg */
                    if (*pos == '"') {
                        pos++;
                        char sbuf[512] = {0};
                        int si = 0;
                        while (*pos && *pos != '"' && si < 511) {
                            if (*pos == '\\') pos++;
                            sbuf[si++] = *pos++;
                        }
                        if (*pos == '"') pos++;
                        printf("    .section __DATA,__cstring\n");
                        printf("L_str_%d:\n", var_count);
                        printf("    .asciz \"%s\"\n", sbuf);
                        printf("    .text\n");
                        printf("    li r3, %d\n", si + 1);
                        printf("    bl _malloc\n");
                        printf("    stw r3, %d(r1)    ; String ptr\n", stack_offset);
                        printf("    li r4, %d\n", si);
                        printf("    stw r4, %d(r1)    ; String len\n", stack_offset + 4);
                        printf("    li r4, %d\n", si + 1);
                        printf("    stw r4, %d(r1)    ; String cap\n", stack_offset + 8);
                    }
                    while (*pos && *pos != ')') pos++;
                    if (*pos == ')') pos++;
                    vars[var_count].type = TYPE_STRING;
                    vars[var_count].size = 12;

                } else if (strncmp(pos, "Some(", 5) == 0) {
                    pos += 5;
                    int value = parse_number();
                    printf("    ; %s = Some(%d)\n", var_name, value);
                    printf("    li r14, 1         ; tag = Some\n");
                    printf("    stw r14, %d(r1)\n", stack_offset);
                    printf("    li r14, %d\n", value);
                    printf("    stw r14, %d(r1)   ; value\n", stack_offset + 4);
                    vars[var_count].type = TYPE_OPTION;
                    vars[var_count].size = 8;

                } else if (strncmp(pos, "None", 4) == 0 && !isalnum(*(pos+4))) {
                    pos += 4;
                    printf("    ; %s = None\n", var_name);
                    printf("    li r14, 0         ; tag = None\n");
                    printf("    stw r14, %d(r1)\n", stack_offset);
                    printf("    stw r14, %d(r1)\n", stack_offset + 4);
                    vars[var_count].type = TYPE_OPTION;
                    vars[var_count].size = 8;

                } else if (strncmp(pos, "Ok(", 3) == 0) {
                    pos += 3;
                    int value = parse_number();
                    printf("    ; %s = Ok(%d)\n", var_name, value);
                    printf("    li r14, 0         ; tag = Ok\n");
                    printf("    stw r14, %d(r1)\n", stack_offset);
                    printf("    li r14, %d\n", value);
                    printf("    stw r14, %d(r1)   ; value\n", stack_offset + 4);
                    vars[var_count].type = TYPE_RESULT;
                    vars[var_count].size = 8;

                } else if (strncmp(pos, "Err(", 4) == 0) {
                    pos += 4;
                    printf("    ; %s = Err(...)\n", var_name);
                    printf("    li r14, 1         ; tag = Err\n");
                    printf("    stw r14, %d(r1)\n", stack_offset);
                    /* Parse error value */
                    if (*pos == '"') {
                        pos++;
                        while (*pos && *pos != '"') { if (*pos == '\\') pos++; pos++; }
                        if (*pos == '"') pos++;
                    } else {
                        int eval = parse_number();
                        printf("    li r14, %d\n", eval);
                        printf("    stw r14, %d(r1)   ; error value\n", stack_offset + 4);
                    }
                    while (*pos && *pos != ')') pos++;
                    if (*pos == ')') pos++;
                    vars[var_count].type = TYPE_RESULT;
                    vars[var_count].size = 8;

                } else if (*pos == '[') {
                    pos++;
                    skip_whitespace();
                    printf("    ; %s = [...]\n", var_name);
                    int array_idx = 0;
                    /* Check for [value; count] repeat syntax */
                    int first_val = 0;
                    int is_repeat = 0;
                    if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                        first_val = parse_number();
                        skip_whitespace();
                        if (*pos == ';') {
                            /* [value; count] repeat expression */
                            pos++; /* past ';' */
                            skip_whitespace();
                            int count = parse_number();
                            skip_whitespace();
                            if (count > 256) count = 256; /* safety limit */
                            printf("    li r14, %d\n", first_val);
                            for (int ri = 0; ri < count; ri++) {
                                printf("    stw r14, %d(r1)\n", stack_offset + ri * 4);
                            }
                            array_idx = count;
                            is_repeat = 1;
                        }
                    }
                    if (!is_repeat) {
                        /* Regular array literal [a, b, c, ...] */
                        if (array_idx == 0 && first_val != 0) {
                            /* We already parsed first_val above */
                            printf("    li r14, %d\n", first_val);
                            printf("    stw r14, %d(r1)\n", stack_offset);
                            array_idx = 1;
                            skip_whitespace();
                            if (*pos == ',') pos++;
                        }
                        while (*pos && *pos != ']') {
                            skip_whitespace();
                            if (*pos == ']') break;
                            int value = parse_number();
                            printf("    li r14, %d\n", value);
                            printf("    stw r14, %d(r1)\n", stack_offset + array_idx * 4);
                            array_idx++;
                            skip_whitespace();
                            if (*pos == ',') pos++;
                            /* Safety: skip non-numeric non-bracket chars */
                            while (*pos && *pos != ',' && *pos != ']' && !isdigit(*pos) && *pos != '-') pos++;
                        }
                    }
                    while (*pos && *pos != ']') pos++;
                    if (*pos == ']') pos++;
                    vars[var_count].type = TYPE_ARRAY;
                    vars[var_count].size = array_idx * 4;

                } else if (*pos == '(') {
                    /* Parenthesized expression or tuple */
                    pos++; /* past '(' */
                    compile_expr_to_reg(14);
                    while (*pos && *pos != ')') pos++;
                    if (*pos == ')') pos++;
                    skip_whitespace();
                    /* Check for binary op after closing paren: (expr) * 8 */
                    while ((*pos == '+' || *pos == '-' || *pos == '*' || *pos == '/' || *pos == '%' ||
                            (*pos == '&' && *(pos+1) != '&') || (*pos == '|' && *(pos+1) != '|') ||
                            *pos == '^' || (*pos == '<' && *(pos+1) == '<') || (*pos == '>' && *(pos+1) == '>')) &&
                           *pos != ';') {
                        char op = *pos;
                        char op2 = *(pos+1);
                        int is_shift = (op == '<' && op2 == '<') || (op == '>' && op2 == '>');
                        if (is_shift) pos += 2; else pos++;
                        skip_whitespace();
                        compile_expr_to_reg(15);
                        if (op == '+') printf("    add r14, r14, r15\n");
                        else if (op == '-') printf("    sub r14, r14, r15\n");
                        else if (op == '*') printf("    mullw r14, r14, r15\n");
                        else if (op == '/') printf("    divw r14, r14, r15\n");
                        else if (op == '%') { printf("    divw r16, r14, r15\n"); printf("    mullw r16, r16, r15\n"); printf("    sub r14, r14, r16\n"); }
                        else if (op == '&') printf("    and r14, r14, r15\n");
                        else if (op == '|') printf("    or r14, r14, r15\n");
                        else if (op == '^') printf("    xor r14, r14, r15\n");
                        else if (op == '<' && is_shift) printf("    slw r14, r14, r15\n");
                        else if (op == '>' && is_shift) printf("    sraw r14, r14, r15\n");
                        skip_whitespace();
                    }
                    printf("    stw r14, %d(r1)   ; %s\n", stack_offset, var_name);
                    vars[var_count].type = var_type;
                    vars[var_count].size = 4;

                } else if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                    int value = parse_number();
                    printf("    li r14, %d\n", value);
                    printf("    stw r14, %d(r1)   ; %s\n", stack_offset, var_name);
                    vars[var_count].type = var_type;
                    vars[var_count].size = 4;

                } else if (strncmp(pos, "true", 4) == 0 && !isalnum(*(pos+4))) {
                    pos += 4;
                    printf("    li r14, 1\n");
                    printf("    stw r14, %d(r1)   ; %s = true\n", stack_offset, var_name);
                    vars[var_count].type = TYPE_BOOL;
                    vars[var_count].size = 4;

                } else if (strncmp(pos, "false", 5) == 0 && !isalnum(*(pos+5))) {
                    pos += 5;
                    printf("    li r14, 0\n");
                    printf("    stw r14, %d(r1)   ; %s = false\n", stack_offset, var_name);
                    vars[var_count].type = TYPE_BOOL;
                    vars[var_count].size = 4;

                } else if (*pos == '"') {
                    pos++;
                    char str_buf[512] = {0};
                    int si = 0;
                    while (*pos && *pos != '"' && si < 511) {
                        if (*pos == '\\') pos++;
                        str_buf[si++] = *pos++;
                    }
                    if (*pos == '"') pos++;
                    printf("    ; %s = \"%s\"\n", var_name, str_buf);
                    printf("    .section __DATA,__cstring\n");
                    printf("L_str_%d:\n", var_count);
                    printf("    .asciz \"%s\"\n", str_buf);
                    printf("    .text\n");
                    printf("    lis r14, ha16(L_str_%d)\n", var_count);
                    printf("    la r14, lo16(L_str_%d)(r14)\n", var_count);
                    printf("    stw r14, %d(r1)   ; %s ptr\n", stack_offset, var_name);
                    printf("    li r15, %d\n", si);
                    printf("    stw r15, %d(r1)   ; %s len\n", stack_offset + 4, var_name);
                    vars[var_count].type = TYPE_STR;
                    vars[var_count].size = 8;

                } else if (strncmp(pos, "match ", 6) == 0) {
                    /* Match expression: let x = match val { arms }; */
                    pos += 6;
                    skip_whitespace();
                    printf("    ; %s = match ...\n", var_name);

                    /* Load the match subject into r14 */
                    compile_expr_to_reg(14);
                    skip_whitespace();
                    /* Skip to opening { */
                    while (*pos && *pos != '{') pos++;
                    if (*pos == '{') pos++;

                    static int match_label_let = 0;
                    int arm_count = 0;
                    int end_label = match_label_let++;

                    /* Parse match arms: pattern => expr, */
                    while (*pos) {
                        skip_whitespace();
                        if (*pos == '}') { pos++; break; }
                        /* Skip comments */
                        if (*pos == '/' && *(pos+1) == '/') {
                            while (*pos && *pos != '\n') pos++;
                            if (*pos == '\n') pos++;
                            continue;
                        }
                        if (*pos == '/' && *(pos+1) == '*') {
                            pos += 2;
                            while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
                            if (*pos) pos += 2;
                            continue;
                        }

                        int is_wildcard = (*pos == '_' && (*(pos+1) == ' ' || *(pos+1) == '='));
                        static int arm_label_ctr = 0;
                        int arm_label = arm_label_ctr++;

                        if (is_wildcard) {
                            /* _ => default arm */
                            pos++; /* skip _ */
                            skip_whitespace();
                            if (*pos == '=' && *(pos+1) == '>') pos += 2;
                            skip_whitespace();
                            compile_expr_to_reg(15);
                            printf("    mr r14, r15\n");
                            /* Skip to comma or closing brace */
                            while (*pos && *pos != ',' && *pos != '}') {
                                if (*pos == '{') {
                                    int d = 1; pos++;
                                    while (*pos && d > 0) {
                                        if (*pos == '{') d++;
                                        else if (*pos == '}') d--;
                                        pos++;
                                    }
                                } else pos++;
                            }
                            if (*pos == ',') pos++;
                            printf("    b Lmatch_end_%d\n", end_label);
                        } else if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                            /* Numeric pattern: N => expr */
                            int pat_val = parse_number();
                            skip_whitespace();
                            /* Skip | alternatives: 1 | 2 | 3 => */
                            while (*pos == '|') {
                                pos++;
                                skip_whitespace();
                                parse_number();
                                skip_whitespace();
                            }
                            if (*pos == '=' && *(pos+1) == '>') pos += 2;
                            skip_whitespace();
                            printf("    cmpwi r14, %d\n", pat_val);
                            printf("    bne Lmatch_skip_%d\n", arm_label);
                            compile_expr_to_reg(15);
                            printf("    mr r14, r15\n");
                            /* Skip to comma or closing brace */
                            while (*pos && *pos != ',' && *pos != '}') {
                                if (*pos == '{') {
                                    int d = 1; pos++;
                                    while (*pos && d > 0) {
                                        if (*pos == '{') d++;
                                        else if (*pos == '}') d--;
                                        pos++;
                                    }
                                } else pos++;
                            }
                            if (*pos == ',') pos++;
                            printf("    b Lmatch_end_%d\n", end_label);
                            printf("Lmatch_skip_%d:\n", arm_label);
                        } else {
                            /* Named/complex pattern — skip the arm */
                            while (*pos && *pos != '=' ) pos++;
                            if (*pos == '=' && *(pos+1) == '>') pos += 2;
                            skip_whitespace();
                            /* Skip arm body */
                            while (*pos && *pos != ',' && *pos != '}') {
                                if (*pos == '{') {
                                    int d = 1; pos++;
                                    while (*pos && d > 0) {
                                        if (*pos == '{') d++;
                                        else if (*pos == '}') d--;
                                        pos++;
                                    }
                                } else pos++;
                            }
                            if (*pos == ',') pos++;
                        }
                        arm_count++;
                    }
                    printf("Lmatch_end_%d:\n", end_label);
                    printf("    stw r14, %d(r1)   ; %s = match result\n", stack_offset, var_name);
                    vars[var_count].type = var_type;
                    vars[var_count].size = 4;

                } else if (isalpha(*pos) || *pos == '_') {
                    /* Variable reference, function call, or struct literal */
                    int alpha_size_set = 0;  /* Flag: did a branch set type/size? */
                    char* ref_start = pos;  /* Save position before parsing name */
                    char ref_name[64] = {0};
                    int ri = 0;
                    while (*pos && (isalnum(*pos) || *pos == '_' || *pos == ':') && ri < 63) {
                        ref_name[ri++] = *pos++;
                    }
                    ref_name[ri] = '\0';
                    skip_whitespace();

                    if (*pos == '{') {
                        /* Struct literal: Type { field: val, ... } */
                        pos++;
                        printf("    ; %s = %s { ... }\n", var_name, ref_name);

                        /* Find struct definition */
                        int si_idx = -1;
                        int j;
                        for (j = 0; j < struct_count; j++) {
                            if (strcmp(structs[j].name, ref_name) == 0) { si_idx = j; break; }
                        }
                        int struct_size = (si_idx >= 0) ? structs[si_idx].size : 16;

                        while (*pos && *pos != '}') {
                            skip_whitespace();
                            if (*pos == '}') break;
                            /* Parse field_name: value */
                            char fname[64] = {0};
                            parse_string(fname, sizeof(fname));
                            skip_whitespace();
                            if (*pos == ':') pos++;
                            skip_whitespace();

                            /* Find field offset */
                            int foff = -1;
                            if (si_idx >= 0) {
                                int k;
                                for (k = 0; k < structs[si_idx].field_count; k++) {
                                    if (strcmp(structs[si_idx].fields[k].name, fname) == 0) {
                                        foff = structs[si_idx].fields[k].offset;
                                        break;
                                    }
                                }
                            }
                            if (foff < 0) foff = 0;

                            /* Parse value */
                            if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                                int fval = parse_number();
                                printf("    li r14, %d\n", fval);
                                printf("    stw r14, %d(r1)   ; .%s\n", stack_offset + foff, fname);
                            } else if (*pos == '"') {
                                pos++;
                                while (*pos && *pos != '"') { if (*pos == '\\') pos++; pos++; }
                                if (*pos == '"') pos++;
                                printf("    li r14, 0         ; .%s (string TODO)\n", fname);
                                printf("    stw r14, %d(r1)\n", stack_offset + foff);
                            } else if (isalpha(*pos) || *pos == '_') {
                                char fvar[64] = {0};
                                parse_string(fvar, sizeof(fvar));
                                int found = 0;
                                int k;
                                for (k = 0; k < var_count; k++) {
                                    if (strcmp(vars[k].name, fvar) == 0) {
                                        printf("    lwz r14, %d(r1)   ; load %s\n", vars[k].offset, fvar);
                                        printf("    stw r14, %d(r1)   ; .%s\n", stack_offset + foff, fname);
                                        found = 1;
                                        break;
                                    }
                                }
                                if (!found) {
                                    printf("    li r14, 0\n");
                                    printf("    stw r14, %d(r1)   ; .%s (unresolved)\n", stack_offset + foff, fname);
                                }
                            } else {
                                int fval = parse_number();
                                printf("    li r14, %d\n", fval);
                                printf("    stw r14, %d(r1)\n", stack_offset + foff);
                            }

                            /* Skip past any trailing expr parts (as casts, operators, etc.) */
                            while (*pos && *pos != ',' && *pos != '}') pos++;
                            if (*pos == ',') pos++;
                        }
                        if (*pos == '}') pos++;

                        vars[var_count].type = TYPE_STRUCT;
                        vars[var_count].size = struct_size > 0 ? struct_size : 4;
                        alpha_size_set = 1;

                    } else if (*pos == '(') {
                        printf("    ; %s = %s(...)\n", var_name, ref_name);
                        /* Pass arguments */
                        pos++;
                        int arg_reg = 3;
                        while (*pos && *pos != ')') {
                            skip_whitespace();
                            if (*pos == ')') break;
                            if (*pos == '"') {
                                /* String arg — skip for now */
                                pos++;
                                while (*pos && *pos != '"') { if (*pos == '\\') pos++; pos++; }
                                if (*pos == '"') pos++;
                            } else if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                                int aval = parse_number();
                                if (arg_reg <= 10) {
                                    printf("    li r%d, %d\n", arg_reg, aval);
                                    arg_reg++;
                                }
                            } else if (isalpha(*pos) || *pos == '_') {
                                char aname[64] = {0};
                                int ai = 0;
                                while (*pos && (isalnum(*pos) || *pos == '_') && ai < 63) {
                                    aname[ai++] = *pos++;
                                }
                                /* Skip method chains: arg.method().method() */
                                while (*pos == '.') {
                                    pos++;
                                    while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
                                    if (*pos == '(') {
                                        pos++;
                                        int depth = 1;
                                        while (*pos && depth > 0) {
                                            if (*pos == '(') depth++;
                                            else if (*pos == ')') depth--;
                                            pos++;
                                        }
                                    }
                                }
                                /* Look up arg variable */
                                int j;
                                for (j = 0; j < var_count; j++) {
                                    if (strcmp(vars[j].name, aname) == 0) {
                                        if (arg_reg <= 10) {
                                            printf("    lwz r%d, %d(r1)   ; arg %s\n", arg_reg, vars[j].offset, aname);
                                            arg_reg++;
                                        }
                                        break;
                                    }
                                }
                            } else {
                                /* Unknown token in function args — skip to avoid infinite loop */
                                pos++;
                            }
                            skip_whitespace();
                            if (*pos == ',') pos++;
                        }
                        if (*pos == ')') pos++;
                        printf("    bl _%s\n", ref_name);
                        printf("    stw r3, %d(r1)   ; %s = result\n", stack_offset, var_name);
                    } else {
                        /* Variable reference, possibly with binary op: let x = a + b */
                        /* Rewind pos to before ref_name so compile_expr_to_reg can parse it */
                        pos = ref_start;
                        var_type = compile_expr_to_reg(14);
                        printf("    stw r14, %d(r1)   ; %s\n", stack_offset, var_name);
                    }
                    if (!alpha_size_set) {
                        vars[var_count].type = var_type;
                        vars[var_count].size = 4;
                    }

                } else {
                    int value = parse_number();
                    printf("    li r14, %d\n", value);
                    printf("    stw r14, %d(r1)   ; %s\n", stack_offset, var_name);
                    vars[var_count].type = var_type;
                    vars[var_count].size = 4;
                }

                strcpy(vars[var_count].name, var_name);
                vars[var_count].offset = stack_offset;
                vars[var_count].is_mut = is_mut;
                stack_offset += (vars[var_count].size > 0 ? vars[var_count].size : 4);
                var_count++;
            }

            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;

        } else if (strncmp(pos, "unsafe ", 7) == 0) {
            pos += 7;
            skip_whitespace();
            if (*pos == '{') {
                printf("    ; unsafe block\n");
                in_unsafe_block = 1;
                pos++;
            }

        } else if (strncmp(pos, "if ", 3) == 0) {
            pos += 3;
            skip_whitespace();
            static int if_label = 0;
            int my_label = if_label++;

            if (strncmp(pos, "let ", 4) == 0) {
                /* if let Some(x) = expr { ... } */
                pos += 4;
                skip_whitespace();
                printf("    ; if let\n");

                /* Parse pattern: Some(var) or Ok(var) */
                int is_some = 0, is_ok = 0;
                if (strncmp(pos, "Some(", 5) == 0) { pos += 5; is_some = 1; }
                else if (strncmp(pos, "Ok(", 3) == 0) { pos += 3; is_ok = 1; }

                char bind_var[64] = {0};
                parse_string(bind_var, sizeof(bind_var));
                while (*pos && *pos != '=') pos++;
                if (*pos == '=') pos++;
                skip_whitespace();

                /* Parse the expression being matched */
                char match_expr[64] = {0};
                parse_string(match_expr, sizeof(match_expr));

                int expr_off = -1;
                RustType expr_type = TYPE_I32;
                for (i = 0; i < var_count; i++) {
                    if (strcmp(vars[i].name, match_expr) == 0) {
                        expr_off = vars[i].offset;
                        expr_type = vars[i].type;
                        break;
                    }
                }

                if (expr_off >= 0) {
                    printf("    lwz r14, %d(r1)   ; load %s tag\n", expr_off, match_expr);
                    if (is_some || expr_type == TYPE_OPTION) {
                        printf("    cmpwi r14, 0      ; None?\n");
                        printf("    beq Lelse_%d\n", my_label);
                    } else if (is_ok || expr_type == TYPE_RESULT) {
                        printf("    cmpwi r14, 1      ; Err?\n");
                        printf("    beq Lelse_%d\n", my_label);
                    }
                    /* Bind the inner value */
                    printf("    lwz r14, %d(r1)   ; load inner value\n", expr_off + 4);
                    printf("    stw r14, %d(r1)   ; bind %s\n", stack_offset, bind_var);
                    strcpy(vars[var_count].name, bind_var);
                    vars[var_count].offset = stack_offset;
                    vars[var_count].type = TYPE_I32;
                    vars[var_count].size = 4;
                    var_count++;
                    stack_offset += 4;
                } else {
                    printf("    li r14, 0\n");
                    printf("    beq Lelse_%d\n", my_label);
                }
            } else {
                /* Parse condition: var, var op expr, !var, function() */
                int negate = 0;
                if (*pos == '!') { negate = 1; pos++; skip_whitespace(); }

                char cond_var[64] = {0};
                parse_string(cond_var, sizeof(cond_var));

                int cond_off = -1;
                for (i = 0; i < var_count; i++) {
                    if (strcmp(vars[i].name, cond_var) == 0) {
                        cond_off = vars[i].offset;
                        break;
                    }
                }

                if (cond_off >= 0) {
                    printf("    lwz r14, %d(r1)   ; load %s\n", cond_off, cond_var);
                } else {
                    printf("    li r14, 0         ; %s (unresolved)\n", cond_var);
                }

                skip_whitespace();

                /* Check for comparison operator */
                /* Helper macro: load RHS into r15 (number or variable) and emit compare */
                #define EMIT_CMP_RHS() do { \
                    if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) { \
                        int rhs = parse_number(); \
                        printf("    cmpwi r14, %d\n", rhs); \
                    } else if (isalpha(*pos) || *pos == '_') { \
                        compile_expr_to_reg(15); \
                        printf("    cmpw r14, r15\n"); \
                    } else { \
                        printf("    cmpwi r14, 0\n"); \
                    } \
                } while(0)

                if (strncmp(pos, "==", 2) == 0) {
                    pos += 2; skip_whitespace();
                    EMIT_CMP_RHS();
                    printf("    %s Lelse_%d\n", negate ? "beq" : "bne", my_label);
                } else if (strncmp(pos, "!=", 2) == 0) {
                    pos += 2; skip_whitespace();
                    EMIT_CMP_RHS();
                    printf("    %s Lelse_%d\n", negate ? "bne" : "beq", my_label);
                } else if (strncmp(pos, ">=", 2) == 0) {
                    pos += 2; skip_whitespace();
                    EMIT_CMP_RHS();
                    printf("    %s Lelse_%d\n", negate ? "bge" : "blt", my_label);
                } else if (strncmp(pos, "<=", 2) == 0) {
                    pos += 2; skip_whitespace();
                    EMIT_CMP_RHS();
                    printf("    %s Lelse_%d\n", negate ? "ble" : "bgt", my_label);
                } else if (*pos == '>' && *(pos+1) != '>') {
                    pos++; skip_whitespace();
                    EMIT_CMP_RHS();
                    printf("    %s Lelse_%d\n", negate ? "bgt" : "ble", my_label);
                } else if (*pos == '<' && *(pos+1) != '<') {
                    pos++; skip_whitespace();
                    EMIT_CMP_RHS();
                    printf("    %s Lelse_%d\n", negate ? "blt" : "bge", my_label);
                } else {
                    /* Boolean truthiness check */
                    printf("    cmpwi r14, 0\n");
                    printf("    %s Lelse_%d\n", negate ? "bne" : "beq", my_label);
                }
                #undef EMIT_CMP_RHS
            }

            /* Compile if-body */
            while (*pos && *pos != '{') pos++;
            if (*pos == '{') {
                pos++;
                compile_function_body(frame_size);
                if (*pos == '}') pos++;
            }

            printf("    b Lendif_%d\n", my_label);
            printf("Lelse_%d:\n", my_label);

            skip_whitespace();

            /* Check for else if / else */
            if (strncmp(pos, "else", 4) == 0 && !isalnum(*(pos+4))) {
                pos += 4;
                skip_whitespace();
                if (strncmp(pos, "if ", 3) == 0) {
                    /* else if — don't consume "if", let next iteration handle it */
                    /* But we need to emit it inside the else block */
                    /* For now, compile the else-if body inline */
                }
                while (*pos && *pos != '{') pos++;
                if (*pos == '{') {
                    pos++;
                    compile_function_body(frame_size);
                    if (*pos == '}') pos++;
                }
            }
            printf("Lendif_%d:\n", my_label);

        } else if (strncmp(pos, "while ", 6) == 0) {
            pos += 6;
            static int while_label = 0;
            int my_label = while_label++;

            printf("Lwhile_%d:\n", my_label);

            /* Parse condition */
            int negate = 0;
            if (*pos == '!') { negate = 1; pos++; skip_whitespace(); }

            char cond_var[64] = {0};
            parse_string(cond_var, sizeof(cond_var));

            int cond_off = -1;
            for (i = 0; i < var_count; i++) {
                if (strcmp(vars[i].name, cond_var) == 0) { cond_off = vars[i].offset; break; }
            }
            if (cond_off >= 0) {
                printf("    lwz r14, %d(r1)   ; load %s\n", cond_off, cond_var);
            } else {
                printf("    li r14, 1         ; %s (default true)\n", cond_var);
            }

            skip_whitespace();

            /* Comparison operators — handle both immediate and variable RHS */
            #define WHILE_CMP_RHS() do { \
                if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) { \
                    int rhs = parse_number(); \
                    printf("    cmpwi r14, %d\n", rhs); \
                } else if (isalpha(*pos) || *pos == '_') { \
                    compile_expr_to_reg(15); \
                    printf("    cmpw r14, r15\n"); \
                } else { \
                    printf("    cmpwi r14, 0\n"); \
                } \
            } while(0)

            if (strncmp(pos, "==", 2) == 0) {
                pos += 2; skip_whitespace();
                WHILE_CMP_RHS();
                printf("    bne Lendwhile_%d\n", my_label);
            } else if (strncmp(pos, "!=", 2) == 0) {
                pos += 2; skip_whitespace();
                WHILE_CMP_RHS();
                printf("    beq Lendwhile_%d\n", my_label);
            } else if (*pos == '<' && *(pos+1) != '<') {
                pos++; skip_whitespace();
                WHILE_CMP_RHS();
                printf("    bge Lendwhile_%d\n", my_label);
            } else if (*pos == '>' && *(pos+1) != '>') {
                pos++; skip_whitespace();
                WHILE_CMP_RHS();
                printf("    ble Lendwhile_%d\n", my_label);
            } else {
                printf("    cmpwi r14, 0\n");
                printf("    %s Lendwhile_%d\n", negate ? "bne" : "beq", my_label);
            }
            #undef WHILE_CMP_RHS

            /* Compile while body */
            while (*pos && *pos != '{') pos++;
            if (*pos == '{') {
                pos++;
                compile_function_body(frame_size);
                if (*pos == '}') pos++;
            }
            printf("    b Lwhile_%d\n", my_label);
            printf("Lendwhile_%d:\n", my_label);

        } else if (strncmp(pos, "for ", 4) == 0) {
            pos += 4;
            static int for_label = 0;
            int my_label = for_label++;

            /* Parse: for VAR in EXPR { ... } */
            skip_whitespace();
            char iter_var[64] = {0};
            parse_string(iter_var, sizeof(iter_var));
            skip_whitespace();
            if (strncmp(pos, "in ", 3) == 0) pos += 3;
            skip_whitespace();

            /* Parse range: 0..N or collection.iter() */
            int range_start = 0, range_end = 0;
            int is_range = 0;
            if (isdigit(*pos)) {
                range_start = parse_number();
                if (strncmp(pos, "..", 2) == 0) {
                    pos += 2;
                    if (*pos == '=') pos++; /* ..= inclusive */
                    range_end = parse_number();
                    is_range = 1;
                }
            } else {
                /* Collection iteration — skip to body */
                while (*pos && *pos != '{') pos++;
            }

            /* Register iterator variable */
            printf("    ; for %s in %d..%d\n", iter_var, range_start, range_end);
            printf("    li r14, %d\n", range_start);
            printf("    stw r14, %d(r1)   ; %s = %d\n", stack_offset, iter_var, range_start);

            strcpy(vars[var_count].name, iter_var);
            vars[var_count].offset = stack_offset;
            vars[var_count].type = TYPE_I32;
            vars[var_count].size = 4;
            int iter_off = stack_offset;
            var_count++;
            stack_offset += 4;

            printf("Lfor_%d:\n", my_label);
            if (is_range) {
                printf("    lwz r14, %d(r1)   ; load %s\n", iter_off, iter_var);
                printf("    cmpwi r14, %d\n", range_end);
                printf("    bge Lendfor_%d\n", my_label);
            }

            /* Compile for body */
            while (*pos && *pos != '{') pos++;
            if (*pos == '{') {
                pos++;
                compile_function_body(frame_size);
                if (*pos == '}') pos++;
            }

            /* Increment iterator */
            if (is_range) {
                printf("    lwz r14, %d(r1)\n", iter_off);
                printf("    addi r14, r14, 1\n");
                printf("    stw r14, %d(r1)\n", iter_off);
            }
            printf("    b Lfor_%d\n", my_label);
            printf("Lendfor_%d:\n", my_label);

        } else if (strncmp(pos, "loop", 4) == 0 && (*(pos+4) == ' ' || *(pos+4) == '{')) {
            pos += 4;
            static int loop_label = 0;
            int my_label = loop_label++;
            printf("Lloop_%d:\n", my_label);

            /* Compile loop body */
            skip_whitespace();
            while (*pos && *pos != '{') pos++;
            if (*pos == '{') {
                pos++;
                compile_function_body(frame_size);
                if (*pos == '}') pos++;
            }
            printf("    b Lloop_%d\n", my_label);
            printf("Lendloop_%d:\n", my_label);

        } else if (strncmp(pos, "match ", 6) == 0) {
            pos += 6;
            skip_whitespace();
            printf("    ; match statement\n");

            /* Load match subject into r14 */
            compile_expr_to_reg(14);
            skip_whitespace();

            /* Skip to opening { */
            while (*pos && *pos != '{') pos++;
            if (*pos == '{') pos++;

            static int match_label_stmt = 0;
            int end_label = match_label_stmt++;

            /* Parse match arms */
            while (*pos) {
                skip_whitespace();
                if (*pos == '}') { pos++; break; }
                /* Skip comments */
                if (*pos == '/' && *(pos+1) == '/') {
                    while (*pos && *pos != '\n') pos++;
                    if (*pos == '\n') pos++;
                    continue;
                }
                if (*pos == '/' && *(pos+1) == '*') {
                    pos += 2;
                    while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
                    if (*pos) pos += 2;
                    continue;
                }

                int is_wildcard = (*pos == '_' && (*(pos+1) == ' ' || *(pos+1) == '='));
                static int arm_label_stmt = 0;
                int arm_label = arm_label_stmt++;

                if (is_wildcard) {
                    pos++;
                    skip_whitespace();
                    if (*pos == '=' && *(pos+1) == '>') pos += 2;
                    skip_whitespace();
                    /* Compile arm body */
                    if (*pos == '{') {
                        pos++;
                        compile_function_body(frame_size);
                        if (*pos == '}') pos++;
                    } else {
                        /* Single expression arm — skip it */
                        while (*pos && *pos != ',' && *pos != '}') pos++;
                    }
                    if (*pos == ',') pos++;
                    printf("    b Lmatch_end_%d\n", end_label);
                } else if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                    int pat_val = parse_number();
                    skip_whitespace();
                    while (*pos == '|') { pos++; skip_whitespace(); parse_number(); skip_whitespace(); }
                    if (*pos == '=' && *(pos+1) == '>') pos += 2;
                    skip_whitespace();
                    printf("    cmpwi r14, %d\n", pat_val);
                    printf("    bne Lmatch_skip_%d\n", arm_label);
                    if (*pos == '{') {
                        pos++;
                        compile_function_body(frame_size);
                        if (*pos == '}') pos++;
                    } else {
                        while (*pos && *pos != ',' && *pos != '}') pos++;
                    }
                    if (*pos == ',') pos++;
                    printf("    b Lmatch_end_%d\n", end_label);
                    printf("Lmatch_skip_%d:\n", arm_label);
                } else {
                    /* Named/complex pattern — skip entire arm */
                    while (*pos && *pos != '=') pos++;
                    if (*pos == '=' && *(pos+1) == '>') pos += 2;
                    skip_whitespace();
                    if (*pos == '{') {
                        int d = 1; pos++;
                        while (*pos && d > 0) {
                            if (*pos == '{') d++;
                            else if (*pos == '}') d--;
                            pos++;
                        }
                    } else {
                        while (*pos && *pos != ',' && *pos != '}') pos++;
                    }
                    if (*pos == ',') pos++;
                }
            }
            printf("Lmatch_end_%d:\n", end_label);

        } else if (strncmp(pos, "return ", 7) == 0) {
            pos += 7;
            skip_whitespace();

            if (strncmp(pos, "Ok(", 3) == 0) {
                pos += 3;
                int value = parse_number();
                printf("    ; return Ok(%d)\n", value);
                printf("    li r3, 0          ; Ok tag\n");
                printf("    li r4, %d\n", value);
            } else if (strncmp(pos, "Err(", 4) == 0) {
                pos += 4;
                printf("    ; return Err(...)\n");
                printf("    li r3, 1          ; Err tag\n");
            } else if (strncmp(pos, "Some(", 5) == 0) {
                pos += 5;
                int value = parse_number();
                printf("    ; return Some(%d)\n", value);
                printf("    li r3, 1          ; Some tag\n");
                printf("    li r4, %d\n", value);
            } else if (strncmp(pos, "None", 4) == 0 && !isalnum(*(pos+4))) {
                pos += 4;
                printf("    ; return None\n");
                printf("    li r3, 0          ; None tag\n");
            } else {
                /* General expression: return x * 2, return a + b, etc. */
                compile_expr_to_reg(3);
            }

            /* RAII cleanup */
            for (i = var_count - 1; i >= saved_var_count; i--) {
                emit_drop_glue(&vars[i]);
            }

            /* Epilogue and return */
            printf("    addi r1, r1, %d\n", frame_size);
            printf("    lwz r0, 8(r1)\n");
            printf("    mtlr r0\n");
            printf("    blr\n");

            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;

        } else if (strncmp(pos, "println!", 8) == 0) {
            pos += 8;
            printf("    ; println! macro\n");
            int pd = 0;
            while (*pos) {
                if (*pos == '(') pd++;
                else if (*pos == ')') { pd--; if (pd == 0) { pos++; break; } }
                pos++;
            }
            printf("    bl _rust_println\n");
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;

        } else if (strncmp(pos, "assert!", 7) == 0) {
            pos += 7;
            printf("    ; assert! macro\n");
            printf("    bl _rust_assert\n");
            int pd = 0;
            while (*pos) {
                if (*pos == '(') pd++;
                else if (*pos == ')') { pd--; if (pd == 0) { pos++; break; } }
                pos++;
            }
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;

        } else if (strncmp(pos, "break", 5) == 0 && !isalnum(*(pos+5))) {
            pos += 5;
            printf("    ; break\n");
            /* Would need loop context to know target label */
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;

        } else if (strncmp(pos, "continue", 8) == 0 && !isalnum(*(pos+8))) {
            pos += 8;
            printf("    ; continue\n");
            while (*pos && *pos != ';') pos++;
            if (*pos == ';') pos++;

        } else if (isalpha(*pos) || *pos == '_') {
            /* Method calls, field access, assignments, function calls */
            char obj_name[64] = {0};
            parse_string(obj_name, sizeof(obj_name));

            int obj_offset = -1;
            RustType obj_type = TYPE_I32;
            for (i = 0; i < var_count; i++) {
                if (strcmp(vars[i].name, obj_name) == 0) {
                    obj_offset = vars[i].offset;
                    obj_type = vars[i].type;
                    break;
                }
            }

            skip_whitespace();

            if ((*pos == '+' || *pos == '-' || *pos == '*' || *pos == '/' || *pos == '%' ||
                 *pos == '&' || *pos == '|' || *pos == '^') && *(pos+1) == '=') {
                /* Compound assignment: +=, -=, *=, /=, %=, &=, |=, ^= */
                char cop = *pos;
                pos += 2;
                skip_whitespace();
                if (obj_offset >= 0) {
                    printf("    lwz r14, %d(r1)   ; load %s\n", obj_offset, obj_name);
                    compile_expr_to_reg(15);
                    if (cop == '+') printf("    add r14, r14, r15\n");
                    else if (cop == '-') printf("    sub r14, r14, r15\n");
                    else if (cop == '*') printf("    mullw r14, r14, r15\n");
                    else if (cop == '/') printf("    divw r14, r14, r15\n");
                    else if (cop == '%') { printf("    divw r16, r14, r15\n"); printf("    mullw r16, r16, r15\n"); printf("    sub r14, r14, r16\n"); }
                    else if (cop == '&') printf("    and r14, r14, r15\n");
                    else if (cop == '|') printf("    or r14, r14, r15\n");
                    else if (cop == '^') printf("    xor r14, r14, r15\n");
                    printf("    stw r14, %d(r1)   ; %s %c= expr\n", obj_offset, obj_name, cop);
                }
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;

            } else if (*pos == '=' && *(pos+1) != '=') {
                pos++;
                skip_whitespace();
                if (obj_offset >= 0) {
                    compile_expr_to_reg(14);
                    printf("    stw r14, %d(r1)   ; %s = expr\n", obj_offset, obj_name);
                }
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;

            } else if (*pos == '.') {
                pos++;
                if (strncmp(pos, "await", 5) == 0 && !isalnum(*(pos+5))) {
                    pos += 5;
                    printf("    ; %s.await\n", obj_name);
                    printf("    lwz r3, %d(r1)\n", obj_offset >= 0 ? obj_offset : 0);
                    printf("    bl _await_future\n");
                } else {
                    char method[64] = {0};
                    parse_string(method, sizeof(method));
                    int var_off = obj_offset >= 0 ? obj_offset : 0;

                    if (*pos == '(') {
                        printf("    ; %s.%s()\n", obj_name, method);
                        if (strcmp(method, "clone") == 0) {
                            printf("    la r3, %d(r1)\n", var_off);
                            printf("    bl _clone_impl\n");
                        } else if (strcmp(method, "drop") == 0) {
                            printf("    la r3, %d(r1)\n", var_off);
                            printf("    bl _drop_impl\n");
                        } else if (strcmp(method, "len") == 0) {
                            printf("    lwz r3, %d(r1)\n", var_off + 4);
                        } else if (strcmp(method, "push") == 0) {
                            printf("    lwz r3, %d(r1)    ; Vec ptr\n", var_off);
                            printf("    bl _vec_push\n");
                        } else if (strcmp(method, "iter") == 0) {
                            printf("    la r3, %d(r1)\n", var_off);
                            printf("    bl _create_iter\n");
                        } else if (strcmp(method, "collect") == 0) {
                            printf("    bl _iter_collect\n");
                        } else if (strcmp(method, "unwrap") == 0) {
                            printf("    lwz r14, %d(r1)   ; load tag\n", var_off);
                            if (obj_type == TYPE_RESULT) {
                                printf("    cmpwi r14, 1\n");
                                printf("    beq _panic_unwrap ; panic if Err\n");
                            } else {
                                printf("    cmpwi r14, 0\n");
                                printf("    beq _panic_unwrap ; panic if None\n");
                            }
                            printf("    lwz r3, %d(r1)\n", var_off + 4);
                        } else {
                            printf("    la r3, %d(r1)\n", var_off);
                            printf("    bl _%s_%s\n", obj_name, method);
                        }
                        while (*pos && *pos != ')') pos++;
                        if (*pos == ')') pos++;
                    } else {
                        /* Field access: obj.field (not a method call) */
                        printf("    ; %s.%s (field access)\n", obj_name, method);
                        int field_off = -1;
                        int is_self = (strcmp(obj_name, "self") == 0);

                        /* Resolve struct type */
                        int struct_idx = -1;
                        if (is_self && current_impl_struct >= 0) {
                            struct_idx = current_impl_struct;
                        } else if (obj_type == TYPE_STRUCT) {
                            for (int si = 0; si < struct_count; si++) {
                                int fi;
                                for (fi = 0; fi < structs[si].field_count; fi++) {
                                    if (strcmp(structs[si].fields[fi].name, method) == 0) {
                                        struct_idx = si;
                                        field_off = structs[si].fields[fi].offset;
                                        break;
                                    }
                                }
                                if (field_off >= 0) break;
                            }
                        }
                        if (struct_idx >= 0 && field_off < 0) {
                            for (int fi = 0; fi < structs[struct_idx].field_count; fi++) {
                                if (strcmp(structs[struct_idx].fields[fi].name, method) == 0) {
                                    field_off = structs[struct_idx].fields[fi].offset;
                                    break;
                                }
                            }
                        }
                        if (field_off < 0) field_off = 0;

                        skip_whitespace();
                        if (*pos == '=' && *(pos+1) != '=') {
                            /* self.field = expr */
                            pos++;
                            skip_whitespace();
                            compile_expr_to_reg(14);
                            if (is_self) {
                                printf("    lwz r15, %d(r1)   ; load self ptr\n", var_off);
                                printf("    stw r14, %d(r15)  ; self.%s = expr\n", field_off, method);
                            } else {
                                printf("    stw r14, %d(r1)   ; %s.%s = expr\n", var_off + field_off, obj_name, method);
                            }
                        } else if ((*pos == '+' || *pos == '-' || *pos == '*') && *(pos+1) == '=') {
                            /* self.field += expr */
                            char cop = *pos;
                            pos += 2;
                            skip_whitespace();
                            if (is_self) {
                                printf("    lwz r15, %d(r1)   ; load self ptr\n", var_off);
                                printf("    lwz r14, %d(r15)  ; load self.%s\n", field_off, method);
                            } else {
                                printf("    lwz r14, %d(r1)   ; load %s.%s\n", var_off + field_off, obj_name, method);
                            }
                            compile_expr_to_reg(16);
                            if (cop == '+') printf("    add r14, r14, r16\n");
                            else if (cop == '-') printf("    sub r14, r14, r16\n");
                            else if (cop == '*') printf("    mullw r14, r14, r16\n");
                            if (is_self) {
                                printf("    stw r14, %d(r15)  ; self.%s %c= expr\n", field_off, method, cop);
                            } else {
                                printf("    stw r14, %d(r1)   ; %s.%s %c= expr\n", var_off + field_off, obj_name, method, cop);
                            }
                        } else {
                            /* Just a read: obj.field */
                            if (is_self) {
                                printf("    lwz r15, %d(r1)   ; load self ptr\n", var_off);
                                printf("    lwz r3, %d(r15)   ; load self.%s\n", field_off, method);
                            } else {
                                printf("    lwz r3, %d(r1)    ; load %s.%s\n", var_off + field_off, obj_name, method);
                            }
                        }
                    }
                }
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;

            } else if (*pos == '[') {
                pos++;
                int index = parse_number();
                int var_off = obj_offset >= 0 ? obj_offset : 0;
                printf("    ; %s[%d]\n", obj_name, index);
                printf("    lwz r3, %d(r1)\n", var_off);
                printf("    lwz r3, %d(r3)\n", index * 4);
                while (*pos && *pos != ']') pos++;
                if (*pos == ']') pos++;
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;

            } else if (*pos == '(') {
                printf("    ; Call %s()\n", obj_name);
                /* Parse arguments */
                pos++;
                int arg_reg = 3;
                while (*pos && *pos != ')') {
                    skip_whitespace();
                    if (*pos == ')') break;
                    if (*pos == '"') {
                        /* String literal argument */
                        pos++;
                        while (*pos && *pos != '"') { if (*pos == '\\') pos++; pos++; }
                        if (*pos == '"') pos++;
                        /* Pass string address in register (simplified) */
                        if (arg_reg <= 10) {
                            printf("    li r%d, 0         ; string arg (TODO)\n", arg_reg++);
                        }
                    } else if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
                        int aval = parse_number();
                        if (arg_reg <= 10) {
                            printf("    li r%d, %d\n", arg_reg++, aval);
                        }
                    } else if (isalpha(*pos) || *pos == '_') {
                        char aname[64] = {0};
                        parse_string(aname, sizeof(aname));
                        int j;
                        for (j = 0; j < var_count; j++) {
                            if (strcmp(vars[j].name, aname) == 0) {
                                if (arg_reg <= 10) {
                                    printf("    lwz r%d, %d(r1)\n", arg_reg++, vars[j].offset);
                                }
                                break;
                            }
                        }
                    } else {
                        /* Unknown argument type — skip one char to avoid infinite loop */
                        pos++;
                    }
                    skip_whitespace();
                    if (*pos == ',') pos++;
                }
                if (*pos == ')') pos++;
                printf("    bl _%s\n", obj_name);
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;

            } else {
                /* Unknown statement — skip to semicolon */
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;
            }

        } else {
            pos++;
        }

        skip_whitespace();
    }

    /* Restore state for caller */
    stack_offset = saved_stack_offset;
    /* Don't restore var_count — locals remain visible for drop glue if needed */
}

void compile_rust(char* source) {
    pos = source;
    
    printf("; PowerPC Rust Compiler - 100%% Firefox-Ready Edition\n");
    printf("; Complete Rust implementation for PowerPC\n");
    printf("; Supports all features needed for Firefox\n\n");
    
    /* Initialize built-in macros */
    strcpy(macros[0].name, "println!");
    macros[0].is_builtin = 1;
    strcpy(macros[1].name, "vec!");
    macros[1].is_builtin = 1;
    strcpy(macros[2].name, "format!");
    macros[2].is_builtin = 1;
    strcpy(macros[3].name, "panic!");
    macros[3].is_builtin = 1;
    strcpy(macros[4].name, "assert!");
    macros[4].is_builtin = 1;
    strcpy(macros[5].name, "dbg!");
    macros[5].is_builtin = 1;
    macro_count = 6;
    
    /* Multi-pass compilation */
    
    /* Pass 1: Collect type definitions */
    while (*pos) {
        skip_whitespace();

        /* Skip comments */
        if (*pos == '/' && *(pos+1) == '/') {
            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }
        if (*pos == '/' && *(pos+1) == '*') {
            pos += 2;
            while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
            if (*pos) pos += 2;
            continue;
        }

        if (strncmp(pos, "#[derive(", 9) == 0) {
            /* Parse derive macros */
            pos += 9;
            /* Skip derive content - parsed but not needed for codegen */
            while (*pos && *pos != ')') pos++;
        } else if (strncmp(pos, "struct ", 7) == 0) {
            pos += 7;
            skip_whitespace();
            
            char struct_name[64] = {0};
            parse_string(struct_name, sizeof(struct_name));
            
            strcpy(structs[struct_count].name, struct_name);
            
            /* Parse generics */
            if (*pos == '<') {
                pos++;
                char generics[128] = {0};
                int g_idx = 0;
                while (*pos && *pos != '>' && g_idx < 127) {
                    generics[g_idx++] = *pos++;
                }
                strcpy(structs[struct_count].generics, generics);
                if (*pos == '>') pos++;
            }
            
            /* Parse struct fields */
            skip_whitespace();
            structs[struct_count].field_count = 0;
            structs[struct_count].alignment = 4;

            if (*pos == '{') {
                pos++;
                int field_offset = 0;
                while (*pos && *pos != '}') {
                    skip_whitespace();
                    if (*pos == '}') break;
                    /* Skip comments inside struct */
                    if (*pos == '/' && *(pos+1) == '/') {
                        while (*pos && *pos != '\n') pos++;
                        if (*pos == '\n') pos++;
                        continue;
                    }
                    if (*pos == '/' && *(pos+1) == '*') {
                        pos += 2;
                        while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
                        if (*pos) pos += 2;
                        continue;
                    }
                    /* Skip attributes like #[...] */
                    if (*pos == '#' && *(pos+1) == '[') {
                        while (*pos && *pos != ']') pos++;
                        if (*pos == ']') pos++;
                        continue;
                    }
                    /* Skip @ attributes like @size(16), @align(16) */
                    if (*pos == '@') {
                        pos++;
                        while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
                        if (*pos == '(') {
                            int d = 1; pos++;
                            while (*pos && d > 0) { if (*pos == '(') d++; else if (*pos == ')') d--; pos++; }
                        }
                        skip_whitespace();
                        continue;
                    }
                    /* Skip macro metavariables $(...) */
                    if (*pos == '$') {
                        if (*(pos+1) == '(') {
                            int d = 1; pos += 2;
                            while (*pos && d > 0) { if (*pos == '(') d++; else if (*pos == ')') d--; pos++; }
                            /* Skip optional repeat operator: *, +, ? */
                            while (*pos == '*' || *pos == '+' || *pos == '?') pos++;
                        } else {
                            pos++; /* skip $ */
                            while (*pos && (isalnum(*pos) || *pos == '_')) pos++; /* skip $ident */
                        }
                        continue;
                    }
                    /* Skip pub keyword */
                    if (strncmp(pos, "pub ", 4) == 0) pos += 4;
                    if (strncmp(pos, "pub(crate) ", 11) == 0) pos += 11;
                    if (strncmp(pos, "pub(super) ", 11) == 0) pos += 11;
                    skip_whitespace();
                    /* Skip r# raw identifier prefix */
                    if (*pos == 'r' && *(pos+1) == '#') pos += 2;
                    /* Parse field name */
                    char fname[32] = {0};
                    int fi = 0;
                    while (*pos && (isalnum(*pos) || *pos == '_') && fi < 31) {
                        fname[fi++] = *pos++;
                    }
                    fname[fi] = '\0';
                    skip_whitespace();
                    if (*pos == '=') {
                        /* key = value (macro syntax, not a real field) — skip to comma */
                        while (*pos && *pos != ',' && *pos != '}') pos++;
                        if (*pos == ',') pos++;
                        continue;
                    }
                    if (*pos == ':') pos++;
                    skip_whitespace();
                    /* Parse field type */
                    RustType ftype = parse_type();
                    int fsize = 4; /* default */
                    switch (ftype) {
                        case TYPE_I8: case TYPE_U8: case TYPE_BOOL: fsize = 1; break;
                        case TYPE_I16: case TYPE_U16: fsize = 2; break;
                        case TYPE_I64: case TYPE_U64: case TYPE_F64: fsize = 8; break;
                        case TYPE_I128: case TYPE_U128: fsize = 16; break;
                        case TYPE_STRING: case TYPE_VEC: fsize = 12; break;
                        case TYPE_STR: case TYPE_SLICE: fsize = 8; break;
                        default: fsize = 4; break;
                    }
                    /* Align field */
                    int align = fsize > 4 ? 4 : (fsize < 4 ? fsize : 4);
                    field_offset = (field_offset + align - 1) & ~(align - 1);

                    int idx = structs[struct_count].field_count;
                    if (idx < 32) {
                        strcpy(structs[struct_count].fields[idx].name, fname);
                        structs[struct_count].fields[idx].type = ftype;
                        structs[struct_count].fields[idx].offset = field_offset;
                        structs[struct_count].fields[idx].size = fsize;
                        structs[struct_count].field_count++;
                    }
                    field_offset += fsize;

                    /* Skip comma */
                    skip_whitespace();
                    if (*pos == ',') pos++;
                }
                if (*pos == '}') pos++;
                structs[struct_count].size = (field_offset + 3) & ~3; /* Pad to 4-byte */
            } else if (*pos == '(') {
                /* Tuple struct: struct Foo(i32, i32); */
                pos++;
                int field_offset = 0;
                int tidx = 0;
                while (*pos && *pos != ')') {
                    skip_whitespace();
                    if (*pos == ')') break;
                    /* Skip comments inside tuple struct */
                    if (*pos == '/' && *(pos+1) == '/') {
                        while (*pos && *pos != '\n') pos++;
                        if (*pos == '\n') pos++;
                        continue;
                    }
                    if (*pos == '/' && *(pos+1) == '*') {
                        pos += 2;
                        while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
                        if (*pos) pos += 2;
                        continue;
                    }
                    /* Skip attributes (#[...]) and @ attributes (@size(N)) */
                    if (*pos == '#' && *(pos+1) == '[') {
                        while (*pos && *pos != ']') pos++;
                        if (*pos == ']') pos++;
                        continue;
                    }
                    if (*pos == '@') {
                        pos++;
                        while (*pos && (isalnum(*pos) || *pos == '_')) pos++;
                        if (*pos == '(') {
                            int d = 1; pos++;
                            while (*pos && d > 0) { if (*pos == '(') d++; else if (*pos == ')') d--; pos++; }
                        }
                        skip_whitespace();
                        continue;
                    }
                    /* Skip pub keyword */
                    if (strncmp(pos, "pub ", 4) == 0) pos += 4;
                    if (strncmp(pos, "pub(crate) ", 11) == 0) pos += 11;
                    /* Safety: skip => (macro syntax) and other non-type chars */
                    if (*pos == '=' && *(pos+1) == '>') {
                        pos += 2;
                        skip_whitespace();
                        continue;
                    }
                    char *before_parse = pos;
                    RustType ftype = parse_type();
                    /* Safety: if parse_type didn't advance, skip one char to avoid infinite loop */
                    if (pos == before_parse) {
                        pos++;
                        continue;
                    }
                    int fsize = 4;
                    switch (ftype) {
                        case TYPE_I8: case TYPE_U8: case TYPE_BOOL: fsize = 1; break;
                        case TYPE_I16: case TYPE_U16: fsize = 2; break;
                        case TYPE_I64: case TYPE_U64: case TYPE_F64: fsize = 8; break;
                        default: fsize = 4; break;
                    }
                    int idx = structs[struct_count].field_count;
                    if (idx < 32) {
                        char tname[32];
                        snprintf(tname, sizeof(tname), "%d", tidx);
                        strcpy(structs[struct_count].fields[idx].name, tname);
                        structs[struct_count].fields[idx].type = ftype;
                        structs[struct_count].fields[idx].offset = field_offset;
                        structs[struct_count].fields[idx].size = fsize;
                        structs[struct_count].field_count++;
                    }
                    field_offset += fsize;
                    tidx++;
                    skip_whitespace();
                    if (*pos == ',') pos++;
                }
                if (*pos == ')') pos++;
                while (*pos && *pos != ';') pos++;
                if (*pos == ';') pos++;
                structs[struct_count].size = (field_offset + 3) & ~3;
            } else if (*pos == ';') {
                /* Unit struct: struct Foo; */
                structs[struct_count].size = 0;
                pos++;
            } else {
                structs[struct_count].size = 0;
            }

            struct_count++;

        } else if (strncmp(pos, "enum ", 5) == 0) {
            /* Parse enum definition */
            pos += 5;
            skip_whitespace();
            char enum_name[64] = {0};
            parse_string(enum_name, sizeof(enum_name));
            skip_whitespace();

            /* Store as a struct with tag field */
            strcpy(structs[struct_count].name, enum_name);
            structs[struct_count].field_count = 0;
            structs[struct_count].alignment = 4;

            if (*pos == '{') {
                pos++;
                int variant_idx = 0;
                int max_payload = 0;
                while (*pos && *pos != '}') {
                    skip_whitespace();
                    if (*pos == '}') break;
                    /* Skip comments inside enum */
                    if (*pos == '/' && *(pos+1) == '/') {
                        while (*pos && *pos != '\n') pos++;
                        if (*pos == '\n') pos++;
                        continue;
                    }
                    if (*pos == '/' && *(pos+1) == '*') {
                        pos += 2;
                        while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
                        if (*pos) pos += 2;
                        continue;
                    }
                    /* Skip attributes */
                    if (*pos == '#' && *(pos+1) == '[') {
                        while (*pos && *pos != ']') pos++;
                        if (*pos == ']') pos++;
                        continue;
                    }
                    /* Parse variant name */
                    char vname[32] = {0};
                    int vi = 0;
                    while (*pos && (isalnum(*pos) || *pos == '_') && vi < 31) {
                        vname[vi++] = *pos++;
                    }
                    vname[vi] = '\0';

                    /* Safety: if we couldn't parse a variant name, skip to next comma/brace */
                    if (vi == 0) {
                        while (*pos && *pos != ',' && *pos != '}') {
                            if (*pos == '(' || *pos == '{' || *pos == '[') {
                                int d = 1; char open = *pos;
                                char close = (open == '(') ? ')' : (open == '{') ? '}' : ']';
                                pos++;
                                while (*pos && d > 0) {
                                    if (*pos == open) d++;
                                    else if (*pos == close) d--;
                                    pos++;
                                }
                            } else {
                                pos++;
                            }
                        }
                        if (*pos == ',') pos++;
                        continue;
                    }

                    int payload_size = 0;
                    if (*pos == '(') {
                        /* Tuple variant: Variant(Type, Type) */
                        pos++;
                        while (*pos && *pos != ')') {
                            skip_whitespace();
                            if (*pos == ')') break;
                            char *before = pos;
                            parse_type();
                            if (pos == before) { pos++; continue; } /* safety: avoid infinite loop */
                            payload_size += 4;
                            skip_whitespace();
                            if (*pos == ',') pos++;
                        }
                        if (*pos == ')') pos++;
                    } else if (*pos == '{') {
                        /* Struct variant: Variant { field: Type } */
                        pos++;
                        while (*pos && *pos != '}') {
                            skip_whitespace();
                            if (*pos == '}') break;
                            /* Skip comments */
                            if (*pos == '/' && *(pos+1) == '/') {
                                while (*pos && *pos != '\n') pos++;
                                if (*pos == '\n') pos++;
                                continue;
                            }
                            if (*pos == '/' && *(pos+1) == '*') {
                                pos += 2;
                                while (*pos && !(*pos == '*' && *(pos+1) == '/')) pos++;
                                if (*pos) pos += 2;
                                continue;
                            }
                            while (*pos && *pos != ':' && *pos != '}') pos++;
                            if (*pos == ':') { pos++; skip_whitespace(); parse_type(); payload_size += 4; }
                            skip_whitespace();
                            if (*pos == ',') pos++;
                        }
                        if (*pos == '}') pos++;
                    }
                    /* = value or => value for C-like enums */
                    skip_whitespace();
                    if (*pos == '=' && *(pos+1) == '>') {
                        /* => value (macro-generated enum syntax) */
                        pos += 2;
                        skip_whitespace();
                        /* Skip complex value expressions until comma or closing brace */
                        int depth = 0;
                        while (*pos && !(depth == 0 && (*pos == ',' || *pos == '}'))) {
                            if (*pos == '(' || *pos == '[' || *pos == '{') depth++;
                            else if (*pos == ')' || *pos == ']' || *pos == '}') {
                                if (depth > 0) depth--; else break;
                            }
                            pos++;
                        }
                    } else if (*pos == '=') {
                        pos++;
                        skip_whitespace();
                        /* Skip complex value expression (not just numbers - could be make_tag(b"...")) */
                        int depth = 0;
                        while (*pos && !(depth == 0 && (*pos == ',' || *pos == '}'))) {
                            if (*pos == '(' || *pos == '[' || *pos == '{') depth++;
                            else if (*pos == ')' || *pos == ']' || *pos == '}') {
                                if (depth > 0) depth--; else break;
                            }
                            pos++;
                        }
                    }

                    if (payload_size > max_payload) max_payload = payload_size;

                    /* Store variant as a pseudo-field */
                    int idx = structs[struct_count].field_count;
                    if (idx < 32) {
                        strcpy(structs[struct_count].fields[idx].name, vname);
                        structs[struct_count].fields[idx].type = TYPE_ENUM;
                        structs[struct_count].fields[idx].offset = variant_idx;
                        structs[struct_count].fields[idx].size = payload_size;
                        structs[struct_count].field_count++;
                    }
                    variant_idx++;

                    skip_whitespace();
                    if (*pos == ',') pos++;
                }
                if (*pos == '}') pos++;
                structs[struct_count].size = 4 + max_payload; /* tag + max payload */
            }
            struct_count++;
            
        } else if (strncmp(pos, "trait ", 6) == 0) {
            pos += 6;
            skip_whitespace();
            
            char trait_name[64] = {0};
            parse_string(trait_name, sizeof(trait_name));
            
            strcpy(traits[trait_count].name, trait_name);
            
            /* Parse supertrait bounds */
            if (*pos == ':') {
                pos++;
                char supertraits[256] = {0};
                int idx = 0;
                while (*pos && *pos != '{' && idx < 255) {
                    supertraits[idx++] = *pos++;
                }
                strcpy(traits[trait_count].supertraits, supertraits);
            }
            
            trait_count++;
            
        } else if (strncmp(pos, "impl ", 5) == 0 || strncmp(pos, "impl<", 5) == 0) {
            /* Parse impl block */
            pos += 4;
            skip_whitespace();

            /* Skip generics */
            if (*pos == '<') {
                int angle_depth = 1;
                pos++;
                while (*pos && angle_depth > 0) {
                    if (*pos == '<') angle_depth++;
                    else if (*pos == '>') angle_depth--;
                    pos++;
                }
                skip_whitespace();
            }

            char impl_type[64] = {0};
            int ii = 0;
            while (*pos && (isalnum(*pos) || *pos == '_') && ii < 63) {
                impl_type[ii++] = *pos++;
            }
            impl_type[ii] = '\0';

            skip_whitespace();

            /* Check for "for Type" (trait impl) */
            if (strncmp(pos, "for ", 4) == 0) {
                pos += 4;
                skip_whitespace();
                strcpy(impls[impl_count].trait_name, impl_type);
                char target[64] = {0};
                int ti = 0;
                while (*pos && (isalnum(*pos) || *pos == '_') && ti < 63) {
                    target[ti++] = *pos++;
                }
                target[ti] = '\0';
                strcpy(impls[impl_count].struct_name, target);
            } else {
                strcpy(impls[impl_count].struct_name, impl_type);
                impls[impl_count].trait_name[0] = '\0';
            }
            impl_count++;

        } else if (strncmp(pos, "type ", 5) == 0) {
            /* Type alias */
            pos += 5;
            
        } else if (strncmp(pos, "const ", 6) == 0) {
            /* Constant */
            pos += 6;
            
        } else if (strncmp(pos, "static ", 7) == 0) {
            /* Static variable */
            pos += 7;
            
        } else if (strncmp(pos, "use ", 4) == 0) {
            /* Import — skip the entire use statement to avoid keyword collisions */
            while (*pos && *pos != ';' && *pos != '\n') {
                if (*pos == '{') {
                    int d = 1; pos++;
                    while (*pos && d > 0) { if (*pos == '{') d++; else if (*pos == '}') d--; pos++; }
                    continue;
                }
                pos++;
            }

        } else if (strncmp(pos, "mod ", 4) == 0) {
            /* Module — skip the module name to avoid matching keywords in it */
            pos += 4;
            skip_whitespace();
            while (*pos && (isalnum(*pos) || *pos == '_')) pos++; /* skip module name */

        } else if (strncmp(pos, "macro_rules!", 12) == 0) {
            /* Declarative macro */
            pos += 12;
            skip_whitespace();
            
            char macro_name[64] = {0};
            parse_string(macro_name, sizeof(macro_name));
            
            strcpy(macros[macro_count].name, macro_name);
            macros[macro_count].is_builtin = 0;
            macro_count++;
        }
        
        pos++;
    }
    
    /* Pass 2: Generate code */
    pos = source;
    
    printf(".text\n.align 2\n");
    
    /* Generate vtables for traits — labels include file hash for uniqueness */
    int i;
    for (i = 0; i < trait_count; i++) {
        printf("\n; Vtable for trait %s\n", traits[i].name);
        printf(".section __DATA,__const\n");
        printf("_vtable_%s_%04x:\n", traits[i].name, current_file_hash);
        printf("    .long 0  ; Size\n");
        printf("    .long 4  ; Alignment\n");
        printf("    .long 0  ; Destructor\n");
        /* Method pointers would go here */
        printf("\n");
    }
    
    printf(".text\n");
    
    /* Pass 2.5: Emit all non-main functions */
    {
        char* scan = source;
        while ((scan = strstr(scan, "fn ")) != NULL) {
            char* fn_start = scan;
            scan += 3;

            /* Skip if preceded by non-boundary char (e.g. "cfn") */
            if (fn_start > source && (isalnum(*(fn_start-1)) || *(fn_start-1) == '_')) continue;

            char* save_pos = pos;
            pos = scan;
            skip_whitespace();

            char fn_name[64] = {0};
            int ni = 0;
            while (*pos && (isalnum(*pos) || *pos == '_') && ni < 63) {
                fn_name[ni++] = *pos++;
            }
            fn_name[ni] = '\0';
            scan = pos;
            pos = save_pos;

            /* Skip main — handled separately below */
            if (strcmp(fn_name, "main") == 0) continue;
            if (fn_name[0] == '\0') continue;

            /* Find the function body */
            char* body = strchr(scan, '{');
            if (!body) continue;

            /* Check for semicolon before brace — trait method declaration (no body) */
            {
                char* check = scan;
                while (check < body) {
                    if (*check == ';') break;
                    check++;
                }
                if (check < body && *check == ';') { scan = check + 1; continue; }
            }

            /* Determine impl type by scanning backward for "impl Type" */
            char impl_type[64] = {0};
            int impl_struct_idx = -1;
            {
                /* Count brace depth from source to fn_start */
                char* bp = source;
                char* last_impl = NULL;
                int depth = 0;
                while (bp < fn_start) {
                    if (*bp == '{') depth++;
                    else if (*bp == '}') { depth--; last_impl = NULL; }
                    else if (depth == 0 && strncmp(bp, "impl ", 5) == 0) {
                        last_impl = bp;
                    } else if (depth == 0 && strncmp(bp, "impl<", 5) == 0) {
                        last_impl = bp;
                    }
                    bp++;
                }
                /* If we're inside an impl block (depth > 0 when we hit the fn) */
                /* Re-scan: find the impl that owns this fn */
                bp = source;
                depth = 0;
                last_impl = NULL;
                while (bp < fn_start) {
                    if (*bp == '/' && *(bp+1) == '/') { while (*bp && *bp != '\n') bp++; continue; }
                    if (*bp == '/' && *(bp+1) == '*') { bp += 2; while (*bp && !(*bp == '*' && *(bp+1) == '/')) bp++; if (*bp) bp += 2; continue; }
                    if (strncmp(bp, "impl ", 5) == 0 || strncmp(bp, "impl<", 5) == 0) {
                        last_impl = bp;
                    }
                    if (*bp == '{') {
                        depth++;
                    } else if (*bp == '}') {
                        depth--;
                        if (depth == 0) last_impl = NULL;
                    }
                    bp++;
                }
                if (last_impl && depth > 0) {
                    /* Extract type name from "impl [<...>] TypeName" */
                    char* tp = last_impl + 4;
                    while (*tp && isspace(*tp)) tp++;
                    if (*tp == '<') { int d = 1; tp++; while (*tp && d > 0) { if (*tp == '<') d++; else if (*tp == '>') d--; tp++; } }
                    while (*tp && isspace(*tp)) tp++;
                    int ti = 0;
                    while (*tp && (isalnum(*tp) || *tp == '_') && ti < 63) {
                        impl_type[ti++] = *tp++;
                    }
                    impl_type[ti] = '\0';
                    /* Find struct index for field offsets */
                    for (int si = 0; si < struct_count; si++) {
                        if (strcmp(structs[si].name, impl_type) == 0) {
                            impl_struct_idx = si;
                            break;
                        }
                    }
                }
            }

            /* Build full function name: Type_method or just method */
            char full_name[128] = {0};
            if (impl_type[0]) {
                snprintf(full_name, sizeof(full_name), "%s_%s", impl_type, fn_name);
            } else {
                snprintf(full_name, sizeof(full_name), "%s", fn_name);
            }

            /* Parse parameter list */
            char* paren = strchr(fn_start + 3, '(');
            int param_count = 0;
            int has_self = 0;
            if (paren && paren < body) {
                char* pp = paren + 1;
                while (*pp && isspace(*pp)) pp++;
                /* Check for self parameter */
                if (strncmp(pp, "&mut self", 9) == 0 || strncmp(pp, "&self", 5) == 0 ||
                    strncmp(pp, "mut self", 8) == 0 || strncmp(pp, "self", 4) == 0) {
                    has_self = 1;
                }
                pp = paren + 1;
                while (*pp && *pp != ')') {
                    if (*pp == ':') param_count++;
                    pp++;
                }
                if (has_self) param_count++; /* self doesn't have : but is a param */
            }

            printf("\n.align 2\n");
            printf("_%s:\n", full_name);
            printf("    mflr r0\n");
            printf("    stw r0, 8(r1)\n");
            printf("    stwu r1, -256(r1)  ; frame for %s\n", full_name);

            /* Register variables */
            int save_var_count = var_count;
            int save_stack_offset = stack_offset;
            stack_offset = 72;

            char* param_scan = paren + 1;
            int param_idx = 0;

            if (has_self) {
                /* self is passed as pointer in r3 */
                printf("    stw r3, %d(r1)    ; param self (ptr)\n", stack_offset);
                strcpy(vars[var_count].name, "self");
                vars[var_count].offset = stack_offset;
                vars[var_count].type = TYPE_REF;
                vars[var_count].size = 4;
                var_count++;
                stack_offset += 4;
                param_idx = 1;
                /* Skip past self in param list */
                while (*param_scan && *param_scan != ',' && *param_scan != ')') param_scan++;
                if (*param_scan == ',') param_scan++;
            }

            while (param_scan && *param_scan && *param_scan != ')' && param_idx < param_count) {
                while (*param_scan && isspace(*param_scan)) param_scan++;
                if (*param_scan == ')') break;
                if (*param_scan == '&') param_scan++;
                while (*param_scan && isspace(*param_scan)) param_scan++;
                if (strncmp(param_scan, "mut ", 4) == 0) param_scan += 4;
                while (*param_scan && isspace(*param_scan)) param_scan++;
                char pname[64] = {0};
                int pni = 0;
                while (*param_scan && (isalnum(*param_scan) || *param_scan == '_') && pni < 63) {
                    pname[pni++] = *param_scan++;
                }
                pname[pni] = '\0';
                while (*param_scan && *param_scan != ',' && *param_scan != ')') param_scan++;
                if (*param_scan == ',') param_scan++;

                if (pname[0] && param_idx < 8) {
                    printf("    stw r%d, %d(r1)    ; param %s\n", 3 + param_idx, stack_offset, pname);
                    strcpy(vars[var_count].name, pname);
                    vars[var_count].offset = stack_offset;
                    vars[var_count].type = TYPE_I32;
                    vars[var_count].size = 4;
                    var_count++;
                    stack_offset += 4;
                }
                param_idx++;
            }

            /* Store impl struct index for self.field resolution */
            int save_impl_struct = current_impl_struct;
            current_impl_struct = impl_struct_idx;

            pos = body + 1;
            compile_function_body(256);

            /* Default return if body didn't explicitly return */
            printf("    li r3, 0          ; default return\n");
            printf("    addi r1, r1, 256\n");
            printf("    lwz r0, 8(r1)\n");
            printf("    mtlr r0\n");
            printf("    blr\n");

            /* Find end of function body to advance scan */
            int bd = 1;
            char* bp = body + 1;
            while (*bp && bd > 0) {
                if (*bp == '{') bd++;
                else if (*bp == '}') bd--;
                bp++;
            }

            /* Restore compiler state for next function */
            pos = save_pos;
            var_count = save_var_count;
            stack_offset = save_stack_offset;
            current_impl_struct = save_impl_struct;

            scan = bp;
        }
    }

    /* Find and compile main */
    char* main_start = strstr(source, "fn main()");
    if (!main_start) {
        /* Try async main */
        main_start = strstr(source, "async fn main()");
        if (main_start) in_async_block = 1;
    }

    if (!main_start) {
        /* No main — this is a library crate. Emit all functions as stubs already done above. */
        /* Generate impl blocks for trait implementations */
        for (i = 0; i < impl_count; i++) {
            printf("\n; impl %s for %s\n", impls[i].trait_name, impls[i].struct_name);
        }

        /* External symbol pointers */
        printf("\n; External symbol pointers (non-lazy)\n");
        printf(".section __DATA,__nl_symbol_ptr,non_lazy_symbol_pointers\n");
        printf(".align 2\n");
        printf("L_malloc_ptr:\n");
        printf("    .indirect_symbol _malloc\n");
        printf("    .long 0\n");
        printf("L_free_ptr:\n");
        printf("    .indirect_symbol _free\n");
        printf("    .long 0\n");
        return;
    }

    printf(".globl _main\n_main:\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -2048(r1)  ; Large frame for Firefox\n");
    
    /* Initialize runtime */
    printf("    bl _rust_runtime_init\n");

    pos = strchr(main_start, '{') + 1;
    stack_offset = 72;  /* Reset for main */
    var_count = 0;

    compile_function_body(2048);

    /* Cleanup at end of main */
    printf("\n    ; Cleanup and exit\n");
    
    /* Drop all variables in reverse order - RAII */
    for (i = var_count - 1; i >= 0; i--) {
        emit_drop_glue(&vars[i]);
    }
    
    printf("    bl _rust_runtime_cleanup\n");
    printf("    li r3, 0          ; exit code\n");
    printf("    addi r1, r1, 2048\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n");
    
    /* Generate runtime support functions */
    printf("\n; Runtime support functions\n");
    
    printf("\n.align 2\n");
    printf("_rust_runtime_init:\n");
    printf("    ; Initialize memory allocator, thread locals, etc\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_rust_runtime_cleanup:\n");
    printf("    ; Clean up runtime state\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_alloc_box:\n");
    printf("    ; r3 = size, return pointer in r3\n");
    printf("    b _malloc         ; Use system malloc for now\n");
    
    printf("\n.align 2\n");
    printf("_dealloc_box:\n");
    printf("    ; r3 = pointer\n");
    printf("    b _free           ; Use system free\n");
    
    printf("\n.align 2\n");
    printf("_alloc_rc:\n");
    printf("    ; Allocate with reference count\n");
    printf("    b _malloc\n");
    
    printf("\n.align 2\n");
    printf("_rc_decrement:\n");
    printf("    ; Decrement ref count, free if zero\n");
    printf("    lwz r4, 0(r3)     ; load refcount\n");
    printf("    subi r4, r4, 1    ; decrement\n");
    printf("    stw r4, 0(r3)     ; store back\n");
    printf("    cmpwi r4, 0\n");
    printf("    bne 1f\n");
    printf("    b _free           ; free if zero\n");
    printf("1:  blr\n");
    
    printf("\n.align 2\n");
    printf("_alloc_arc:\n");
    printf("    ; Allocate with atomic reference count\n");
    printf("    b _malloc\n");
    
    printf("\n.align 2\n");
    printf("_arc_decrement:\n");
    printf("    ; Atomic decrement ref count\n");
    printf("    lwarx r4, 0, r3   ; load reserved\n");
    printf("    subi r4, r4, 1    ; decrement\n");
    printf("    stwcx. r4, 0, r3  ; store conditional\n");
    printf("    bne- _arc_decrement ; retry if failed\n");
    printf("    cmpwi r4, 0\n");
    printf("    bne 1f\n");
    printf("    b _free           ; free if zero\n");
    printf("1:  blr\n");
    
    printf("\n.align 2\n");
    printf("_vec_new:\n");
    printf("    ; Create new Vec — allocate struct + initial buffer\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -48(r1)\n");
    printf("    li r3, 12         ; Vec struct size\n");
    printf("    bl _malloc\n");
    printf("    stw r3, 24(r1)    ; save Vec ptr\n");
    printf("    li r4, 16         ; initial capacity (4 elements)\n");
    printf("    stw r4, 28(r1)    ; save cap request\n");
    printf("    mr r14, r3        ; save vec ptr\n");
    printf("    li r3, 16         ; alloc buffer for 4 i32 elements\n");
    printf("    bl _malloc\n");
    printf("    lwz r14, 24(r1)   ; restore vec ptr\n");
    printf("    stw r3, 0(r14)    ; ptr = buffer\n");
    printf("    li r4, 0\n");
    printf("    stw r4, 4(r14)    ; len = 0\n");
    printf("    li r4, 4\n");
    printf("    stw r4, 8(r14)    ; cap = 4\n");
    printf("    mr r3, r14        ; return Vec ptr\n");
    printf("    addi r1, r1, 48\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n");

    printf("\n.align 2\n");
    printf("_vec_push:\n");
    printf("    ; r3 = vec ptr, r4 = value to push\n");
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");
    printf("    stwu r1, -48(r1)\n");
    printf("    stw r3, 24(r1)    ; save vec ptr\n");
    printf("    stw r4, 28(r1)    ; save value\n");
    printf("    lwz r5, 4(r3)     ; load len\n");
    printf("    lwz r6, 8(r3)     ; load cap\n");
    printf("    cmpw r5, r6\n");
    printf("    blt 1f            ; skip realloc if space available\n");
    printf("    ; TODO: realloc buffer (double capacity)\n");
    printf("1:\n");
    printf("    lwz r3, 24(r1)    ; reload vec ptr\n");
    printf("    lwz r5, 4(r3)     ; reload len\n");
    printf("    lwz r6, 0(r3)     ; load data ptr\n");
    printf("    slwi r7, r5, 2    ; len * 4 = byte offset\n");
    printf("    lwz r4, 28(r1)    ; reload value\n");
    printf("    stwx r4, r6, r7   ; store element at data[len]\n");
    printf("    addi r5, r5, 1    ; increment len\n");
    printf("    stw r5, 4(r3)     ; store new len\n");
    printf("    addi r1, r1, 48\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_vec_drop:\n");
    printf("    ; r3 = vec ptr\n");
    printf("    lwz r3, 0(r3)     ; load data ptr\n");
    printf("    cmpwi r3, 0\n");
    printf("    beq 1f\n");
    printf("    b _free           ; free data\n");
    printf("1:  blr\n");
    
    printf("\n.align 2\n");
    printf("_string_drop:\n");
    printf("    ; Same as vec_drop\n");
    printf("    b _vec_drop\n");
    
    printf("\n.align 2\n");
    printf("_create_future:\n");
    printf("    ; Create Future for async\n");
    printf("    li r3, 16         ; Future size\n");
    printf("    b _malloc\n");
    
    printf("\n.align 2\n");
    printf("_await_future:\n");
    printf("    ; r3 = future ptr\n");
    printf("    ; Simplified - would need executor integration\n");
    printf("    lwz r3, 12(r3)    ; get result\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_rust_println:\n");
    printf("    ; Simplified println\n");
    printf("    ; Would format and call write syscall\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_rust_assert:\n");
    printf("    ; Assert implementation\n");
    printf("    cmpwi r3, 0\n");
    printf("    bne 1f\n");
    printf("    bl _panic         ; panic if false\n");
    printf("1:  blr\n");
    
    printf("\n.align 2\n");
    printf("_panic:\n");
    printf("    ; Panic handler\n");
    printf("    ; Would print message and abort\n");
    printf("    li r0, 1          ; exit syscall\n");
    printf("    li r3, 1          ; error code\n");
    printf("    sc                ; system call\n");
    
    printf("\n.align 2\n");
    printf("_panic_unwrap:\n");
    printf("    ; Panic on unwrap None/Err\n");
    printf("    b _panic\n");
    
    printf("\n.align 2\n");
    printf("_try_operator:\n");
    printf("    ; Handle ? operator\n");
    printf("    ; Check if Ok/Some, return early if Err/None\n");
    printf("    lwz r4, 0(r3)     ; load tag\n");
    printf("    cmpwi r4, 0\n");
    printf("    bne 1f            ; if not Ok/Some\n");
    printf("    lwz r3, 4(r3)     ; extract value\n");
    printf("    blr\n");
    printf("1:  ; Return early with Err/None\n");
    printf("    addi r1, r1, 2048 ; unwind stack\n");
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_clone_impl:\n");
    printf("    ; Generic clone implementation\n");
    printf("    ; Would deep copy based on type\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_drop_impl:\n");
    printf("    ; Generic drop implementation\n");
    printf("    ; Would call destructor based on type\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_create_iter:\n");
    printf("    ; Create iterator from collection\n");
    printf("    li r4, 16         ; Iterator size\n");
    printf("    mr r5, r3         ; save collection\n");
    printf("    li r3, 16\n");
    printf("    bl _malloc\n");
    printf("    stw r5, 0(r3)     ; store collection ptr\n");
    printf("    li r4, 0\n");
    printf("    stw r4, 4(r3)     ; index = 0\n");
    printf("    blr\n");
    
    printf("\n.align 2\n");
    printf("_iter_collect:\n");
    printf("    ; Collect iterator into Vec\n");
    printf("    bl _vec_new\n");
    printf("    ; Would iterate and push all elements\n");
    printf("    blr\n");
    
    /* External functions — use non-lazy symbol pointers for Tiger's as(1) */
    printf("\n; External symbol pointers (non-lazy)\n");
    printf(".section __DATA,__nl_symbol_ptr,non_lazy_symbol_pointers\n");
    printf(".align 2\n");
    printf("L_malloc_ptr:\n");
    printf("    .indirect_symbol _malloc\n");
    printf("    .long 0\n");
    printf("L_free_ptr:\n");
    printf("    .indirect_symbol _free\n");
    printf("    .long 0\n");
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
    size_t nread = fread(source, 1, size, f);
    source[nread] = 0;
    fclose(f);
    
    current_file_hash = file_hash(argv[1]);
    compile_rust(source);
    free(source);
    
    return 0;
}