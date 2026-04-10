#include "asc/Parse/Parser.h"
#include "llvm/ADT/StringSwitch.h"

namespace asc {

/// Precedence levels (higher = binds tighter).
/// Based on RFC-0015 precedence table.
int Parser::getPrecedence(tok::TokenKind kind) {
  switch (kind) {
  case tok::star:
  case tok::slash:
  case tok::percent:
    return 13;
  case tok::plus:
  case tok::minus:
    return 12;
  case tok::lessless:
  case tok::greatergreater:
    return 11;
  case tok::amp:
    return 10;
  case tok::caret:
    return 9;
  case tok::pipe:
    return 8;
  case tok::dotdot:
  case tok::dotdotequal:
    return 7;
  case tok::equalequal:
  case tok::exclaimequal:
  case tok::less:
  case tok::greater:
  case tok::lessequal:
  case tok::greaterequal:
    return 6;
  case tok::ampamp:
    return 5;
  case tok::pipepipe:
    return 4;
  default:
    return -1;
  }
}

BinaryOp Parser::toBinaryOp(tok::TokenKind kind) {
  switch (kind) {
  case tok::plus:           return BinaryOp::Add;
  case tok::minus:          return BinaryOp::Sub;
  case tok::star:           return BinaryOp::Mul;
  case tok::slash:          return BinaryOp::Div;
  case tok::percent:        return BinaryOp::Mod;
  case tok::amp:            return BinaryOp::BitAnd;
  case tok::pipe:           return BinaryOp::BitOr;
  case tok::caret:          return BinaryOp::BitXor;
  case tok::lessless:       return BinaryOp::Shl;
  case tok::greatergreater: return BinaryOp::Shr;
  case tok::equalequal:     return BinaryOp::Eq;
  case tok::exclaimequal:   return BinaryOp::Ne;
  case tok::less:           return BinaryOp::Lt;
  case tok::greater:        return BinaryOp::Gt;
  case tok::lessequal:      return BinaryOp::Le;
  case tok::greaterequal:   return BinaryOp::Ge;
  case tok::ampamp:         return BinaryOp::LogAnd;
  case tok::pipepipe:       return BinaryOp::LogOr;
  case tok::dotdot:         return BinaryOp::Range;
  case tok::dotdotequal:    return BinaryOp::RangeInclusive;
  default:                  return BinaryOp::Add; // unreachable
  }
}

bool Parser::isRightAssociative(tok::TokenKind kind) {
  return kind == tok::dotdot || kind == tok::dotdotequal;
}

Expr *Parser::parseExpr() {
  return parseAssignExpr();
}

Expr *Parser::parseAssignExpr() {
  Expr *lhs = parseBinaryExpr(1);
  if (!lhs)
    return nullptr;

  SourceLocation loc = tok.getLocation();
  AssignOp aop;
  bool isAssign = true;

  switch (tok.getKind()) {
  case tok::equal:             aop = AssignOp::Assign; break;
  case tok::plusequal:         aop = AssignOp::AddAssign; break;
  case tok::minusequal:       aop = AssignOp::SubAssign; break;
  case tok::starequal:        aop = AssignOp::MulAssign; break;
  case tok::slashequal:       aop = AssignOp::DivAssign; break;
  case tok::percentequal:     aop = AssignOp::ModAssign; break;
  case tok::ampequal:         aop = AssignOp::BitAndAssign; break;
  case tok::pipeequal:        aop = AssignOp::BitOrAssign; break;
  case tok::caretequal:       aop = AssignOp::BitXorAssign; break;
  case tok::lesslessequal:    aop = AssignOp::ShlAssign; break;
  case tok::greatergreaterequal: aop = AssignOp::ShrAssign; break;
  default: isAssign = false; break;
  }

  if (isAssign) {
    advance();
    Expr *rhs = parseAssignExpr(); // right-associative
    return ctx.create<AssignExpr>(aop, lhs, rhs, loc);
  }

  return lhs;
}

Expr *Parser::parseBinaryExpr(int minPrec) {
  Expr *lhs = parseUnaryExpr();
  if (!lhs)
    return nullptr;

  while (true) {
    int prec = getPrecedence(tok.getKind());
    if (prec < minPrec)
      break;

    tok::TokenKind opKind = tok.getKind();
    SourceLocation opLoc = tok.getLocation();
    advance();

    int nextMinPrec = isRightAssociative(opKind) ? prec : prec + 1;
    Expr *rhs = parseBinaryExpr(nextMinPrec);
    if (!rhs)
      return lhs;

    lhs = ctx.create<BinaryExpr>(toBinaryOp(opKind), lhs, rhs, opLoc);
  }

  return lhs;
}

Expr *Parser::parseUnaryExpr() {
  SourceLocation loc = tok.getLocation();

  if (tok.is(tok::minus)) {
    advance();
    Expr *operand = parseUnaryExpr();
    return ctx.create<UnaryExpr>(UnaryOp::Neg, operand, loc);
  }
  if (tok.is(tok::exclaim)) {
    advance();
    Expr *operand = parseUnaryExpr();
    return ctx.create<UnaryExpr>(UnaryOp::Not, operand, loc);
  }
  if (tok.is(tok::tilde)) {
    advance();
    Expr *operand = parseUnaryExpr();
    return ctx.create<UnaryExpr>(UnaryOp::BitNot, operand, loc);
  }
  if (tok.is(tok::amp)) {
    advance();
    Expr *operand = parseUnaryExpr();
    return ctx.create<UnaryExpr>(UnaryOp::AddrOf, operand, loc);
  }
  if (tok.is(tok::star)) {
    advance();
    Expr *operand = parseUnaryExpr();
    return ctx.create<UnaryExpr>(UnaryOp::Deref, operand, loc);
  }

  return parsePostfixExpr();
}

Expr *Parser::parsePostfixExpr() {
  Expr *expr = parsePrimaryExpr();
  if (!expr)
    return nullptr;

  while (true) {
    SourceLocation loc = tok.getLocation();

    // Field access or method call: expr.name or expr.0 (tuple index)
    if (tok.is(tok::dot)) {
      advance();
      if (!tok.is(tok::identifier) && !tok.is(tok::integer_literal)) {
        error("expected field or method name");
        return expr;
      }
      std::string name = tok.getSpelling().str();
      advance();

      // Method call: expr.name(args)
      if (tok.is(tok::l_paren)) {
        advance();
        auto args = parseArgList();
        expect(tok::r_paren);
        expr = ctx.create<MethodCallExpr>(expr, std::move(name),
                                          std::move(args),
                                          std::vector<Type *>{}, loc);
      } else {
        expr = ctx.create<FieldAccessExpr>(expr, std::move(name), loc);
      }
      continue;
    }

    // Index: expr[index]
    if (tok.is(tok::l_bracket)) {
      advance();
      Expr *index = parseExpr();
      expect(tok::r_bracket);
      expr = ctx.create<IndexExpr>(expr, index, loc);
      continue;
    }

    // Function call: expr(args)
    if (tok.is(tok::l_paren)) {
      advance();
      auto args = parseArgList();
      expect(tok::r_paren);
      expr = ctx.create<CallExpr>(expr, std::move(args),
                                  std::vector<Type *>{}, loc);
      continue;
    }

    // Try operator: expr?
    if (tok.is(tok::question)) {
      advance();
      expr = ctx.create<TryExpr>(expr, loc);
      continue;
    }

    // Cast: expr as Type
    if (tok.is(tok::kw_as)) {
      advance();
      Type *targetType = parseType();
      expr = ctx.create<CastExpr>(expr, targetType, loc);
      continue;
    }

    break;
  }

  return expr;
}

Expr *Parser::parsePrimaryExpr() {
  SourceLocation loc = tok.getLocation();

  // Integer literal.
  if (tok.is(tok::integer_literal)) {
    auto *expr = ctx.create<IntegerLiteral>(
        tok.getIntegerValue(), tok.getSuffixType().str(), loc);
    advance();
    return expr;
  }

  // Float literal.
  if (tok.is(tok::float_literal)) {
    auto *expr = ctx.create<FloatLiteral>(
        tok.getFloatValue(), tok.getSuffixType().str(), loc);
    advance();
    return expr;
  }

  // String literal.
  if (tok.is(tok::string_literal)) {
    std::string val = tok.getSpelling().str();
    advance();
    return ctx.create<StringLiteral>(std::move(val), loc);
  }

  // Char literal.
  if (tok.is(tok::char_literal)) {
    // DECISION: Store the raw char literal value as first character.
    std::string sp = tok.getSpelling().str();
    uint32_t val = 0;
    if (sp.size() >= 3)
      val = static_cast<uint32_t>(sp[1]);
    advance();
    return ctx.create<CharLiteral>(val, loc);
  }

  // Bool literals.
  if (tok.is(tok::kw_true)) {
    advance();
    return ctx.create<BoolLiteral>(true, loc);
  }
  if (tok.is(tok::kw_false)) {
    advance();
    return ctx.create<BoolLiteral>(false, loc);
  }

  // Null literal.
  if (tok.is(tok::kw_null)) {
    advance();
    return ctx.create<NullLiteral>(loc);
  }

  // Template literal.
  if (tok.is(tok::template_no_sub)) {
    std::string text = tok.getSpelling().str();
    advance();
    std::vector<TemplatePart> parts;
    parts.push_back({std::move(text), nullptr});
    return ctx.create<TemplateLiteralExpr>(std::move(parts), loc);
  }
  if (tok.is(tok::template_head)) {
    std::vector<TemplatePart> parts;
    std::string text = tok.getSpelling().str();
    advance();
    Expr *expr = parseExpr();
    parts.push_back({std::move(text), expr});
    while (tok.is(tok::template_middle)) {
      text = tok.getSpelling().str();
      advance();
      expr = parseExpr();
      parts.push_back({std::move(text), expr});
    }
    if (tok.is(tok::template_tail)) {
      parts.push_back({tok.getSpelling().str(), nullptr});
      advance();
    }
    return ctx.create<TemplateLiteralExpr>(std::move(parts), loc);
  }

  // Parenthesized expression, tuple literal, or closure.
  if (tok.is(tok::l_paren)) {
    advance();

    // () => body — empty closure or empty tuple.
    if (tok.is(tok::r_paren)) {
      advance();
      if (tok.is(tok::fat_arrow)) {
        return parseClosureExpr();
      }
      return ctx.create<TupleLiteral>(std::vector<Expr *>{}, loc);
    }

    // Detect typed closure: (identifier : type, ...) => body
    // Heuristic: if current is identifier and next is ':', it's a typed closure.
    bool isTypedClosure = tok.is(tok::identifier) && lexer.peek().is(tok::colon);
    if (isTypedClosure) {
      // Parse closure parameter list.
      std::vector<ClosureParam> params;
      while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
        std::string pname;
        if (tok.is(tok::identifier)) {
          pname = tok.getSpelling().str();
          advance();
        }
        expect(tok::colon);
        Type *ptype = parseType();
        params.push_back({std::move(pname), ptype});
        if (!consume(tok::comma))
          break;
      }
      expect(tok::r_paren);
      // Optional return type: ): retType =>
      Type *retType = nullptr;
      if (tok.is(tok::colon)) {
        advance();
        retType = parseType();
      }
      expect(tok::fat_arrow);
      Expr *body = nullptr;
      if (tok.is(tok::l_brace)) {
        body = parseBlockExpr();
      } else {
        body = parseExpr();
      }
      return ctx.create<ClosureExpr>(std::move(params), retType, body, loc);
    }

    // Not a typed closure — parse as expression.
    Expr *first = parseExpr();
    if (tok.is(tok::comma)) {
      // Tuple literal.
      std::vector<Expr *> elements;
      elements.push_back(first);
      while (consume(tok::comma)) {
        if (tok.is(tok::r_paren))
          break;
        elements.push_back(parseExpr());
      }
      expect(tok::r_paren);
      return ctx.create<TupleLiteral>(std::move(elements), loc);
    }
    expect(tok::r_paren);

    // Check for arrow function: (expr) => body
    if (tok.is(tok::fat_arrow)) {
      return parseClosureExpr();
    }

    return ctx.create<ParenExpr>(first, loc);
  }

