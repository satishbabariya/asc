add_executable(ascSemaTests
  SemaTest.cpp
)

target_link_libraries(ascSemaTests
  PRIVATE
  ascSema
  ascAST
  ascBasic
  gtest
  gtest_main
)

gtest_discover_tests(ascSemaTests)
