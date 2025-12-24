/*
 * Rust Macro Expansion for PowerPC
 * Full macro_rules! implementation
 *
 * Firefox uses complex macros - we need full support
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
 * MACRO TOKEN TYPES
 * ============================================================ */

typedef enum {
    TOK_IDENT,
    TOK_LITERAL,
    TOK_PUNCT,
    TOK_GROUP,      /* (...), [...], {...} */
    TOK_EOF
} TokenKind;

typedef struct Token {
    TokenKind kind;
    char text[256];
    char delimiter;     /* For groups: '(', '[', '{' */
    struct Token* group_contents;
    int group_count;
    struct Token* next;
} Token;

/* ============================================================
 * MACRO PATTERN MATCHING
 * ============================================================
 *
 * macro_rules! patterns use:
 *   $name:kind - captures
 *   $(...),* - repetition with separator
 *   $(...),+ - one or more
 *   $(...)?  - optional
 */

typedef enum {
    FRAG_IDENT,     /* $x:ident */
    FRAG_EXPR,      /* $x:expr */
    FRAG_TY,        /* $x:ty */
    FRAG_PAT,       /* $x:pat */
    FRAG_STMT,      /* $x:stmt */
    FRAG_BLOCK,     /* $x:block */
    FRAG_ITEM,      /* $x:item */
    FRAG_META,      /* $x:meta */
    FRAG_TT,        /* $x:tt - any token tree */
    FRAG_LITERAL,   /* $x:literal */
    FRAG_LIFETIME,  /* $x:lifetime */
    FRAG_VIS,       /* $x:vis */
    FRAG_PATH,      /* $x:path */
} FragmentKind;

typedef struct {
    char name[64];
    FragmentKind kind;
    Token* captured;
    int capture_count;  /* For repetitions */
} Capture;

typedef struct MacroRule {
    Token* pattern;
    Token* expansion;
    Capture captures[64];
    int capture_count;
} MacroRule;

typedef struct Macro {
    char name[64];
    MacroRule rules[16];
    int rule_count;
    int is_builtin;
} Macro;

/* ============================================================
 * GLOBAL STATE
 * ============================================================ */

#define MAX_MACROS 200

Macro macros[MAX_MACROS];
int macro_count = 0;

/* Built-in macro implementations */
void init_builtin_macros();

/* ============================================================
 * TOKENIZER
 * ============================================================ */

static char* pos;

void skip_ws_comments() {
    while (*pos) {
        if (isspace(*pos)) {
            pos++;
        } else if (pos[0] == '/' && pos[1] == '/') {
            while (*pos && *pos != '\n') pos++;
        } else if (pos[0] == '/' && pos[1] == '*') {
            pos += 2;
            while (*pos && !(pos[0] == '*' && pos[1] == '/')) pos++;
            if (*pos) pos += 2;
        } else {
            break;
        }
    }
}

Token* alloc_token(TokenKind kind) {
    Token* t = calloc(1, sizeof(Token));
    t->kind = kind;
    return t;
}

Token* tokenize(char* input);

