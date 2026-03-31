#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define MAX_PROD 20
#define MAX_LEN 20

// Global Variables
int num_prods;
char prods[MAX_PROD][MAX_LEN];
char non_terminals[MAX_PROD];
int num_nt = 0;
char terminals[MAX_PROD];
int num_t = 0;

char first[26][MAX_LEN];
char follow[26][MAX_LEN];
char table[26][128][MAX_LEN]; // LL(1) Parse Table

// Helper Functions
void add_to_set(char *set, char val) {
    if (val == '\0') return;
    for (int i = 0; set[i] != '\0'; i++) {
        if (set[i] == val) return; // Already exists
    }
    int len = strlen(set);
    set[len] = val;
    set[len + 1] = '\0';
}

int is_non_terminal(char c) {
    return (c >= 'A' && c <= 'Z');
}

void add_terminal(char c) {
    if (c != '#' && !is_non_terminal(c)) {
        add_to_set(terminals, c);
    }
}

// Compute FIRST set for a given character
void compute_first(char c, char *result) {
    if (!is_non_terminal(c)) {
        add_to_set(result, c);
        return;
    }

    for (int i = 0; i < num_prods; i++) {
        if (prods[i][0] == c) {
            if (prods[i][2] == '#') {
                add_to_set(result, '#');
            } else {
                int j = 2;
                int continue_derivation = 1;
                while (prods[i][j] != '\0' && continue_derivation) {
                    char temp_first[MAX_LEN] = "";
                    compute_first(prods[i][j], temp_first);
                    
                    continue_derivation = 0;
                    for (int k = 0; temp_first[k] != '\0'; k++) {
                        if (temp_first[k] == '#') {
                            continue_derivation = 1;
                        } else {
                            add_to_set(result, temp_first[k]);
                        }
                    }
                    j++;
                }
                if (continue_derivation) {
                    add_to_set(result, '#');
                }
            }
        }
    }
}

// Compute FOLLOW set for a given character
void compute_follow(char c, char *result) {
    if (prods[0][0] == c) {
        add_to_set(result, '$'); // Start symbol gets $
    }

    for (int i = 0; i < num_prods; i++) {
        for (int j = 2; prods[i][j] != '\0'; j++) {
            if (prods[i][j] == c) {
                int k = j + 1;
                int continue_derivation = 1;

                while (prods[i][k] != '\0' && continue_derivation) {
                    char temp_first[MAX_LEN] = "";
                    compute_first(prods[i][k], temp_first);
                    
                    continue_derivation = 0;
                    for (int m = 0; temp_first[m] != '\0'; m++) {
                        if (temp_first[m] == '#') {
                            continue_derivation = 1;
                        } else {
                            add_to_set(result, temp_first[m]);
                        }
                    }
                    k++;
                }

                if (continue_derivation && prods[i][0] != c) {
                    char temp_follow[MAX_LEN] = "";
                    compute_follow(prods[i][0], temp_follow);
                    for (int m = 0; temp_follow[m] != '\0'; m++) {
                        add_to_set(result, temp_follow[m]);
                    }
                }
            }
        }
    }
}

// Construct LL(1) Parse Table
void build_table() {
    for (int i = 0; i < 26; i++) {
        for (int j = 0; j < 128; j++) {
            table[i][j][0] = '\0'; // Initialize empty
        }
    }

    for (int i = 0; i < num_prods; i++) {
        char lhs = prods[i][0];
        char rhs_first[MAX_LEN] = "";
        
        // Find FIRST(RHS)
        int j = 2;
        int continue_derivation = 1;
        while (prods[i][j] != '\0' && continue_derivation) {
            char temp_first[MAX_LEN] = "";
            compute_first(prods[i][j], temp_first);
            continue_derivation = 0;
            for (int k = 0; temp_first[k] != '\0'; k++) {
                if (temp_first[k] == '#') {
                    continue_derivation = 1;
                } else {
                    add_to_set(rhs_first, temp_first[k]);
                }
            }
            j++;
        }
        if (continue_derivation) {
            add_to_set(rhs_first, '#');
        }

        // Rule 1: For each terminal 'a' in FIRST(RHS), add A -> RHS to M[A, a]
        for (int k = 0; rhs_first[k] != '\0'; k++) {
            char terminal = rhs_first[k];
            if (terminal != '#') {
                strcpy(table[lhs - 'A'][terminal], prods[i]);
            }
        }

        // Rule 2: If epsilon ('#') is in FIRST(RHS), for each terminal 'b' in FOLLOW(A), add A -> RHS to M[A, b]
        if (strchr(rhs_first, '#') != NULL) {
            char lhs_follow[MAX_LEN];
            strcpy(lhs_follow, follow[lhs - 'A']);
            for (int k = 0; lhs_follow[k] != '\0'; k++) {
                char terminal = lhs_follow[k];
                strcpy(table[lhs - 'A'][terminal], prods[i]);
            }
        }
    }
}

int main() {
    printf("Enter number of productions: ");
    scanf("%d", &num_prods);

    printf("Enter productions (format: E=TR) - use '#' for epsilon:\n");
    for (int i = 0; i < num_prods; i++) {
        scanf("%s", prods[i]);
        
        // Track unique non-terminals
        add_to_set(non_terminals, prods[i][0]);
        
        // Track unique terminals
        for (int j = 2; prods[i][j] != '\0'; j++) {
            add_terminal(prods[i][j]);
        }
    }
    add_to_set(terminals, '$'); // Add EOF marker to terminals

    // 1. Calculate FIRST sets
    for (int i = 0; non_terminals[i] != '\0'; i++) {
        char nt = non_terminals[i];
        compute_first(nt, first[nt - 'A']);
    }

    // 2. Calculate FOLLOW sets
    for (int i = 0; non_terminals[i] != '\0'; i++) {
        char nt = non_terminals[i];
        compute_follow(nt, follow[nt - 'A']);
    }

    // 3. Print FIRST and FOLLOW sets cleanly
    printf("\n=================================================\n");
    printf("%-10s | %-15s | %-15s\n", "NON-TERM", "FIRST SET", "FOLLOW SET");
    printf("=================================================\n");
    for (int i = 0; non_terminals[i] != '\0'; i++) {
        char nt = non_terminals[i];
        printf("    %-6c | { %-11s } | { %-11s }\n", nt, first[nt - 'A'], follow[nt - 'A']);
    }
    printf("=================================================\n");

    // 4. Build and Print LL(1) Parse Table
    build_table();

    printf("\n================================= LL(1) PARSING TABLE =================================\n");
    printf("%-10s |", "NT \\ T");
    for (int i = 0; terminals[i] != '\0'; i++) {
        printf(" %-10c |", terminals[i]);
    }
    printf("\n---------------------------------------------------------------------------------------\n");

    for (int i = 0; non_terminals[i] != '\0'; i++) {
        char nt = non_terminals[i];
        printf("    %-6c |", nt);
        for (int j = 0; terminals[j] != '\0'; j++) {
            char t = terminals[j];
            if (strlen(table[nt - 'A'][t]) > 0) {
                printf(" %-10s |", table[nt - 'A'][t]);
            } else {
                printf(" %-10s |", ""); // Empty cell
            }
        }
        printf("\n");
    }
    printf("=======================================================================================\n");

    return 0;
}