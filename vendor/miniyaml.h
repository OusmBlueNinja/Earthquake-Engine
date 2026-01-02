#pragma once

// MiniYAML: tiny YAML-ish writer/reader for simple files.
// Supports a small, predictable subset:
// - Indentation with spaces
// - Mappings: "key: value" or "key:"
// - Sequences: "- " items
// - Comments starting with '#'
// This is NOT a full YAML 1.2 parser.

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct myaml_writer_t
{
    FILE *f;
} myaml_writer_t;

void myaml_writer_init(myaml_writer_t *w, FILE *f);

void myaml_write_indent(myaml_writer_t *w, int spaces);
void myaml_write_raw(myaml_writer_t *w, const char *s);
void myaml_write_linef(myaml_writer_t *w, int indent_spaces, const char *fmt, ...);

void myaml_write_key(myaml_writer_t *w, int indent_spaces, const char *key); // writes "<indent>key:"
void myaml_write_key_str(myaml_writer_t *w, int indent_spaces, const char *key, const char *value); // quoted
void myaml_write_key_u32(myaml_writer_t *w, int indent_spaces, const char *key, uint32_t value);
void myaml_write_key_i32(myaml_writer_t *w, int indent_spaces, const char *key, int32_t value);
void myaml_write_key_f32(myaml_writer_t *w, int indent_spaces, const char *key, float value);
void myaml_write_key_bool(myaml_writer_t *w, int indent_spaces, const char *key, int value01);

void myaml_write_quoted(myaml_writer_t *w, const char *s);

typedef struct myaml_reader_t
{
    char *data;
    uint32_t size;
    uint32_t off;
    uint32_t line_no;
} myaml_reader_t;

int myaml_reader_load_file(myaml_reader_t *r, const char *path);
void myaml_reader_free(myaml_reader_t *r);

// Gets next logical line (stripped of trailing \r/\n). Returns 1 if a line is produced.
// - out_line points into internal buffer (valid until next call).
// - out_indent is count of leading spaces.
// - out_is_seq is 1 if line begins with "- " after indent (and out_line points after "- ").
int myaml_next_line(myaml_reader_t *r, const char **out_line, int *out_indent, int *out_is_seq);

// Parses "key: value" on a single line. Returns 1 if key exists.
// - value can be empty string when the line is "key:".
int myaml_split_kv(const char *line, const char **out_key, const char **out_value);

// Helpers (no leading/trailing whitespace; supports optional quotes for strings).
int myaml_parse_u32(const char *s, uint32_t *out);
int myaml_parse_i32(const char *s, int32_t *out);
int myaml_parse_f32(const char *s, float *out);
int myaml_parse_bool01(const char *s, int *out01);
int myaml_unquote_inplace(char *s);

#ifdef __cplusplus
} // extern "C"
#endif

