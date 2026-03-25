add_library(ascDriver
  Driver.cpp
)

target_link_libraries(ascDriver
  PUBLIC
  ascBasic
  ascLex
  ascParse
  ascAST
  ascSema
  ascHIR
  ascAnalysis
  ascCodeGen
  LLVMSupport
)
