#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"  // from @googletest
#include "gtest/gtest.h"  // from @googletest
#include "include/Dialect/Comb/IR/CombDialect.h"
#include "include/Dialect/Comb/IR/CombOps.h"
#include "lib/Transforms/YosysOptimizer/LUTImporter.h"
#include "llvm/include/llvm/ADT/STLExtras.h"            // from @llvm-project
#include "llvm/include/llvm/ADT/SmallVector.h"          // from @llvm-project
#include "llvm/include/llvm/Support/Path.h"             // from @llvm-project
#include "mlir/include/mlir//IR/Location.h"             // from @llvm-project
#include "mlir/include/mlir/Dialect/Arith/IR/Arith.h"   // from @llvm-project
#include "mlir/include/mlir/Dialect/Func/IR/FuncOps.h"  // from @llvm-project
#include "mlir/include/mlir/IR/BuiltinAttributes.h"     // from @llvm-project
#include "mlir/include/mlir/IR/BuiltinOps.h"            // from @llvm-project
#include "mlir/include/mlir/IR/MLIRContext.h"           // from @llvm-project
#include "mlir/include/mlir/IR/OwningOpRef.h"           // from @llvm-project
#include "mlir/include/mlir/Support/LLVM.h"             // from @llvm-project

// Block clang-format from reordering
// clang-format off
#include "tools/cpp/runfiles/runfiles.h" // from @bazel_tools
#include "kernel/yosys.h" // from @at_clifford_yosys
// clang-format on

namespace mlir::heir {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;
using ::testing::ElementsAreArray;
using ::testing::Test;

static constexpr std::string_view kWorkspaceDir = "heir";

// Returns a list of cell names that are topologically ordered using the Yosys
// toder output. This is extracted from the lines containing cells in the
// output:
// -- Running command `torder -stop * P*;' --

// 14. Executing TORDER pass (print cells in topological order).
// module test_add
//   cell $abc$167$auto$blifparse.cc:525:parse_blif$168
//   cell $abc$167$auto$blifparse.cc:525:parse_blif$170
//   cell $abc$167$auto$blifparse.cc:525:parse_blif$169
//   cell $abc$167$auto$blifparse.cc:525:parse_blif$171
llvm::SmallVector<std::string, 10> getTopologicalOrder(
    std::stringstream &torderOutput) {
  llvm::SmallVector<std::string, 10> cells;
  std::string line;
  while (std::getline(torderOutput, line)) {
    auto lineCell = line.find("cell ");
    if (lineCell != std::string::npos) {
      cells.push_back(line.substr(lineCell + 5, std::string::npos));
    }
  }
  return cells;
}

class LUTImporterTestFixture : public Test {
 protected:
  void SetUp() override {
    context.loadDialect<heir::comb::CombDialect, arith::ArithDialect,
                        func::FuncDialect>();
    module_ = ModuleOp::create(UnknownLoc::get(&context));
    Yosys::yosys_setup();
  }

  func::FuncOp runImporter(const std::string &rtlil) {
    std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest());
    SmallString<128> workspaceRelativePath;
    llvm::sys::path::append(workspaceRelativePath, kWorkspaceDir, rtlil);
    Yosys::run_pass("read_rtlil " +
                    runfiles->Rlocation(workspaceRelativePath.str().str()));
    Yosys::run_pass("proc; hierarchy -generate lut* o:Y i:P i:*;");

    // Get topological ordering.
    std::stringstream cellOrder;
    Yosys::log_streams.push_back(&cellOrder);
    Yosys::run_pass("torder -stop i P*;");
    Yosys::log_streams.clear();

    LUTImporter lutImporter = LUTImporter(&context);

    auto topologicalOrder = getTopologicalOrder(cellOrder);
    Yosys::RTLIL::Design *design = Yosys::yosys_get_design();
    auto func =
        lutImporter.importModule(design->top_module(), topologicalOrder);
    module_->push_back(func);
    return func;
  }

  void TearDown() override { Yosys::run_pass("delete"); }

  MLIRContext context;

  OwningOpRef<ModuleOp> module_;
};

