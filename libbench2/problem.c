/*
 * Copyright (c) 2001 Matteo Frigo
 * Copyright (c) 2001 Steven G. Johnson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* $Id: problem.c,v 1.17 2003-02-09 00:35:56 stevenj Exp $ */

#include "config.h"
#include "bench.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

typedef enum {
     SAME, PADDED, HALFISH
} n_transform;

/* funny transformations for last dimension of PROBLEM_REAL */
static int transform_n(int n, n_transform nt)
{
     switch (nt) {
	 case SAME: return n;
	 case PADDED: return 2*(n/2+1);
	 case HALFISH: return (n/2+1);
	 default: BENCH_ASSERT(0); return 0;
     }
}

/* do what I mean */
static bench_tensor *dwim(bench_tensor *t, bench_iodim *last_iodim,
			  n_transform nti, n_transform nto)
{
     int i;
     bench_iodim *d, *d1, *dL;

     if (!FINITE_RNK(t->rnk) || t->rnk < 1)
	  return t;

     i = t->rnk;
     d1 = last_iodim;
     dL = t->dims + i-1;

     while (--i >= 0) {
	  d = t->dims + i;
	  if (!d->is) 
	       d->is = d1->is * transform_n(d1->n, d1==dL ? nti : SAME); 
	  if (!d->os) 
	       d->os = d1->os * transform_n(d1->n, d1==dL ? nto : SAME); 
	  d1 = d;
     }

     *last_iodim = *d1;
     return t;
}

static const char *parseint(const char *s, int *n)
{
     int sign = 1;

     *n = 0;

     if (*s == '-') { 
	  sign = -1;
	  ++s;
     } else if (*s == '+') { 
	  sign = +1; 
	  ++s; 
     }

     BENCH_ASSERT(isdigit(*s));
     while (isdigit(*s)) {
	  *n = *n * 10 + (*s - '0');
	  ++s;
     }
     
     *n *= sign;
     return s;
}

struct dimlist { bench_iodim car; struct dimlist *cdr; };

static const char *parsetensor(const char *s, bench_tensor **tp)
{
     struct dimlist *l = 0, *m;
     bench_tensor *t;
     int rnk = 0;

 L1:
     m = (struct dimlist *)bench_malloc(sizeof(struct dimlist));
     /* nconc onto l */
     m->cdr = l; l = m;
     ++rnk; 

     s = parseint(s, &m->car.n);

     if (*s == ':') {
	  /* read input stride */
	  ++s;
	  s = parseint(s, &m->car.is);
	  if (*s == ':') {
	       /* read output stride */
	       ++s;
	       s = parseint(s, &m->car.os);
	  } else {
	       /* default */
	       m->car.os = m->car.is;
	  }
     } else {
	  m->car.is = 0;
	  m->car.os = 0;
     }

     if (*s == 'x' || *s == 'X') {
	  ++s;
	  goto L1;
     }
     
     /* now we have a dimlist.  Build bench_tensor */
     t = mktensor(rnk);
     while (--rnk >= 0) {
	  bench_iodim *d = t->dims + rnk;
	  BENCH_ASSERT(l);
	  m = l; l = m->cdr;
	  d->n = m->car.n;
	  d->is = m->car.is;
	  d->os = m->car.os;
	  bench_free(m);
     }

     *tp = t;
     return s;
}

/* parse a problem description, return a problem */
bench_problem *problem_parse(const char *s)
{
     bench_problem *p;
     bench_iodim last_iodim = {1,1,1};
     bench_tensor *sz;
     n_transform nti = SAME, nto = SAME;

     p = bench_malloc(sizeof(bench_problem));

     p->kind = PROBLEM_COMPLEX;
     p->sign = -1;
     p->in = p->out = 0;
     p->inphys = p->outphys = 0;
     p->in_place = 0;
     p->destroy_input = 0;
     p->split = 0;
     p->userinfo = 0;
     p->sz = p->vecsz = 0;
     p->ini = p->outi = 0;

 L1:
     switch (tolower(*s)) {
	 case 'i': p->in_place = 1; ++s; goto L1;
	 case 'o': p->in_place = 0; ++s; goto L1;
	 case 'd': p->destroy_input = 1; ++s; goto L1;
	 case '/': p->split = 1; ++s; goto L1;
	 case 'f': 
	 case '-': p->sign = -1; ++s; goto L1;
	 case 'b': 
	 case '+': p->sign = 1; ++s; goto L1;
	 case 'r': p->kind = PROBLEM_REAL; ++s; goto L1;
	 case 'c': p->kind = PROBLEM_COMPLEX; ++s; goto L1;
	 default : ;
     }

     s = parsetensor(s, &sz);

     if (p->kind == PROBLEM_REAL) {
	  if (p->sign < 0) {
	       nti = p->in_place ? PADDED : SAME;
	       nto = HALFISH;
	  }
	  else {
	       nti = HALFISH;
	       nto = p->in_place ? PADDED : SAME;
	  }
     }

     if (*s == '*') { /* "external" vector */
	  ++s;
	  p->sz = dwim(sz, &last_iodim, nti, nto);
	  s = parsetensor(s, &sz);
	  p->vecsz = dwim(sz, &last_iodim, SAME, SAME);
     } else if (*s == 'v' || *s == 'V') { /* "internal" vector */
	  bench_tensor *vecsz;
	  ++s;
	  s = parsetensor(s, &vecsz);
	  p->vecsz = dwim(vecsz, &last_iodim, SAME, SAME);
	  p->sz = dwim(sz, &last_iodim, nti, nto);
     } else {
	  p->sz = dwim(sz, &last_iodim, nti, nto);
	  p->vecsz = mktensor(0);
     }

     if (!p->in_place)
	  p->out = ((bench_real *) p->in) + (1 << 20);  /* whatever */

     BENCH_ASSERT(p->sz && p->vecsz);
     BENCH_ASSERT(!*s);
     return p;
}

void problem_destroy(bench_problem *p)
{
     BENCH_ASSERT(p);
     problem_free(p);
     bench_free(p);
}

