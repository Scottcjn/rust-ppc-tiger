/*
 * Rust Function & Trait Compilation for PowerPC
 * Handles multiple functions, generics, and trait dispatch
 *
 * This enables Firefox's heavy use of traits and generic code
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
 * FUNCTION REPRESENTATION
 * ============================================================ */

typedef enum {
    FN_NORMAL,
    FN_CONST,
    FN_ASYNC,
    FN_UNSAFE,
    FN_EXTERN
} FunctionKind;

typedef struct Parameter {
    char name[64];
    char type_str[128];
    int is_self;
    int is_mut_self;
    int is_ref;
    int is_mut_ref;
} Parameter;

typedef struct Function {
    char name[64];
    char mangled_name[256];
    FunctionKind kind;

    Parameter params[16];
    int param_count;

    char return_type[128];
    char generic_params[256];   /* <T, U: Clone> */
    char where_clause[512];     /* where T: Display */

    int stack_size;
    int is_method;              /* Part of impl block */
    char self_type[64];         /* Type of self for methods */

    /* For code generation */
    int label_counter;
    int temp_var_count;
} Function;

/* ============================================================
 * TRAIT REPRESENTATION
 * ============================================================ */

typedef struct TraitMethod {
    char name[64];
    char signature[256];
    int has_default_impl;
} TraitMethod;

typedef struct Trait {
    char name[64];
    char generic_params[128];
    char supertraits[256];      /* : Clone + Debug */

    TraitMethod methods[32];
    int method_count;

    /* Associated types */
    char assoc_types[8][64];
    int assoc_type_count;

    /* Associated constants */
    char assoc_consts[8][128];
    int assoc_const_count;
} Trait;

/* ============================================================
 * IMPL BLOCK & VTABLE
 * ============================================================ */

typedef struct ImplMethod {
    char name[64];
    int function_index;         /* Index into functions array */
} ImplMethod;

typedef struct ImplBlock {
    char type_name[64];         /* Type we're implementing for */
    char trait_name[64];        /* Trait (empty for inherent impl) */
    char generic_params[128];

    ImplMethod methods[32];
    int method_count;
} ImplBlock;

typedef struct VTable {
    char type_name[64];
    char trait_name[64];
    char method_ptrs[32][64];   /* Mangled function names */
    int method_count;
    int size;
    int alignment;
} VTable;

/* ============================================================
 * GLOBAL STATE
 * ============================================================ */

#define MAX_FUNCTIONS 500
#define MAX_TRAITS 100
#define MAX_IMPLS 200
#define MAX_VTABLES 500

Function functions[MAX_FUNCTIONS];
Trait traits[MAX_TRAITS];
ImplBlock impls[MAX_IMPLS];
VTable vtables[MAX_VTABLES];

int function_count = 0;
int trait_count = 0;
int impl_count = 0;
int vtable_count = 0;

/* ============================================================
 * NAME MANGLING
 * ============================================================
 *
 * Rust uses complex name mangling for generics.
 * Format: _ZN<crate_len><crate><module_len><module><fn_len><fn>E<type_params>
 */

void mangle_name(Function* fn, char* output) {
    /* Simplified mangling for PowerPC Mach-O */
    if (fn->generic_params[0]) {
        snprintf(output, 255, "_%s$%s", fn->name, fn->generic_params);
        /* Replace < > , with _ */
        for (char* p = output; *p; p++) {
            if (*p == '<' || *p == '>' || *p == ',' || *p == ' ') *p = '_';
        }
    } else {
        snprintf(output, 255, "_%s", fn->name);
    }
}

void mangle_method(const char* type_name, const char* method_name, char* output) {
    snprintf(output, 255, "_%s_%s", type_name, method_name);
}

/* ============================================================
 * PARSING FUNCTIONS
 * ============================================================ */

void skip_ws(char** pos) {
    while (**pos && isspace(**pos)) (*pos)++;
}

