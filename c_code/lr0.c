#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX 20

char prod[MAX][MAX];
int n;

typedef struct {
    char item[MAX][MAX];
    int count;
} State;

State states[MAX];
int state_count = 0;

char ACTION[MAX][MAX][10];
int GOTO[MAX][MAX];

char terminals[MAX], nonterminals[MAX];
int tcount = 0, ntcount = 0;

// Add item
int add_item(State *s, char *it) {
    for(int i=0;i<s->count;i++)
        if(strcmp(s->item[i], it)==0) return 0;
    strcpy(s->item[s->count++], it);
    return 1;
}

// Closure
void closure(State *s) {
    int change = 1;
    while(change) {
        change = 0;
        for(int i=0;i<s->count;i++) {
            char *dot = strchr(s->item[i], '.');
            if(dot && isupper(*(dot+1))) {
                char nt = *(dot+1);
                for(int j=0;j<n;j++) {
                    if(prod[j][0]==nt) {
                        char temp[MAX];
                        sprintf(temp,"%c->.%s",prod[j][0],prod[j]+3);
                        if(add_item(s,temp)) change=1;
                    }
                }
            }
        }
    }
}

// GOTO
void goto_state(State *src, State *dest, char sym) {
    dest->count=0;
    for(int i=0;i<src->count;i++) {
        char *dot = strchr(src->item[i], '.');
        if(dot && *(dot+1)==sym) {
            char temp[MAX];
            strcpy(temp,src->item[i]);

            int pos = dot - src->item[i];
            temp[pos] = sym;
            temp[pos+1] = '.';

            add_item(dest,temp);
        }
    }
    closure(dest);
}

// Check existing state
int exists(State *s) {
    for(int i=0;i<state_count;i++) {
        if(states[i].count != s->count) continue;

        int match=1;
        for(int j=0;j<s->count;j++) {
            int found=0;
            for(int k=0;k<states[i].count;k++) {
                if(strcmp(s->item[j], states[i].item[k])==0)
                    found=1;
            }
            if(!found) match=0;
        }
        if(match) return i;
    }
    return -1;
}

// Find terminals & non-terminals
void find_symbols() {
    for(int i=0;i<n;i++) {
        char lhs = prod[i][0];

        if(!strchr(nonterminals, lhs))
            nonterminals[ntcount++] = lhs;

        for(int j=3;j<strlen(prod[i]);j++) {
            char c = prod[i][j];

            if(isupper(c)) {
                if(!strchr(nonterminals, c))
                    nonterminals[ntcount++] = c;
            }
            else {
                if(!strchr(terminals, c))
                    terminals[tcount++] = c;
            }
        }
    }
    terminals[tcount++] = '$';
}

// Build parsing table
void build_table() {
    for(int i=0;i<state_count;i++) {
        for(int j=0;j<states[i].count;j++) {

            char *item = states[i].item[j];
            char *dot = strchr(item,'.');

            // ACCEPT
            if(strcmp(item,"Z->S.")==0) {
                strcpy(ACTION[i][tcount-1],"ACC");
            }

            // REDUCE
            else if(dot && *(dot+1)=='\0') {
                char lhs = item[0];
                char rhs[10];
                strcpy(rhs, item+3);
                rhs[strlen(rhs)-1] = '\0';

                int prod_no = -1;

                for(int k=0;k<n;k++) {
                    if(prod[k][0]==lhs &&
                       strcmp(prod[k]+3, rhs)==0)
                        prod_no = k+1;
                }

                for(int t=0;t<tcount;t++) {
                    sprintf(ACTION[i][t],"R%d",prod_no);
                }
            }

            // SHIFT
            else if(dot && islower(*(dot+1))) {
                char sym = *(dot+1);

                State temp;
                goto_state(&states[i], &temp, sym);

                int jstate = exists(&temp);

                int col = strchr(terminals,sym)-terminals;
                sprintf(ACTION[i][col],"S%d",jstate);
            }

            // GOTO
            else if(dot && isupper(*(dot+1))) {
                char sym = *(dot+1);

                State temp;
                goto_state(&states[i], &temp, sym);

                int jstate = exists(&temp);

                int col = strchr(nonterminals,sym)-nonterminals;
                GOTO[i][col] = jstate;
            }
        }
    }
}

// Print grammar
void print_grammar() {
    printf("\nGiven Grammar:\n");
    for(int i=0;i<n;i++)
        printf("%s\n",prod[i]);
}

// Print augmented
void print_augmented(char start) {
    printf("\nAugmented Grammar:\n");
    printf("Z->%c\n",start);
    for(int i=0;i<n;i++)
        printf("%s\n",prod[i]);
}

// Print final augmented
void print_final_augmented(char start) {
    printf("\nFinal Augmented Grammar:\n");
    printf("Z->.%c\n",start);
    for(int i=0;i<n;i++)
        printf("%c->.%s\n",prod[i][0],prod[i]+3);
}

// Print states
void print_states() {
    printf("\nCanonical Collection:\n");
    for(int i=0;i<state_count;i++) {
        printf("\nI%d:\n",i);
        for(int j=0;j<states[i].count;j++)
            printf("%s\n",states[i].item[j]);
    }
}

// Print table (FIXED)
void print_table() {
    printf("\n\nLR(0) Parsing Table:\n");

    printf("\n%-6s", "State");

    for(int i=0;i<tcount;i++)
        printf("%-6c", terminals[i]);

    for(int i=0;i<ntcount;i++)
        printf("%-6c", nonterminals[i]);

    printf("\n");

    for(int i=0;i<state_count;i++) {
        printf("I%-5d", i);

        for(int j=0;j<tcount;j++) {
            if(strlen(ACTION[i][j]) == 0)
                printf("%-6s", "-");
            else
                printf("%-6s", ACTION[i][j]);
        }

        for(int j=0;j<ntcount;j++) {
            if(GOTO[i][j] == -1)
                printf("%-6s", "-");
            else
                printf("%-6d", GOTO[i][j]);
        }

        printf("\n");
    }
}

int main() {
    printf("Enter number of productions: ");
    scanf("%d",&n);

    printf("Enter productions (e.g., S->AA):\n");
    for(int i=0;i<n;i++)
        scanf("%s",prod[i]);

    // Initialize tables
    for(int i=0;i<MAX;i++)
        for(int j=0;j<MAX;j++) {
            strcpy(ACTION[i][j], "");
            GOTO[i][j] = -1;
        }

    char start = prod[0][0];

    print_grammar();
    print_augmented(start);
    print_final_augmented(start);

    // Initial state
    states[0].count=0;

    char temp[MAX];
    sprintf(temp,"Z->.%c",start);
    add_item(&states[0],temp);

    for(int i=0;i<n;i++) {
        sprintf(temp,"%c->.%s",prod[i][0],prod[i]+3);
        add_item(&states[0],temp);
    }

    closure(&states[0]);
    state_count=1;

    // Build states
    for(int i=0;i<state_count;i++) {
        for(char c='A'; c<='z'; c++) {
            if(!isalnum(c)) continue;

            State new_state;
            goto_state(&states[i],&new_state,c);

            if(new_state.count==0) continue;

            int id = exists(&new_state);

            if(id==-1)
                states[state_count++] = new_state;
        }
    }

    print_states();

    find_symbols();
    build_table();
    print_table();

    return 0;
}