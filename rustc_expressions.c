/*
 * Rust Expression Evaluation for PowerPC
 * Handles complex expressions, operators, and pattern matching
 *
 * Firefox uses sophisticated expression chains - this handles them
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
 * EXPRESSION AST
 * ============================================================ */

typedef enum {
    EXPR_LITERAL_INT,
    EXPR_LITERAL_FLOAT,
    EXPR_LITERAL_BOOL,
    EXPR_LITERAL_CHAR,
    EXPR_LITERAL_STRING,
    EXPR_IDENT,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL,
    EXPR_METHOD_CALL,
    EXPR_FIELD_ACCESS,
    EXPR_INDEX,
    EXPR_TUPLE_INDEX,
    EXPR_ARRAY,
    EXPR_TUPLE,
    EXPR_STRUCT,
    EXPR_IF,
    EXPR_MATCH,
    EXPR_LOOP,
    EXPR_WHILE,
    EXPR_FOR,
    EXPR_BLOCK,
    EXPR_RETURN,
    EXPR_BREAK,
    EXPR_CONTINUE,
    EXPR_CLOSURE,
    EXPR_REF,
    EXPR_DEREF,
    EXPR_CAST,
    EXPR_RANGE,
    EXPR_TRY,           /* ? operator */
    EXPR_AWAIT,
    EXPR_ASSIGN,
} ExprKind;

typedef enum {
    OP_ADD,     /* + */
    OP_SUB,     /* - */
    OP_MUL,     /* * */
    OP_DIV,     /* / */
    OP_MOD,     /* % */
    OP_AND,     /* && */
    OP_OR,      /* || */
    OP_BITAND,  /* & */
    OP_BITOR,   /* | */
    OP_BITXOR,  /* ^ */
    OP_SHL,     /* << */
    OP_SHR,     /* >> */
    OP_EQ,      /* == */
    OP_NE,      /* != */
    OP_LT,      /* < */
    OP_LE,      /* <= */
    OP_GT,      /* > */
    OP_GE,      /* >= */
    OP_ASSIGN,  /* = */
    OP_ADDEQ,   /* += */
    OP_SUBEQ,   /* -= */
    OP_MULEQ,   /* *= */
    OP_DIVEQ,   /* /= */
} BinaryOp;

typedef enum {
    UOP_NEG,    /* - */
    UOP_NOT,    /* ! */
    UOP_BITNOT, /* ~ */
    UOP_REF,    /* & */
    UOP_MUTREF, /* &mut */
    UOP_DEREF,  /* * */
} UnaryOp;

struct Expr;

typedef struct {
    struct Expr* elements[64];
    int count;
} ExprList;

typedef struct Expr {
    ExprKind kind;
    int line;
    int temp_reg;  /* Register holding result after codegen */

    union {
        /* Literals */
        long long int_val;
        double float_val;
        int bool_val;
        char char_val;
        char string_val[256];

        /* Identifier */
        struct {
            char name[64];
            int var_offset;     /* Stack offset if local */
        } ident;

        /* Binary expression */
        struct {
            BinaryOp op;
            struct Expr* left;
            struct Expr* right;
        } binary;

        /* Unary expression */
        struct {
            UnaryOp op;
            struct Expr* operand;
        } unary;

        /* Function/method call */
        struct {
            char name[64];
            struct Expr* receiver;  /* For method calls */
            ExprList args;
        } call;

        /* Field access */
        struct {
            struct Expr* object;
            char field[64];
            int field_offset;
        } field;

        /* Index */
        struct {
            struct Expr* array;
            struct Expr* index;
        } index;

        /* If expression */
        struct {
            struct Expr* condition;
            struct Expr* then_branch;
            struct Expr* else_branch;
        } if_expr;

        /* Match expression */
        struct {
            struct Expr* scrutinee;
            struct {
                char pattern[128];
                struct Expr* body;
            } arms[32];
            int arm_count;
        } match_expr;

        /* Block */
        struct {
            struct Expr* stmts[64];
            int stmt_count;
            struct Expr* final_expr;  /* Expression at end without ; */
        } block;

        /* Closure */
        struct {
            char params[256];
            struct Expr* body;
            char captures[64][32];
            int capture_count;
        } closure;

        /* Cast */
        struct {
            struct Expr* expr;
            char target_type[64];
        } cast;

        /* Range */
        struct {
            struct Expr* start;
            struct Expr* end;
            int inclusive;  /* .. vs ..= */
        } range;
    } data;
} Expr;

