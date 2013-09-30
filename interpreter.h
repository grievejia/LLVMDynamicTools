#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "main.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Support/DynamicLibrary.h"

#include <ffi.h>

// AllocaHolder - Object to track all of the blocks of memory allocated by
// alloca.  When the function returns, this object is popped off the execution
// stack, which causes the dtor to be run, which frees all the alloca'd memory.
class AllocaHolder {
	friend class AllocaHolderHandle;
	std::vector<void*> Allocations;
	unsigned RefCnt;
public:
	AllocaHolder() : RefCnt(0) {}
	void add(void *mem) { Allocations.push_back(mem); }
	~AllocaHolder() {
		for (unsigned i = 0; i < Allocations.size(); ++i)
			free(Allocations[i]);
	}
};

// AllocaHolderHandle gives AllocaHolder value semantics so we can stick it into a vector...
class AllocaHolderHandle {
	AllocaHolder *H;
public:
	AllocaHolderHandle(): H(new AllocaHolder()) { H->RefCnt++; }
	AllocaHolderHandle(const AllocaHolderHandle &AH): H(AH.H) { H->RefCnt++; }
	~AllocaHolderHandle() { if (--H->RefCnt == 0) delete H; }

	void add(void *mem) { H->add(mem); }
};

typedef std::vector<llvm::GenericValue> ValuePlaneTy;

// ExecutionContext struct - This struct represents one stack frame currently
// executing.
//
struct ExecutionContext {
	llvm::Function             *CurFunction;// The currently executing function
	llvm::BasicBlock           *CurBB;      // The currently executing BB
	llvm::BasicBlock::iterator  CurInst;    // The next instruction to execute
	std::map<llvm::Value*, llvm::GenericValue> Values; // LLVM values used in this invocation
	std::vector<llvm::GenericValue>  VarArgs; // Values passed through an ellipsis
	llvm::CallSite             Caller;     // Holds the call that called subframes.
	                               // NULL if main func or debugger invoked fn
	AllocaHolderHandle    Allocas;    // Track memory allocated by alloca
};

// My own lightweight interpreter implementation
class MyInterpreter: public llvm::ExecutionEngine, public llvm::InstVisitor<MyInterpreter>
{
private:
	llvm::GenericValue exitValue;          // The return value of the called function
	llvm::DataLayout td;
	llvm::IntrinsicLowering *il;

	// The runtime stack of executing code.  The top of the stack is the current function record.
	std::vector<ExecutionContext> ECStack;

	// SwitchToNewBasicBlock - Start execution in a new basic block and run any PHI nodes in the top of the block.  This is used for intraprocedural control flow.
	void SwitchToNewBasicBlock(llvm::BasicBlock *Dest, ExecutionContext &SF);

	llvm::GenericValue getConstantExprValue(llvm::ConstantExpr *CE, ExecutionContext &SF);
	llvm::GenericValue getOperandValue(llvm::Value *V, ExecutionContext &SF);
	llvm::GenericValue executeTruncInst(llvm::Value *SrcVal, llvm::Type *DstTy, ExecutionContext &SF);
	llvm::GenericValue executeSExtInst(llvm::Value *SrcVal, llvm::Type *DstTy, ExecutionContext &SF);
	llvm::GenericValue executeZExtInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeFPTruncInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeFPExtInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeFPToUIInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeFPToSIInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeUIToFPInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeSIToFPInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executePtrToIntInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeIntToPtrInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeBitCastInst(llvm::Value *SrcVal, llvm::Type *DstTy,ExecutionContext &SF);
	llvm::GenericValue executeCastOperation(llvm::Instruction::CastOps opcode, llvm::Value *SrcVal, llvm::Type *Ty, ExecutionContext &SF);
	llvm::GenericValue executeGEPOperation(llvm::Value *Ptr, llvm::gep_type_iterator I,llvm::gep_type_iterator E, ExecutionContext &SF);

	void initializeExternalFunctions();

	// Overrides
	void *getPointerToFunction(llvm::Function *F) { return (void*)F; }
	void *getPointerToBasicBlock(llvm::BasicBlock *BB) { return (void*)BB; }
public:
	explicit MyInterpreter(llvm::Module *M);
	~MyInterpreter();

