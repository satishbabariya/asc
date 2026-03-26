#include "asc/Parse/Parser.h"
#include "llvm/ADT/StringSwitch.h"

namespace asc {

Parser::Parser(Lexer &lexer, ASTContext &ctx, DiagnosticEngine &diags)
    : lexer(lexer), ctx(ctx), diags(diags) {
  advance(); // prime the first token
}

void Parser::advance() { tok = lexer.lex(); }

bool Parser::check(tok::TokenKind kind) const { return tok.is(kind); }

bool Parser::consume(tok::TokenKind kind) {
  if (tok.is(kind)) {
    advance();
    return true;
  }
  return false;
}

bool Parser::expect(tok::TokenKind kind) {
  if (consume(kind))
    return true;
  std::string msg = "expected '";
  const char *spelling = tok::getPunctuatorSpelling(kind);
  if (spelling[0])
    msg += spelling;
  else
    msg += tok::getTokenName(kind);
  msg += "'";
  error(msg);
  return false;
}

void Parser::error(llvm::StringRef msg) {
  diags.emitError(tok.getLocation(), DiagID::ErrUnexpectedToken, msg);
}

void Parser::error(DiagID id, llvm::StringRef msg) {
  diags.emitError(tok.getLocation(), id, msg);
}

void Parser::skipToSync() {
  while (!tok.is(tok::eof)) {
    if (tok.isOneOf(tok::semicolon, tok::r_brace))  {
      advance();
      return;
    }
    if (tok.isOneOf(tok::l_brace, tok::kw_function, tok::kw_struct,
                    tok::kw_enum, tok::kw_trait, tok::kw_impl,
                    tok::kw_import, tok::kw_export, tok::kw_const,
                    tok::kw_let, tok::kw_type, tok::kw_static))
      return;
    advance();
  }
}

std::vector<Decl *> Parser::parseProgram() {
  std::vector<Decl *> items;
  while (!tok.is(tok::eof)) {
    // Skip doc comments at top level.
    if (tok.isOneOf(tok::doc_line_comment, tok::doc_block_comment)) {
      advance();
      continue;
    }
    Decl *item = parseItem();
    if (item)
      items.push_back(item);
    else
      skipToSync();
  }
  return items;
}

Decl *Parser::parseItem() {
  // Collect attributes.
  std::vector<std::string> attrs;
  while (tok.is(tok::attribute)) {
    attrs.push_back(tok.getSpelling().str());
    advance();
  }

  Decl *decl = nullptr;

  if (tok.is(tok::kw_function) || tok.is(tok::kw_fn))
    decl = parseFunctionDef();
  else if (tok.is(tok::kw_struct))
    decl = parseStructDef();
  else if (tok.is(tok::kw_enum))
    decl = parseEnumDef();
  else if (tok.is(tok::kw_trait))
    decl = parseTraitDef();
  else if (tok.is(tok::kw_impl))
    decl = parseImplBlock();
  else if (tok.is(tok::kw_import))
    decl = parseImportDecl();
  else if (tok.is(tok::kw_export))
    decl = parseExportDecl();
  else if (tok.is(tok::kw_type))
    decl = parseTypeAlias();
  else if (tok.is(tok::kw_const))
    decl = parseConstDef();
  else if (tok.is(tok::kw_static))
    decl = parseStaticDef();
  else if (tok.is(tok::identifier)) {
    // Detect unsupported TypeScript features (RFC-0015 §21).
    llvm::StringRef spelling = tok.getSpelling();
    if (spelling == "class") {
      error(DiagID::ErrUnsupportedFeature,
            "'class' is not supported; use 'struct' + 'impl' instead");
      skipToSync();
      return nullptr;
    }
    if (spelling == "interface") {
      error(DiagID::ErrUnsupportedFeature,
            "'interface' is not supported; use 'trait' instead");
      skipToSync();
      return nullptr;
    }
    if (spelling == "async") {
      error(DiagID::ErrUnsupportedFeature,
            "'async/await' is not supported; use task.spawn for concurrency");
      skipToSync();
      return nullptr;
    }
    if (spelling == "namespace") {
      error(DiagID::ErrUnsupportedFeature,
            "'namespace' is not supported; use modules (import/export)");
      skipToSync();
      return nullptr;
    }
    if (spelling == "try") {
      error(DiagID::ErrUnsupportedFeature,
            "'try/catch' is not supported; use Result<T,E> and ? operator");
      skipToSync();
      return nullptr;
    }
    if (spelling == "throw") {
      error(DiagID::ErrUnsupportedFeature,
            "'throw' is not supported; use panic!() or Result::Err");
      skipToSync();
      return nullptr;
    }
    error("expected declaration");
    return nullptr;
  } else {
    error("expected declaration");
    return nullptr;
  }

  if (decl) {
    for (auto &a : attrs)
      decl->addAttribute(std::move(a));
  }
  return decl;
}

// Type parsing is shared by all parse files, placed here.
Type *Parser::parseType() {
  Type *t = parsePrimaryType();
  if (!t)
    return nullptr;

  // Check for nullable: `T | null`
  if (tok.is(tok::pipe)) {
    advance();
    if (tok.is(tok::kw_null)) {
      advance();
      return ctx.create<NullableType>(t, t->getLocation());
    }
    error("expected 'null' after '|' in type");
  }

  return t;
}

Type *Parser::parsePrimaryType() {
  SourceLocation loc = tok.getLocation();

  // Ownership types: own<T>, ref<T>, refmut<T>
  if (tok.is(tok::kw_own)) {
    advance();
    expect(tok::less);
    Type *inner = parseType();
    expect(tok::greater);
    return ctx.create<OwnType>(inner, loc);
  }
  if (tok.is(tok::kw_ref)) {
    advance();
    if (tok.is(tok::less)) {
      advance();
      Type *inner = parseType();
      expect(tok::greater);
      return ctx.create<RefType>(inner, loc);
    }
    // Bare `ref` as identifier (shouldn't happen in type context)
    error("expected '<' after 'ref'");
    return nullptr;
  }
  if (tok.is(tok::kw_refmut)) {
    advance();
    expect(tok::less);
    Type *inner = parseType();
    expect(tok::greater);
    return ctx.create<RefMutType>(inner, loc);
  }

  // dyn Trait
  if (tok.is(tok::kw_dyn)) {
    advance();
    std::vector<DynTraitType::TraitBound> bounds;
    do {
      if (!tok.is(tok::identifier)) {
        error("expected trait name");
        return nullptr;
      }
      DynTraitType::TraitBound bound;
      bound.name = tok.getSpelling().str();
      advance();
      if (tok.is(tok::less)) {
        advance();
        while (!tok.is(tok::greater) && !tok.is(tok::eof)) {
          Type *arg = parseType();
          if (arg)
            bound.genericArgs.push_back(arg);
          if (!consume(tok::comma))
            break;
        }
        expect(tok::greater);
      }
      bounds.push_back(std::move(bound));
    } while (consume(tok::plus));
    return ctx.create<DynTraitType>(std::move(bounds), loc);
  }

  // Tuple type or grouped type: (T1, T2) or (T)
  if (tok.is(tok::l_paren)) {
    advance();
    if (tok.is(tok::r_paren)) {
      advance();
      return ctx.create<TupleType>(std::vector<Type *>{}, loc);
    }
    std::vector<Type *> elements;
    elements.push_back(parseType());
    if (tok.is(tok::comma)) {
      advance();
      while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
        elements.push_back(parseType());
        if (!consume(tok::comma))
          break;
      }
    }
    expect(tok::r_paren);
    // Check for function type: (T1, T2) -> ReturnType
    if (tok.is(tok::arrow)) {
      advance();
      Type *retType = parseType();
      return ctx.create<FunctionType>(std::move(elements), retType, loc);
    }
    if (elements.size() == 1)
      return elements[0]; // grouped, not tuple
    return ctx.create<TupleType>(std::move(elements), loc);
  }

