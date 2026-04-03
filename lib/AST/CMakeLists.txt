add_library(ascAST
  ASTContext.cpp
  Type.cpp
)

target_link_libraries(ascAST
  PUBLIC
  ascBasic
)
