add_executable(ascSemaTests
  SemaTest.cpp
)

target_link_libraries(ascSemaTests
  PRIVATE
  ascSema
  ascParse
  ascLex
  ascAST
  ascBasic
  gtest
  gtest_main
)

gtest_discover_tests(ascSemaTests)