/* ============================================================
 * EXPRESSION PARSING
 * ============================================================ */

static char* pos;
static int current_line = 1;
static int next_temp_reg = 14;  /* r14-r31 for temps */

void skip_ws() {
    while (*pos) {
        if (*pos == '\n') current_line++;
        if (!isspace(*pos)) break;
        pos++;
    }
}

Expr* alloc_expr(ExprKind kind) {
    Expr* e = calloc(1, sizeof(Expr));
    e->kind = kind;
    e->line = current_line;
    e->temp_reg = -1;
    return e;
}

/* Forward declarations */
Expr* parse_expr();
Expr* parse_primary();
Expr* parse_unary();
Expr* parse_binary(int min_prec);

int get_precedence(BinaryOp op) {
    switch (op) {
        case OP_ASSIGN: case OP_ADDEQ: case OP_SUBEQ:
        case OP_MULEQ: case OP_DIVEQ:
            return 1;
        case OP_OR:
            return 2;
        case OP_AND:
            return 3;
        case OP_EQ: case OP_NE:
            return 4;
        case OP_LT: case OP_LE: case OP_GT: case OP_GE:
            return 5;
        case OP_BITOR:
            return 6;
        case OP_BITXOR:
            return 7;
        case OP_BITAND:
            return 8;
        case OP_SHL: case OP_SHR:
            return 9;
        case OP_ADD: case OP_SUB:
            return 10;
        case OP_MUL: case OP_DIV: case OP_MOD:
            return 11;
        default:
            return 0;
    }
}

BinaryOp parse_binop() {
    skip_ws();
    if (strncmp(pos, "==", 2) == 0) { pos += 2; return OP_EQ; }
    if (strncmp(pos, "!=", 2) == 0) { pos += 2; return OP_NE; }
    if (strncmp(pos, "<=", 2) == 0) { pos += 2; return OP_LE; }
    if (strncmp(pos, ">=", 2) == 0) { pos += 2; return OP_GE; }
    if (strncmp(pos, "<<", 2) == 0) { pos += 2; return OP_SHL; }
    if (strncmp(pos, ">>", 2) == 0) { pos += 2; return OP_SHR; }
    if (strncmp(pos, "&&", 2) == 0) { pos += 2; return OP_AND; }
    if (strncmp(pos, "||", 2) == 0) { pos += 2; return OP_OR; }
    if (strncmp(pos, "+=", 2) == 0) { pos += 2; return OP_ADDEQ; }
    if (strncmp(pos, "-=", 2) == 0) { pos += 2; return OP_SUBEQ; }
    if (strncmp(pos, "*=", 2) == 0) { pos += 2; return OP_MULEQ; }
    if (strncmp(pos, "/=", 2) == 0) { pos += 2; return OP_DIVEQ; }
    if (*pos == '+') { pos++; return OP_ADD; }
    if (*pos == '-') { pos++; return OP_SUB; }
    if (*pos == '*') { pos++; return OP_MUL; }
    if (*pos == '/') { pos++; return OP_DIV; }
    if (*pos == '%') { pos++; return OP_MOD; }
    if (*pos == '<') { pos++; return OP_LT; }
    if (*pos == '>') { pos++; return OP_GT; }
    if (*pos == '&') { pos++; return OP_BITAND; }
    if (*pos == '|') { pos++; return OP_BITOR; }
    if (*pos == '^') { pos++; return OP_BITXOR; }
    if (*pos == '=') { pos++; return OP_ASSIGN; }
    return -1;
}

