#include "Interpreter.h"

using namespace llvm;
using namespace llvm_interpreter;

DynamicValue Interpreter::callExternalFunction(const llvm::Function* f, std::vector<DynamicValue>&& argValues)
{
	// FIXME: implement external calls
	return DynamicValue::getUndefValue();
}
