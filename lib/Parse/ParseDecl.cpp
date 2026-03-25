#include "asc/Parse/Parser.h"

namespace asc {

FunctionDecl *Parser::parseFunctionDef() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'function' or 'fn'

  if (!tok.is(tok::identifier)) {
    error(DiagID::ErrExpectedIdentifier, "expected function name");
    skipToSync();
    return nullptr;
  }
  std::string name = tok.getSpelling().str();
  advance();

  auto genericParams = parseGenericParams();
  expect(tok::l_paren);
  auto params = parseParamList();
  expect(tok::r_paren);

  Type *returnType = nullptr;
  if (consume(tok::colon))
    returnType = parseType();

  auto whereClause = parseWhereClause();

  CompoundStmt *body = nullptr;
  if (tok.is(tok::l_brace))
    body = parseBlock();
  else
    expect(tok::semicolon);

  return ctx.create<FunctionDecl>(std::move(name), std::move(genericParams),
                                  std::move(params), returnType, body,
                                  std::move(whereClause), loc);
}

std::vector<ParamDecl> Parser::parseParamList() {
  std::vector<ParamDecl> params;
  while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
    ParamDecl param;
    param.loc = tok.getLocation();

    // Self parameters: ref<Self>, refmut<Self>, own<Self>
    if (tok.is(tok::kw_ref) && lexer.peek().is(tok::less)) {
      advance(); // ref
      advance(); // <
      if (tok.is(tok::kw_Self)) {
        advance();
        expect(tok::greater);
        param.name = "self";
        param.isSelfRef = true;
        params.push_back(std::move(param));
        if (!consume(tok::comma))
          break;
        continue;
      }
    }
    if (tok.is(tok::kw_refmut) && lexer.peek().is(tok::less)) {
      advance(); // refmut
      advance(); // <
      if (tok.is(tok::kw_Self)) {
        advance();
        expect(tok::greater);
        param.name = "self";
        param.isSelfRefMut = true;
        params.push_back(std::move(param));
        if (!consume(tok::comma))
          break;
        continue;
      }
    }

    if (!tok.is(tok::identifier)) {
      error(DiagID::ErrExpectedIdentifier, "expected parameter name");
      break;
    }
    param.name = tok.getSpelling().str();
    advance();

    expect(tok::colon);
    param.type = parseType();

    params.push_back(std::move(param));
    if (!consume(tok::comma))
      break;
  }
  return params;
}

std::vector<GenericParam> Parser::parseGenericParams() {
  std::vector<GenericParam> params;
  if (!tok.is(tok::less))
    return params;
  advance(); // consume <

  while (!tok.is(tok::greater) && !tok.is(tok::eof)) {
    GenericParam gp;
    gp.loc = tok.getLocation();

    // Const generic: `const N: usize`
    if (tok.is(tok::kw_const)) {
      advance();
      gp.isConst = true;
      if (tok.is(tok::identifier)) {
        gp.name = tok.getSpelling().str();
        advance();
      }
      expect(tok::colon);
      gp.constType = parseType();
      params.push_back(std::move(gp));
      if (!consume(tok::comma))
        break;
      continue;
    }

    if (!tok.is(tok::identifier))
      break;
    gp.name = tok.getSpelling().str();
    advance();

    // Bounds: T: Bound1 + Bound2
    if (consume(tok::colon)) {
      do {
        Type *bound = parseType();
        if (bound)
          gp.bounds.push_back(bound);
      } while (consume(tok::plus));
    }

    params.push_back(std::move(gp));
    if (!consume(tok::comma))
      break;
  }
  expect(tok::greater);
  return params;
}

std::vector<Type *> Parser::parseGenericArgs() {
  std::vector<Type *> args;
  if (!tok.is(tok::less))
    return args;
  advance();
  while (!tok.is(tok::greater) && !tok.is(tok::eof)) {
    Type *arg = parseType();
    if (arg)
      args.push_back(arg);
    if (!consume(tok::comma))
      break;
  }
  expect(tok::greater);
  return args;
}

std::vector<WhereConstraint> Parser::parseWhereClause() {
  std::vector<WhereConstraint> constraints;
  if (!tok.is(tok::kw_where))
    return constraints;
  advance();

  while (tok.is(tok::identifier)) {
    WhereConstraint wc;
    wc.loc = tok.getLocation();
    wc.typeName = tok.getSpelling().str();
    advance();
    expect(tok::colon);
    do {
      Type *bound = parseType();
      if (bound)
        wc.bounds.push_back(bound);
    } while (consume(tok::plus));
    constraints.push_back(std::move(wc));
    if (!consume(tok::comma))
      break;
  }
  return constraints;
}