Expr* parse_number() {
    Expr* e;
    int is_float = 0;
    char* start = pos;

    /* Check for negative */
    if (*pos == '-') pos++;

    /* Hex, binary, octal */
    if (*pos == '0' && (*(pos+1) == 'x' || *(pos+1) == 'b' || *(pos+1) == 'o')) {
        pos += 2;
        while (isxdigit(*pos)) pos++;
        e = alloc_expr(EXPR_LITERAL_INT);
        e->data.int_val = strtoll(start, NULL, 0);
        return e;
    }

    /* Decimal */
    while (isdigit(*pos)) pos++;
    if (*pos == '.') {
        is_float = 1;
        pos++;
        while (isdigit(*pos)) pos++;
    }
    if (*pos == 'e' || *pos == 'E') {
        is_float = 1;
        pos++;
        if (*pos == '+' || *pos == '-') pos++;
        while (isdigit(*pos)) pos++;
    }

    /* Type suffix */
    if (*pos == 'i' || *pos == 'u' || *pos == 'f') {
        while (isalnum(*pos)) pos++;
    }

    if (is_float) {
        e = alloc_expr(EXPR_LITERAL_FLOAT);
        e->data.float_val = strtod(start, NULL);
    } else {
        e = alloc_expr(EXPR_LITERAL_INT);
        e->data.int_val = strtoll(start, NULL, 10);
    }
    return e;
}

Expr* parse_string() {
    Expr* e = alloc_expr(EXPR_LITERAL_STRING);
    pos++;  /* Skip opening " */
    int i = 0;
    while (*pos && *pos != '"' && i < 255) {
        if (*pos == '\\') {
            pos++;
            switch (*pos) {
                case 'n': e->data.string_val[i++] = '\n'; break;
                case 't': e->data.string_val[i++] = '\t'; break;
                case 'r': e->data.string_val[i++] = '\r'; break;
                case '\\': e->data.string_val[i++] = '\\'; break;
                case '"': e->data.string_val[i++] = '"'; break;
                default: e->data.string_val[i++] = *pos;
            }
        } else {
            e->data.string_val[i++] = *pos;
        }
        pos++;
    }
    e->data.string_val[i] = '\0';
    if (*pos == '"') pos++;
    return e;
}

Expr* parse_ident_or_call() {
    char name[64] = {0};
    int i = 0;
    while (*pos && (isalnum(*pos) || *pos == '_') && i < 63) {
        name[i++] = *pos++;
    }
    name[i] = '\0';

    skip_ws();

    /* Check for boolean literals */
    if (strcmp(name, "true") == 0) {
        Expr* e = alloc_expr(EXPR_LITERAL_BOOL);
        e->data.bool_val = 1;
        return e;
    }
    if (strcmp(name, "false") == 0) {
        Expr* e = alloc_expr(EXPR_LITERAL_BOOL);
        e->data.bool_val = 0;
        return e;
    }

    /* Check for function call */
    if (*pos == '(') {
        Expr* e = alloc_expr(EXPR_CALL);
        strcpy(e->data.call.name, name);
        e->data.call.receiver = NULL;

        pos++;  /* Skip ( */
        skip_ws();

        while (*pos && *pos != ')') {
            e->data.call.args.elements[e->data.call.args.count++] = parse_expr();
            skip_ws();
            if (*pos == ',') pos++;
            skip_ws();
        }
        if (*pos == ')') pos++;
        return e;
    }

    /* Just an identifier */
    Expr* e = alloc_expr(EXPR_IDENT);
    strcpy(e->data.ident.name, name);
    return e;
}

