add_executable(ascParseTests
  ParserTest.cpp
)

target_link_libraries(ascParseTests
  PRIVATE
  ascParse
  ascLex
  ascAST
  ascBasic
  gtest
  gtest_main
)

gtest_discover_tests(ascParseTests)