StructDecl *Parser::parseStructDef() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'struct'

  if (!tok.is(tok::identifier)) {
    error(DiagID::ErrExpectedIdentifier, "expected struct name");
    skipToSync();
    return nullptr;
  }
  std::string name = tok.getSpelling().str();
  advance();

  auto genericParams = parseGenericParams();

  std::vector<FieldDecl *> fields;
  if (tok.is(tok::l_brace)) {
    advance();
    while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
      SourceLocation floc = tok.getLocation();
      if (!tok.is(tok::identifier)) {
        error("expected field name");
        skipToSync();
        continue;
      }
      std::string fname = tok.getSpelling().str();
      advance();
      expect(tok::colon);
      Type *ftype = parseType();
      fields.push_back(ctx.create<FieldDecl>(std::move(fname), ftype, floc));
      if (!consume(tok::comma))
        break;
    }
    expect(tok::r_brace);
  } else {
    // Unit struct
    consume(tok::semicolon);
  }

  return ctx.create<StructDecl>(std::move(name), std::move(genericParams),
                                std::move(fields), loc);
}

EnumDecl *Parser::parseEnumDef() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'enum'

  if (!tok.is(tok::identifier)) {
    error(DiagID::ErrExpectedIdentifier, "expected enum name");
    skipToSync();
    return nullptr;
  }
  std::string name = tok.getSpelling().str();
  advance();

  auto genericParams = parseGenericParams();
  expect(tok::l_brace);

  std::vector<EnumVariantDecl *> variants;
  while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
    SourceLocation vloc = tok.getLocation();
    if (!tok.is(tok::identifier)) {
      error("expected variant name");
      skipToSync();
      continue;
    }
    std::string vname = tok.getSpelling().str();
    advance();

    EnumVariantDecl::VariantKind vk = EnumVariantDecl::VariantKind::Unit;
    std::vector<Type *> tupleTypes;
    std::vector<FieldDecl *> structFields;
    Expr *value = nullptr;

    if (tok.is(tok::l_paren)) {
      // Tuple variant: Variant(T1, T2)
      vk = EnumVariantDecl::VariantKind::Tuple;
      advance();
      while (!tok.is(tok::r_paren) && !tok.is(tok::eof)) {
        tupleTypes.push_back(parseType());
        if (!consume(tok::comma))
          break;
      }
      expect(tok::r_paren);
    } else if (tok.is(tok::l_brace)) {
      // Struct variant: Variant { field: Type }
      vk = EnumVariantDecl::VariantKind::Struct;
      advance();
      while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
        SourceLocation floc = tok.getLocation();
        std::string fname = tok.getSpelling().str();
        advance();
        expect(tok::colon);
        Type *ftype = parseType();
        structFields.push_back(
            ctx.create<FieldDecl>(std::move(fname), ftype, floc));
        if (!consume(tok::comma))
          break;
      }
      expect(tok::r_brace);
    } else if (tok.is(tok::equal)) {
      // Valued variant: Variant = 42
      vk = EnumVariantDecl::VariantKind::Valued;
      advance();
      value = parseExpr();
    }

    variants.push_back(ctx.create<EnumVariantDecl>(
        std::move(vname), vk, std::move(tupleTypes),
        std::move(structFields), value, vloc));
    if (!consume(tok::comma))
      break;
  }
  expect(tok::r_brace);

  return ctx.create<EnumDecl>(std::move(name), std::move(genericParams),
                              std::move(variants), loc);
}

TraitDecl *Parser::parseTraitDef() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'trait'

  if (!tok.is(tok::identifier)) {
    error(DiagID::ErrExpectedIdentifier, "expected trait name");
    skipToSync();
    return nullptr;
  }
  std::string name = tok.getSpelling().str();
  advance();

  auto genericParams = parseGenericParams();

  // Supertraits: trait Foo: Bar + Baz
  std::vector<Type *> supertraits;
  if (consume(tok::colon)) {
    do {
      Type *st = parseType();
      if (st)
        supertraits.push_back(st);
    } while (consume(tok::plus));
  }

  expect(tok::l_brace);

  std::vector<TraitItem> items;
  while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
    // Skip doc comments.
    if (tok.isOneOf(tok::doc_line_comment, tok::doc_block_comment)) {
      advance();
      continue;
    }

    // Collect attributes.
    std::vector<std::string> attrs;
    while (tok.is(tok::attribute)) {
      attrs.push_back(tok.getSpelling().str());
      advance();
    }

    // Associated type
    if (tok.is(tok::kw_type)) {
      advance();
      TraitItem item;
      item.isAssocType = true;
      if (tok.is(tok::identifier)) {
        item.assocTypeName = tok.getSpelling().str();
        advance();
      }
      if (consume(tok::equal))
        item.assocTypeDefault = parseType();
      consume(tok::semicolon);
      items.push_back(std::move(item));
      continue;
    }

    // Method
    if (tok.is(tok::kw_function) || tok.is(tok::kw_fn)) {
      TraitItem item;
      item.method = parseFunctionDef();
      if (item.method) {
        for (auto &a : attrs)
          item.method->addAttribute(std::move(a));
      }
      items.push_back(std::move(item));
      continue;
    }

    error("expected trait item");
    skipToSync();
  }
  expect(tok::r_brace);

  return ctx.create<TraitDecl>(std::move(name), std::move(genericParams),
                               std::move(supertraits), std::move(items), loc);
}

