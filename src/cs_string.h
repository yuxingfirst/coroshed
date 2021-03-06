/*
 * coro_sched - A mini coroutine schedule framework
 * Copyright (C) 2014 xiongj(yuxingfirst@gmail.com).
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

/*this file use the code from twemproxy(https://github.com/twitter/twemproxy), */

#ifndef _CS_STRING_H_
#define _CS_STRING_H_

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

struct string {
    uint32_t len;   /* string length */
    uint8_t  *data; /* string data */
};

#define string(_str)   { sizeof(_str) - 1, (uint8_t *)(_str) }
#define null_string    { 0, NULL }

#define string_set_text(_str, _text) do {       \
    (_str)->len = (uint32_t)(sizeof(_text) - 1);\
    (_str)->data = (uint8_t *)(_text);          \
} while (0);

#define string_set_raw(_str, _raw) do {         \
    (_str)->len = (uint32_t)(cs_strlen(_raw));  \
    (_str)->data = (uint8_t *)(_raw);           \
} while (0);

void string_init(struct string *str);
void string_deinit(struct string *str);
bool string_empty(const struct string *str);
int string_duplicate(struct string *dst, const struct string *src);
int string_copy(struct string *dst, const uint8_t *src, uint32_t srclen);
int string_compare(const struct string *s1, const struct string *s2);

/*
 * Wrapper around common routines for manipulating C character
 * strings
 */
#define cs_memcpy(_d, _c, _n)           \
    memcpy(_d, _c, (size_t)(_n))

#define cs_memmove(_d, _c, _n)          \
    memmove(_d, _c, (size_t)(_n))

#define cs_memchr(_d, _c, _n)           \
    memchr(_d, _c, (size_t)(_n))

#define cs_strlen(_s)                   \
    strlen((char *)(_s))

#define cs_strncmp(_s1, _s2, _n)        \
    strncmp((char *)(_s1), (char *)(_s2), (size_t)(_n))

#define cs_strchr(_p, _l, _c)           \
    _cs_strchr((uint8_t *)(_p), (uint8_t *)(_l), (uint8_t)(_c))

#define cs_strrchr(_p, _s, _c)          \
    _cs_strrchr((uint8_t *)(_p),(uint8_t *)(_s), (uint8_t)(_c))

#define cs_strndup(_s, _n)              \
    (uint8_t *)strndup((char *)(_s), (size_t)(_n));

#define cs_snprintf(_s, _n, ...)        \
    snprintf((char *)(_s), (size_t)(_n), __VA_ARGS__)

#define cs_scnprintf(_s, _n, ...)       \
    _scnprintf((char *)(_s), (size_t)(_n), __VA_ARGS__)

#define cs_vscnprintf(_s, _n, _f, _a)   \
    _vscnprintf((char *)(_s), (size_t)(_n), _f, _a)

static inline uint8_t *
_cs_strchr(uint8_t *p, uint8_t *last, uint8_t c)
{
    while (p < last) {
        if (*p == c) {
            return p;
        }
        p++;
    }

    return NULL;
}

static inline uint8_t *
_cs_strrchr(uint8_t *p, uint8_t *start, uint8_t c)
{
    while (p >= start) {
        if (*p == c) {
            return p;
        }
        p--;
    }

    return NULL;
}

#endif
