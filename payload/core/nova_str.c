/*
 * NOVA Boot - freestanding C helpers.
 *
 * The payload is linked with -nostdlib, so it must not depend on libc.
 * These are deliberately small, simple implementations.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "nova_core.h"

void *nova_memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

void *nova_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

int nova_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++)
    {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

size_t nova_strlen(const char *s)
{
    size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

int nova_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int nova_strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i])
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}

static int nova_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

int nova_strcasecmp(const char *a, const char *b)
{
    while (*a && nova_tolower((unsigned char)*a) == nova_tolower((unsigned char)*b))
    {
        a++;
        b++;
    }
    return nova_tolower((unsigned char)*a) - nova_tolower((unsigned char)*b);
}

char *nova_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *nova_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

char *nova_strcat(char *dst, const char *src)
{
    char *d = dst + nova_strlen(dst);
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

void nova_cstr_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst_size)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_size && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

int nova_atoi(const char *s)
{
    int sign = 1, value = 0;
    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-')
    {
        sign = -1;
        s++;
    }
    else if (*s == '+')
    {
        s++;
    }
    while (*s >= '0' && *s <= '9')
        value = value * 10 + (*s++ - '0');
    return value * sign;
}

uint32_t nova_strtoul_hex(const char *s)
{
    uint32_t value = 0;
    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    for (;;)
    {
        char c = *s;
        uint32_t digit;
        if (c >= '0' && c <= '9')
            digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (uint32_t)(c - 'A' + 10);
        else
            break;
        value = (value << 4) | digit;
        s++;
    }
    return value;
}

bool nova_wildcard(const char *pattern, const char *name)
{
    const char *star = NULL;
    const char *mark = NULL;

    while (*name)
    {
        if (*pattern == '?' ||
            (*pattern && nova_tolower((unsigned char)*pattern) ==
                             nova_tolower((unsigned char)*name)))
        {
            pattern++;
            name++;
        }
        else if (*pattern == '*')
        {
            star = pattern++;
            mark = name;
        }
        else if (star)
        {
            pattern = star + 1;
            name = ++mark;
        }
        else
        {
            return false;
        }
    }
    while (*pattern == '*')
        pattern++;
    return *pattern == '\0';
}

const char *nova_state_label(nova_state_t state)
{
    switch (state)
    {
    case NOVA_ST_OK:
        return "READY";
    case NOVA_ST_WARN:
        return "PARTIAL";
    case NOVA_ST_ERROR:
        return "INVALID";
    case NOVA_ST_MISSING:
    default:
        return "MISSING";
    }
}
