extern short noic, magic, nows, scale;
extern short regex;
extern struct history opt_hist;

void disp_regex(void);
void clr_regex(void);
void start_regex(char *);
int regex_srch(int);
void parsopt(char *);
void anykey(void);