void parse_ident(char** pos, char* out, int max) {
    int i = 0;
    while (**pos && (isalnum(**pos) || **pos == '_') && i < max - 1) {
        out[i++] = *(*pos)++;
    }
    out[i] = '\0';
}

Function* parse_function(char** pos) {
    skip_ws(pos);

    Function* fn = &functions[function_count];
    memset(fn, 0, sizeof(Function));

    /* Parse modifiers */
    while (1) {
        if (strncmp(*pos, "const ", 6) == 0) {
            fn->kind = FN_CONST;
            *pos += 6;
        } else if (strncmp(*pos, "async ", 6) == 0) {
            fn->kind = FN_ASYNC;
            *pos += 6;
        } else if (strncmp(*pos, "unsafe ", 7) == 0) {
            fn->kind = FN_UNSAFE;
            *pos += 7;
        } else if (strncmp(*pos, "extern ", 7) == 0) {
            fn->kind = FN_EXTERN;
            *pos += 7;
            /* Skip ABI string like "C" */
            if (**pos == '"') {
                (*pos)++;
                while (**pos && **pos != '"') (*pos)++;
                if (**pos == '"') (*pos)++;
            }
        } else if (strncmp(*pos, "pub ", 4) == 0) {
            *pos += 4;
        } else {
            break;
        }
        skip_ws(pos);
    }

    /* Expect "fn" */
    if (strncmp(*pos, "fn ", 3) != 0) return NULL;
    *pos += 3;
    skip_ws(pos);

    /* Function name */
    parse_ident(pos, fn->name, 64);
    skip_ws(pos);

    /* Generic parameters <T, U: Trait> */
    if (**pos == '<') {
        (*pos)++;
        int depth = 1;
        int i = 0;
        while (**pos && depth > 0 && i < 255) {
            if (**pos == '<') depth++;
            if (**pos == '>') depth--;
            if (depth > 0) fn->generic_params[i++] = **pos;
            (*pos)++;
        }
        fn->generic_params[i] = '\0';
    }
    skip_ws(pos);

    /* Parameters */
    if (**pos == '(') {
        (*pos)++;
        skip_ws(pos);

        while (**pos && **pos != ')') {
            Parameter* param = &fn->params[fn->param_count];
            memset(param, 0, sizeof(Parameter));

            /* Check for self parameter */
            if (strncmp(*pos, "&mut self", 9) == 0) {
                param->is_self = 1;
                param->is_mut_self = 1;
                param->is_ref = 1;
                param->is_mut_ref = 1;
                strcpy(param->name, "self");
                *pos += 9;
                fn->is_method = 1;
            } else if (strncmp(*pos, "&self", 5) == 0) {
                param->is_self = 1;
                param->is_ref = 1;
                strcpy(param->name, "self");
                *pos += 5;
                fn->is_method = 1;
            } else if (strncmp(*pos, "mut self", 8) == 0) {
                param->is_self = 1;
                param->is_mut_self = 1;
                strcpy(param->name, "self");
                *pos += 8;
                fn->is_method = 1;
            } else if (strncmp(*pos, "self", 4) == 0 &&
                       !isalnum(*(*pos + 4)) && *(*pos + 4) != '_') {
                param->is_self = 1;
                strcpy(param->name, "self");
                *pos += 4;
                fn->is_method = 1;
            } else {
                /* Regular parameter: name: type */
                if (strncmp(*pos, "mut ", 4) == 0) {
                    *pos += 4;
                }
                parse_ident(pos, param->name, 64);
                skip_ws(pos);

                if (**pos == ':') {
                    (*pos)++;
                    skip_ws(pos);

                    /* Parse type */
                    if (**pos == '&') {
                        param->is_ref = 1;
                        (*pos)++;
                        if (strncmp(*pos, "mut ", 4) == 0) {
                            param->is_mut_ref = 1;
                            *pos += 4;
                        }
                    }

                    int i = 0;
                    int depth = 0;
                    while (**pos && i < 127) {
                        if (**pos == '<') depth++;
                        if (**pos == '>') depth--;
                        if (depth == 0 && (**pos == ',' || **pos == ')')) break;
                        param->type_str[i++] = *(*pos)++;
                    }
                    param->type_str[i] = '\0';
                }
            }

            fn->param_count++;
            skip_ws(pos);
            if (**pos == ',') (*pos)++;
            skip_ws(pos);
        }

        if (**pos == ')') (*pos)++;
    }
    skip_ws(pos);

    /* Return type */
    if (strncmp(*pos, "->", 2) == 0) {
        *pos += 2;
        skip_ws(pos);

        int i = 0;
        int depth = 0;
        while (**pos && i < 127) {
            if (**pos == '<') depth++;
            if (**pos == '>') depth--;
            if (**pos == '{' || (depth == 0 && strncmp(*pos, "where", 5) == 0)) break;
            fn->return_type[i++] = *(*pos)++;
        }
        fn->return_type[i] = '\0';
        /* Trim trailing whitespace */
        while (i > 0 && isspace(fn->return_type[i-1])) fn->return_type[--i] = '\0';
    }
    skip_ws(pos);

    /* Where clause */
    if (strncmp(*pos, "where", 5) == 0) {
        *pos += 5;
        skip_ws(pos);

        int i = 0;
        while (**pos && **pos != '{' && i < 511) {
            fn->where_clause[i++] = *(*pos)++;
        }
        fn->where_clause[i] = '\0';
    }

    /* Generate mangled name */
    mangle_name(fn, fn->mangled_name);

    function_count++;
    return fn;
}

