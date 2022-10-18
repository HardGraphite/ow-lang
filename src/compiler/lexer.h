#pragma once

#include <stdbool.h>

#include "location.h"
#include <utilities/attributes.h>

struct ow_istream;
struct ow_sharedstr;
struct ow_token;

/// Lexical analyzer, converting string to tokens
struct ow_lexer;

/// Error info reported by lexer.
struct ow_lexer_error {
	struct ow_source_location location;
	struct ow_sharedstr *message;
};

/// Create a lexer.
ow_nodiscard struct ow_lexer *ow_lexer_new(void);
/// Delete a lexer created by `ow_lexer_new()`.
void ow_lexer_del(struct ow_lexer *lexer);
/// Config print lexer details or not.
void ow_lexer_verbose(struct ow_lexer *lexer, bool status);
/// Reset and use the given source to start new scanning.
void ow_lexer_source(struct ow_lexer *lexer,
	struct ow_istream *stream, struct ow_sharedstr *file_name);
/// Delete data.
void ow_lexer_clear(struct ow_lexer *lexer);
/// Scan for next token. On error, return false, and `result` will not be modified.
bool ow_lexer_next(struct ow_lexer *lexer, struct ow_token *result);
/// Get last error. If no error ever occurred, return NULL.
struct ow_lexer_error *ow_lexer_error(struct ow_lexer *lexer);