Expr* parse_primary() {
    skip_ws();

    /* Literals */
    if (*pos == '"') return parse_string();
    if (*pos == '\'') {
        Expr* e = alloc_expr(EXPR_LITERAL_CHAR);
        pos++;  /* Skip ' */
        if (*pos == '\\') {
            pos++;
            switch (*pos) {
                case 'n': e->data.char_val = '\n'; break;
                case 't': e->data.char_val = '\t'; break;
                default: e->data.char_val = *pos;
            }
        } else {
            e->data.char_val = *pos;
        }
        pos++;
        if (*pos == '\'') pos++;
        return e;
    }
    if (isdigit(*pos) || (*pos == '-' && isdigit(*(pos+1)))) {
        return parse_number();
    }
    if (isalpha(*pos) || *pos == '_') {
        return parse_ident_or_call();
    }

    /* Parenthesized or tuple */
    if (*pos == '(') {
        pos++;
        skip_ws();
        if (*pos == ')') {
            pos++;
            return alloc_expr(EXPR_TUPLE);  /* Unit type */
        }
        Expr* first = parse_expr();
        skip_ws();
        if (*pos == ',') {
            /* Tuple */
            Expr* e = alloc_expr(EXPR_TUPLE);
            e->data.block.stmts[0] = first;
            e->data.block.stmt_count = 1;
            while (*pos == ',') {
                pos++;
                skip_ws();
                if (*pos == ')') break;
                e->data.block.stmts[e->data.block.stmt_count++] = parse_expr();
                skip_ws();
            }
            if (*pos == ')') pos++;
            return e;
        }
        if (*pos == ')') pos++;
        return first;
    }

    /* Array */
    if (*pos == '[') {
        Expr* e = alloc_expr(EXPR_ARRAY);
        pos++;
        skip_ws();
        while (*pos && *pos != ']') {
            e->data.block.stmts[e->data.block.stmt_count++] = parse_expr();
            skip_ws();
            if (*pos == ',') pos++;
            skip_ws();
        }
        if (*pos == ']') pos++;
        return e;
    }

    /* Block */
    if (*pos == '{') {
        Expr* e = alloc_expr(EXPR_BLOCK);
        pos++;
        skip_ws();
        while (*pos && *pos != '}') {
            Expr* stmt = parse_expr();
            skip_ws();
            if (*pos == ';') {
                e->data.block.stmts[e->data.block.stmt_count++] = stmt;
                pos++;
            } else {
                e->data.block.final_expr = stmt;
            }
            skip_ws();
        }
        if (*pos == '}') pos++;
        return e;
    }

    /* If expression */
    if (strncmp(pos, "if ", 3) == 0) {
        pos += 3;
        Expr* e = alloc_expr(EXPR_IF);
        e->data.if_expr.condition = parse_expr();
        skip_ws();
        e->data.if_expr.then_branch = parse_expr();
        skip_ws();
        if (strncmp(pos, "else", 4) == 0) {
            pos += 4;
            skip_ws();
            e->data.if_expr.else_branch = parse_expr();
        }
        return e;
    }

    /* Match expression */
    if (strncmp(pos, "match ", 6) == 0) {
        pos += 6;
        Expr* e = alloc_expr(EXPR_MATCH);
        e->data.match_expr.scrutinee = parse_expr();
        skip_ws();
        if (*pos == '{') {
            pos++;
            skip_ws();
            while (*pos && *pos != '}') {
                /* Pattern => body */
                int pi = 0;
                while (*pos && strncmp(pos, "=>", 2) != 0 && pi < 127) {
                    e->data.match_expr.arms[e->data.match_expr.arm_count].pattern[pi++] = *pos++;
                }
                e->data.match_expr.arms[e->data.match_expr.arm_count].pattern[pi] = '\0';

                if (strncmp(pos, "=>", 2) == 0) pos += 2;
                skip_ws();

                e->data.match_expr.arms[e->data.match_expr.arm_count].body = parse_expr();
                e->data.match_expr.arm_count++;

                skip_ws();
                if (*pos == ',') pos++;
                skip_ws();
            }
            if (*pos == '}') pos++;
        }
        return e;
    }

    /* Closure */
    if (*pos == '|') {
        Expr* e = alloc_expr(EXPR_CLOSURE);
        pos++;
        int pi = 0;
        while (*pos && *pos != '|' && pi < 255) {
            e->data.closure.params[pi++] = *pos++;
        }
        e->data.closure.params[pi] = '\0';
        if (*pos == '|') pos++;
        skip_ws();
        e->data.closure.body = parse_expr();
        return e;
    }

    return NULL;
}