  // Array literal: [expr, ...] or [expr; count]
  if (tok.is(tok::l_bracket)) {
    advance();
    if (tok.is(tok::r_bracket)) {
      advance();
      return ctx.create<ArrayLiteral>(std::vector<Expr *>{}, loc);
    }
    Expr *first = parseExpr();
    if (tok.is(tok::semicolon)) {
      // Array repeat: [value; count]
      advance();
      Expr *count = parseExpr();
      expect(tok::r_bracket);
      return ctx.create<ArrayRepeatExpr>(first, count, loc);
    }
    std::vector<Expr *> elements;
    elements.push_back(first);
    while (consume(tok::comma)) {
      if (tok.is(tok::r_bracket))
        break;
      elements.push_back(parseExpr());
    }
    expect(tok::r_bracket);
    return ctx.create<ArrayLiteral>(std::move(elements), loc);
  }

  // Block expression: { ... }
  if (tok.is(tok::l_brace))
    return parseBlockExpr();

  // If expression.
  if (tok.is(tok::kw_if))
    return parseIfExpr();

  // Match expression.
  if (tok.is(tok::kw_match))
    return parseMatchExpr();

  // Loop expression.
  if (tok.is(tok::kw_loop))
    return parseLoopExpr();

  // While expression.
  if (tok.is(tok::kw_while))
    return parseWhileExpr();

