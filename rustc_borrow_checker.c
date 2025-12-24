/*
 * Rust Borrow Checker for PowerPC
 * Implements Rust's ownership and borrowing rules
 *
 * This is the heart of Rust's memory safety - without this,
 * it's just "C with fancy syntax"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * OWNERSHIP MODEL
 * ============================================================
 *
 * Rust's Three Rules:
 * 1. Each value has exactly one owner
 * 2. When the owner goes out of scope, the value is dropped
 * 3. You can have EITHER:
 *    - One mutable reference (&mut T)
 *    - Any number of immutable references (&T)
 *    - But NOT both at the same time
 */

typedef enum {
    OWNER_OWNED,        /* Variable owns the value */
    OWNER_MOVED,        /* Value was moved away */
    OWNER_BORROWED,     /* Value is borrowed (immutable) */
    OWNER_MUT_BORROWED, /* Value is mutably borrowed */
    OWNER_DROPPED       /* Value was dropped */
} OwnershipState;

typedef enum {
    LIFETIME_STATIC,    /* 'static - lives forever */
    LIFETIME_FUNCTION,  /* Lives for function duration */
    LIFETIME_BLOCK,     /* Lives for block duration */
    LIFETIME_TEMP,      /* Temporary - single expression */
    LIFETIME_NAMED      /* Named lifetime 'a, 'b, etc */
} LifetimeKind;

typedef struct Lifetime {
    int id;
    LifetimeKind kind;
    char name[32];      /* 'a, 'b, 'static, etc */
    int scope_depth;    /* Nesting level where created */
    int start_line;
    int end_line;
    struct Lifetime* outlives;  /* This lifetime must outlive these */
    int outlives_count;
} Lifetime;

typedef struct Borrow {
    int id;
    int source_var_id;      /* What variable we're borrowing from */
    int is_mutable;         /* &mut or & */
    Lifetime* lifetime;     /* How long the borrow lasts */
    int line_created;
    int line_last_used;
    int is_active;          /* Still in scope? */
} Borrow;

typedef struct Variable {
    int id;
    char name[64];
    int scope_depth;
    OwnershipState state;
    Lifetime* lifetime;

    /* Borrow tracking */
    Borrow* borrows[16];    /* Active borrows of this variable */
    int borrow_count;
    int active_mut_borrow;  /* ID of current &mut borrow, or -1 */
    int active_immut_count; /* Count of current & borrows */

    /* Move tracking */
    int moved_at_line;
    char moved_to[64];      /* Variable/expr it was moved to */

    /* For references */
    int is_reference;
    int referent_var_id;    /* If this is a ref, what does it point to? */
    int ref_is_mutable;
} Variable;

/* Global state */
#define MAX_VARS 1000
#define MAX_LIFETIMES 500
#define MAX_BORROWS 1000
#define MAX_ERRORS 100

Variable variables[MAX_VARS];
Lifetime lifetimes[MAX_LIFETIMES];
Borrow borrows[MAX_BORROWS];

int var_count = 0;
int lifetime_count = 0;
int borrow_count = 0;
int current_scope = 0;
int current_line = 1;

/* Error tracking */
typedef struct {
    int line;
    char message[256];
    char hint[256];
} BorrowError;

BorrowError errors[MAX_ERRORS];
int error_count = 0;

/* ============================================================
 * ERROR REPORTING
 * ============================================================ */

void emit_error(int line, const char* msg, const char* hint) {
    if (error_count >= MAX_ERRORS) return;

    errors[error_count].line = line;
    strncpy(errors[error_count].message, msg, 255);
    strncpy(errors[error_count].hint, hint, 255);
    error_count++;

    fprintf(stderr, "error[E0]: %s\n", msg);
    fprintf(stderr, "  --> source.rs:%d\n", line);
    if (hint[0]) {
        fprintf(stderr, "  = help: %s\n", hint);
    }
    fprintf(stderr, "\n");
}

/* ============================================================
 * LIFETIME MANAGEMENT
 * ============================================================ */

Lifetime* create_lifetime(LifetimeKind kind, const char* name) {
    if (lifetime_count >= MAX_LIFETIMES) return NULL;

    Lifetime* lt = &lifetimes[lifetime_count++];
    lt->id = lifetime_count;
    lt->kind = kind;
    strncpy(lt->name, name, 31);
    lt->scope_depth = current_scope;
    lt->start_line = current_line;
    lt->end_line = -1;  /* Unknown until scope ends */
    lt->outlives = NULL;
    lt->outlives_count = 0;

    return lt;
}