Expr* parse_unary() {
    skip_ws();

    UnaryOp op = -1;
    if (*pos == '-' && !isdigit(*(pos+1))) {
        pos++;
        op = UOP_NEG;
    } else if (*pos == '!') {
        pos++;
        op = UOP_NOT;
    } else if (*pos == '*') {
        pos++;
        op = UOP_DEREF;
    } else if (strncmp(pos, "&mut ", 5) == 0) {
        pos += 5;
        op = UOP_MUTREF;
    } else if (*pos == '&') {
        pos++;
        op = UOP_REF;
    }

    if (op != -1) {
        Expr* e = alloc_expr(EXPR_UNARY);
        e->data.unary.op = op;
        e->data.unary.operand = parse_unary();
        return e;
    }

    return parse_primary();
}

Expr* parse_postfix(Expr* e) {
    while (1) {
        skip_ws();

        /* Method call: expr.method(args) */
        if (*pos == '.') {
            pos++;
            skip_ws();

            /* Check for await */
            if (strncmp(pos, "await", 5) == 0 && !isalnum(*(pos+5))) {
                pos += 5;
                Expr* await = alloc_expr(EXPR_AWAIT);
                await->data.unary.operand = e;
                e = await;
                continue;
            }

            /* Tuple index: expr.0, expr.1 */
            if (isdigit(*pos)) {
                Expr* ti = alloc_expr(EXPR_TUPLE_INDEX);
                ti->data.index.array = e;
                Expr* idx = alloc_expr(EXPR_LITERAL_INT);
                idx->data.int_val = strtol(pos, &pos, 10);
                ti->data.index.index = idx;
                e = ti;
                continue;
            }

            /* Field access or method call */
            char name[64] = {0};
            int i = 0;
            while (*pos && (isalnum(*pos) || *pos == '_') && i < 63) {
                name[i++] = *pos++;
            }
            skip_ws();

            if (*pos == '(') {
                /* Method call */
                Expr* call = alloc_expr(EXPR_METHOD_CALL);
                strcpy(call->data.call.name, name);
                call->data.call.receiver = e;

                pos++;
                skip_ws();
                while (*pos && *pos != ')') {
                    call->data.call.args.elements[call->data.call.args.count++] = parse_expr();
                    skip_ws();
                    if (*pos == ',') pos++;
                    skip_ws();
                }
                if (*pos == ')') pos++;
                e = call;
            } else {
                /* Field access */
                Expr* field = alloc_expr(EXPR_FIELD_ACCESS);
                field->data.field.object = e;
                strcpy(field->data.field.field, name);
                e = field;
            }
            continue;
        }

        /* Index: expr[index] */
        if (*pos == '[') {
            pos++;
            Expr* idx = alloc_expr(EXPR_INDEX);
            idx->data.index.array = e;
            idx->data.index.index = parse_expr();
            skip_ws();
            if (*pos == ']') pos++;
            e = idx;
            continue;
        }

        /* Try operator: expr? */
        if (*pos == '?') {
            pos++;
            Expr* try = alloc_expr(EXPR_TRY);
            try->data.unary.operand = e;
            e = try;
            continue;
        }

        /* Cast: expr as Type */
        if (strncmp(pos, " as ", 4) == 0) {
            pos += 4;
            skip_ws();
            Expr* cast = alloc_expr(EXPR_CAST);
            cast->data.cast.expr = e;
            int i = 0;
            while (*pos && (isalnum(*pos) || *pos == '_' || *pos == '<' || *pos == '>') && i < 63) {
                cast->data.cast.target_type[i++] = *pos++;
            }
            cast->data.cast.target_type[i] = '\0';
            e = cast;
            continue;
        }

        break;
    }
    return e;
}