  // For expression.
  if (tok.is(tok::kw_for))
    return parseForExpr();

  // Channel creation: chan<T>(capacity)
  if (tok.is(tok::kw_chan)) {
    advance();
    // Parse generic arg: <T>
    Type *elemType = nullptr;
    if (consume(tok::less)) {
      elemType = parseType();
      expect(tok::greater);
    }
    // Parse capacity: (N)
    expect(tok::l_paren);
    Expr *capacity = parseExpr();
    expect(tok::r_paren);
    // Emit as a call to chan_make(capacity) — macro-like.
    std::vector<Expr *> args;
    if (capacity) args.push_back(capacity);
    return ctx.create<MacroCallExpr>("chan_make", std::move(args), loc);
  }

  // task.spawn(closure) — parse as method call on 'task' namespace.
  if (tok.is(tok::kw_task) && lexer.peek().is(tok::dot)) {
    advance(); // task
    advance(); // .
    std::string methodName;
    if (tok.is(tok::identifier)) {
      methodName = tok.getSpelling().str();
      advance();
    }
    expect(tok::l_paren);
    auto spawnArgs = parseArgList();
    expect(tok::r_paren);
    // Emit as a macro call: task_spawn(args) or task_join(args)
    return ctx.create<MacroCallExpr>("task_" + methodName,
                                      std::move(spawnArgs), loc);
  }