ImplDecl *Parser::parseImplBlock() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'impl'

  auto genericParams = parseGenericParams();

  Type *firstType = parseType();
  Type *traitType = nullptr;
  Type *targetType = firstType;

  // `impl Trait for Type`
  if (tok.is(tok::kw_for)) {
    advance();
    traitType = firstType;
    targetType = parseType();
  }

  expect(tok::l_brace);

  std::vector<FunctionDecl *> methods;
  while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
    if (tok.isOneOf(tok::doc_line_comment, tok::doc_block_comment)) {
      advance();
      continue;
    }

    std::vector<std::string> attrs;
    while (tok.is(tok::attribute)) {
      attrs.push_back(tok.getSpelling().str());
      advance();
    }

    if (tok.is(tok::kw_function) || tok.is(tok::kw_fn)) {
      auto *fn = parseFunctionDef();
      if (fn) {
        for (auto &a : attrs)
          fn->addAttribute(std::move(a));
        methods.push_back(fn);
      }
    } else {
      error("expected method in impl block");
      skipToSync();
    }
  }
  expect(tok::r_brace);

  return ctx.create<ImplDecl>(std::move(genericParams), targetType, traitType,
                              std::move(methods), loc);
}

ImportDecl *Parser::parseImportDecl() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'import'

  expect(tok::l_brace);
  std::vector<ImportSpecifier> specifiers;
  while (!tok.is(tok::r_brace) && !tok.is(tok::eof)) {
    ImportSpecifier spec;
    // `type` keyword for type-only import
    if (tok.is(tok::kw_type)) {
      spec.isTypeOnly = true;
      advance();
    }
    if (!tok.is(tok::identifier)) {
      error("expected import name");
      break;
    }
    spec.name = tok.getSpelling().str();
    advance();
    // `as Alias`
    if (tok.is(tok::kw_as)) {
      advance();
      if (tok.is(tok::identifier)) {
        spec.alias = tok.getSpelling().str();
        advance();
      }
    }
    specifiers.push_back(std::move(spec));
    if (!consume(tok::comma))
      break;
  }
  expect(tok::r_brace);

  expect(tok::kw_from);
  std::string modulePath;
  if (tok.is(tok::string_literal)) {
    modulePath = tok.getSpelling().str();
    // Strip quotes.
    if (modulePath.size() >= 2)
      modulePath = modulePath.substr(1, modulePath.size() - 2);
    advance();
  } else {
    error("expected module path string");
  }
  consume(tok::semicolon);

  return ctx.create<ImportDecl>(std::move(modulePath),
                                std::move(specifiers), loc);
}

ExportDecl *Parser::parseExportDecl() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'export'

  Decl *inner = parseItem();
  return ctx.create<ExportDecl>(inner, loc);
}

TypeAliasDecl *Parser::parseTypeAlias() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'type'

  if (!tok.is(tok::identifier)) {
    error(DiagID::ErrExpectedIdentifier, "expected type alias name");
    skipToSync();
    return nullptr;
  }
  std::string name = tok.getSpelling().str();
  advance();

  auto genericParams = parseGenericParams();
  expect(tok::equal);
  Type *aliased = parseType();
  consume(tok::semicolon);

  return ctx.create<TypeAliasDecl>(std::move(name), std::move(genericParams),
                                   aliased, loc);
}

ConstDecl *Parser::parseConstDef() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'const'

  if (!tok.is(tok::identifier)) {
    error(DiagID::ErrExpectedIdentifier, "expected const name");
    skipToSync();
    return nullptr;
  }
  std::string name = tok.getSpelling().str();
  advance();

  Type *type = nullptr;
  if (consume(tok::colon))
    type = parseType();

  Expr *init = nullptr;
  if (consume(tok::equal))
    init = parseExpr();

  consume(tok::semicolon);

  return ctx.create<ConstDecl>(std::move(name), type, init, loc);
}

StaticDecl *Parser::parseStaticDef() {
  SourceLocation loc = tok.getLocation();
  advance(); // consume 'static'

  bool isMut = consume(tok::kw_mut);

  if (!tok.is(tok::identifier)) {
    error(DiagID::ErrExpectedIdentifier, "expected static variable name");
    skipToSync();
    return nullptr;
  }
  std::string name = tok.getSpelling().str();
  advance();

  expect(tok::colon);
  Type *type = parseType();

  Expr *init = nullptr;
  if (consume(tok::equal))
    init = parseExpr();

  consume(tok::semicolon);

  return ctx.create<StaticDecl>(std::move(name), type, init, isMut, loc);
}

} // namespace asc