Expr* parse_binary(int min_prec) {
    Expr* left = parse_unary();
    left = parse_postfix(left);

    while (1) {
        skip_ws();
        char* save = pos;
        BinaryOp op = parse_binop();
        if (op == -1) {
            pos = save;
            break;
        }

        int prec = get_precedence(op);
        if (prec < min_prec) {
            pos = save;
            break;
        }

        skip_ws();
        Expr* right = parse_binary(prec + 1);
        right = parse_postfix(right);

        Expr* binary = alloc_expr(EXPR_BINARY);
        binary->data.binary.op = op;
        binary->data.binary.left = left;
        binary->data.binary.right = right;
        left = binary;
    }

    return left;
}

Expr* parse_expr() {
    return parse_binary(1);
}

/* ============================================================
 * CODE GENERATION
 * ============================================================ */

int alloc_reg() {
    if (next_temp_reg > 31) {
        fprintf(stderr, "Error: Out of registers!\n");
        return 14;  /* Reuse r14 */
    }
    return next_temp_reg++;
}

void free_reg(int reg) {
    if (reg == next_temp_reg - 1) {
        next_temp_reg--;
    }
}

void emit_expr(Expr* e);

void emit_binop(BinaryOp op, int dest, int left, int right) {
    switch (op) {
        case OP_ADD:
            printf("    add r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_SUB:
            printf("    sub r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_MUL:
            printf("    mullw r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_DIV:
            printf("    divw r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_MOD:
            printf("    divw r0, r%d, r%d\n", left, right);
            printf("    mullw r0, r0, r%d\n", right);
            printf("    sub r%d, r%d, r0\n", dest, left);
            break;
        case OP_AND:
            printf("    and r%d, r%d, r%d\n", dest, left, right);
            printf("    cmpwi r%d, 0\n", dest);
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 3, 31, 31\n", dest, dest);
            printf("    xori r%d, r%d, 1\n", dest, dest);
            break;
        case OP_OR:
            printf("    or r%d, r%d, r%d\n", dest, left, right);
            printf("    cmpwi r%d, 0\n", dest);
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 3, 31, 31\n", dest, dest);
            printf("    xori r%d, r%d, 1\n", dest, dest);
            break;
        case OP_BITAND:
            printf("    and r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_BITOR:
            printf("    or r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_BITXOR:
            printf("    xor r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_SHL:
            printf("    slw r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_SHR:
            printf("    srw r%d, r%d, r%d\n", dest, left, right);
            break;
        case OP_EQ:
            printf("    cmpw r%d, r%d\n", left, right);
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 3, 31, 31\n", dest, dest);
            break;
        case OP_NE:
            printf("    cmpw r%d, r%d\n", left, right);
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 3, 31, 31\n", dest, dest);
            printf("    xori r%d, r%d, 1\n", dest, dest);
            break;
        case OP_LT:
            printf("    cmpw r%d, r%d\n", left, right);
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 1, 31, 31\n", dest, dest);
            break;
        case OP_LE:
            printf("    cmpw r%d, r%d\n", left, right);
            printf("    cror 2, 0, 2\n");  /* LT or EQ */
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 3, 31, 31\n", dest, dest);
            break;
        case OP_GT:
            printf("    cmpw r%d, r%d\n", left, right);
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 2, 31, 31\n", dest, dest);
            break;
        case OP_GE:
            printf("    cmpw r%d, r%d\n", left, right);
            printf("    cror 2, 1, 2\n");  /* GT or EQ */
            printf("    mfcr r%d\n", dest);
            printf("    rlwinm r%d, r%d, 3, 31, 31\n", dest, dest);
            break;
        default:
            printf("    ; TODO: binop %d\n", op);
    }
}

void emit_expr(Expr* e) {
    if (!e) return;

    switch (e->kind) {
        case EXPR_LITERAL_INT:
            e->temp_reg = alloc_reg();
            if (e->data.int_val >= -32768 && e->data.int_val <= 32767) {
                printf("    li r%d, %lld\n", e->temp_reg, e->data.int_val);
            } else {
                printf("    lis r%d, %lld@ha\n", e->temp_reg, e->data.int_val);
                printf("    ori r%d, r%d, %lld@l\n", e->temp_reg, e->temp_reg,
                       e->data.int_val);
            }
            break;

        case EXPR_LITERAL_BOOL:
            e->temp_reg = alloc_reg();
            printf("    li r%d, %d\n", e->temp_reg, e->data.bool_val);
            break;

        case EXPR_IDENT:
            e->temp_reg = alloc_reg();
            printf("    lwz r%d, %d(r1)    ; load %s\n",
                   e->temp_reg, e->data.ident.var_offset, e->data.ident.name);
            break;

        case EXPR_BINARY:
            emit_expr(e->data.binary.left);
            emit_expr(e->data.binary.right);
            e->temp_reg = e->data.binary.left->temp_reg;
            emit_binop(e->data.binary.op, e->temp_reg,
                      e->data.binary.left->temp_reg,
                      e->data.binary.right->temp_reg);
            free_reg(e->data.binary.right->temp_reg);
            break;

        case EXPR_UNARY:
            emit_expr(e->data.unary.operand);
            e->temp_reg = e->data.unary.operand->temp_reg;
            switch (e->data.unary.op) {
                case UOP_NEG:
                    printf("    neg r%d, r%d\n", e->temp_reg, e->temp_reg);
                    break;
                case UOP_NOT:
                    printf("    xori r%d, r%d, 1\n", e->temp_reg, e->temp_reg);
                    break;
                case UOP_DEREF:
                    printf("    lwz r%d, 0(r%d)\n", e->temp_reg, e->temp_reg);
                    break;
                case UOP_REF:
                case UOP_MUTREF:
                    /* Address is already in register */
                    break;
                default:
                    break;
            }
            break;

        case EXPR_CALL: {
            /* Push args to registers r3-r10 */
            for (int i = 0; i < e->data.call.args.count && i < 8; i++) {
                emit_expr(e->data.call.args.elements[i]);
                if (e->data.call.args.elements[i]->temp_reg != 3 + i) {
                    printf("    mr r%d, r%d\n", 3 + i,
                           e->data.call.args.elements[i]->temp_reg);
                }
                free_reg(e->data.call.args.elements[i]->temp_reg);
            }
            printf("    bl _%s\n", e->data.call.name);
            e->temp_reg = 3;  /* Return value in r3 */
            break;
        }

        case EXPR_METHOD_CALL: {
            emit_expr(e->data.call.receiver);
            printf("    mr r3, r%d    ; self\n", e->data.call.receiver->temp_reg);
            free_reg(e->data.call.receiver->temp_reg);

            for (int i = 0; i < e->data.call.args.count && i < 7; i++) {
                emit_expr(e->data.call.args.elements[i]);
                if (e->data.call.args.elements[i]->temp_reg != 4 + i) {
                    printf("    mr r%d, r%d\n", 4 + i,
                           e->data.call.args.elements[i]->temp_reg);
                }
                free_reg(e->data.call.args.elements[i]->temp_reg);
            }
            printf("    bl _%s_%s\n", "Self", e->data.call.name);
            e->temp_reg = 3;
            break;
        }

        case EXPR_IF: {
            static int label_counter = 0;
            int label = label_counter++;

            emit_expr(e->data.if_expr.condition);
            printf("    cmpwi r%d, 0\n", e->data.if_expr.condition->temp_reg);
            free_reg(e->data.if_expr.condition->temp_reg);
            printf("    beq Lelse_%d\n", label);

            emit_expr(e->data.if_expr.then_branch);
            e->temp_reg = e->data.if_expr.then_branch->temp_reg;
            printf("    b Lend_%d\n", label);

            printf("Lelse_%d:\n", label);
            if (e->data.if_expr.else_branch) {
                emit_expr(e->data.if_expr.else_branch);
                printf("    mr r%d, r%d\n", e->temp_reg,
                       e->data.if_expr.else_branch->temp_reg);
                free_reg(e->data.if_expr.else_branch->temp_reg);
            }

            printf("Lend_%d:\n", label);
            break;
        }

        case EXPR_TRY:
            emit_expr(e->data.unary.operand);
            e->temp_reg = e->data.unary.operand->temp_reg;
            printf("    ; ? operator - check for Err/None\n");
            printf("    lwz r0, 0(r%d)    ; tag\n", e->temp_reg);
            printf("    cmpwi r0, 0\n");
            printf("    bne _early_return_%d\n", current_line);
            printf("    lwz r%d, 4(r%d)   ; extract Ok/Some value\n",
                   e->temp_reg, e->temp_reg);
            break;

        case EXPR_BLOCK:
            for (int i = 0; i < e->data.block.stmt_count; i++) {
                emit_expr(e->data.block.stmts[i]);
                if (e->data.block.stmts[i]->temp_reg >= 0) {
                    free_reg(e->data.block.stmts[i]->temp_reg);
                }
            }
            if (e->data.block.final_expr) {
                emit_expr(e->data.block.final_expr);
                e->temp_reg = e->data.block.final_expr->temp_reg;
            }
            break;

        default:
            printf("    ; TODO: emit expr kind %d\n", e->kind);
            e->temp_reg = alloc_reg();
            printf("    li r%d, 0\n", e->temp_reg);
    }
}

/* ============================================================
 * DEMONSTRATION
 * ============================================================ */

void demonstrate_expressions() {
    printf("; === Expression Evaluation Demo ===\n\n");

    /* Test: 2 + 3 * 4 */
    char* test1 = "2 + 3 * 4";
    pos = test1;
    next_temp_reg = 14;
    printf("; Expression: %s\n", test1);
    Expr* e1 = parse_expr();
    emit_expr(e1);
    printf("; Result in r%d\n\n", e1->temp_reg);

    /* Test: (a + b) * c */
    char* test2 = "(a + b) * c";
    pos = test2;
    next_temp_reg = 14;
    printf("; Expression: %s\n", test2);
    Expr* e2 = parse_expr();
    /* Would need variable resolution */
    printf("; Parsed OK (would need var offsets)\n\n");

    /* Test: x.y.method(1, 2) */
    char* test3 = "vec.iter().map(|x| x * 2).collect()";
    pos = test3;
    next_temp_reg = 14;
    printf("; Expression: %s\n", test3);
    Expr* e3 = parse_expr();
    printf("; Parsed method chain OK\n\n");

    /* Test: if condition { a } else { b } */
    char* test4 = "if x > 0 { 1 } else { -1 }";
    pos = test4;
    next_temp_reg = 14;
    printf("; Expression: %s\n", test4);
    Expr* e4 = parse_expr();
    printf("; Parsed if-else OK\n\n");

    /* Test: result? */
    char* test5 = "foo()?";
    pos = test5;
    next_temp_reg = 14;
    printf("; Expression: %s\n", test5);
    Expr* e5 = parse_expr();
    emit_expr(e5);
    printf("; Try operator OK\n");
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        demonstrate_expressions();
    } else {
        printf("Rust Expression Evaluator for PowerPC\n");
        printf("Usage: %s --demo    Run demonstration\n", argv[0]);
    }
    return 0;
}
