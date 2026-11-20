/* bsh: the BShitOS shell. */
#ifndef BSH_H
#define BSH_H

#include "bs.h"

#define LINE_MAX 512
#define WORDS_MAX 32
#define NAMES_MAX 160

/* SGR colours, so every part of bsh agrees on what means what */
#define C_RESET   "\033[0m"
#define C_DIM     "\033[90m"
#define C_ERROR   "\033[91m"
#define C_PROGRAM "\033[92m"
#define C_STRING  "\033[93m"
#define C_DIR     "\033[94m"
#define C_BUILTIN "\033[95m"
#define C_VAR     "\033[96m"
#define C_BOLD    "\033[1m"

/* util.c */
void put(const char *s);
void put_n(const char *s, unsigned int n);
void put_padded(const char *s, unsigned int width);
void move_bytes(void *dst, const void *src, unsigned int n); /* overlap-safe */
void copy_string(char *dst, const char *src, unsigned int size);
int append(char *dst, unsigned int size, const char *src); /* -1 if it did not fit */
int starts_with(const char *s, const char *prefix);
int ends_with(const char *s, const char *suffix);
int contains(const char *haystack, const char *needle);
void format_uint(unsigned int value, char *out);
int is_name_char(char c, int first);
unsigned int uptime(void);
int bsh_error(const char *what, const char *message); /* prints "bsh: what: message", returns 1 */

/* vars.c: shell variables and aliases */
const char *var_get(const char *name); /* 0 when unset */
int var_set(const char *name, const char *value);
void var_unset(const char *name);
void var_list(void);
int valid_name(const char *s, unsigned int length);
const char *alias_get(const char *name);
int alias_set(const char *name, const char *value);
int alias_unset(const char *name);
void alias_list(void);
const char *alias_name(int i); /* 0 past the end */

/* run.c: parsing and running command lines */
extern int last_status;
int run_line(const char *line);
int run_script(const char *path, int argc, char **argv);
int run_script_exited(void); /* whether the last script ended with exit */
int find_program(const char *name, char *path, unsigned int size); /* 0 if found */
void set_positional(int argc, char **argv);
int exit_requested(void);
void request_exit(int status);

/* commands.c: what can be typed as a command, for colouring and completion */
enum command_kind { KIND_NONE, KIND_BUILTIN, KIND_ALIAS, KIND_PROGRAM };
void commands_refresh(void);
int command_count(void);
const char *command_name(int i);
enum command_kind command_kind(const char *word);
const char *closest_command(const char *word); /* a likely typo target, or 0 */

/* builtins.c */
typedef int (*builtin_fn)(int argc, char **argv);
struct builtin {
    const char *name, *usage, *about;
    builtin_fn run;
};
extern const struct builtin builtins[];
const struct builtin *builtin_find(const char *name);

/* edit.c: the line editor and history */
int edit_line(const char *prompt, char *out, unsigned int size); /* length, or -1 if dropped */
void history_load(void);
void history_add(const char *line);
int history_count(void);
const char *history_entry(int i); /* 0 is the oldest kept */
void history_clear(void);

#endif
