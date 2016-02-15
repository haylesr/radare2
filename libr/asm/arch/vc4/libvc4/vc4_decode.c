#ifndef _GNU_SOURCE
#define _GNU_SOURCE         /* See feature_test_macros(7) */
#endif
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <inttypes.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include "eval.h"
#include "vc4.h"


void vc4_build_values(struct vc4_val *vals, const struct vc4_opcode *op,
		      const uint8_t *b, uint32_t len)
{
	memset(vals, 0, sizeof(struct vc4_val) * 256);

	uint16_t mask;
	uint16_t i, w;
	uint32_t wi;
	mask = 0;
	w = wi = 0;
	const char *c = op->string;
	int ci;

	while (*c) {
		ci = (int)(unsigned char)*c++;

		if (mask == 0) {
			mask = 0x8000;
			if (len >= (wi + 2)) {
				w = vc4_get_le16(b + wi);
			} else {
				w = 0;
			}
			wi += 2;
		}

		vals[ci].value <<= 1;
		if ((w & mask) != 0)
			vals[ci].value |= 1;
		vals[ci].length++;

		mask >>= 1;
	}

	for (i=0; i<256; i++) {
		if (vals[i].length == 32)
			vals[i].value =
			  ((vals[i].value >> 16) & 0xffff) |
			  ((vals[i].value & 0xffff) << 16);
	}
}

static char *vc4_expand_expr(const char *p, const struct vc4_info *info,
			     const struct vc4_val *vals)
{
	char *exp2;
	char *np;
	int r;

	exp2 = strdup("");

	for (; *p != 0; p++) {
		np = NULL;
		if ((*p >= 'a' && *p <= 'z') || *p == '$') {

			const struct vc4_val *val = vals + (int)*p;

			if (strchr(info->signed_ops, *p)) {
				int32_t t = (int32_t)val->value;
				
				if (t & (1 << (val->length - 1)))
					t -= 1 << val->length;

				r = asprintf(&np, "%s%d", exp2, t);
			} else {
				r = asprintf(&np, "%s%u", exp2, val->value);
			}
		} else {
			r = asprintf(&np, "%s%c", exp2, *p);
		}
		assert(r > 0);

		if (np != NULL) {
			free(exp2);
			exp2 = np;
		}
	}

	return exp2;
}

static int64_t vc4_eval_expr(const char *exp, const struct vc4_info *info,
			     const struct vc4_val *vals)
{
	char *exp2;
	int64_t ev;

	exp2 = vc4_expand_expr(exp, info, vals);

	ev = eval(exp2);

	free(exp2);

	return ev;
}

char *vc4_display(const struct vc4_info *info, const struct vc4_opcode *op,
		  uint32_t addr, const uint8_t *b, uint32_t len)
{
	struct vc4_val vals[256];

	vc4_build_values(vals, op, b, len);

	vals['$'].value = addr;
	vals['$'].length = 32;

	char *fmt;
	char *exp;

	const char *c = op->format;
	const char *q;

	int l0, r;

	char *d;
	char *md = NULL;

	d = strdup("");

	while ((q = strchr(c, '%')) != NULL) {

		vc4_strncat(&d, c, q - c);
		c += q - c;

		assert(c[0] == '%');

		r = sscanf(c, "%m[^{]{%m[^}]}%n", &fmt, &exp, &l0);

		if (r < 2 || fmt == NULL || exp == NULL) {
			fprintf(stderr, "bad line  %s/%s/%s %d %d\n", fmt, exp, c+l0, l0, r);
			abort();
		}

		c += l0;

		int64_t ev = vc4_eval_expr(exp, info, vals);

		if (strcmp(fmt, "%s") == 0) {
			assert(strlen(exp) == 1);
			struct vc4_decode_table *t = info->tables;
			while (t != NULL && t->code != exp[0]) {
				t = t->next;
			}
			assert(t != NULL);
			assert(ev >= 0);
			assert(ev < t->count);
			r = asprintf(&md, fmt, t->tab[ev]);
		} else {
			r = asprintf(&md, fmt, (uint32_t)ev);
		}
		assert(r >= 0);

		if (md != NULL) {
			vc4_strcat(&d, md);
			free(md);
		}

		free(fmt);
		free(exp);
	}

	vc4_strcat(&d, c);

	return d;
}

const struct vc4_opcode *vc4_get_opcode(const struct vc4_info *info, const uint8_t *b, size_t l)
{
	uint16_t b0;
	uint16_t b1;
	const struct vc4_opcode_tab *t;
	size_t i;
	int deb = 0;

	if (l < 2) {
		fprintf(stderr, "overrun 1!\n");
		b0 = 0;
	} else {
		b0 = vc4_get_le16(b);
	}

	b0 = vc4_get_le16(b);

	if (b0 == 0xa013)
		deb = 1;

	t = info->opcodes[b0];
	if (t == NULL)
		return NULL;

	if (deb) {
/*
		printf("\n");
		for (i=0; i<t->count; i++) {
			printf("D %04x %04x   %04x %04x  %s\n",
			       t->tab[i]->ins_mask[0], t->tab[i]->ins[0],
			       t->tab[i]->ins_mask[1], t->tab[i]->ins[1],
			       t->tab[i]->format);
		}
*/
	}

	if (t->count == 1)
		return t->tab[0];
/*
	for (i=0; i<t->count; i++) {
		if ((b0 & t->tab[i]->ins_mask[0]) == t->tab[i]->ins[0] &&
		    t->tab[i]->ins_mask[1] == 0)
			return t->tab[i];
	}
*/
	if (l < 4) {
/*
		fprintf(stderr, "overrun 2 %04x!\n", b0);
		for (i=0; i<t->count; i++) {
			fprintf(stderr, "> %04x %04x %s!\n",
				t->tab[i]->ins_mask[0],
				t->tab[i]->ins[0],
				t->tab[i]->format);
		}
*/
		b1 = 0;
	} else {
		b1 = vc4_get_le16(b + 2);
	}

	for (i = 0; i < t->count; i++) {
		if (((b0 & t->tab[i]->ins_mask[0]) == t->tab[i]->ins[0]) &&
		    ((t->tab[i]->ins_mask[1] == 0) ||
		     (b1 & t->tab[i]->ins_mask[1]) == t->tab[i]->ins[1]))
			return t->tab[i];
	}

	return NULL;
}
