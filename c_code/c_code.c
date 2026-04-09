#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <emscripten.h>

#define MAX_SYMBOLS 100
#define MAX_PRODS 100
#define MAX_RHS 20
#define STR_LEN 64
#define JSON_BUF_SIZE 131072
#define MAX_ITEMS_PER_SET 100
#define MAX_STATES 100
#define EPSILON_SYMBOL -1
#define INVALID_SYMBOL -2

typedef struct {
    char name[STR_LEN];
    bool is_term;
} Symbol;

typedef struct {
    int lhs;
    int rhs[MAX_RHS];
    int rhs_len;
} Production;

typedef struct {
    int prod_idx;
    int dot_pos;
} LRItem;

typedef struct {
    LRItem items[MAX_ITEMS_PER_SET];
    int num_items;
} LRItemSet;

typedef struct {
    LRItemSet sets[MAX_STATES];
    int num_sets;
} LRCollection;

static Symbol symbols[MAX_SYMBOLS];
static int num_symbols = 0;

static Production prods[MAX_PRODS];
static int num_prods = 0;

static bool nullable[MAX_SYMBOLS];
static bool first[MAX_SYMBOLS][MAX_SYMBOLS];
static bool follow[MAX_SYMBOLS][MAX_SYMBOLS];

static char error_msg[512];
static char json_buf[JSON_BUF_SIZE];
static int json_pos = 0;

static void clear_error() {
    error_msg[0] = '\0';
}

static bool has_error() {
    return error_msg[0] != '\0';
}

static void set_error(const char* fmt, ...) {
    if (has_error()) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_msg, sizeof(error_msg), fmt, args);
    va_end(args);
}

static void reset_state() {
    num_symbols = 0;
    num_prods = 0;
    memset(nullable, 0, sizeof(nullable));
    memset(first, 0, sizeof(first));
    memset(follow, 0, sizeof(follow));
    clear_error();
}

