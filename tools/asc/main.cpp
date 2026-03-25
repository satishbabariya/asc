#include "asc/Driver/Driver.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"

int main(int argc, char **argv) {
  llvm::InitLLVM initLLVM(argc, argv);

  asc::Driver driver;

  asc::ExitCode ec = driver.parseArgs(argc, argv);
  if (ec != asc::ExitCode::Success)
    return static_cast<int>(ec);

  ec = driver.run();
  return static_cast<int>(ec);
}
