add_library(ascLex
  Lexer.cpp
)

target_link_libraries(ascLex
  PUBLIC
  ascBasic
)