/* ============================================================
 * CODE GENERATION FOR FUNCTIONS
 * ============================================================ */

void emit_function_prologue(Function* fn) {
    printf("\n.align 2\n");
    printf(".globl %s\n", fn->mangled_name);
    printf("%s:\n", fn->mangled_name);

    /* Standard PowerPC prologue */
    printf("    mflr r0\n");
    printf("    stw r0, 8(r1)\n");

    /* Calculate stack size: params + locals + 64 bytes min */
    fn->stack_size = 64 + (fn->param_count * 8);
    if (fn->stack_size < 64) fn->stack_size = 64;
    /* Align to 16 bytes */
    fn->stack_size = (fn->stack_size + 15) & ~15;

    printf("    stwu r1, -%d(r1)\n", fn->stack_size);

    /* Save callee-saved registers if needed */
    printf("    stw r13, %d(r1)    ; save r13-r31 if used\n", fn->stack_size - 4);

    /* Store parameters */
    for (int i = 0; i < fn->param_count && i < 8; i++) {
        printf("    stw r%d, %d(r1)    ; %s\n",
               3 + i, 24 + i * 4, fn->params[i].name);
    }
}

void emit_function_epilogue(Function* fn) {
    /* Restore callee-saved registers */
    printf("    lwz r13, %d(r1)\n", fn->stack_size - 4);

    /* Standard PowerPC epilogue */
    printf("    addi r1, r1, %d\n", fn->stack_size);
    printf("    lwz r0, 8(r1)\n");
    printf("    mtlr r0\n");
    printf("    blr\n");
}

/* ============================================================
 * TRAIT AND IMPL HANDLING
 * ============================================================ */

Trait* parse_trait(char** pos) {
    skip_ws(pos);

    if (strncmp(*pos, "trait ", 6) != 0) return NULL;
    *pos += 6;

    Trait* tr = &traits[trait_count];
    memset(tr, 0, sizeof(Trait));

    skip_ws(pos);
    parse_ident(pos, tr->name, 64);
    skip_ws(pos);

    /* Generics */
    if (**pos == '<') {
        (*pos)++;
        int i = 0;
        int depth = 1;
        while (**pos && depth > 0 && i < 127) {
            if (**pos == '<') depth++;
            if (**pos == '>') depth--;
            if (depth > 0) tr->generic_params[i++] = **pos;
            (*pos)++;
        }
        tr->generic_params[i] = '\0';
    }
    skip_ws(pos);

    /* Supertraits */
    if (**pos == ':') {
        (*pos)++;
        skip_ws(pos);
        int i = 0;
        while (**pos && **pos != '{' && i < 255) {
            tr->supertraits[i++] = *(*pos)++;
        }
        tr->supertraits[i] = '\0';
    }

    trait_count++;
    return tr;
}