  // Unsafe block.
  if (tok.is(tok::kw_unsafe)) {
    advance();
    auto *body = parseBlock();
    return ctx.create<UnsafeBlockExpr>(body, loc);
  }

  // Return as expression (lowest precedence).
  if (tok.is(tok::kw_return)) {
    advance();
    // DECISION: Return parsed as a statement in parseStmt; here we handle
    // it as an expression for block trailing position.
    Expr *value = nullptr;
    if (!tok.isOneOf(tok::semicolon, tok::r_brace, tok::comma))
      value = parseExpr();
    // Return is not a real expression in our AST, but we'll wrap it.
    // For now, just return the value or null.
    return value ? value : ctx.create<NullLiteral>(loc);
  }

  // Labelled loop: `label: loop/while/for { ... }`
  if (tok.is(tok::identifier)) {
    const Token &next = lexer.peek();
    if (next.is(tok::colon)) {
      std::string label = tok.getSpelling().str();
      SourceLocation labelLoc = tok.getLocation();
      advance(); // consume identifier
      advance(); // consume colon
      if (tok.is(tok::kw_loop)) {
        advance();
        auto *body = parseBlock();
        return ctx.create<LoopExpr>(body, std::move(label), labelLoc);
      }
      if (tok.is(tok::kw_while)) {
        advance(); // consume 'while'
        // Check for while let
        if (tok.is(tok::kw_let)) {
          advance(); // consume 'let'
          Pattern *pattern = parsePattern();
          expect(tok::equal);
          Expr *scrutinee = parseExpr();
          auto *body = parseBlock();
          return ctx.create<WhileLetExpr>(pattern, scrutinee, body, std::move(label), labelLoc);
        }
        Expr *condition = parseExpr();
        auto *body = parseBlock();
        return ctx.create<WhileExpr>(condition, body, std::move(label), labelLoc);
      }
      if (tok.is(tok::kw_for)) {
        advance();
        expect(tok::l_paren);
        bool isConst = false;
        if (consume(tok::kw_const)) isConst = true;
        std::string varName;
        if (tok.is(tok::kw_let)) advance();
        if (tok.is(tok::identifier)) { varName = tok.getSpelling().str(); advance(); }
        expect(tok::kw_of);
        Expr *iterable = parseExpr();
        expect(tok::r_paren);
        auto *body = parseBlock();
        return ctx.create<ForExpr>(std::move(varName), isConst, iterable, body, std::move(label), labelLoc);
      }
      // Not a labelled loop — this is an error
      error(DiagID::ErrExpectedExpression, "expected loop, while, or for after label");
      return nullptr;
    }
  }

