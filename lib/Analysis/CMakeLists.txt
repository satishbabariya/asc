add_library(ascAnalysis
  LivenessAnalysis.cpp
  RegionInference.cpp
  AliasCheck.cpp
  MoveCheck.cpp
  SendSyncCheck.cpp
  DropInsertion.cpp
  PanicScopeWrap.cpp
)

target_link_libraries(ascAnalysis
  PUBLIC
  ascHIR
  ascBasic
  MLIRIR
  MLIRAnalysis
  MLIRPass
)