ImplBlock* parse_impl(char** pos) {
    skip_ws(pos);

    if (strncmp(*pos, "impl", 4) != 0) return NULL;
    *pos += 4;

    ImplBlock* impl = &impls[impl_count];
    memset(impl, 0, sizeof(ImplBlock));

    skip_ws(pos);

    /* Generic params */
    if (**pos == '<') {
        (*pos)++;
        int i = 0;
        int depth = 1;
        while (**pos && depth > 0 && i < 127) {
            if (**pos == '<') depth++;
            if (**pos == '>') depth--;
            if (depth > 0) impl->generic_params[i++] = **pos;
            (*pos)++;
        }
        impl->generic_params[i] = '\0';
    }
    skip_ws(pos);

    /* Parse "TraitName for TypeName" or just "TypeName" */
    char first_name[64] = {0};
    parse_ident(pos, first_name, 64);
    skip_ws(pos);

    if (strncmp(*pos, "for ", 4) == 0) {
        /* impl Trait for Type */
        strcpy(impl->trait_name, first_name);
        *pos += 4;
        skip_ws(pos);
        parse_ident(pos, impl->type_name, 64);
    } else {
        /* impl Type (inherent impl) */
        strcpy(impl->type_name, first_name);
    }

    impl_count++;
    return impl;
}

/* ============================================================
 * VTABLE GENERATION
 * ============================================================ */

VTable* generate_vtable(ImplBlock* impl, Trait* tr) {
    VTable* vt = &vtables[vtable_count];
    memset(vt, 0, sizeof(VTable));

    strcpy(vt->type_name, impl->type_name);
    strcpy(vt->trait_name, impl->trait_name);

    /* Add size and alignment */
    vt->size = 8;       /* Default, would be calculated */
    vt->alignment = 4;

    /* Add method pointers */
    for (int i = 0; i < impl->method_count && i < 32; i++) {
        char mangled[64];
        mangle_method(impl->type_name, impl->methods[i].name, mangled);
        strcpy(vt->method_ptrs[vt->method_count++], mangled);
    }

    vtable_count++;
    return vt;
}

void emit_vtable(VTable* vt) {
    printf("\n; VTable for %s as %s\n", vt->type_name, vt->trait_name);
    printf(".section __DATA,__const\n");
    printf(".align 2\n");
    printf("_vtable_%s_as_%s:\n", vt->type_name, vt->trait_name);
    printf("    .long %d          ; size\n", vt->size);
    printf("    .long %d          ; alignment\n", vt->alignment);
    printf("    .long _drop_%s    ; destructor\n", vt->type_name);

    for (int i = 0; i < vt->method_count; i++) {
        printf("    .long %s    ; method %d\n", vt->method_ptrs[i], i);
    }

    printf(".text\n");
}

/* ============================================================
 * TRAIT OBJECT (DYN) HANDLING
 * ============================================================
 *
 * A trait object (dyn Trait) is a fat pointer:
 *   - 4 bytes: pointer to data
 *   - 4 bytes: pointer to vtable
 */

void emit_trait_object_call(const char* obj_name, const char* method_name,
                            int method_index) {
    printf("    ; %s.%s() via vtable\n", obj_name, method_name);
    printf("    lwz r3, 0(r14)    ; data ptr\n");
    printf("    lwz r12, 4(r14)   ; vtable ptr\n");
    printf("    lwz r12, %d(r12)  ; method %d ptr\n",
           12 + method_index * 4, method_index);
    printf("    mtctr r12\n");
    printf("    bctrl             ; call method\n");
}

/* ============================================================
 * GENERIC MONOMORPHIZATION
 * ============================================================
 *
 * For each concrete type used with a generic function,
 * we generate a specialized version.
 */