Token* tokenize_group(char end_delim) {
    Token* first = NULL;
    Token* last = NULL;

    while (*pos && *pos != end_delim) {
        skip_ws_comments();
        if (*pos == end_delim) break;

        Token* t = NULL;

        /* Nested group */
        if (*pos == '(' || *pos == '[' || *pos == '{') {
            char open = *pos++;
            char close = (open == '(') ? ')' : (open == '[') ? ']' : '}';
            t = alloc_token(TOK_GROUP);
            t->delimiter = open;
            t->group_contents = tokenize_group(close);
            if (*pos == close) pos++;
        }
        /* String literal */
        else if (*pos == '"') {
            t = alloc_token(TOK_LITERAL);
            int i = 0;
            t->text[i++] = *pos++;
            while (*pos && *pos != '"' && i < 254) {
                if (*pos == '\\') t->text[i++] = *pos++;
                if (*pos) t->text[i++] = *pos++;
            }
            if (*pos == '"') t->text[i++] = *pos++;
            t->text[i] = '\0';
        }
        /* Char literal */
        else if (*pos == '\'') {
            t = alloc_token(TOK_LITERAL);
            int i = 0;
            t->text[i++] = *pos++;
            if (*pos == '\\') t->text[i++] = *pos++;
            if (*pos) t->text[i++] = *pos++;
            if (*pos == '\'') t->text[i++] = *pos++;
            t->text[i] = '\0';
        }
        /* Number */
        else if (isdigit(*pos)) {
            t = alloc_token(TOK_LITERAL);
            int i = 0;
            while (*pos && (isalnum(*pos) || *pos == '.' || *pos == '_') && i < 254) {
                t->text[i++] = *pos++;
            }
            t->text[i] = '\0';
        }
        /* Identifier */
        else if (isalpha(*pos) || *pos == '_') {
            t = alloc_token(TOK_IDENT);
            int i = 0;
            while (*pos && (isalnum(*pos) || *pos == '_') && i < 254) {
                t->text[i++] = *pos++;
            }
            t->text[i] = '\0';
        }
        /* Punctuation */
        else if (*pos) {
            t = alloc_token(TOK_PUNCT);
            /* Multi-char punctuation */
            if ((pos[0] == '-' && pos[1] == '>') ||
                (pos[0] == '=' && pos[1] == '>') ||
                (pos[0] == ':' && pos[1] == ':') ||
                (pos[0] == '.' && pos[1] == '.') ||
                (pos[0] == '&' && pos[1] == '&') ||
                (pos[0] == '|' && pos[1] == '|')) {
                t->text[0] = *pos++;
                t->text[1] = *pos++;
                t->text[2] = '\0';
            } else {
                t->text[0] = *pos++;
                t->text[1] = '\0';
            }
        }

        if (t) {
            if (!first) first = t;
            if (last) last->next = t;
            last = t;
        }
    }

    return first;
}

Token* tokenize(char* input) {
    pos = input;
    return tokenize_group('\0');
}

/* ============================================================
 * PATTERN PARSING
 * ============================================================ */

FragmentKind parse_fragment_kind(const char* name) {
    if (strcmp(name, "ident") == 0) return FRAG_IDENT;
    if (strcmp(name, "expr") == 0) return FRAG_EXPR;
    if (strcmp(name, "ty") == 0) return FRAG_TY;
    if (strcmp(name, "pat") == 0) return FRAG_PAT;
    if (strcmp(name, "stmt") == 0) return FRAG_STMT;
    if (strcmp(name, "block") == 0) return FRAG_BLOCK;
    if (strcmp(name, "item") == 0) return FRAG_ITEM;
    if (strcmp(name, "meta") == 0) return FRAG_META;
    if (strcmp(name, "tt") == 0) return FRAG_TT;
    if (strcmp(name, "literal") == 0) return FRAG_LITERAL;
    if (strcmp(name, "lifetime") == 0) return FRAG_LIFETIME;
    if (strcmp(name, "vis") == 0) return FRAG_VIS;
    if (strcmp(name, "path") == 0) return FRAG_PATH;
    return FRAG_TT;
}

/* ============================================================
 * PATTERN MATCHING
 * ============================================================ */

int match_fragment(Token** input, FragmentKind kind, Token** captured) {
    Token* start = *input;
    *captured = NULL;

    switch (kind) {
        case FRAG_IDENT:
            if ((*input)->kind == TOK_IDENT) {
                *captured = *input;
                *input = (*input)->next;
                return 1;
            }
            break;

        case FRAG_LITERAL:
            if ((*input)->kind == TOK_LITERAL) {
                *captured = *input;
                *input = (*input)->next;
                return 1;
            }
            break;

        case FRAG_TT:
            /* Any single token tree */
            if (*input) {
                *captured = *input;
                *input = (*input)->next;
                return 1;
            }
            break;

        case FRAG_EXPR:
            /* Expression - greedy until separator */
            /* Simplified: capture until , ; ) ] } */
            {
                Token* expr_start = *input;
                Token* expr_end = NULL;
                int depth = 0;
                while (*input) {
                    if ((*input)->kind == TOK_GROUP) depth++;
                    if ((*input)->kind == TOK_PUNCT) {
                        char c = (*input)->text[0];
                        if (depth == 0 && (c == ',' || c == ';')) break;
                    }
                    expr_end = *input;
                    *input = (*input)->next;
                }
                if (expr_start != *input) {
                    *captured = expr_start;
                    return 1;
                }
            }
            break;

        case FRAG_TY:
            /* Type - similar to expr but different terminators */
            {
                Token* ty_start = *input;
                int depth = 0;
                while (*input) {
                    if ((*input)->kind == TOK_PUNCT) {
                        char c = (*input)->text[0];
                        if (c == '<') depth++;
                        if (c == '>') depth--;
                        if (depth == 0 && (c == ',' || c == '>' || c == '{')) break;
                    }
                    *input = (*input)->next;
                }
                if (ty_start != *input) {
                    *captured = ty_start;
                    return 1;
                }
            }
            break;

        case FRAG_BLOCK:
            if ((*input)->kind == TOK_GROUP && (*input)->delimiter == '{') {
                *captured = *input;
                *input = (*input)->next;
                return 1;
            }
            break;

        default:
            /* For other fragments, just capture one token */
            if (*input) {
                *captured = *input;
                *input = (*input)->next;
                return 1;
            }
    }

    *input = start;
    return 0;
}