  // Identifier, path, or struct literal.
  if (tok.is(tok::identifier)) {
    std::string name = tok.getSpelling().str();
    advance();

    // Check for macro call: name!(args)
    if (tok.is(tok::exclaim) && lexer.peek().is(tok::l_paren)) {
      return parseMacroCallExpr(std::move(name));
    }

    // Check for generic struct literal: Type<Args> { ... }
    // Heuristic: uppercase first letter + < indicates generic type, not comparison.
    if (tok.is(tok::less) && !name.empty() && name[0] >= 'A' && name[0] <= 'Z') {
      advance(); // <
      std::vector<Type *> genericArgs;
      while (!tok.is(tok::greater) && !tok.is(tok::greatergreater) &&
             !tok.is(tok::eof)) {
        Type *arg = parseType();
        if (arg) genericArgs.push_back(arg);
        if (!consume(tok::comma)) break;
      }
      if (tok.is(tok::greatergreater))
        tok = Token(tok::greater, tok.getLocation(), ">");
      else
        expect(tok::greater);

      // Followed by { — struct literal with generic args.
      if (tok.is(tok::l_brace)) {
        // Mangle the struct name using same scheme as Sema::mangleGenericName.
        // This creates e.g. "Pair_i32_i32" matching what monomorphizeType produces.
        std::string monoName = name;
        for (auto *ga : genericArgs) {
          monoName += "_";
          if (auto *bt = dynamic_cast<BuiltinType *>(ga)) {
            switch (bt->getBuiltinKind()) {
            case BuiltinTypeKind::I8: monoName += "i8"; break;
            case BuiltinTypeKind::I16: monoName += "i16"; break;
            case BuiltinTypeKind::I32: monoName += "i32"; break;
            case BuiltinTypeKind::I64: monoName += "i64"; break;
            case BuiltinTypeKind::F32: monoName += "f32"; break;
            case BuiltinTypeKind::F64: monoName += "f64"; break;
            case BuiltinTypeKind::Bool: monoName += "bool"; break;
            default: monoName += "type"; break;
            }
          } else if (auto *nt = dynamic_cast<NamedType *>(ga)) {
            monoName += nt->getName().str();
          } else {
            monoName += "type";
          }
        }
        advance(); // {
        std::vector<FieldInit> fields;
        while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
          FieldInit fi;
          fi.loc = tok.getLocation();
          if (!tok.is(tok::identifier)) { error("expected field name"); break; }
          fi.name = tok.getSpelling().str();
          advance();
          if (consume(tok::colon)) fi.value = parseExpr();
          else fi.value = nullptr;
          fields.push_back(std::move(fi));
          if (!consume(tok::comma)) break;
        }
        expect(tok::r_brace);
        return ctx.create<StructLiteral>(std::move(monoName),
                                         std::move(fields), nullptr, loc);
      }
      // Not followed by { — fall through (might be comparison chain).
      // This is incorrect but rare. TODO: better disambiguation.
    }