  // Slice [T] or Array [T; N]
  if (tok.is(tok::l_bracket)) {
    advance();
    Type *elem = parseType();
    if (tok.is(tok::semicolon)) {
      advance();
      // Array: [T; N] — parse N as integer literal for now.
      if (tok.is(tok::integer_literal)) {
        uint64_t size = tok.getIntegerValue();
        advance();
        expect(tok::r_bracket);
        return ctx.create<ArrayType>(elem, size, loc);
      }
      error("expected array size");
      expect(tok::r_bracket);
      return ctx.create<ArrayType>(elem, 0, loc);
    }
    expect(tok::r_bracket);
    return ctx.create<SliceType>(elem, loc);
  }

  // Named type or builtin.
  if (tok.is(tok::identifier)) {
    std::string name = tok.getSpelling().str();
    advance();

    // Check for builtin types.
    auto btk = llvm::StringSwitch<int>(name)
                   .Case("i8", (int)BuiltinTypeKind::I8)
                   .Case("i16", (int)BuiltinTypeKind::I16)
                   .Case("i32", (int)BuiltinTypeKind::I32)
                   .Case("i64", (int)BuiltinTypeKind::I64)
                   .Case("i128", (int)BuiltinTypeKind::I128)
                   .Case("u8", (int)BuiltinTypeKind::U8)
                   .Case("u16", (int)BuiltinTypeKind::U16)
                   .Case("u32", (int)BuiltinTypeKind::U32)
                   .Case("u64", (int)BuiltinTypeKind::U64)
                   .Case("u128", (int)BuiltinTypeKind::U128)
                   .Case("f32", (int)BuiltinTypeKind::F32)
                   .Case("f64", (int)BuiltinTypeKind::F64)
                   .Case("bool", (int)BuiltinTypeKind::Bool)
                   .Case("char", (int)BuiltinTypeKind::Char)
                   .Case("usize", (int)BuiltinTypeKind::USize)
                   .Case("isize", (int)BuiltinTypeKind::ISize)
                   .Case("void", (int)BuiltinTypeKind::Void)
                   .Case("never", (int)BuiltinTypeKind::Never)
                   .Default(-1);
    if (btk >= 0)
      return ctx.getBuiltinType(static_cast<BuiltinTypeKind>(btk));

    // Check for path type: Foo::Bar
    std::vector<std::string> segments;
    segments.push_back(name);
    while (tok.is(tok::coloncolon)) {
      advance();
      if (!tok.is(tok::identifier)) {
        error("expected identifier after '::'");
        break;
      }
      segments.push_back(tok.getSpelling().str());
      advance();
    }

    // Generic args.
    std::vector<Type *> genericArgs;
    if (tok.is(tok::less)) {
      advance();
      while (!tok.is(tok::greater) && !tok.is(tok::eof)) {
        Type *arg = parseType();
        if (arg)
          genericArgs.push_back(arg);
        if (!consume(tok::comma))
          break;
      }
      expect(tok::greater);
    }

    if (segments.size() == 1)
      return ctx.create<NamedType>(std::move(name), std::move(genericArgs),
                                   loc);
    return ctx.create<PathType>(std::move(segments), std::move(genericArgs),
                                loc);
  }

