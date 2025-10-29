#ifndef TEST_H
#define TEST_H

#include <stdlib.h>

#define LOG_SIZE_LINES (2500000)

#define COLOR_SEED (5564)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))
#endif


extern double simulation_factor;
extern int badblock;

void draw_update(int wait, int umap);


struct console
{
    int pos;
    char *line[LOG_SIZE_LINES];
    int disp;
};

extern struct console conlog;

#define CONSOLE(c,f,a...) do { int _l=snprintf(NULL,0,f,a); if((c)->line[(c)->pos]!=NULL) free((c)->line[(c)->pos]); (c)->line[(c)->pos]=malloc(_l+1); sprintf((c)->line[(c)->pos],f,a); if(++(c)->pos>=ARRAY_SIZE((c)->line)) { (c)->pos=0; } (c)->disp=(c)->pos; } while(0)

#endif
