add_library(ascParse
  Parser.cpp
  ParseDecl.cpp
  ParseStmt.cpp
  ParseExpr.cpp
)

target_link_libraries(ascParse
  PUBLIC
  ascLex
  ascAST
)