	// run - Start execution with the specified function and arguments.
	llvm::GenericValue runFunction(llvm::Function *F, const std::vector<llvm::GenericValue> &ArgValues);

	void *getPointerToNamedFunction(const std::string &Name, bool AbortOnFailure = true)
	{
		// Won't implement
		return 0;
	}

	// recompileAndRelinkFunction - For the interpreter, functions are always up-to-date.
	void *recompileAndRelinkFunction(llvm::Function *F)
	{
		return getPointerToFunction(F);
	}

	// freeMachineCodeForFunction - The interpreter does not generate any code.
	void freeMachineCodeForFunction(llvm::Function *F) { }

	// Methods used to execute code:
	// Place a call on the stack
	void callFunction(llvm::Function *F, const std::vector<llvm::GenericValue> &ArgVals);
	llvm::GenericValue callExternalFunction(llvm::Function *F, const std::vector<llvm::GenericValue> &ArgVals);
	void popStackAndReturnValueToCaller(llvm::Type *RetTy, llvm::GenericValue Result);
	void run();                // Execute instructions until nothing left to do

	llvm::GenericValue *getFirstVarArg ()
	{
		return &(ECStack.back ().VarArgs[0]);
	}

	// Opcode Implementations
	void visitReturnInst(llvm::ReturnInst &I);
	void visitBranchInst(llvm::BranchInst &I);
	void visitSwitchInst(llvm::SwitchInst &I);
	void visitIndirectBrInst(llvm::IndirectBrInst &I);

	void visitBinaryOperator(llvm::BinaryOperator &I);
	void visitICmpInst(llvm::ICmpInst &I);
	void visitFCmpInst(llvm::FCmpInst &I);
	void visitAllocaInst(llvm::AllocaInst &I);
	void visitLoadInst(llvm::LoadInst &I);
	void visitStoreInst(llvm::StoreInst &I);
	void visitGetElementPtrInst(llvm::GetElementPtrInst &I);
	void visitPHINode(llvm::PHINode &PN) { 
		llvm_unreachable("PHI nodes already handled!"); 
	}
	void visitTruncInst(llvm::TruncInst &I);
	void visitZExtInst(llvm::ZExtInst &I);
	void visitSExtInst(llvm::SExtInst &I);
	void visitFPTruncInst(llvm::FPTruncInst &I);
	void visitFPExtInst(llvm::FPExtInst &I);
	void visitUIToFPInst(llvm::UIToFPInst &I);
	void visitSIToFPInst(llvm::SIToFPInst &I);
	void visitFPToUIInst(llvm::FPToUIInst &I);
	void visitFPToSIInst(llvm::FPToSIInst &I);
	void visitPtrToIntInst(llvm::PtrToIntInst &I);
	void visitIntToPtrInst(llvm::IntToPtrInst &I);
	void visitBitCastInst(llvm::BitCastInst &I);
	void visitSelectInst(llvm::SelectInst &I);

	void visitCallSite(llvm::CallSite CS);
	void visitCallInst(llvm::CallInst &I) { visitCallSite (llvm::CallSite (&I)); }
	void visitInvokeInst(llvm::InvokeInst &I) { visitCallSite (llvm::CallSite (&I)); }
	void visitUnreachableInst(llvm::UnreachableInst &I);

	void visitShl(llvm::BinaryOperator &I);
	void visitLShr(llvm::BinaryOperator &I);
	void visitAShr(llvm::BinaryOperator &I);

	void visitVAArgInst(llvm::VAArgInst &I);
	void visitExtractElementInst(llvm::ExtractElementInst &I);
	void visitInsertElementInst(llvm::InsertElementInst &I);
	void visitShuffleVectorInst(llvm::ShuffleVectorInst &I);

	void visitExtractValueInst(llvm::ExtractValueInst &I);
	void visitInsertValueInst(llvm::InsertValueInst &I);

	void visitInstruction(llvm::Instruction &I) {
		llvm::errs() << I << "\n";
		llvm_unreachable("Instruction not interpretable yet!");
	}
};

#endif