int lifetime_outlives(Lifetime* a, Lifetime* b) {
    /* Does lifetime 'a' outlive lifetime 'b'? */
    if (!a || !b) return 0;

    /* 'static outlives everything */
    if (a->kind == LIFETIME_STATIC) return 1;
    if (b->kind == LIFETIME_STATIC) return 0;

    /* Shallower scope = longer lifetime */
    return a->scope_depth < b->scope_depth;
}

void end_lifetime(Lifetime* lt) {
    if (lt) {
        lt->end_line = current_line;
    }
}

/* ============================================================
 * VARIABLE AND BORROW CREATION
 * ============================================================ */

Variable* create_variable(const char* name, int is_reference, int is_mut_ref) {
    if (var_count >= MAX_VARS) return NULL;

    Variable* var = &variables[var_count++];
    var->id = var_count;
    strncpy(var->name, name, 63);
    var->scope_depth = current_scope;
    var->state = OWNER_OWNED;
    var->lifetime = create_lifetime(LIFETIME_BLOCK, "");
    var->borrow_count = 0;
    var->active_mut_borrow = -1;
    var->active_immut_count = 0;
    var->moved_at_line = -1;
    var->moved_to[0] = '\0';
    var->is_reference = is_reference;
    var->referent_var_id = -1;
    var->ref_is_mutable = is_mut_ref;

    return var;
}

Variable* find_variable(const char* name) {
    for (int i = var_count - 1; i >= 0; i--) {
        if (strcmp(variables[i].name, name) == 0 &&
            variables[i].scope_depth <= current_scope) {
            return &variables[i];
        }
    }
    return NULL;
}

Borrow* create_borrow(Variable* from, int is_mutable, Lifetime* lt) {
    if (borrow_count >= MAX_BORROWS) return NULL;

    Borrow* b = &borrows[borrow_count++];
    b->id = borrow_count;
    b->source_var_id = from->id;
    b->is_mutable = is_mutable;
    b->lifetime = lt;
    b->line_created = current_line;
    b->line_last_used = current_line;
    b->is_active = 1;

    /* Add to variable's borrow list */
    if (from->borrow_count < 16) {
        from->borrows[from->borrow_count++] = b;
    }

    return b;
}

/* ============================================================
 * BORROW CHECKING CORE
 * ============================================================ */

int check_can_borrow_immut(Variable* var) {
    /* Can we take an immutable borrow (&var)? */

    /* Check 1: Is the variable still owned? */
    if (var->state == OWNER_MOVED) {
        char msg[256];
        snprintf(msg, 255, "borrow of moved value: `%s`", var->name);
        char hint[256];
        snprintf(hint, 255, "value moved at line %d to `%s`",
                var->moved_at_line, var->moved_to);
        emit_error(current_line, msg, hint);
        return 0;
    }

    if (var->state == OWNER_DROPPED) {
        char msg[256];
        snprintf(msg, 255, "borrow of dropped value: `%s`", var->name);
        emit_error(current_line, msg, "value was dropped earlier");
        return 0;
    }

    /* Check 2: Is there an active mutable borrow? */
    if (var->active_mut_borrow != -1) {
        char msg[256];
        snprintf(msg, 255,
                "cannot borrow `%s` as immutable because it is also borrowed as mutable",
                var->name);
        emit_error(current_line, msg,
                  "mutable borrow prevents any other borrows");
        return 0;
    }

    return 1;
}

int check_can_borrow_mut(Variable* var) {
    /* Can we take a mutable borrow (&mut var)? */

    /* Check 1: Is the variable still owned? */
    if (var->state == OWNER_MOVED) {
        char msg[256];
        snprintf(msg, 255, "borrow of moved value: `%s`", var->name);
        emit_error(current_line, msg, "");
        return 0;
    }

    /* Check 2: Are there ANY active borrows? */
    if (var->active_mut_borrow != -1) {
        char msg[256];
        snprintf(msg, 255,
                "cannot borrow `%s` as mutable more than once at a time",
                var->name);
        emit_error(current_line, msg,
                  "first mutable borrow occurs here");
        return 0;
    }

    if (var->active_immut_count > 0) {
        char msg[256];
        snprintf(msg, 255,
                "cannot borrow `%s` as mutable because it is also borrowed as immutable",
                var->name);
        emit_error(current_line, msg,
                  "immutable borrow prevents mutable borrow");
        return 0;
    }

    return 1;
}