typedef struct {
    char generic_fn[64];
    char concrete_types[256];
    char mangled_name[256];
} MonomorphizedFn;

MonomorphizedFn mono_fns[1000];
int mono_fn_count = 0;

void monomorphize(Function* fn, const char* concrete_types) {
    /* Check if already monomorphized */
    for (int i = 0; i < mono_fn_count; i++) {
        if (strcmp(mono_fns[i].generic_fn, fn->name) == 0 &&
            strcmp(mono_fns[i].concrete_types, concrete_types) == 0) {
            return;  /* Already done */
        }
    }

    MonomorphizedFn* mono = &mono_fns[mono_fn_count++];
    strcpy(mono->generic_fn, fn->name);
    strcpy(mono->concrete_types, concrete_types);

    /* Create mangled name */
    snprintf(mono->mangled_name, 255, "_%s$%s", fn->name, concrete_types);
    for (char* p = mono->mangled_name; *p; p++) {
        if (*p == '<' || *p == '>' || *p == ',' || *p == ' ') *p = '_';
    }

    printf("\n; Monomorphized: %s<%s>\n", fn->name, concrete_types);
    printf(".globl %s\n", mono->mangled_name);
    printf("%s:\n", mono->mangled_name);

    /* Generate specialized code... */
    /* In real impl, would substitute types throughout */
}

/* ============================================================
 * DEMONSTRATION
 * ============================================================ */

void demonstrate_functions_traits() {
    printf("; === Functions & Traits Demonstration ===\n\n");

    /* Example function */
    char* source1 = "fn add<T: Add>(a: T, b: T) -> T";
    char* pos1 = source1;
    Function* add_fn = parse_function(&pos1);
    if (add_fn) {
        printf("; Parsed function: %s\n", add_fn->name);
        printf(";   Generics: %s\n", add_fn->generic_params);
        printf(";   Mangled: %s\n", add_fn->mangled_name);
        printf(";   Params: %d\n", add_fn->param_count);
        printf(";   Return: %s\n\n", add_fn->return_type);

        emit_function_prologue(add_fn);
        printf("    ; Function body would go here\n");
        printf("    add r3, r3, r4    ; a + b\n");
        emit_function_epilogue(add_fn);
    }

    /* Example method */
    char* source2 = "fn len(&self) -> usize";
    char* pos2 = source2;
    Function* len_fn = parse_function(&pos2);
    if (len_fn) {
        printf("\n; Parsed method: %s (is_method=%d)\n",
               len_fn->name, len_fn->is_method);
    }

    /* Example trait */
    char* source3 = "trait Iterator<Item>: Clone";
    char* pos3 = source3;
    Trait* iter_trait = parse_trait(&pos3);
    if (iter_trait) {
        printf("\n; Parsed trait: %s\n", iter_trait->name);
        printf(";   Generics: %s\n", iter_trait->generic_params);
        printf(";   Supertraits: %s\n", iter_trait->supertraits);
    }

    /* Example impl */
    char* source4 = "impl<T> Clone for Vec<T>";
    char* pos4 = source4;
    ImplBlock* impl = parse_impl(&pos4);
    if (impl) {
        printf("\n; Parsed impl: %s for %s\n",
               impl->trait_name, impl->type_name);
    }

    /* Generate a vtable */
    if (impl && iter_trait) {
        VTable* vt = generate_vtable(impl, iter_trait);
        emit_vtable(vt);
    }

    /* Monomorphization example */
    if (add_fn) {
        monomorphize(add_fn, "i32");
        monomorphize(add_fn, "f64");
    }

    /* Trait object call */
    printf("\n; Example trait object dispatch:\n");
    emit_trait_object_call("iter", "next", 0);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        demonstrate_functions_traits();
    } else {
        printf("Rust Functions & Traits for PowerPC\n");
        printf("Usage: %s --demo    Run demonstration\n", argv[0]);
    }
    return 0;
}
