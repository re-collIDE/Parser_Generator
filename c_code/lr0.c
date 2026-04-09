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

// Function prototypes
int add_item(State *s, char *it);
void closure(State *s);
void goto_state(State *src, State *dest, char sym);
int exists(State *s);
void print_grammar();
void print_augmented(char start);
void print_final_augmented(char start);
void print_states();

int add_item(State *s, char *it) {
    for(int i=0;i<s->count;i++)
        if(strcmp(s->item[i], it)==0) return 0;
    strcpy(s->item[s->count++], it);
    return 1;
}

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

void print_grammar() {
    printf("\nGiven Grammar:\n");
    for(int i=0;i<n;i++)
        printf("%s\n",prod[i]);
}

void print_augmented(char start) {
    printf("\nAugmented Grammar:\n");
    printf("Z->%c\n",start);
    for(int i=0;i<n;i++)
        printf("%s\n",prod[i]);
}

void print_final_augmented(char start) {
    printf("\nFinal Augmented Grammar:\n");
    printf("Z->.%c\n",start);
    for(int i=0;i<n;i++)
        printf("%c->.%s\n",prod[i][0],prod[i]+3);
}

void print_states() {
    printf("\nCanonical Collection:\n");
    for(int i=0;i<state_count;i++) {
        printf("\nI%d:\n",i);
        for(int j=0;j<states[i].count;j++)
            printf("%s\n",states[i].item[j]);
    }
}

int main() {
    printf("Enter number of productions: ");
    scanf("%d",&n);

    printf("Enter productions (e.g., S->AA):\n");
    for(int i=0;i<n;i++)
        scanf("%s",prod[i]);

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

    // Build canonical collection
    for(int i=0;i<state_count;i++) {
        for(char c='A'; c<='z'; c++) {
            if(!isalnum(c)) continue;
            State new_state;
            goto_state(&states[i], &new_state, c);
            if(new_state.count==0) continue;
            int id = exists(&new_state);
            if(id==-1)
                states[state_count++] = new_state;
        }
    }

    print_states();

    return 0;
}
