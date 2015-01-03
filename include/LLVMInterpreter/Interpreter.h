#ifndef DYNPTS_INTERPRETER_H
#define DYNPTS_INTERPRETER_H

#include "Memory.h"
#include "StackFrame.h"

#include "llvm/IR/DataLayout.h"

namespace llvm
{
	class Module;
	class ConstantExpr;
}

namespace llvm_interpreter
{

class Interpreter
{
private:
	llvm::Module* module;
	llvm::DataLayout dataLayout;

	// The global environment
	std::unordered_map<const llvm::GlobalValue*, Address> globalEnv;
	// The global memory
	MemorySection globalMem;
	// Mapping from function pointer to function
	std::unordered_map<Address, const llvm::Function*> funPtrMap;

	// The runtime stack of executing code.  The top of the stack is the current function record.
	StackFrames stack;
	// The stack memory
	MemorySection stackMem;

	Address allocateStackMem(StackFrame& frame, unsigned size);
	Address allocateGlobalMem(const llvm::GlobalValue* v);

	DynamicValue evaluateConstant(const llvm::Constant*);
	DynamicValue evaluateConstantExpr(const llvm::ConstantExpr*);

	// Setting up the stack frame and execute f
	DynamicValue callFunction(const llvm::Function* f, std::vector<DynamicValue>&& argValues);
	// Assuming that the stack frame is set up, go ahead and execute f
	DynamicValue runFunction(StackFrame& frame);
	// External call handler
	DynamicValue callExternalFunction(const llvm::Function* f, std::vector<DynamicValue>&& argValues);
	// Pop the last stack frame off of the stack before returning to the caller
	void popStack();

	DynamicValue evaluateOperand(const StackFrame& frame, const llvm::Value* v);
	void evaluateInstruction(StackFrame& frame, const llvm::Instruction* inst);
public:
	Interpreter(llvm::Module*);
	~Interpreter();

	void evaluateGlobals();
	int runMain(const llvm::Function* mainFn, const std::vector< std::string> mainArgs);
};

}

#endif
