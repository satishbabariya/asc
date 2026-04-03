add_library(ascBasic
  SourceManager.cpp
  Diagnostic.cpp
  TokenKinds.cpp
)

target_link_libraries(ascBasic
  PUBLIC
  LLVMSupport
)

target_include_directories(ascBasic PUBLIC
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
)
