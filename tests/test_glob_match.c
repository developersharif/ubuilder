#include "../src/core/glob_match.h"

#include <stdio.h>
#include <string.h>

extern int test_count;
extern int test_passed;

#define EXPECT(name, cond) do { \
    test_count++; \
    if (cond) { test_passed++; printf("✓ %s\n", name); } \
    else      { printf("✗ %s\n", name); } \
} while (0)

static void test_literal(void) {
    EXPECT("literal: exact match",      ub_glob_match("foo.txt",    "foo.txt", 0) == 1);
    EXPECT("literal: leading-slash anchored", ub_glob_match("/foo.txt", "foo.txt", 0) == 1);
    EXPECT("literal: anchored does not float", ub_glob_match("/foo.txt", "sub/foo.txt", 0) == 0);
    EXPECT("literal: floating matches at depth", ub_glob_match("foo.txt", "sub/foo.txt", 0) == 1);
    EXPECT("literal: mismatch",         ub_glob_match("foo.txt",    "bar.txt", 0) == 0);
}

static void test_question(void) {
    EXPECT("?: single char",            ub_glob_match("f?o.txt",    "foo.txt", 0) == 1);
    EXPECT("?: does not match /",       ub_glob_match("a?b",        "a/b",     0) == 0);
}

static void test_star(void) {
    EXPECT("*: matches segment",        ub_glob_match("*.md",       "README.md", 0) == 1);
    EXPECT("*: does NOT cross /",       ub_glob_match("*.md",       "docs/x.md", 0) == 1); /* floating! */
    EXPECT("*: anchored, no cross /",   ub_glob_match("/*.md",      "docs/x.md", 0) == 0);
    EXPECT("*: prefix",                 ub_glob_match("/src/*.c",   "src/main.c", 0) == 1);
    EXPECT("*: middle",                 ub_glob_match("/a/*/c.txt", "a/b/c.txt", 0) == 1);
    EXPECT("*: too deep for one *",     ub_glob_match("/a/*/c.txt", "a/b/d/c.txt", 0) == 0);
}

static void test_double_star(void) {
    EXPECT("**: cross /",               ub_glob_match("tests/**",     "tests/sub/dir/file.txt", 0) == 1);
    EXPECT("**: cross / on directory",  ub_glob_match("tests/**",     "tests/sub",              1) == 1);
    EXPECT("**/X: floating",            ub_glob_match("**/*.md",      "docs/sub/README.md",     0) == 1);
    EXPECT("**: anchored",              ub_glob_match("/a/**/b.txt",  "a/x/y/z/b.txt",          0) == 1);
    EXPECT("**: no match wrong base",   ub_glob_match("/a/**/b.txt",  "c/x/b.txt",              0) == 0);
}

static void test_bracket(void) {
    EXPECT("[abc]: match a",            ub_glob_match("f[abc]o",   "fao", 0) == 1);
    EXPECT("[abc]: match b",            ub_glob_match("f[abc]o",   "fbo", 0) == 1);
    EXPECT("[abc]: no match d",         ub_glob_match("f[abc]o",   "fdo", 0) == 0);
    EXPECT("[a-c]: range match",        ub_glob_match("f[a-c]o",   "fbo", 0) == 1);
    EXPECT("[!abc]: negation match",    ub_glob_match("f[!abc]o",  "fdo", 0) == 1);
    EXPECT("[!abc]: negation no-match", ub_glob_match("f[!abc]o",  "fao", 0) == 0);
}

static void test_dir_only(void) {
    EXPECT("dir/: dir-only matches dir",  ub_glob_match("docs/",   "docs",     1) == 1);
    EXPECT("dir/: dir-only NOT on file",  ub_glob_match("docs/",   "docs",     0) == 0);
    EXPECT("dir/: nested dir",            ub_glob_match("**/docs/", "src/docs", 1) == 1);
}

static void test_path_excluded(void) {
    const char* pats[] = { "tests/**", "*.md", "/build/" };
    EXPECT("excluded: tests/x.txt",   ub_path_excluded(pats, 3, "tests/x.txt", 0) == 1);
    EXPECT("excluded: README.md",     ub_path_excluded(pats, 3, "README.md",   0) == 1);
    EXPECT("excluded: build/ dir",    ub_path_excluded(pats, 3, "build",       1) == 1);
    EXPECT("excluded: build/ as file",ub_path_excluded(pats, 3, "build",       0) == 0);
    EXPECT("excluded: src/main.c",    ub_path_excluded(pats, 3, "src/main.c",  0) == 0);
    EXPECT("excluded: empty list",    ub_path_excluded(NULL, 0, "anything",    0) == 0);
}

static void test_ext_excluded(void) {
    const char* pats[] = { "ext-curl", "gd", "ext-*-fake" };
    EXPECT("ext: ext-curl form",      ub_ext_excluded(pats, 3, "curl") == 1);
    EXPECT("ext: bare form",          ub_ext_excluded(pats, 3, "gd") == 1);
    EXPECT("ext: glob form",          ub_ext_excluded(pats, 3, "something-fake") == 1);
    EXPECT("ext: not in list",        ub_ext_excluded(pats, 3, "mbstring") == 0);
    EXPECT("ext: empty list",         ub_ext_excluded(NULL, 0, "curl") == 0);
}

static void test_backslash_normalize(void) {
    EXPECT("normalize: windows path", ub_glob_match("docs/*.md", "docs\\readme.md", 0) == 1);
}

void test_glob_match(void) {
    test_literal();
    test_question();
    test_star();
    test_double_star();
    test_bracket();
    test_dir_only();
    test_path_excluded();
    test_ext_excluded();
    test_backslash_normalize();
}