    // Check for path: Foo::Bar
    if (tok.is(tok::coloncolon)) {
      std::vector<std::string> segments;
      segments.push_back(std::move(name));
      while (consume(tok::coloncolon)) {
        if (tok.is(tok::identifier)) {
          segments.push_back(tok.getSpelling().str());
          advance();
        } else {
          break;
        }
      }
      // Path with function call: Foo::Bar::baz(args)
      if (tok.is(tok::l_paren)) {
        advance();
        auto args = parseArgList();
        expect(tok::r_paren);
        auto *path = ctx.create<PathExpr>(std::move(segments),
                                          std::vector<Type *>{}, loc);
        return ctx.create<CallExpr>(path, std::move(args),
                                    std::vector<Type *>{}, loc);
      }
      // Path with struct literal: Enum::Variant { field: value, ... }
      if (tok.is(tok::l_brace)) {
        // Treat as a struct literal with the path as the type name.
        // Use the last segment as the struct name for StructLiteral.
        std::string structName = segments.back();
        advance(); // consume {
        std::vector<FieldInit> fields;
        while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
          FieldInit fi;
          fi.loc = tok.getLocation();
          if (!tok.is(tok::identifier)) {
            error("expected field name");
            break;
          }
          fi.name = tok.getSpelling().str();
          advance();
          if (consume(tok::colon)) {
            fi.value = parseExpr();
          } else {
            fi.value = nullptr; // shorthand
          }
          fields.push_back(std::move(fi));
          if (!consume(tok::comma))
            break;
        }
        expect(tok::r_brace);
        // Store as struct literal; Sema can resolve the variant.
        return ctx.create<StructLiteral>(std::move(structName),
                                         std::move(fields), nullptr, loc);
      }
      return ctx.create<PathExpr>(std::move(segments),
                                  std::vector<Type *>{}, loc);
    }

    // Check for struct literal: Name { field: value, ... }
    if (tok.is(tok::l_brace)) {
      // DECISION: Struct literal only when identifier is followed by {
      // and the first thing inside is `identifier:` or `..`.
      // Peek to disambiguate from block expression.
      // Disambiguate struct literal from block expression.
      // Current token is {. Peek inside: if we see `identifier` or `..` or `}`,
      // it's a struct literal. Block expressions starting with identifier are
      // handled by falling through to block parsing.
      const Token &firstInBrace = lexer.peek();
      bool isStructLit = false;
      if (firstInBrace.is(tok::r_brace) || firstInBrace.is(tok::dotdot)) {
        isStructLit = true;
      } else if (firstInBrace.is(tok::identifier)) {
        // DECISION: Only treat as struct literal if the outer name starts
        // with uppercase (type name convention). This avoids misinterpreting
        // `while n { result = ... }` as a struct literal `n { result: ...}`.
        if (!name.empty() && name[0] >= 'A' && name[0] <= 'Z')
          isStructLit = true;
      }

      if (isStructLit) {
        advance(); // consume {
        std::vector<FieldInit> fields;
        Expr *spread = nullptr;
        while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
          if (tok.is(tok::dotdot)) {
            advance();
            spread = parseExpr();
            break;
          }
          FieldInit fi;
          fi.loc = tok.getLocation();
          if (!tok.is(tok::identifier)) {
            error("expected field name");
            break;
          }
          fi.name = tok.getSpelling().str();
          advance();
          if (consume(tok::colon)) {
            fi.value = parseExpr();
          } else {
            fi.value = nullptr; // shorthand
          }
          fields.push_back(std::move(fi));
          if (!consume(tok::comma))
            break;
        }
        expect(tok::r_brace);
        return ctx.create<StructLiteral>(std::move(name), std::move(fields),
                                         spread, loc);
      }
    }

    return ctx.create<DeclRefExpr>(std::move(name), loc);
  }

  error(DiagID::ErrExpectedExpression, "expected expression");
  return nullptr;
}

Expr *Parser::parseIfExpr() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'if'

  // Check for `if let Pattern = expr { ... }`
  if (tok.is(tok::kw_let)) {
    advance(); // consume 'let'
    Pattern *pattern = parsePattern();
    expect(tok::equal);
    Expr *scrutinee = parseExpr();
    auto *thenBlock = parseBlock();

    Stmt *elseBlock = nullptr;
    if (consume(tok::kw_else)) {
      if (tok.is(tok::kw_if)) {
        auto *elseIf = parseIfExpr();
        elseBlock = ctx.create<ExprStmt>(elseIf, elseIf->getLocation());
      } else {
        elseBlock = parseBlock();
      }
    }
    return ctx.create<IfLetExpr>(pattern, scrutinee, thenBlock, elseBlock, loc);
  }

  Expr *condition = parseExpr();
  auto *thenBlock = parseBlock();

  Stmt *elseBlock = nullptr;
  if (consume(tok::kw_else)) {
    if (tok.is(tok::kw_if)) {
      auto *elseIf = parseIfExpr();
      elseBlock = ctx.create<ExprStmt>(elseIf, elseIf->getLocation());
    } else {
      auto *eb = parseBlock();
      elseBlock = eb;
    }
  }

  return ctx.create<IfExpr>(condition, thenBlock, elseBlock, loc);
}