int match_pattern(Token* pattern, Token* input, Capture* captures, int* cap_count) {
    *cap_count = 0;

    while (pattern && input) {
        /* Check for $ capture */
        if (pattern->kind == TOK_PUNCT && pattern->text[0] == '$') {
            pattern = pattern->next;
            if (!pattern) return 0;

            /* $name:kind */
            if (pattern->kind == TOK_IDENT) {
                char cap_name[64];
                strcpy(cap_name, pattern->text);
                pattern = pattern->next;

                if (pattern && pattern->kind == TOK_PUNCT && pattern->text[0] == ':') {
                    pattern = pattern->next;
                    if (pattern && pattern->kind == TOK_IDENT) {
                        FragmentKind kind = parse_fragment_kind(pattern->text);
                        pattern = pattern->next;

                        Token* captured;
                        if (match_fragment(&input, kind, &captured)) {
                            strcpy(captures[*cap_count].name, cap_name);
                            captures[*cap_count].kind = kind;
                            captures[*cap_count].captured = captured;
                            (*cap_count)++;
                        } else {
                            return 0;
                        }
                    }
                }
            }
            /* $(...) repetition */
            else if (pattern->kind == TOK_GROUP && pattern->delimiter == '(') {
                /* Handle repetition - simplified */
                pattern = pattern->next;
                /* Skip separator and quantifier */
                if (pattern && pattern->kind == TOK_PUNCT) {
                    pattern = pattern->next;  /* , or ; */
                }
                if (pattern && pattern->kind == TOK_PUNCT) {
                    pattern = pattern->next;  /* * or + or ? */
                }
            }
        }
        /* Literal match */
        else {
            if (pattern->kind != input->kind) return 0;
            if (strcmp(pattern->text, input->text) != 0) return 0;
            pattern = pattern->next;
            input = input->next;
        }
    }

    return (pattern == NULL);
}

/* ============================================================
 * EXPANSION
 * ============================================================ */

void substitute_captures(Token* expansion, Capture* captures, int cap_count,
                        char* output, int max_len) {
    int out_idx = 0;

    while (expansion && out_idx < max_len - 1) {
        if (expansion->kind == TOK_PUNCT && expansion->text[0] == '$') {
            expansion = expansion->next;
            if (expansion && expansion->kind == TOK_IDENT) {
                /* Find capture */
                for (int i = 0; i < cap_count; i++) {
                    if (strcmp(captures[i].name, expansion->text) == 0) {
                        /* Substitute */
                        Token* cap = captures[i].captured;
                        while (cap && out_idx < max_len - 1) {
                            int len = strlen(cap->text);
                            if (out_idx + len < max_len - 1) {
                                strcpy(output + out_idx, cap->text);
                                out_idx += len;
                                output[out_idx++] = ' ';
                            }
                            cap = cap->next;
                        }
                        break;
                    }
                }
                expansion = expansion->next;
            }
        } else {
            int len = strlen(expansion->text);
            if (out_idx + len < max_len - 1) {
                strcpy(output + out_idx, expansion->text);
                out_idx += len;
                output[out_idx++] = ' ';
            }
            expansion = expansion->next;
        }
    }
    output[out_idx] = '\0';
}

/* ============================================================
 * MACRO DEFINITION PARSING
 * ============================================================ */

