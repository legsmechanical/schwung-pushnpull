#ifndef SHAPER_H
#define SHAPER_H
typedef struct { int curve; double slope; double shift; double attack; } shaper_t;
void   shaper_init(shaper_t *s);
double shaper_eval(const shaper_t *s, double phase);
#endif
