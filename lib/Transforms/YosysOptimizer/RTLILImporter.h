#ifndef HEIR_LIB_TRANSFORMS_YOSYSOPTIMIZER_RTLILIMPORTER_H_
#define HEIR_LIB_TRANSFORMS_YOSYSOPTIMIZER_RTLILIMPORTER_H_

#include "kernel/rtlil.h"                     // from @at_clifford_yosys
#include "llvm/include/llvm/ADT/MapVector.h"  // from @llvm-project
#include "llvm/include/llvm/ADT/StringMap.h"  // from @llvm-project
#include "mlir/include/mlir/Dialect/Func/IR/FuncOps.h"  // from @llvm-project
#include "mlir/include/mlir/IR/ImplicitLocOpBuilder.h"  // from @llvm-project
#include "mlir/include/mlir/IR/MLIRContext.h"           // from @llvm-project
#include "mlir/include/mlir/IR/Operation.h"             // from @llvm-project
#include "mlir/include/mlir/IR/Value.h"                 // from @llvm-project
#include "mlir/include/mlir/Support/LLVM.h"             // from @llvm-project

namespace mlir {
namespace heir {

class RTLILImporter {
 public:
  RTLILImporter(MLIRContext *context) : context(context) {}

  // importModule imports an RTLIL module to an MLIR function using the provided
  // config. cellOrdering is a topologically sorted list of cells that can be
  // used to sequentially create the MLIR representation.
  func::FuncOp importModule(Yosys::RTLIL::Module *module,
                            const SmallVector<std::string, 10> &cellOrdering);

 protected:
  // cellToOp converts an RTLIL cell to an MLIR operation.
  virtual Operation *createOp(Yosys::RTLIL::Cell *cell,
                              SmallVector<Value, 4> &inputs,
                              ImplicitLocOpBuilder &b) const = 0;

  // Returns a list of RTLIL cell inputs.
  virtual SmallVector<Yosys::RTLIL::SigSpec, 4> getInputs(
      Yosys::RTLIL::Cell *cell) const = 0;

  // Returns an RTLIL cell output.
  virtual Yosys::RTLIL::SigSpec getOutput(Yosys::RTLIL::Cell *cell) const = 0;

 private:
  MLIRContext *context;

  llvm::StringMap<Value> wireNameToValue;
  Value getWireValue(Yosys::RTLIL::Wire *wire);
  void addWireValue(Yosys::RTLIL::Wire *wire, Value value);

  // getBit gets the MLIR Value corresponding to the given connection. This
  // assumes that the connection is a single bit.
  Value getBit(const Yosys::RTLIL::SigSpec &conn, ImplicitLocOpBuilder &b);

  // addResultBit assigns an mlir result to the result connection.
  void addResultBit(
      const Yosys::RTLIL::SigSpec &conn, Value result,
      llvm::MapVector<Yosys::RTLIL::Wire *, SmallVector<Value>> &retBitValues);
};

}  // namespace heir
}  // namespace mlir

#endif  // HEIR_LIB_TRANSFORMS_YOSYSOPTIMIZER_RTLILIMPORTER_H_
