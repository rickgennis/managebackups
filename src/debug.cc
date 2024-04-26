#ifndef DEBUG_C
#define DEBUG_C

/* Borrowed from:
 * Exim - an Internet mail transport agent    
 * Copyright (c) University of Cambridge 1995 - 2018 
 * Copyright (c) The Exim Maintainers 2015 - 2021 
 */

#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

bit_table debug_options[]      = { /* must be in alphabetical order and use
                                 only the enum values from debug.h */
  BIT_TABLE(D, all),
  BIT_TABLE(D, backup),
  BIT_TABLE(D, cache),
  BIT_TABLE(D, config),
  BIT_TABLE(D, exec),
  BIT_TABLE(D, faub),
  BIT_TABLE(D, link),
  BIT_TABLE(D, netproto),
  BIT_TABLE(D, notify),
  BIT_TABLE(D, prune),
  BIT_TABLE(D, recalc),
  BIT_TABLE(D, scan),
  BIT_TABLE(D, transfer),
  BIT_TABLE(D, tripwire),
};

int debug_notall[]         = {
  -1
};

int ndebug_options = nelem(debug_options);


void bits_clear(unsigned int *selector, size_t selsize, int *bits) {
    for(; *bits != -1; ++bits)
        BIT_CLEAR(selector, selsize, *bits);
}


void bits_set(unsigned int *selector, size_t selsize, int *bits) {
    for(; *bits != -1; ++bits)
        BIT_SET(selector, selsize, *bits);
}


static inline uschar skip_whitespace(const uschar ** sp) { 
    while (isspace(**sp)) (*sp)++; return **sp; 
}


void decode_bits(unsigned int *selector, size_t selsize, int *notall,
  uschar *parsestring, bit_table *options, int count) {
    
    if (!parsestring)
        return;

    if (*parsestring == '=') {
        char *end;    /* Not uschar */
        memset(selector, 0, sizeof(*selector)*selsize);
        *selector = (int)strtoul(CS parsestring+1, &end, 0);

        if (!*end)
            return;

        printf("unknown debugging selection: %s\n", parsestring);
        return;
    }

    /* Handle symbolic setting */

    else while (1) {
        bool adding;
        uschar *s;
        int len;
        bit_table *start, *end;

        Uskip_whitespace(&parsestring);

        if (!*parsestring)
            return;

        if (*parsestring != '+' && *parsestring != '-') {
            printf("unknown debugging flag (should be + or -: %s\n", parsestring);
            return;
        }

        adding = *parsestring++ == '+';
        s = parsestring;

        while (isalnum(*parsestring) || *parsestring == '_') 
            parsestring++;
        len = parsestring - s;

        start = options;
        end = options + count;

        while (start < end) {
            bit_table *middle = start + (end - start)/2;
            int c = Ustrncmp(s, middle->name, len);
            if (c == 0) {
                if (middle->name[len] != 0) {
                    c = -1; 
                }
                else {
                    unsigned int bit = middle->bit;

                    if (bit == -1) {
                        if (adding) {
                            memset(selector, -1, sizeof(*selector)*selsize);
                            bits_clear(selector, selsize, notall);
                        }
                        else
                            memset(selector, 0, sizeof(*selector)*selsize);
                    }
                    else if (adding)
                        BIT_SET(selector, selsize, bit);
                    else
                        BIT_CLEAR(selector, selsize, bit);

                    break;  /* Out of loop to match selector name */
                }
            }

            if (c < 0) 
                end = middle; 
            else 
                start = middle + 1;
        }  /* Loop to match selector name */

        if (start >= end) {
            printf("unknown debugging selection: %c%s\n", adding ? '+' : '-', s);
            return;
        }
    }    /* Loop for selector names */
}

#endif