static char* trim_in_place(char* s) {
    while (*s && isspace((unsigned char) *s)) {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char) s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

static bool is_epsilon_name(const char* name) {
    return strcmp(name, "ε") == 0 || strcmp(name, "e") == 0 || strcmp(name, "epsilon") == 0;
}

static bool is_upper_str(const char* s) {
    return s[0] >= 'A' && s[0] <= 'Z';
}

static bool symbol_name_exists(const char* name) {
    for (int i = 0; i < num_symbols; i++) {
        if (strcmp(symbols[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static int get_symbol(const char* name, bool is_term) {
    if (is_epsilon_name(name)) {
        return EPSILON_SYMBOL;
    }
    for (int i = 0; i < num_symbols; i++) {
        if (strcmp(symbols[i].name, name) == 0) {
            return i;
        }
    }
    if (num_symbols >= MAX_SYMBOLS) {
        set_error("Too many symbols. Maximum supported symbols: %d.", MAX_SYMBOLS);
        return INVALID_SYMBOL;
    }
    snprintf(symbols[num_symbols].name, sizeof(symbols[num_symbols].name), "%s", name);
    symbols[num_symbols].is_term = is_term;
    return num_symbols++;
}

static bool begin_production(int lhs) {
    if (num_prods >= MAX_PRODS) {
        set_error("Too many productions. Maximum supported productions: %d.", MAX_PRODS);
        return false;
    }
    prods[num_prods].lhs = lhs;
    prods[num_prods].rhs_len = 0;
    return true;
}

static bool push_rhs_symbol(int prod_index, int sym) {
    if (prods[prod_index].rhs_len >= MAX_RHS) {
        set_error("Production exceeds maximum RHS length of %d symbols.", MAX_RHS);
        return false;
    }
    prods[prod_index].rhs[prods[prod_index].rhs_len++] = sym;
    return true;
}

static void add_production_from_part(int lhs, char* rhs_part) {
    if (has_error()) return;
    if (!begin_production(lhs)) return;
    int prod_index = num_prods;
    char* trimmed_part = trim_in_place(rhs_part);
    if (*trimmed_part != '\0') {
        char* token_ctx = NULL;
        char* token = strtok_r(trimmed_part, " \t\r", &token_ctx);
        while (token) {
            int sym = get_symbol(token, !is_upper_str(token));
            if (sym == INVALID_SYMBOL) return;
            if (sym != EPSILON_SYMBOL && !push_rhs_symbol(prod_index, sym)) return;
            token = strtok_r(NULL, " \t\r", &token_ctx);
        }
    }
    num_prods++;
}

static void parse_grammar(const char* grammar_str) {
    reset_state();
    char* copy = strdup(grammar_str ? grammar_str : "");
    if (!copy) {
        set_error("Failed to allocate memory while parsing the grammar.");
        return;
    }
    char* line_ctx = NULL;
    char* line = strtok_r(copy, "\n", &line_ctx);
    while (line && !has_error()) {
        char* trimmed_line = trim_in_place(line);
        if (*trimmed_line != '\0') {
            char* arrow = strstr(trimmed_line, "->");
            int arrow_len = 2;
            if (!arrow) {
                arrow = strstr(trimmed_line, "→");
                arrow_len = (int) strlen("→");
            }
            if (!arrow) {
                set_error("Invalid production: %s", trimmed_line);
                break;
            }
            *arrow = '\0';
            char* lhs_str = trim_in_place(trimmed_line);
            char* rhs_str = trim_in_place(arrow + arrow_len);
            if (*lhs_str == '\0') {
                set_error("Missing left-hand side in production: %s", line);
                break;
            }
            int lhs = get_symbol(lhs_str, false);
            if (lhs == INVALID_SYMBOL) break;
            if (*rhs_str == '\0') {
                add_production_from_part(lhs, rhs_str);
            } else {
                char rhs_copy[512];
                snprintf(rhs_copy, sizeof(rhs_copy), "%s", rhs_str);
                char* alt_ctx = NULL;
                char* rhs_part = strtok_r(rhs_copy, "|", &alt_ctx);
                while (rhs_part && !has_error()) {
                    add_production_from_part(lhs, rhs_part);
                    rhs_part = strtok_r(NULL, "|", &alt_ctx);
                }
            }
        }
        line = strtok_r(NULL, "\n", &line_ctx);
    }
    if (!has_error() && num_prods == 0) {
        set_error("The grammar does not contain any productions.");
    }
    free(copy);
}

static void compute_first_follow() {
    if (has_error()) return;
    for (int i = 0; i < num_symbols; i++) {
        if (symbols[i].is_term) first[i][i] = true;
    }
    bool changed = true;
    while (changed && !has_error()) {
        changed = false;
        for (int i = 0; i < num_prods; i++) {
            int lhs = prods[i].lhs;
            bool all_null = true;
            for (int j = 0; j < prods[i].rhs_len; j++) {
                int rhs_sym = prods[i].rhs[j];
                for (int k = 0; k < num_symbols; k++) {
                    if (first[rhs_sym][k] && !first[lhs][k]) {
                        first[lhs][k] = true;
                        changed = true;
                    }
                }
                if (!nullable[rhs_sym]) {
                    all_null = false;
                    break;
                }
            }
            if (all_null && !nullable[lhs]) {
                nullable[lhs] = true;
                changed = true;
            }
        }
    }
    if (num_prods > 0) {
        int start_sym = prods[0].lhs;
        int eof_sym = get_symbol("$", true);
        if (eof_sym >= 0) follow[start_sym][eof_sym] = true;
    }
    changed = true;
    while (changed && !has_error()) {
        changed = false;
        for (int i = 0; i < num_prods; i++) {
            int lhs = prods[i].lhs;
            for (int j = 0; j < prods[i].rhs_len; j++) {
                int sym_B = prods[i].rhs[j];
                if (symbols[sym_B].is_term) continue;
                bool all_null = true;
                for (int k = j + 1; k < prods[i].rhs_len; k++) {
                    int sym_beta = prods[i].rhs[k];
                    for (int m = 0; m < num_symbols; m++) {
                        if (first[sym_beta][m] && !follow[sym_B][m]) {
                            follow[sym_B][m] = true;
                            changed = true;
                        }
                    }
                    if (!nullable[sym_beta]) {
                        all_null = false;
                        break;
                    }
                }
                if (all_null) {
                    for (int m = 0; m < num_symbols; m++) {
                        if (follow[lhs][m] && !follow[sym_B][m]) {
                            follow[sym_B][m] = true;
                            changed = true;
                        }
                    }
                }
            }
        }
    }
}

static void clear_json() {
    json_pos = 0;
    json_buf[0] = '\0';
}

static void j_append(const char* str) {
    size_t len = strlen(str);
    if ((size_t) json_pos + len >= sizeof(json_buf)) {
        set_error("Generated output exceeded the supported JSON size.");
        return;
    }
    memcpy(json_buf + json_pos, str, len + 1);
    json_pos += (int) len;
}

static void j_append_fmt(const char* fmt, ...) {
    char tmp[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    j_append(tmp);
}

static void j_append_json_string(const char* str) {
    j_append("\"");
    for (const unsigned char* p = (const unsigned char*) str; *p && !has_error(); p++) {
        switch (*p) {
            case '\\': j_append("\\\\"); break;
            case '"': j_append("\\\""); break;
            case '\n': j_append("\\n"); break;
            case '\r': j_append("\\r"); break;
            case '\t': j_append("\\t"); break;
            default:
                if (*p < 0x20) j_append_fmt("\\u%04x", *p);
                else { char ch[2] = {(char) *p, '\0'}; j_append(ch); }
                break;
        }
    }
    j_append("\"");
}

static void j_append_symbol_name(const char* name) {
    j_append_json_string(name);
}

static void append_production_json(const char* lhs_name, const Production* prod) {
    j_append("{\"lhs\":");
    j_append_symbol_name(lhs_name);
    j_append(",\"rhs\":[");
    if (prod->rhs_len == 0) j_append_json_string("ε");
    else {
        for (int j = 0; j < prod->rhs_len; j++) {
            if (j > 0) j_append(",");
            j_append_symbol_name(symbols[prod->rhs[j]].name);
        }
    }
    j_append("]}");
}

static bool production_matches_terminal(int prod_index, int term_index) {
    bool prod_first = false;
    bool prod_null = true;
    for (int j = 0; j < prods[prod_index].rhs_len; j++) {
        int rhs_sym = prods[prod_index].rhs[j];
        if (first[rhs_sym][term_index]) prod_first = true;
        if (!nullable[rhs_sym]) { prod_null = false; break; }
    }
    if (prod_first) return true;
    return prod_null && follow[prods[prod_index].lhs][term_index];
}

static void append_error_response() {
    clear_json();
    j_append("{\"error\":");
    j_append_json_string(error_msg[0] ? error_msg : "Unknown error.");
    j_append("}");
}

static const char* build_augmented_start_symbol() {
    static char augmented_name[STR_LEN];
    if (num_prods == 0) { augmented_name[0] = '\0'; return augmented_name; }
    snprintf(augmented_name, sizeof(augmented_name), "%s'", symbols[prods[0].lhs].name);
    while (symbol_name_exists(augmented_name) && strlen(augmented_name) < STR_LEN - 2) {
        strncat(augmented_name, "'", STR_LEN - strlen(augmented_name) - 1);
    }
    return augmented_name;
}

static bool add_item_to_set(LRItemSet* set, int prod_idx, int dot_pos) {
    for (int i = 0; i < set->num_items; i++) {
        if (set->items[i].prod_idx == prod_idx && set->items[i].dot_pos == dot_pos) return false;
    }
    if (set->num_items >= MAX_ITEMS_PER_SET) return false;
    set->items[set->num_items].prod_idx = prod_idx;
    set->items[set->num_items].dot_pos = dot_pos;
    set->num_items++;
    return true;
}

static void compute_lr0_closure(LRItemSet* set) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < set->num_items; i++) {
            int p_idx = set->items[i].prod_idx;
            int dot = set->items[i].dot_pos;
            if (dot < prods[p_idx].rhs_len) {
                int next_sym = prods[p_idx].rhs[dot];
                if (!symbols[next_sym].is_term) {
                    for (int j = 0; j < num_prods; j++) {
                        if (prods[j].lhs == next_sym) {
                            if (add_item_to_set(set, j, 0)) changed = true;
                        }
                    }
                }
            }
        }
    }
}

static void compute_lr0_goto(const LRItemSet* src, LRItemSet* dest, int sym_idx) {
    dest->num_items = 0;
    for (int i = 0; i < src->num_items; i++) {
        int p_idx = src->items[i].prod_idx;
        int dot = src->items[i].dot_pos;
        if (dot < prods[p_idx].rhs_len && prods[p_idx].rhs[dot] == sym_idx) {
            add_item_to_set(dest, p_idx, dot + 1);
        }
    }
    if (dest->num_items > 0) compute_lr0_closure(dest);
}

static bool are_sets_equal(const LRItemSet* s1, const LRItemSet* s2) {
    if (s1->num_items != s2->num_items) return false;
    for (int i = 0; i < s1->num_items; i++) {
        bool found = false;
        for (int j = 0; j < s2->num_items; j++) {
            if (s1->items[i].prod_idx == s2->items[j].prod_idx && 
                s1->items[i].dot_pos == s2->items[j].dot_pos) {
                found = true; break;
            }
        }
        if (!found) return false;
    }
    return true;
}

EMSCRIPTEN_KEEPALIVE
char* process_ll1(const char* grammar_str) {
    parse_grammar(grammar_str);
    compute_first_follow();
    if (has_error()) { append_error_response(); return strdup(json_buf); }
    clear_json();
    j_append("{");
    j_append("\"start_symbol\":");
    if (num_prods > 0) j_append_symbol_name(symbols[prods[0].lhs].name);
    else j_append_json_string("");
    j_append(",");
    j_append("\"terminals\":[");
    bool first_t = true;
    for (int i = 0; i < num_symbols; i++) {
        if (symbols[i].is_term && strcmp(symbols[i].name, "$") != 0) {
            if (!first_t) j_append(",");
            j_append_symbol_name(symbols[i].name);
            first_t = false;
        }
    }
    j_append("],");
    j_append("\"non_terminals\":[");
    bool first_nt = true;
    for (int i = 0; i < num_symbols; i++) {
        if (!symbols[i].is_term) {
            if (!first_nt) j_append(",");
            j_append_symbol_name(symbols[i].name);
            first_nt = false;
        }
    }
    j_append("],");
    j_append("\"productions\":[");
    for (int i = 0; i < num_prods; i++) {
        if (i > 0) j_append(",");
        append_production_json(symbols[prods[i].lhs].name, &prods[i]);
    }
    j_append("],");
    j_append("\"first_sets\":{");
    first_nt = true;
    for (int i = 0; i < num_symbols; i++) {
        if (!symbols[i].is_term) {
            if (!first_nt) j_append(",");
            j_append_symbol_name(symbols[i].name);
            j_append(":[");
            bool first_s = true;
            for (int j = 0; j < num_symbols; j++) {
                if (first[i][j]) {
                    if (!first_s) j_append(",");
                    j_append_symbol_name(symbols[j].name);
                    first_s = false;
                }
            }
            if (nullable[i]) {
                if (!first_s) j_append(",");
                j_append_json_string("ε");
            }
            j_append("]");
            first_nt = false;
        }
    }
    j_append("},");
    j_append("\"follow_sets\":{");
    first_nt = true;
    for (int i = 0; i < num_symbols; i++) {
        if (!symbols[i].is_term) {
            if (!first_nt) j_append(",");
            j_append_symbol_name(symbols[i].name);
            j_append(":[");
            bool first_s = true;
            for (int j = 0; j < num_symbols; j++) {
                if (follow[i][j]) {
                    if (!first_s) j_append(",");
                    j_append_symbol_name(symbols[j].name);
                    first_s = false;
                }
            }
            j_append("]");
            first_nt = false;
        }
    }
    j_append("},");
    j_append("\"ll1_table\":[");
    bool first_row = true;
    bool is_ll1 = true;
    for (int i = 0; i < num_symbols; i++) {
        if (!symbols[i].is_term) {
            for (int t = 0; t < num_symbols; t++) {
                if (!symbols[t].is_term) continue;
                int chosen_prod = -1;
                for (int p = 0; p < num_prods; p++) {
                    if (prods[p].lhs == i && production_matches_terminal(p, t)) {
                        if (chosen_prod == -1) chosen_prod = p;
                        else is_ll1 = false;
                    }
                }
                if (chosen_prod != -1) {
                    if (!first_row) j_append(",");
                    j_append("{\"nt\":");
                    j_append_symbol_name(symbols[i].name);
                    j_append(",\"t\":");
                    j_append_symbol_name(symbols[t].name);
                    j_append_fmt(",\"prod_idx\":%d}", chosen_prod);
                    first_row = false;
                }
            }
        }
    }
    j_append("],");
    j_append("\"is_ll1\":");
    j_append(is_ll1 ? "true" : "false");
    j_append("}");
    if (has_error()) append_error_response();
    return strdup(json_buf);
}

EMSCRIPTEN_KEEPALIVE
char* process_lr0(const char* grammar_str) {
    parse_grammar(grammar_str);
    if (has_error()) { append_error_response(); return strdup(json_buf); }

    int original_num_prods = num_prods;
    const char* augmented_name = build_augmented_start_symbol();
    int aug_lhs = get_symbol(augmented_name, false);
    int start_sym = prods[0].lhs;

    prods[num_prods].lhs = aug_lhs;
    prods[num_prods].rhs[0] = start_sym;
    prods[num_prods].rhs_len = 1;
    int aug_prod_idx = num_prods++;

    LRCollection coll;
    coll.num_sets = 0;
    LRItemSet initial;
    initial.num_items = 0;
    add_item_to_set(&initial, aug_prod_idx, 0);
    compute_lr0_closure(&initial);
    coll.sets[coll.num_sets++] = initial;

    for (int i = 0; i < coll.num_sets; i++) {
        for (int s = 0; s < num_symbols; s++) {
            LRItemSet next;
            compute_lr0_goto(&coll.sets[i], &next, s);
            if (next.num_items > 0) {
                bool exists = false;
                for (int k = 0; k < coll.num_sets; k++) {
                    if (are_sets_equal(&coll.sets[k], &next)) { exists = true; break; }
                }
                if (!exists && coll.num_sets < MAX_STATES) coll.sets[coll.num_sets++] = next;
            }
        }
    }

    clear_json();
    j_append("{");
    j_append("\"start_symbol\":");
    j_append_symbol_name(symbols[start_sym].name);
    j_append(",\"augmented_start_symbol\":");
    j_append_symbol_name(augmented_name);
    j_append(",");

    j_append("\"item_sets\":[");
    for (int i = 0; i < coll.num_sets; i++) {
        if (i > 0) j_append(",");
        j_append_fmt("{\"id\":%d,\"items\":[", i);
        for (int j = 0; j < coll.sets[i].num_items; j++) {
            if (j > 0) j_append(",");
            int p_idx = coll.sets[i].items[j].prod_idx;
            j_append("{\"lhs\":");
            j_append_symbol_name(symbols[prods[p_idx].lhs].name);
            j_append(",\"rhs\":[");
            for (int k = 0; k < prods[p_idx].rhs_len; k++) {
                if (k > 0) j_append(",");
                j_append_symbol_name(symbols[prods[p_idx].rhs[k]].name);
            }
            j_append_fmt("],\"dot\":%d}", coll.sets[i].items[j].dot_pos);
        }
        j_append("]}");
    }
    j_append("],");

    j_append("\"action_goto_table\":[");
    bool first_entry = true;
    int eof_sym = get_symbol("$", true);
    for (int i = 0; i < coll.num_sets; i++) {
        for (int s = 0; s < num_symbols; s++) {
            LRItemSet next;
            compute_lr0_goto(&coll.sets[i], &next, s);
            if (next.num_items > 0) {
                int target = -1;
                for (int k = 0; k < coll.num_sets; k++) {
                    if (are_sets_equal(&coll.sets[k], &next)) { target = k; break; }
                }
                if (target != -1) {
                    if (!first_entry) j_append(",");
                    j_append_fmt("{\"state\":%d,\"sym\":", i);
                    j_append_symbol_name(symbols[s].name);
                    j_append_fmt(",\"type\":%s,\"target\":%d}", symbols[s].is_term ? "\"shift\"" : "\"goto\"", target);
                    first_entry = false;
                }
            }
        }
        for (int j = 0; j < coll.sets[i].num_items; j++) {
            int p_idx = coll.sets[i].items[j].prod_idx;
            int dot = coll.sets[i].items[j].dot_pos;
            if (dot == prods[p_idx].rhs_len) {
                if (p_idx == aug_prod_idx) {
                    if (!first_entry) j_append(",");
                    j_append_fmt("{\"state\":%d,\"sym\":\"$\",\"type\":\"accept\"}", i);
                    first_entry = false;
                } else {
                    for (int t = 0; t < num_symbols; t++) {
                        if (symbols[t].is_term) {
                            if (!first_entry) j_append(",");
                            j_append_fmt("{\"state\":%d,\"sym\":", i);
                            j_append_symbol_name(symbols[t].name);
                            j_append_fmt(",\"type\":\"reduce\",\"prod_idx\":%d}", p_idx);
                            first_entry = false;
                        }
                    }
                }
            }
        }
    }
    j_append("]}");
    return strdup(json_buf);
}

EMSCRIPTEN_KEEPALIVE
void free_json(char* ptr) {
    free(ptr);
}