  // Self type
  if (tok.is(tok::kw_Self)) {
    advance();
    return ctx.create<NamedType>("Self", std::vector<Type *>{}, loc);
  }

  error(DiagID::ErrExpectedType, "expected type");
  return nullptr;
}

// Pattern parsing (shared).
Pattern *Parser::parsePattern() {
  SourceLocation loc = tok.getLocation();

  // Wildcard: _
  if (tok.is(tok::identifier) && tok.getSpelling() == "_") {
    advance();
    return ctx.create<WildcardPattern>(loc);
  }

  // Literal patterns: numbers, strings, bools
  if (tok.is(tok::integer_literal)) {
    Expr *lit = ctx.create<IntegerLiteral>(tok.getIntegerValue(),
                                           tok.getSuffixType().str(), loc);
    advance();
    // Check for range pattern.
    if (tok.is(tok::dotdotequal)) {
      advance();
      Expr *end = nullptr;
      if (tok.is(tok::integer_literal)) {
        end = ctx.create<IntegerLiteral>(tok.getIntegerValue(),
                                         tok.getSuffixType().str(),
                                         tok.getLocation());
        advance();
      }
      return ctx.create<RangePattern>(lit, end, true, loc);
    }
    return ctx.create<LiteralPattern>(lit, loc);
  }
  if (tok.is(tok::string_literal)) {
    Expr *lit = ctx.create<StringLiteral>(tok.getSpelling().str(), loc);
    advance();
    return ctx.create<LiteralPattern>(lit, loc);
  }
  if (tok.is(tok::kw_true)) {
    Expr *lit = ctx.create<BoolLiteral>(true, loc);
    advance();
    return ctx.create<LiteralPattern>(lit, loc);
  }
  if (tok.is(tok::kw_false)) {
    Expr *lit = ctx.create<BoolLiteral>(false, loc);
    advance();
    return ctx.create<LiteralPattern>(lit, loc);
  }
  if (tok.is(tok::kw_null)) {
    Expr *lit = ctx.create<NullLiteral>(loc);
    advance();
    return ctx.create<LiteralPattern>(lit, loc);
  }

  // Tuple pattern: (p1, p2)
  if (tok.is(tok::l_paren)) {
    advance();
    std::vector<Pattern *> elements;
    while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
      elements.push_back(parsePattern());
      if (!consume(tok::comma))
        break;
    }
    expect(tok::r_paren);
    return ctx.create<TuplePattern>(std::move(elements), loc);
  }

  // Slice pattern: [p1, p2, ..rest]
  if (tok.is(tok::l_bracket)) {
    advance();
    std::vector<Pattern *> elements;
    std::string rest;
    while (!tok.is(tok::r_bracket) && !tok.is(tok::eof)) {
      if (tok.is(tok::dotdot)) {
        advance();
        if (tok.is(tok::identifier)) {
          rest = tok.getSpelling().str();
          advance();
        }
        break;
      }
      elements.push_back(parsePattern());
      if (!consume(tok::comma))
        break;
    }
    expect(tok::r_bracket);
    return ctx.create<SlicePattern>(std::move(elements), std::move(rest), loc);
  }

  // Identifier pattern or enum pattern: Name | Name::Variant(...)
  if (tok.is(tok::identifier)) {
    std::vector<std::string> path;
    path.push_back(tok.getSpelling().str());
    advance();

    while (tok.is(tok::coloncolon)) {
      advance();
      if (!tok.is(tok::identifier))
        break;
      path.push_back(tok.getSpelling().str());
      advance();
    }

    // Enum pattern with args: Variant(p1, p2)
    if (tok.is(tok::l_paren)) {
      advance();
      std::vector<Pattern *> args;
      while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
        args.push_back(parsePattern());
        if (!consume(tok::comma))
          break;
      }
      expect(tok::r_paren);
      return ctx.create<EnumPattern>(std::move(path), std::move(args), loc);
    }

    // Struct pattern: Name { field: pattern, ... }
    if (tok.is(tok::l_brace) && path.size() == 1) {
      advance();
      std::vector<FieldPattern> fields;
      while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
        std::string fname = tok.getSpelling().str();
        SourceLocation floc = tok.getLocation();
        advance();
        Pattern *fpat = nullptr;
        if (consume(tok::colon))
          fpat = parsePattern();
        fields.push_back({std::move(fname), fpat, floc});
        if (!consume(tok::comma))
          break;
      }
      expect(tok::r_brace);
      return ctx.create<StructPattern>(path[0], std::move(fields), loc);
    }

    // Simple identifier or enum path without args
    if (path.size() == 1) {
      return ctx.create<IdentPattern>(path[0], false, loc);
    }
    return ctx.create<EnumPattern>(std::move(path),
                                   std::vector<Pattern *>{}, loc);
  }

  error("expected pattern");
  return ctx.create<WildcardPattern>(loc);
}

} // namespace asc
