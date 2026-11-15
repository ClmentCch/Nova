/*
 * NOVA Boot - read-only INI parser.
 *
 * Handles the subset of hekate/emuMMC ini files that NOVA needs:
 *   [section]
 *   key=value
 *   ; comment   # comment
 * CRLF and LF line endings, arbitrary horizontal whitespace.
 *
 * Copyright (c) 2026 NOVA contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "nova_core.h"

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static bool is_sep(char c)
{
    return c == ';' || c == '#';
}

/* Copies a trimmed [start,end) slice, dropping trailing whitespace. */
static void copy_trimmed(char *out, uint32_t out_size, const char *start, uint32_t len)
{
    uint32_t end = len;
    while (end > 0 && is_space(start[end - 1]))
        end--;
    if (end >= out_size)
        end = out_size ? out_size - 1 : 0;
    for (uint32_t i = 0; i < end; i++)
        out[i] = start[i];
    if (out_size)
        out[end] = '\0';
}

bool nova_ini_get(const char *text, uint32_t len,
                  const char *section, const char *key,
                  char *out, uint32_t out_size)
{
    if (!text || !section || !key || !out || !out_size)
        return false;

    out[0] = '\0';

    char current[64] = {0};
    uint32_t pos = 0;
    bool found = false;

    while (pos < len && !found)
    {
        /* Extract one line. */
        uint32_t start = pos;
        while (pos < len && text[pos] != '\n')
            pos++;
        uint32_t end = pos;
        if (pos < len)
            pos++;

        /* Trim leading whitespace. */
        while (start < end && is_space(text[start]))
            start++;
        while (end > start && is_space(text[end - 1]))
            end--;
        if (start >= end || is_sep(text[start]))
            continue;

        if (text[start] == '[')
        {
            uint32_t close = start + 1;
            while (close < end && text[close] != ']')
                close++;
            copy_trimmed(current, sizeof(current), text + start + 1, close - (start + 1));
            continue;
        }

        /* key = value */
        uint32_t eq = start;
        while (eq < end && text[eq] != '=')
            eq++;
        if (eq >= end)
            continue;

        char this_key[48];
        uint32_t key_end = eq;
        while (key_end > start && is_space(text[key_end - 1]))
            key_end--;
        copy_trimmed(this_key, sizeof(this_key), text + start, key_end - start);

        if (nova_strcasecmp(current, section) != 0 || nova_strcasecmp(this_key, key) != 0)
            continue;

        uint32_t vs = eq + 1;
        while (vs < end && is_space(text[vs]))
            vs++;
        /* Strip inline comment. */
        uint32_t ve = vs;
        while (ve < end && !is_sep(text[ve]))
            ve++;
        while (ve > vs && is_space(text[ve - 1]))
            ve--;
        copy_trimmed(out, out_size, text + vs, ve - vs);
        found = true;
    }

    return found;
}

bool nova_ini_has_id(const char *text, uint32_t len, const char *id)
{
    if (!text || !id)
        return false;

    char value[32];
    uint32_t pos = 0;
    while (pos < len)
    {
        uint32_t start = pos;
        while (pos < len && text[pos] != '\n')
            pos++;
        uint32_t end = pos;
        if (pos < len)
            pos++;

        while (start < end && is_space(text[start]))
            start++;
        while (end > start && is_space(text[end - 1]))
            end--;
        if (start >= end || text[start] == '[' || is_sep(text[start]))
            continue;

        uint32_t eq = start;
        while (eq < end && text[eq] != '=')
            eq++;
        if (eq >= end)
            continue;

        char this_key[32];
        uint32_t key_end = eq;
        while (key_end > start && is_space(text[key_end - 1]))
            key_end--;
        copy_trimmed(this_key, sizeof(this_key), text + start, key_end - start);
        if (nova_strcasecmp(this_key, "id") != 0)
            continue;

        uint32_t vs = eq + 1;
        while (vs < end && is_space(text[vs]))
            vs++;
        uint32_t ve = vs;
        while (ve < end && !is_sep(text[ve]))
            ve++;
        copy_trimmed(value, sizeof(value), text + vs, ve - vs);
        if (nova_strcasecmp(value, id) == 0)
            return true;
    }
    return false;
}