int check_can_move(Variable* var) {
    /* Can we move this value? */

    if (var->state == OWNER_MOVED) {
        char msg[256];
        snprintf(msg, 255, "use of moved value: `%s`", var->name);
        char hint[256];
        snprintf(hint, 255, "value was moved at line %d", var->moved_at_line);
        emit_error(current_line, msg, hint);
        return 0;
    }

    /* Cannot move if there are active borrows */
    if (var->active_mut_borrow != -1 || var->active_immut_count > 0) {
        char msg[256];
        snprintf(msg, 255,
                "cannot move out of `%s` because it is borrowed",
                var->name);
        emit_error(current_line, msg, "");
        return 0;
    }

    return 1;
}

int check_can_use(Variable* var) {
    /* Can we use this value at all? */

    if (var->state == OWNER_MOVED) {
        char msg[256];
        snprintf(msg, 255, "use of moved value: `%s`", var->name);
        char hint[256];
        snprintf(hint, 255,
                "move occurs because `%s` has type which does not implement `Copy`",
                var->name);
        emit_error(current_line, msg, hint);
        return 0;
    }

    if (var->state == OWNER_DROPPED) {
        char msg[256];
        snprintf(msg, 255, "use of dropped value: `%s`", var->name);
        emit_error(current_line, msg, "");
        return 0;
    }

    return 1;
}

/* ============================================================
 * BORROW OPERATIONS
 * ============================================================ */

Variable* do_immut_borrow(Variable* source, const char* ref_name) {
    if (!check_can_borrow_immut(source)) return NULL;

    Lifetime* lt = create_lifetime(LIFETIME_BLOCK, "");
    Borrow* b = create_borrow(source, 0, lt);

    source->active_immut_count++;
    source->state = OWNER_BORROWED;

    /* Create the reference variable */
    Variable* ref = create_variable(ref_name, 1, 0);
    ref->referent_var_id = source->id;

    printf("    ; &%s -> %s (immutable borrow)\n", source->name, ref_name);

    return ref;
}

Variable* do_mut_borrow(Variable* source, const char* ref_name) {
    if (!check_can_borrow_mut(source)) return NULL;

    Lifetime* lt = create_lifetime(LIFETIME_BLOCK, "");
    Borrow* b = create_borrow(source, 1, lt);

    source->active_mut_borrow = b->id;
    source->state = OWNER_MUT_BORROWED;

    /* Create the reference variable */
    Variable* ref = create_variable(ref_name, 1, 1);
    ref->referent_var_id = source->id;

    printf("    ; &mut %s -> %s (mutable borrow)\n", source->name, ref_name);

    return ref;
}

void do_move(Variable* from, const char* to_name) {
    if (!check_can_move(from)) return;

    from->state = OWNER_MOVED;
    from->moved_at_line = current_line;
    strncpy(from->moved_to, to_name, 63);

    printf("    ; move %s -> %s\n", from->name, to_name);
}

void end_borrow(Variable* ref) {
    if (!ref->is_reference) return;

    Variable* source = &variables[ref->referent_var_id - 1];

    if (ref->ref_is_mutable) {
        source->active_mut_borrow = -1;
        printf("    ; end &mut borrow of %s\n", source->name);
    } else {
        source->active_immut_count--;
        printf("    ; end & borrow of %s\n", source->name);
    }

    /* If no more borrows, restore owned state */
    if (source->active_mut_borrow == -1 && source->active_immut_count == 0) {
        source->state = OWNER_OWNED;
    }
}

/* ============================================================
 * SCOPE MANAGEMENT
 * ============================================================ */

void enter_scope() {
    current_scope++;
    printf("    ; enter scope %d\n", current_scope);
}

void exit_scope() {
    printf("    ; exit scope %d\n", current_scope);

    /* Drop all variables in this scope (in reverse order) */
    for (int i = var_count - 1; i >= 0; i--) {
        if (variables[i].scope_depth == current_scope) {
            Variable* var = &variables[i];

            /* End any borrows first */
            if (var->is_reference) {
                end_borrow(var);
            }

            /* Drop the value if owned */
            if (var->state == OWNER_OWNED ||
                var->state == OWNER_BORROWED) {
                printf("    ; drop %s\n", var->name);
                var->state = OWNER_DROPPED;
                end_lifetime(var->lifetime);
            }
        }
    }

    current_scope--;
}

/* ============================================================
 * NON-LEXICAL LIFETIMES (NLL)
 * ============================================================
 *
 * Modern Rust uses NLL - borrows end at last use, not at
 * scope exit. This makes more code compile.
 */

