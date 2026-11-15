/* Tekken 2's settings file (t2_config.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "t2_config.h"

typedef struct {
    char *key, *value;
} Entry;

struct T2Config {
    char *path;
    Entry *e;
    size_t n, cap;
};

static char *dup_n(const char *s, size_t n)
{
    char *d = malloc(n + 1);

    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

static void trim(const char **s, size_t *n)
{
    while (*n && (**s == ' ' || **s == '\t')) {
        (*s)++, (*n)--;
    }
    while (*n && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' || (*s)[*n - 1] == '\r')) {
        (*n)--;
    }
}

T2Config *t2_config_load(const char *path)
{
    T2Config *c = calloc(1, sizeof(T2Config));
    size_t size = 0;
    char *text, *line, *next;
    int no = 0;

    if (!c || !(c->path = dup_n(path, strlen(path)))) {
        free(c);
        return NULL;
    }
    text = SDL_LoadFile(path, &size);
    if (!text) {
        return c;
    }
    for (line = text; line && *line; line = next) {
        const char *k = line, *v, *eq;
        char *hash, *key, *value;
        size_t kn, vn;

        next = strchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        no++;
        if ((hash = strchr(line, '#')) != NULL) {
            *hash = 0;
        }
        eq = strchr(line, '=');
        kn = eq ? (size_t)(eq - line) : strlen(line);
        trim(&k, &kn);
        if (!eq) {
            if (kn) {
                fprintf(stderr, "t2: %s:%d: not \"key = value\": ignored\n", path, no);
            }
            continue;
        }
        v = eq + 1;
        vn = strlen(v);
        trim(&v, &vn);
        if (!kn) {
            fprintf(stderr, "t2: %s:%d: no key: ignored\n", path, no);
            continue;
        }
        key = dup_n(k, kn);
        value = dup_n(v, vn);
        if (key && value) {
            t2_config_set(c, key, value);
        }
        free(key);
        free(value);
    }
    SDL_free(text);
    return c;
}

void t2_config_free(T2Config *c)
{
    size_t i;

    if (!c) {
        return;
    }
    for (i = 0; i < c->n; i++) {
        free(c->e[i].key);
        free(c->e[i].value);
    }
    free(c->e);
    free(c->path);
    free(c);
}

const char *t2_config_get(const T2Config *c, const char *key)
{
    size_t i;

    for (i = 0; c && i < c->n; i++) {
        if (!strcmp(c->e[i].key, key)) {
            return c->e[i].value;
        }
    }
    return NULL;
}

const char *t2_config_get_or(const T2Config *c, const char *key, const char *fallback)
{
    const char *v = t2_config_get(c, key);

    return v ? v : fallback;
}

int t2_config_get_int(const T2Config *c, const char *key, int fallback)
{
    const char *v = t2_config_get(c, key);
    char *end;
    long n;

    if (!v || !*v) {
        return fallback;
    }
    n = strtol(v, &end, 10);
    return *end ? fallback : (int)n;
}

void t2_config_set(T2Config *c, const char *key, const char *value)
{
    char *v = dup_n(value, strlen(value));
    size_t i;

    if (!v) {
        return;
    }
    for (i = 0; i < c->n; i++) {
        if (!strcmp(c->e[i].key, key)) {
            free(c->e[i].value);
            c->e[i].value = v;
            return;
        }
    }
    if (c->n == c->cap) {
        size_t cap = c->cap ? c->cap * 2 : 32;
        Entry *e = realloc(c->e, cap * sizeof(Entry));

        if (!e) {
            free(v);
            return;
        }
        c->e = e;
        c->cap = cap;
    }
    c->e[c->n].key = dup_n(key, strlen(key));
    c->e[c->n].value = v;
    if (!c->e[c->n].key) {
        free(v);
        return;
    }
    c->n++;
}

void t2_config_set_int(T2Config *c, const char *key, int value)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%d", value);
    t2_config_set(c, key, buf);
}

int t2_config_save(const T2Config *c)
{
    char tmp[1100];
    SDL_IOStream *f;
    size_t i;
    int rc = 0;

    if (snprintf(tmp, sizeof(tmp), "%s.part", c->path) >= (int)sizeof(tmp) ||
        !(f = SDL_IOFromFile(tmp, "wb"))) {
        return -1;
    }
    if (!SDL_IOprintf(f, "# Tekken 2 Recompiled: controls and settings panel choices. "
                         "The game rewrites this file: values are kept, comments are not.\n")) {
        rc = -1;
    }
    for (i = 0; i < c->n; i++) {
        if (!SDL_IOprintf(f, "%s = %s\n", c->e[i].key, c->e[i].value)) {
            rc = -1;
        }
    }
    if (!SDL_CloseIO(f)) {
        rc = -1;
    }
    if (rc == 0 && !SDL_RenamePath(tmp, c->path)) {
        /* Windows will not rename over an existing file on every SDL path */
        SDL_RemovePath(c->path);
        rc = SDL_RenamePath(tmp, c->path) ? 0 : -1;
    }
    return rc;
}

const char *t2_config_path(const T2Config *c)
{
    return c->path;
}
