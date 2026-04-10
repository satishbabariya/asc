#include "asc/Parse/Parser.h"

namespace asc {

CompoundStmt *Parser::parseBlock() {
  SourceLocation loc = tok.getLocation();
  expect(tok::l_brace);

  std::vector<Stmt *> stmts;
  Expr *trailingExpr = nullptr;

  while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
    // Skip doc comments.
    if (tok.isOneOf(tok::doc_line_comment, tok::doc_block_comment)) {
      advance();
      continue;
    }

    Stmt *stmt = parseStmt();
    if (!stmt)
      continue;

    // Check if this is a trailing expression (no semicolon before }).
    // An ExprStmt without semicolon at end of block is a trailing expr.
    if (auto *es = dynamic_cast<ExprStmt *>(stmt)) {
      if (tok.is(tok::r_brace)) {
        trailingExpr = es->getExpr();
        break;
      }
    }
    stmts.push_back(stmt);
  }

  expect(tok::r_brace);
  return ctx.create<CompoundStmt>(std::move(stmts), trailingExpr, loc);
}

Stmt *Parser::parseStmt() {
  SourceLocation loc = tok.getLocation();

  // Let/const bindings.
  if (tok.is(tok::kw_let) || tok.is(tok::kw_const))
    return parseLetOrConstStmt();

  // Return statement.
  if (tok.is(tok::kw_return)) {
    advance();
    Expr *value = nullptr;
    if (!tok.isOneOf(tok::semicolon, tok::r_brace))
      value = parseExpr();
    consume(tok::semicolon);
    return ctx.create<ReturnStmt>(value, loc);
  }

  // Break statement.
  if (tok.is(tok::kw_break)) {
    advance();
    std::string label;
    Expr *value = nullptr;
    // Label: `break outer;` — identifier followed by semicolon or brace
    if (tok.is(tok::identifier) &&
        lexer.peek().isOneOf(tok::semicolon, tok::r_brace)) {
      label = tok.getSpelling().str();
      advance();
    }
    if (!tok.isOneOf(tok::semicolon, tok::r_brace))
      value = parseExpr();
    consume(tok::semicolon);
    return ctx.create<BreakStmt>(value, std::move(label), loc);
  }

  // Continue statement.
  if (tok.is(tok::kw_continue)) {
    advance();
    std::string label;
    if (tok.is(tok::identifier)) {
      label = tok.getSpelling().str();
      advance();
    }
    consume(tok::semicolon);
    return ctx.create<ContinueStmt>(std::move(label), loc);
  }

  // Declarations as statements.
  if (tok.isOneOf(tok::kw_function, tok::kw_fn, tok::kw_struct, tok::kw_enum,
                  tok::kw_trait, tok::kw_impl, tok::kw_type)) {
    Decl *decl = parseItem();
    if (decl)
      return ctx.create<ItemStmt>(decl, loc);
    skipToSync();
    return nullptr;
  }

  // Attributes before items.
  if (tok.is(tok::attribute)) {
    Decl *decl = parseItem();
    if (decl)
      return ctx.create<ItemStmt>(decl, loc);
    skipToSync();
    return nullptr;
  }

  // Expression statement.
  Expr *expr = parseExpr();
  if (!expr) {
    skipToSync();
    return nullptr;
  }

  // Consume optional semicolon.
  bool hadSemi = consume(tok::semicolon);
  (void)hadSemi;

  return ctx.create<ExprStmt>(expr, loc);
}

Stmt *Parser::parseLetOrConstStmt() {
  SourceLocation loc = tok.getLocation();
  bool isConst = tok.is(tok::kw_const);
  advance();

  Pattern *pattern = nullptr;
  std::string name;

  // Simple name or destructuring pattern.
  if (tok.is(tok::identifier)) {
    name = tok.getSpelling().str();
    advance();
  } else if (tok.is(tok::l_bracket) || tok.is(tok::l_brace) ||
             tok.is(tok::l_paren)) {
    pattern = parsePattern();
  } else {
    error(DiagID::ErrExpectedIdentifier, "expected variable name or pattern");
    skipToSync();
    return nullptr;
  }

  Type *type = nullptr;
  if (consume(tok::colon))
    type = parseType();

  Expr *init = nullptr;
  if (consume(tok::equal))
    init = parseExpr();

  // `else` for let-else
  // DECISION: let-else parses `else { block }` but we store it as part of
  // the VarDecl for simplicity.

  consume(tok::semicolon);

  auto *decl = ctx.create<VarDecl>(std::move(name), isConst, type, init,
                                   pattern, loc);
  if (isConst)
    return ctx.create<ConstStmt>(decl, loc);
  return ctx.create<LetStmt>(decl, loc);
}

} // namespace asc