// Note that we cannot lower truth tables to LLVM, so we must assert the IR
// rather than executing the code.
TEST_F(LUTImporterTestFixture, AddOneLUT3) {
  std::vector<std::vector<bool>> expectedLuts = {
      {0, 1, 1, 0, 0, 0, 0, 0}, {0, 0, 0, 1, 1, 1, 1, 0},
      {0, 0, 0, 0, 0, 0, 0, 1}, {0, 1, 1, 0, 0, 0, 0, 0},
      {0, 0, 0, 1, 1, 1, 1, 0}, {0, 0, 0, 0, 0, 0, 0, 1},
      {0, 1, 1, 0, 0, 0, 0, 0}, {0, 0, 0, 1, 1, 1, 1, 0},
      {0, 0, 0, 0, 0, 0, 0, 1}, {0, 1, 1, 0, 0, 0, 0, 0},
      {1, 0, 0, 0, 0, 0, 0, 0}};

  auto func =
      runImporter("lib/Transforms/YosysOptimizer/tests/add_one_lut3.rtlil");

  auto funcType = func.getFunctionType();
  EXPECT_EQ(funcType.getNumInputs(), 1);
  EXPECT_EQ(funcType.getInput(0).getIntOrFloatBitWidth(), 8);
  EXPECT_EQ(funcType.getNumResults(), 1);
  EXPECT_EQ(funcType.getResult(0).getIntOrFloatBitWidth(), 8);

  auto combOps = func.getOps<comb::TruthTableOp>().begin();
  for (size_t i = 0; i < expectedLuts.size(); i++) {
    SmallVector<bool> table(llvm::map_range(
        (*combOps++).getLookupTableAttr().getAsValueRange<IntegerAttr>(),
        [](const APInt &a) { return !a.isZero(); }));
    EXPECT_THAT(table, ElementsAreArray(expectedLuts[i]));
  }
}

TEST_F(LUTImporterTestFixture, AddOneLUT5) {
  auto func =
      runImporter("lib/Transforms/YosysOptimizer/tests/add_one_lut5.rtlil");

  auto funcType = func.getFunctionType();
  EXPECT_EQ(funcType.getNumInputs(), 1);
  EXPECT_EQ(funcType.getInput(0).getIntOrFloatBitWidth(), 8);
  EXPECT_EQ(funcType.getNumResults(), 1);
  EXPECT_EQ(funcType.getResult(0).getIntOrFloatBitWidth(), 8);

  auto combOps = func.getOps<comb::TruthTableOp>();
  for (auto combOp : combOps) {
    SmallVector<bool> table(llvm::map_range(
        combOp.getLookupTableAttr().getAsValueRange<IntegerAttr>(),
        [](const APInt &a) { return !a.isZero(); }));
    EXPECT_EQ(table.size(), 32);
  }
}

// This test doubles the input, which simply connects the output wire to the
// first bits of the input and a constant.
// comb.concat inputs are written in MSB to LSB ordering. See
// https://circt.llvm.org/docs/Dialects/Comb/RationaleComb/#endianness-operand-ordering-and-internal-representation
TEST_F(LUTImporterTestFixture, DoubleInput) {
  auto func =
      runImporter("lib/Transforms/YosysOptimizer/tests/double_input.rtlil");

  auto funcType = func.getFunctionType();
  EXPECT_EQ(funcType.getNumInputs(), 1);
  EXPECT_EQ(funcType.getInput(0).getIntOrFloatBitWidth(), 8);
  EXPECT_EQ(funcType.getNumResults(), 1);
  EXPECT_EQ(funcType.getResult(0).getIntOrFloatBitWidth(), 8);

  auto returnOp = *func.getOps<func::ReturnOp>().begin();
  auto concatOp = returnOp.getOperands()[0].getDefiningOp<comb::ConcatOp>();
  ASSERT_TRUE(concatOp);
  EXPECT_EQ(concatOp->getNumOperands(), 8);
  arith::ConstantOp constOp =
      concatOp->getOperands()[7].getDefiningOp<arith::ConstantOp>();
  ASSERT_TRUE(constOp);
  auto constVal = dyn_cast<IntegerAttr>(constOp.getValue());
  ASSERT_TRUE(constVal);
  EXPECT_EQ(constVal.getInt(), 0);
}

TEST_F(LUTImporterTestFixture, MultipleInputs) {
  auto func =
      runImporter("lib/Transforms/YosysOptimizer/tests/multiple_inputs.rtlil");

  auto funcType = func.getFunctionType();
  EXPECT_EQ(funcType.getNumInputs(), 2);
  EXPECT_EQ(funcType.getInput(0).getIntOrFloatBitWidth(), 8);
  EXPECT_EQ(funcType.getInput(1).getIntOrFloatBitWidth(), 8);
  EXPECT_EQ(funcType.getNumResults(), 1);
  EXPECT_EQ(funcType.getResult(0).getIntOrFloatBitWidth(), 8);
}

}  // namespace
}  // namespace mlir::heir

// We use a custom main function here to avoid issues with Yosys' main driver.
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
