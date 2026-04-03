add_executable(asc-compiler
  main.cpp
)

set_target_properties(asc-compiler PROPERTIES OUTPUT_NAME "asc")

target_link_libraries(asc-compiler
  PRIVATE
  ascDriver
)
