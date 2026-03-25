add_library(ascSema
  Sema.cpp
  SemaDecl.cpp
  SemaExpr.cpp
  SemaType.cpp
)

target_link_libraries(ascSema
  PUBLIC
  ascAST
  ascBasic
)