Expr *Parser::parseMatchExpr() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'match'

  Expr *scrutinee = parseExpr();
  expect(tok::l_brace);

  std::vector<MatchArm> arms;
  while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
    MatchArm arm;
    arm.loc = tok.getLocation();
    arm.pattern = parsePattern();
    arm.guard = nullptr;

    // Guard: `if condition`
    if (tok.is(tok::kw_if)) {
      advance();
      arm.guard = parseExpr();
    }

    expect(tok::fat_arrow);
    arm.body = parseExpr();
    arms.push_back(std::move(arm));

    if (!consume(tok::comma)) {
      // Allow omitting comma before }
      if (!tok.is(tok::r_brace))
        consume(tok::comma);
    }
  }
  expect(tok::r_brace);

  return ctx.create<MatchExpr>(scrutinee, std::move(arms), loc);
}

Expr *Parser::parseLoopExpr() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'loop'
  auto *body = parseBlock();
  return ctx.create<LoopExpr>(body, "", loc);
}

Expr *Parser::parseWhileExpr() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'while'

  // Check for `while let Pattern = expr { ... }`
  if (tok.is(tok::kw_let)) {
    advance(); // consume 'let'
    Pattern *pattern = parsePattern();
    expect(tok::equal);
    Expr *scrutinee = parseExpr();
    auto *body = parseBlock();
    return ctx.create<WhileLetExpr>(pattern, scrutinee, body, "", loc);
  }

  Expr *condition = parseExpr();
  auto *body = parseBlock();
  return ctx.create<WhileExpr>(condition, body, "", loc);
}

Expr *Parser::parseForExpr() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'for'
  expect(tok::l_paren);

  bool isConst = false;
  if (tok.is(tok::kw_const)) {
    isConst = true;
    advance();
  } else if (tok.is(tok::kw_let)) {
    advance();
  }

  std::string varName;
  if (tok.is(tok::identifier)) {
    varName = tok.getSpelling().str();
    advance();
  } else {
    error(DiagID::ErrExpectedIdentifier, "expected loop variable name");
  }

  expect(tok::kw_of);
  Expr *iterable = parseExpr();
  expect(tok::r_paren);

  auto *body = parseBlock();
  return ctx.create<ForExpr>(std::move(varName), isConst, iterable, body, "",
                             loc);
}

Expr *Parser::parseClosureExpr() {
  // DECISION: At this point we've already parsed `(expr)` and see `=>`.
  // For a proper closure, we'd need to reparse the params. Since this is
  // complex, we support the simple case: () => expr and (x) => expr.
  SourceLocation loc = tok.getLocation();
  advance(); // consume =>

  std::vector<ClosureParam> params;
  // The params were already consumed as part of the paren expr.
  // For now, return a simple closure with no params.

  Expr *body = nullptr;
  if (tok.is(tok::l_brace)) {
    body = parseBlockExpr();
  } else {
    body = parseExpr();
  }

  return ctx.create<ClosureExpr>(std::move(params), nullptr, body, loc);
}

Expr *Parser::parseBlockExpr() {
  SourceLocation loc = tok.getLocation();
  auto *block = parseBlock();
  return ctx.create<BlockExpr>(block, loc);
}

Expr *Parser::parseMacroCallExpr(std::string name) {
  SourceLocation loc = tok.getLocation();
  advance(); // consume !
  expect(tok::l_paren);

  std::vector<Expr *> args;
  while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
    args.push_back(parseExpr());
    if (!consume(tok::comma))
      break;
  }
  expect(tok::r_paren);

  return ctx.create<MacroCallExpr>(std::move(name), std::move(args), loc);
}

std::vector<Expr *> Parser::parseArgList() {
  std::vector<Expr *> args;
  while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
    args.push_back(parseExpr());
    if (!consume(tok::comma))
      break;
  }
  return args;
}

} // namespace asc