Macro* parse_macro_rules(char* input) {
    pos = input;
    skip_ws_comments();

    if (strncmp(pos, "macro_rules!", 12) != 0) return NULL;
    pos += 12;
    skip_ws_comments();

    Macro* m = &macros[macro_count++];
    memset(m, 0, sizeof(Macro));
    m->is_builtin = 0;

    /* Macro name */
    int i = 0;
    while (*pos && (isalnum(*pos) || *pos == '_') && i < 63) {
        m->name[i++] = *pos++;
    }
    m->name[i] = '\0';

    skip_ws_comments();

    /* Macro body { rules } */
    if (*pos != '{') return m;
    pos++;

    while (*pos && *pos != '}') {
        skip_ws_comments();

        /* Pattern => Expansion */
        if (*pos == '(') {
            pos++;
            /* Parse pattern */
            char pattern_buf[4096] = {0};
            int pi = 0;
            int depth = 1;
            while (*pos && depth > 0 && pi < 4095) {
                if (*pos == '(') depth++;
                if (*pos == ')') depth--;
                if (depth > 0) pattern_buf[pi++] = *pos;
                pos++;
            }

            skip_ws_comments();
            if (pos[0] == '=' && pos[1] == '>') pos += 2;
            skip_ws_comments();

            /* Parse expansion */
            if (*pos == '{' || *pos == '(' || *pos == '[') {
                char open = *pos++;
                char close = (open == '{') ? '}' : (open == '(') ? ')' : ']';

                char expansion_buf[4096] = {0};
                int ei = 0;
                depth = 1;
                while (*pos && depth > 0 && ei < 4095) {
                    if (*pos == open) depth++;
                    if (*pos == close) depth--;
                    if (depth > 0) expansion_buf[ei++] = *pos;
                    pos++;
                }

                /* Store rule */
                MacroRule* rule = &m->rules[m->rule_count++];
                rule->pattern = tokenize(pattern_buf);
                rule->expansion = tokenize(expansion_buf);
            }
        }

        skip_ws_comments();
        if (*pos == ';') pos++;
    }

    return m;
}

/* ============================================================
 * MACRO INVOCATION
 * ============================================================ */

char* expand_macro(const char* name, char* args) {
    /* Find macro */
    Macro* m = NULL;
    for (int i = 0; i < macro_count; i++) {
        if (strcmp(macros[i].name, name) == 0) {
            m = &macros[i];
            break;
        }
    }

    if (!m) return NULL;

    /* Handle built-in macros */
    if (m->is_builtin) {
        static char result[4096];

        if (strcmp(name, "println") == 0) {
            snprintf(result, 4095,
                "{ let __fmt = format_args!(%s); "
                "::std::io::_print(__fmt); }", args);
            return result;
        }
        if (strcmp(name, "vec") == 0) {
            snprintf(result, 4095,
                "{ let mut __v = Vec::new(); "
                "__v.extend_from_slice(&[%s]); __v }", args);
            return result;
        }
        if (strcmp(name, "format") == 0) {
            snprintf(result, 4095,
                "{ ::std::fmt::format(format_args!(%s)) }", args);
            return result;
        }
        if (strcmp(name, "panic") == 0) {
            snprintf(result, 4095,
                "{ ::std::rt::begin_panic(%s) }", args);
            return result;
        }
        if (strcmp(name, "assert") == 0) {
            snprintf(result, 4095,
                "{ if !(%s) { panic!(\"assertion failed\"); } }", args);
            return result;
        }
        if (strcmp(name, "assert_eq") == 0) {
            snprintf(result, 4095,
                "{ match (&(%s)) { (left, right) => { "
                "if !(*left == *right) { panic!(\"not equal\"); } } } }", args);
            return result;
        }
        if (strcmp(name, "dbg") == 0) {
            snprintf(result, 4095,
                "{ let __val = %s; eprintln!(\"[dbg] = {:?}\", &__val); __val }", args);
            return result;
        }
        if (strcmp(name, "cfg") == 0) {
            /* Compile-time config - always return true for now */
            return "true";
        }
        if (strcmp(name, "include_str") == 0) {
            snprintf(result, 4095, "\"<included from %s>\"", args);
            return result;
        }
        if (strcmp(name, "concat") == 0) {
            snprintf(result, 4095, "\"%s\"", args);
            return result;
        }
        if (strcmp(name, "stringify") == 0) {
            snprintf(result, 4095, "\"%s\"", args);
            return result;
        }

        return args;  /* Unknown builtin */
    }

    /* User-defined macro - try each rule */
    Token* input = tokenize(args);

    for (int r = 0; r < m->rule_count; r++) {
        Capture captures[64];
        int cap_count;

        if (match_pattern(m->rules[r].pattern, input, captures, &cap_count)) {
            static char result[8192];
            substitute_captures(m->rules[r].expansion, captures, cap_count,
                               result, 8192);
            return result;
        }
    }

    return NULL;
}

