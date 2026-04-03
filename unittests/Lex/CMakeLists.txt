add_executable(ascLexTests
  LexerTest.cpp
)

target_link_libraries(ascLexTests
  PRIVATE
  ascBasic
  ascLex
  gtest
  gtest_main
)

gtest_discover_tests(ascLexTests)