void analyze_nll(Variable* var) {
    /* Find the last use of each borrow */
    for (int i = 0; i < var->borrow_count; i++) {
        Borrow* b = var->borrows[i];

        /* In real NLL, we'd analyze the CFG to find last use.
         * For now, just check if borrow extends past necessary. */

        if (b->is_active && b->line_last_used < current_line - 1) {
            /* Borrow could end early */
            printf("    ; NLL: borrow of %s could end at line %d\n",
                   var->name, b->line_last_used);

            /* Automatically end the borrow */
            b->is_active = 0;
            if (b->is_mutable) {
                var->active_mut_borrow = -1;
            } else {
                var->active_immut_count--;
            }

            if (var->active_mut_borrow == -1 &&
                var->active_immut_count == 0) {
                var->state = OWNER_OWNED;
            }
        }
    }
}

/* ============================================================
 * LIFETIME ELISION
 * ============================================================
 *
 * Rust has rules for when lifetimes can be inferred:
 * 1. Each elided lifetime in input position becomes a distinct lifetime
 * 2. If there is exactly one input lifetime, it is assigned to all output lifetimes
 * 3. If there is a &self or &mut self, that lifetime is assigned to all output lifetimes
 */

typedef struct {
    char input_lifetimes[8][32];
    int input_count;
    char output_lifetime[32];
    int has_self;
    int self_is_mut;
} FunctionLifetimes;

void elide_lifetimes(FunctionLifetimes* fn) {
    /* Rule 1: Assign distinct lifetimes to inputs */
    for (int i = 0; i < fn->input_count; i++) {
        if (fn->input_lifetimes[i][0] == '\0') {
            snprintf(fn->input_lifetimes[i], 31, "'anon_%d", i);
        }
    }

    /* Rule 2: Single input -> assign to output */
    if (fn->output_lifetime[0] == '\0') {
        if (fn->input_count == 1) {
            strcpy(fn->output_lifetime, fn->input_lifetimes[0]);
        }
        /* Rule 3: &self -> assign to output */
        else if (fn->has_self) {
            strcpy(fn->output_lifetime, "'self");
        }
    }
}

/* ============================================================
 * CODEGEN INTEGRATION
 * ============================================================ */

void emit_borrow_check_prologue() {
    printf("; Borrow checker: analyzing ownership and lifetimes\n");
    printf("; Using Non-Lexical Lifetimes (NLL) for flexibility\n\n");
}

void emit_borrow_check_epilogue() {
    if (error_count > 0) {
        printf("\n; Borrow check FAILED with %d errors\n", error_count);
        printf("; Fix the errors above to continue compilation\n");
    } else {
        printf("\n; Borrow check PASSED - memory safety verified!\n");
    }
}

/* ============================================================
 * TEST / DEMO
 * ============================================================ */

void demonstrate_borrow_checker() {
    printf("; === Borrow Checker Demonstration ===\n\n");

    emit_borrow_check_prologue();

    enter_scope();

    /* let mut x = 5; */
    Variable* x = create_variable("x", 0, 0);
    printf("    ; let mut x = 5\n");
    printf("    li r14, 5\n");
    printf("    stw r14, 0(r1)     ; x\n");

    /* let y = &x; */
    Variable* y = do_immut_borrow(x, "y");
    printf("    la r15, 0(r1)      ; y = &x\n");

    /* let z = &x; -- multiple immut borrows OK */
    Variable* z = do_immut_borrow(x, "z");
    printf("    la r16, 0(r1)      ; z = &x\n");

    /* let w = &mut x; -- ERROR! Can't mut borrow while immut borrowed */
    printf("\n    ; Attempting: let w = &mut x (should fail)\n");
    Variable* w = do_mut_borrow(x, "w");  /* This should error */

    /* End immutable borrows via NLL */
    current_line += 5;  /* Simulate advancing */
    analyze_nll(x);

    /* Now we can mut borrow */
    printf("\n    ; After NLL analysis, trying again\n");
    w = do_mut_borrow(x, "w");
    if (w) {
        printf("    la r17, 0(r1)      ; w = &mut x\n");
    }

    exit_scope();

    emit_borrow_check_epilogue();
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        demonstrate_borrow_checker();
    } else {
        printf("Rust Borrow Checker for PowerPC\n");
        printf("Usage: %s --demo    Run demonstration\n", argv[0]);
        printf("\nThis module integrates with rustc_100_percent.c\n");
        printf("to provide compile-time ownership verification.\n");
    }

    return error_count > 0 ? 1 : 0;
}