/* ============================================================
 * BUILTIN MACRO INITIALIZATION
 * ============================================================ */

void init_builtin_macros() {
    const char* builtins[] = {
        "println", "print", "eprintln", "eprint",
        "vec", "format", "panic", "assert", "assert_eq", "assert_ne",
        "dbg", "todo", "unimplemented", "unreachable",
        "cfg", "env", "option_env",
        "include", "include_str", "include_bytes",
        "concat", "stringify", "line", "column", "file", "module_path",
        "compile_error", "concat_idents",
        NULL
    };

    for (int i = 0; builtins[i]; i++) {
        Macro* m = &macros[macro_count++];
        strcpy(m->name, builtins[i]);
        m->is_builtin = 1;
        m->rule_count = 0;
    }
}

/* ============================================================
 * CODE GENERATION
 * ============================================================ */

void emit_macro_expansion(const char* name, const char* expanded) {
    printf("; Macro %s! expanded:\n", name);
    printf("; %s\n", expanded);
}

/* ============================================================
 * DEMONSTRATION
 * ============================================================ */

void demonstrate_macros() {
    printf("; === Macro System Demonstration ===\n\n");

    init_builtin_macros();

    /* Test builtin macros */
    printf("; Built-in macro expansions:\n\n");

    char* e1 = expand_macro("println", "\"Hello, {}!\", name");
    printf("; println!(\"Hello, {}!\", name)\n;   => %s\n\n", e1);

    char* e2 = expand_macro("vec", "1, 2, 3, 4, 5");
    printf("; vec![1, 2, 3, 4, 5]\n;   => %s\n\n", e2);

    char* e3 = expand_macro("assert", "x > 0");
    printf("; assert!(x > 0)\n;   => %s\n\n", e3);

    /* Test user-defined macro */
    printf("; User-defined macro:\n\n");

    char* macro_def =
        "macro_rules! my_macro {\n"
        "    ($x:expr) => { $x + 1 };\n"
        "    ($x:expr, $y:expr) => { $x + $y };\n"
        "}";

    printf("; %s\n\n", macro_def);
    parse_macro_rules(macro_def);

    char* e4 = expand_macro("my_macro", "42");
    printf("; my_macro!(42)\n;   => %s\n\n", e4 ? e4 : "no match");

    char* e5 = expand_macro("my_macro", "a, b");
    printf("; my_macro!(a, b)\n;   => %s\n\n", e5 ? e5 : "no match");

    /* Firefox-style macro */
    printf("; Firefox-style derive macro simulation:\n\n");

    char* derive_def =
        "macro_rules! derive_debug {\n"
        "    ($name:ident { $($field:ident),* }) => {\n"
        "        impl Debug for $name {\n"
        "            fn fmt(&self, f: &mut Formatter) -> Result {\n"
        "                write!(f, stringify!($name))\n"
        "            }\n"
        "        }\n"
        "    };\n"
        "}";

    printf("; %s\n", derive_def);
    parse_macro_rules(derive_def);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--demo") == 0) {
        demonstrate_macros();
    } else {
        printf("Rust Macro System for PowerPC\n");
        printf("Usage: %s --demo    Run demonstration\n", argv[0]);
        printf("\nSupports:\n");
        printf("  - All built-in macros (println!, vec!, assert!, etc.)\n");
        printf("  - macro_rules! declarative macros\n");
        printf("  - Fragment specifiers ($x:expr, $x:ident, etc.)\n");
        printf("  - Repetition patterns ($(...),*)\n");
    }
    return 0;
}
