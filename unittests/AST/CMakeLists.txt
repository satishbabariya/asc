add_executable(ascASTTests
  ASTTest.cpp
)

target_link_libraries(ascASTTests
  PRIVATE
  ascAST
  ascBasic
  gtest
  gtest_main
)

gtest_discover_tests(ascASTTests)
