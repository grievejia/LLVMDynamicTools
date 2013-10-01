#include "interpreter.h"
#include "llvm/Support/Debug.h"
#include "varInit.h"
#include "pts-external.h"

#include <cstring>

using namespace llvm;

static Variable* retVar = NULL;

MyInterpreter::MyInterpreter(Module* module): ExecutionEngine(module), td(module)
{
	memset(&exitValue.Untyped, 0, sizeof(exitValue.Untyped));
	setDataLayout(&td);
	emitGlobals();

	initializeExternalFunctions();

	il = new IntrinsicLowering(td);
}

MyInterpreter::~MyInterpreter() {
	delete il;
}

GenericValue MyInterpreter::runFunction(Function *F, const std::vector<GenericValue> &ArgValues)
{
	assert (F && "Function *F was null at entry to run()");

	// Try extra hard not to pass extra args to a function that isn't
	// expecting them.  C programmers frequently bend the rules and
	// declare main() with fewer parameters than it actually gets
	// passed, and the interpreter barfs if you pass a function more
	// parameters than it is declared to take. This does not attempt to
	// take into account gratuitous differences in declared types,
	// though.
	std::vector<GenericValue> ActualArgs;
	const unsigned ArgCount = F->getFunctionType()->getNumParams();
	for (unsigned i = 0; i < ArgCount; ++i)
		ActualArgs.push_back(ArgValues[i]);

	// Set up the function call.
	callFunction(F, ActualArgs, NULL);

	// Start executing the function.
	run();

	return exitValue;
}

static void SetValue(Value *V, GenericValue Val, ExecutionContext &SF) {
  SF.Values[V] = Val;
}

void MyInterpreter::callFunction(Function *F, const std::vector<GenericValue> &ArgVals, CallSite* cs)
{
	assert((ECStack.empty() || ECStack.back().Caller.getInstruction() == 0 || ECStack.back().Caller.arg_size() == ArgVals.size()) && "Incorrect number of arguments passed into function call!");

	// Make a new stack frame... and fill it in.
	ECStack.push_back(ExecutionContext());
	ExecutionContext &StackFrame = ECStack.back();
	StackFrame.CurFunction = F;

	// Special handling for external functions.
	if (F->isDeclaration()) {
		GenericValue result = callExternalFunction (F, ArgVals);
		if (extInfo.isExternal(F))
		{
			ptsAnalyzeExtFunction(cs);
		}

		// Simulate a 'ret' instruction of the appropriate type.
		popStackAndReturnValueToCaller (F->getReturnType (), result);
		return;
	}

	if (extInfo.isExternal(F))
	{
		ptsAnalyzeExtFunction(cs);
		return;
	}

	// Get pointers to first LLVM BB & Instruction in function.
	StackFrame.CurBB = F->begin();
	StackFrame.CurInst = StackFrame.CurBB->begin();

	// Run through the function arguments and initialize their values...
	assert((ArgVals.size() == F->arg_size() || (ArgVals.size() > F->arg_size() && F->getFunctionType()->isVarArg())) && "Invalid number of values passed to function invocation!");

	// Handle non-varargs arguments...
	unsigned i = 0;
	for (Function::arg_iterator AI = F->arg_begin(), E = F->arg_end(); AI != E; ++AI, ++i)
		SetValue(AI, ArgVals[i], StackFrame);

	// Handle varargs arguments...
	StackFrame.VarArgs.assign(ArgVals.begin()+i, ArgVals.end());

	// pts-to analysis
	// We do the analysis here after the indirect call target is resolved
	if (cs == NULL)
		return;
	CallSite::arg_iterator csItr = cs->arg_begin();
	for (Function::arg_iterator itr = F->arg_begin(), ite = F->arg_end(); itr != ite; ++itr, ++csItr)
	{
		Value* formalArg = itr;
		if (!isa<PointerType>(formalArg->getType()))
			continue;
		Value* actualArg = *csItr;
		Variable* formalVar = variableFactory.getMappedVar(formalArg);
		Variable* actualVar = ptrToVariable(actualArg);
		if (formalVar == NULL)
			llvm_unreachable("formal var is null?");
		PtsSet actualTgt = ptsGraph.lookup(actualVar);
		ptsGraph.update(formalVar, actualTgt);
	}
}

void MyInterpreter::popStackAndReturnValueToCaller(Type *RetTy, GenericValue Result)
{
	// Pop the current stack frame.
	ECStack.pop_back();

	if (ECStack.empty())
	{  // Finished main.  Put result into exit code...
		if (RetTy && !RetTy->isVoidTy()) // Nonvoid return type?
			exitValue = Result;   // Capture the exit value of the program
		else
			memset(&exitValue.Untyped, 0, sizeof(exitValue.Untyped));
	}
	else
	{
		// If we have a previous stack frame, and we have a previous call, fill in the return value...
		ExecutionContext &CallingSF = ECStack.back();
		if (Instruction *I = CallingSF.Caller.getInstruction())
		{
			// Save result...
			if (!CallingSF.Caller.getType()->isVoidTy())
				SetValue(I, Result, CallingSF);
			if (InvokeInst *II = dyn_cast<InvokeInst> (I))
				SwitchToNewBasicBlock (II->getNormalDest (), CallingSF);
			CallingSF.Caller = CallSite();          // We returned from the call...

			// pts-to analysis
			if (retVar != NULL)
			{
				Variable* dst = variableFactory.getMappedVar(I);
				if (dst == NULL)
					llvm_unreachable("ret dst is null?");
				PtsSet retTgt = ptsGraph.lookup(retVar);
				ptsGraph.update(dst, retTgt);
			}
		}
		retVar = NULL;
	}
}

// SwitchToNewBasicBlock - This method is used to jump to a new basic block. This function handles the actual updating of block and instruction iterators as well as execution of all of the PHI nodes in the destination block.
// This method does this because all of the PHI nodes must be executed atomically, reading their inputs before any of the results are updated.  Not doing this can cause problems if the PHI nodes depend on other PHI nodes for their inputs.  If the input PHI node is updated before it is read, incorrect results can happen.  Thus we use a two phase approach.
void MyInterpreter::SwitchToNewBasicBlock(BasicBlock *Dest, ExecutionContext &SF){
	BasicBlock *PrevBB = SF.CurBB;      // Remember where we came from...
	SF.CurBB   = Dest;                  // Update CurBB to branch destination
	SF.CurInst = SF.CurBB->begin();     // Update new instruction ptr...

	if (!isa<PHINode>(SF.CurInst)) return;  // Nothing fancy to do

	// Loop over all of the PHI nodes in the current block, reading their inputs.
	std::vector<GenericValue> ResultValues;

	for (; PHINode *PN = dyn_cast<PHINode>(SF.CurInst); ++SF.CurInst) {
		// Search for the value corresponding to this previous bb...
		int i = PN->getBasicBlockIndex(PrevBB);
		assert(i != -1 && "PHINode doesn't contain entry for predecessor??");
		Value *IncomingValue = PN->getIncomingValue(i);

		// Save the incoming value for this PHI node...
		ResultValues.push_back(getOperandValue(IncomingValue, SF));
	}

	// Now loop over all of the PHI nodes setting their values...
	SF.CurInst = SF.CurBB->begin();
	for (unsigned i = 0; isa<PHINode>(SF.CurInst); ++SF.CurInst, ++i) {
		PHINode *PN = cast<PHINode>(SF.CurInst);
		SetValue(PN, ResultValues[i], SF);
	}
}

//===----------------------------------------------------------------------===//
//                    Binary Instruction Implementations
//===----------------------------------------------------------------------===//

#define IMPLEMENT_BINARY_OPERATOR(OP, TY) \
	case Type::TY##TyID: \
		Dest.TY##Val = Src1.TY##Val OP Src2.TY##Val; \
		break

static void executeFAddInst(GenericValue &Dest, GenericValue Src1, GenericValue Src2, Type *Ty) {
	switch (Ty->getTypeID()) {
		IMPLEMENT_BINARY_OPERATOR(+, Float);
		IMPLEMENT_BINARY_OPERATOR(+, Double);
	default:
		dbgs() << "Unhandled type for FAdd instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFSubInst(GenericValue &Dest, GenericValue Src1, GenericValue Src2, Type *Ty) {
	switch (Ty->getTypeID()) {
		IMPLEMENT_BINARY_OPERATOR(-, Float);
		IMPLEMENT_BINARY_OPERATOR(-, Double);
	default:
		dbgs() << "Unhandled type for FSub instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFMulInst(GenericValue &Dest, GenericValue Src1, GenericValue Src2, Type *Ty) {
	switch (Ty->getTypeID()) {
		IMPLEMENT_BINARY_OPERATOR(*, Float);
		IMPLEMENT_BINARY_OPERATOR(*, Double);
	default:
		dbgs() << "Unhandled type for FMul instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFDivInst(GenericValue &Dest, GenericValue Src1, GenericValue Src2, Type *Ty) {
	switch (Ty->getTypeID()) {
		IMPLEMENT_BINARY_OPERATOR(/, Float);
		IMPLEMENT_BINARY_OPERATOR(/, Double);
	default:
		dbgs() << "Unhandled type for FDiv instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

static void executeFRemInst(GenericValue &Dest, GenericValue Src1, GenericValue Src2, Type *Ty) {
	switch (Ty->getTypeID()) {
	case Type::FloatTyID:
		Dest.FloatVal = fmod(Src1.FloatVal, Src2.FloatVal);
		break;
	case Type::DoubleTyID:
		Dest.DoubleVal = fmod(Src1.DoubleVal, Src2.DoubleVal);
		break;
	default:
		dbgs() << "Unhandled type for Rem instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
}

#define IMPLEMENT_INTEGER_ICMP(OP, TY) \
	case Type::IntegerTyID:  \
		Dest.IntVal = APInt(1,Src1.IntVal.OP(Src2.IntVal)); \
		break;

#define IMPLEMENT_VECTOR_INTEGER_ICMP(OP, TY)                        \
	case Type::VectorTyID: {                                           \
		assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());    \
		Dest.AggregateVal.resize( Src1.AggregateVal.size() );            \
		for( uint32_t _i=0;_i<Src1.AggregateVal.size();_i++)             \
			Dest.AggregateVal[_i].IntVal = APInt(1, Src1.AggregateVal[_i].IntVal.OP(Src2.AggregateVal[_i].IntVal));\
	} break;

// Handle pointers specially because they must be compared with only as much
// width as the host has.  We _do not_ want to be comparing 64 bit values when
// running on a 32-bit target, otherwise the upper 32 bits might mess up
// comparisons if they contain garbage.
#define IMPLEMENT_POINTER_ICMP(OP) \
	case Type::PointerTyID: \
		Dest.IntVal = APInt(1,(void*)(intptr_t)Src1.PointerVal OP \
							(void*)(intptr_t)Src2.PointerVal); \
		break;

static GenericValue executeICMP_EQ(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(eq,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(eq,Ty);
		IMPLEMENT_POINTER_ICMP(==);
	default:
		dbgs() << "Unhandled type for ICMP_EQ predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_NE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(ne,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(ne,Ty);
		IMPLEMENT_POINTER_ICMP(!=);
	default:
		dbgs() << "Unhandled type for ICMP_NE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_ULT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(ult,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(ult,Ty);
		IMPLEMENT_POINTER_ICMP(<);
	default:
		dbgs() << "Unhandled type for ICMP_ULT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SLT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(slt,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(slt,Ty);
		IMPLEMENT_POINTER_ICMP(<);
	default:
		dbgs() << "Unhandled type for ICMP_SLT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_UGT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(ugt,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(ugt,Ty);
		IMPLEMENT_POINTER_ICMP(>);
	default:
		dbgs() << "Unhandled type for ICMP_UGT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SGT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(sgt,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(sgt,Ty);
		IMPLEMENT_POINTER_ICMP(>);
	default:
		dbgs() << "Unhandled type for ICMP_SGT predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_ULE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(ule,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(ule,Ty);
		IMPLEMENT_POINTER_ICMP(<=);
	default:
		dbgs() << "Unhandled type for ICMP_ULE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SLE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(sle,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(sle,Ty);
		IMPLEMENT_POINTER_ICMP(<=);
	default:
		dbgs() << "Unhandled type for ICMP_SLE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_UGE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(uge,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(uge,Ty);
		IMPLEMENT_POINTER_ICMP(>=);
	default:
		dbgs() << "Unhandled type for ICMP_UGE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeICMP_SGE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_INTEGER_ICMP(sge,Ty);
		IMPLEMENT_VECTOR_INTEGER_ICMP(sge,Ty);
		IMPLEMENT_POINTER_ICMP(>=);
	default:
		dbgs() << "Unhandled type for ICMP_SGE predicate: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

#define IMPLEMENT_FCMP(OP, TY) \
	case Type::TY##TyID: \
		Dest.IntVal = APInt(1,Src1.TY##Val OP Src2.TY##Val); \
		break

#define IMPLEMENT_VECTOR_FCMP_T(OP, TY)                             \
	assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());     \
	Dest.AggregateVal.resize( Src1.AggregateVal.size() );             \
	for( uint32_t _i=0;_i<Src1.AggregateVal.size();_i++)              \
		Dest.AggregateVal[_i].IntVal = APInt(1, Src1.AggregateVal[_i].TY##Val OP Src2.AggregateVal[_i].TY##Val);\
	break;

#define IMPLEMENT_VECTOR_FCMP(OP)                                   \
	case Type::VectorTyID:                                            \
	if(dyn_cast<VectorType>(Ty)->getElementType()->isFloatTy()) {   \
		IMPLEMENT_VECTOR_FCMP_T(OP, Float);                           \
	} else {                                                        \
		IMPLEMENT_VECTOR_FCMP_T(OP, Double);                        \
	}

static GenericValue executeFCMP_OEQ(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_FCMP(==, Float);
		IMPLEMENT_FCMP(==, Double);
		IMPLEMENT_VECTOR_FCMP(==);
	default:
		dbgs() << "Unhandled type for FCmp EQ instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

#define IMPLEMENT_SCALAR_NANS(TY, X,Y)                                      \
	if (TY->isFloatTy()) {                                                    \
		if (X.FloatVal != X.FloatVal || Y.FloatVal != Y.FloatVal) {             \
			Dest.IntVal = APInt(1,false);                                         \
			return Dest;                                                          \
		}                                                                       \
	} else {                                                                  \
		if (X.DoubleVal != X.DoubleVal || Y.DoubleVal != Y.DoubleVal) {         \
			Dest.IntVal = APInt(1,false);                                         \
			return Dest;                                                          \
		}                                                                       \
	}

#define MASK_VECTOR_NANS_T(X,Y, TZ, FLAG)                                   \
	assert(X.AggregateVal.size() == Y.AggregateVal.size());                   \
	Dest.AggregateVal.resize( X.AggregateVal.size() );                        \
	for( uint32_t _i=0;_i<X.AggregateVal.size();_i++) {                       \
		if (X.AggregateVal[_i].TZ##Val != X.AggregateVal[_i].TZ##Val || Y.AggregateVal[_i].TZ##Val != Y.AggregateVal[_i].TZ##Val)           \
			Dest.AggregateVal[_i].IntVal = APInt(1,FLAG);                         \
		else  {                                                                 \
			Dest.AggregateVal[_i].IntVal = APInt(1,!FLAG);                        \
		}                                                                       \
	}

#define MASK_VECTOR_NANS(TY, X,Y, FLAG)                                     \
	if (TY->isVectorTy()) {                                                   \
		if (dyn_cast<VectorType>(TY)->getElementType()->isFloatTy()) {          \
			MASK_VECTOR_NANS_T(X, Y, Float, FLAG)                                 \
		} else {                                                                \
			MASK_VECTOR_NANS_T(X, Y, Double, FLAG)                                \
		}                                                                       \
	}                                                                         \



static GenericValue executeFCMP_ONE(GenericValue Src1, GenericValue Src2, Type *Ty)
{
	GenericValue Dest;
	// if input is scalar value and Src1 or Src2 is NaN return false
	IMPLEMENT_SCALAR_NANS(Ty, Src1, Src2)
	// if vector input detect NaNs and fill mask
	MASK_VECTOR_NANS(Ty, Src1, Src2, false)
	GenericValue DestMask = Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_FCMP(!=, Float);
		IMPLEMENT_FCMP(!=, Double);
		IMPLEMENT_VECTOR_FCMP(!=);
	default:
		dbgs() << "Unhandled type for FCmp NE instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	// in vector case mask out NaN elements
	if (Ty->isVectorTy())
		for( size_t _i=0; _i<Src1.AggregateVal.size(); _i++)
			if (DestMask.AggregateVal[_i].IntVal == false)
				Dest.AggregateVal[_i].IntVal = APInt(1,false);

	return Dest;
}

static GenericValue executeFCMP_OLE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_FCMP(<=, Float);
		IMPLEMENT_FCMP(<=, Double);
		IMPLEMENT_VECTOR_FCMP(<=);
	default:
		dbgs() << "Unhandled type for FCmp LE instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_OGE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_FCMP(>=, Float);
		IMPLEMENT_FCMP(>=, Double);
		IMPLEMENT_VECTOR_FCMP(>=);
	default:
		dbgs() << "Unhandled type for FCmp GE instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_OLT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_FCMP(<, Float);
		IMPLEMENT_FCMP(<, Double);
		IMPLEMENT_VECTOR_FCMP(<);
	default:
		dbgs() << "Unhandled type for FCmp LT instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

static GenericValue executeFCMP_OGT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	switch (Ty->getTypeID()) {
		IMPLEMENT_FCMP(>, Float);
		IMPLEMENT_FCMP(>, Double);
		IMPLEMENT_VECTOR_FCMP(>);
	default:
		dbgs() << "Unhandled type for FCmp GT instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}
	return Dest;
}

#define IMPLEMENT_UNORDERED(TY, X,Y)                                     \
	if (TY->isFloatTy()) {                                                 \
		if (X.FloatVal != X.FloatVal || Y.FloatVal != Y.FloatVal) {          \
			Dest.IntVal = APInt(1,true);                                       \
			return Dest;                                                       \
		}                                                                    \
	} else if (X.DoubleVal != X.DoubleVal || Y.DoubleVal != Y.DoubleVal) { \
		Dest.IntVal = APInt(1,true);                                         \
		return Dest;                                                         \
	}

#define IMPLEMENT_VECTOR_UNORDERED(TY, X,Y, _FUNC)                       \
	if (TY->isVectorTy()) {                                                \
		GenericValue DestMask = Dest;                                        \
		Dest = _FUNC(Src1, Src2, Ty);                                        \
		for( size_t _i=0; _i<Src1.AggregateVal.size(); _i++)               \
			if (DestMask.AggregateVal[_i].IntVal == true)                    \
				Dest.AggregateVal[_i].IntVal = APInt(1,true);                  \
		return Dest;                                                       \
	}

static GenericValue executeFCMP_UEQ(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
	MASK_VECTOR_NANS(Ty, Src1, Src2, true)
	IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OEQ)
	return executeFCMP_OEQ(Src1, Src2, Ty);

}

static GenericValue executeFCMP_UNE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
	MASK_VECTOR_NANS(Ty, Src1, Src2, true)
	IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_ONE)
	return executeFCMP_ONE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ULE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
	MASK_VECTOR_NANS(Ty, Src1, Src2, true)
	IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OLE)
	return executeFCMP_OLE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UGE(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
	MASK_VECTOR_NANS(Ty, Src1, Src2, true)
	IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OGE)
	return executeFCMP_OGE(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ULT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
	MASK_VECTOR_NANS(Ty, Src1, Src2, true)
	IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OLT)
	return executeFCMP_OLT(Src1, Src2, Ty);
}

static GenericValue executeFCMP_UGT(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	IMPLEMENT_UNORDERED(Ty, Src1, Src2)
	MASK_VECTOR_NANS(Ty, Src1, Src2, true)
	IMPLEMENT_VECTOR_UNORDERED(Ty, Src1, Src2, executeFCMP_OGT)
	return executeFCMP_OGT(Src1, Src2, Ty);
}

static GenericValue executeFCMP_ORD(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	if(Ty->isVectorTy()) {
		assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
		Dest.AggregateVal.resize( Src1.AggregateVal.size() );
		if(dyn_cast<VectorType>(Ty)->getElementType()->isFloatTy()) {
			for( size_t _i=0;_i<Src1.AggregateVal.size();_i++)
				Dest.AggregateVal[_i].IntVal = APInt(1,
					( (Src1.AggregateVal[_i].FloatVal ==
					Src1.AggregateVal[_i].FloatVal) &&
					(Src2.AggregateVal[_i].FloatVal ==
					Src2.AggregateVal[_i].FloatVal)));
		} else {
			for( size_t _i=0;_i<Src1.AggregateVal.size();_i++)
				Dest.AggregateVal[_i].IntVal = APInt(1,
					( (Src1.AggregateVal[_i].DoubleVal ==
					Src1.AggregateVal[_i].DoubleVal) &&
					(Src2.AggregateVal[_i].DoubleVal ==
					Src2.AggregateVal[_i].DoubleVal)));
		}
	} else if (Ty->isFloatTy())
		Dest.IntVal = APInt(1,(Src1.FloatVal == Src1.FloatVal && Src2.FloatVal == Src2.FloatVal));
	else {
		Dest.IntVal = APInt(1,(Src1.DoubleVal == Src1.DoubleVal && Src2.DoubleVal == Src2.DoubleVal));
	}
	return Dest;
}

static GenericValue executeFCMP_UNO(GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Dest;
	if(Ty->isVectorTy()) {
		assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
		Dest.AggregateVal.resize( Src1.AggregateVal.size() );
		if(dyn_cast<VectorType>(Ty)->getElementType()->isFloatTy()) {
			for( size_t _i=0;_i<Src1.AggregateVal.size();_i++)
				Dest.AggregateVal[_i].IntVal = APInt(1,
					( (Src1.AggregateVal[_i].FloatVal !=
					Src1.AggregateVal[_i].FloatVal) ||
					(Src2.AggregateVal[_i].FloatVal !=
					Src2.AggregateVal[_i].FloatVal)));
		} else {
			for( size_t _i=0;_i<Src1.AggregateVal.size();_i++)
				Dest.AggregateVal[_i].IntVal = APInt(1,
					( (Src1.AggregateVal[_i].DoubleVal !=
					 Src1.AggregateVal[_i].DoubleVal) ||
					(Src2.AggregateVal[_i].DoubleVal !=
					 Src2.AggregateVal[_i].DoubleVal)));
		}
	} else if (Ty->isFloatTy())
		Dest.IntVal = APInt(1,(Src1.FloatVal != Src1.FloatVal || Src2.FloatVal != Src2.FloatVal));
	else {
		Dest.IntVal = APInt(1,(Src1.DoubleVal != Src1.DoubleVal || Src2.DoubleVal != Src2.DoubleVal));
	}
	return Dest;
}

static GenericValue executeFCMP_BOOL(GenericValue Src1, GenericValue Src2, const Type *Ty, const bool val) {
	GenericValue Dest;
	if(Ty->isVectorTy()) {
		assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
		Dest.AggregateVal.resize( Src1.AggregateVal.size() );
		for( size_t _i=0; _i<Src1.AggregateVal.size(); _i++)
			Dest.AggregateVal[_i].IntVal = APInt(1,val);
	} else {
		Dest.IntVal = APInt(1, val);
	}

	return Dest;
}

static GenericValue executeCmpInst(unsigned predicate, GenericValue Src1, GenericValue Src2, Type *Ty) {
	GenericValue Result;
	switch (predicate) {
		case ICmpInst::ICMP_EQ:    return executeICMP_EQ(Src1, Src2, Ty);
		case ICmpInst::ICMP_NE:    return executeICMP_NE(Src1, Src2, Ty);
		case ICmpInst::ICMP_UGT:   return executeICMP_UGT(Src1, Src2, Ty);
		case ICmpInst::ICMP_SGT:   return executeICMP_SGT(Src1, Src2, Ty);
		case ICmpInst::ICMP_ULT:   return executeICMP_ULT(Src1, Src2, Ty);
		case ICmpInst::ICMP_SLT:   return executeICMP_SLT(Src1, Src2, Ty);
		case ICmpInst::ICMP_UGE:   return executeICMP_UGE(Src1, Src2, Ty);
		case ICmpInst::ICMP_SGE:   return executeICMP_SGE(Src1, Src2, Ty);
		case ICmpInst::ICMP_ULE:   return executeICMP_ULE(Src1, Src2, Ty);
		case ICmpInst::ICMP_SLE:   return executeICMP_SLE(Src1, Src2, Ty);
		case FCmpInst::FCMP_ORD:   return executeFCMP_ORD(Src1, Src2, Ty);
		case FCmpInst::FCMP_UNO:   return executeFCMP_UNO(Src1, Src2, Ty);
		case FCmpInst::FCMP_OEQ:   return executeFCMP_OEQ(Src1, Src2, Ty);
		case FCmpInst::FCMP_UEQ:   return executeFCMP_UEQ(Src1, Src2, Ty);
		case FCmpInst::FCMP_ONE:   return executeFCMP_ONE(Src1, Src2, Ty);
		case FCmpInst::FCMP_UNE:   return executeFCMP_UNE(Src1, Src2, Ty);
		case FCmpInst::FCMP_OLT:   return executeFCMP_OLT(Src1, Src2, Ty);
		case FCmpInst::FCMP_ULT:   return executeFCMP_ULT(Src1, Src2, Ty);
		case FCmpInst::FCMP_OGT:   return executeFCMP_OGT(Src1, Src2, Ty);
		case FCmpInst::FCMP_UGT:   return executeFCMP_UGT(Src1, Src2, Ty);
		case FCmpInst::FCMP_OLE:   return executeFCMP_OLE(Src1, Src2, Ty);
		case FCmpInst::FCMP_ULE:   return executeFCMP_ULE(Src1, Src2, Ty);
		case FCmpInst::FCMP_OGE:   return executeFCMP_OGE(Src1, Src2, Ty);
		case FCmpInst::FCMP_UGE:   return executeFCMP_UGE(Src1, Src2, Ty);
		case FCmpInst::FCMP_FALSE: return executeFCMP_BOOL(Src1, Src2, Ty, false);
		case FCmpInst::FCMP_TRUE:  return executeFCMP_BOOL(Src1, Src2, Ty, true);
		default:
			dbgs() << "Unhandled Cmp predicate\n";
			llvm_unreachable(0);
	}
}

static GenericValue executeSelectInst(GenericValue Src1, GenericValue Src2, GenericValue Src3, const Type *Ty)
{
	GenericValue Dest;
	if(Ty->isVectorTy()) {
		assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
		assert(Src2.AggregateVal.size() == Src3.AggregateVal.size());
		Dest.AggregateVal.resize( Src1.AggregateVal.size() );
		for (size_t i = 0; i < Src1.AggregateVal.size(); ++i)
			Dest.AggregateVal[i] = (Src1.AggregateVal[i].IntVal == 0) ? Src3.AggregateVal[i] : Src2.AggregateVal[i];
	} else {
		Dest = (Src1.IntVal == 0) ? Src3 : Src2;
	}
	return Dest;
}

GenericValue MyInterpreter::getConstantExprValue (ConstantExpr *CE, ExecutionContext &SF) {
	switch (CE->getOpcode()) {
	case Instruction::Trunc:
		return executeTruncInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::ZExt:
		return executeZExtInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::SExt:
		return executeSExtInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::FPTrunc:
		return executeFPTruncInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::FPExt:
		return executeFPExtInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::UIToFP:
		return executeUIToFPInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::SIToFP:
		return executeSIToFPInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::FPToUI:
		return executeFPToUIInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::FPToSI:
		return executeFPToSIInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::PtrToInt:
		return executePtrToIntInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::IntToPtr:
		return executeIntToPtrInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::BitCast:
		return executeBitCastInst(CE->getOperand(0), CE->getType(), SF);
	case Instruction::GetElementPtr:
		return executeGEPOperation(CE->getOperand(0), gep_type_begin(CE), gep_type_end(CE), SF);
	case Instruction::FCmp:
	case Instruction::ICmp:
		return executeCmpInst(CE->getPredicate(),
					getOperandValue(CE->getOperand(0), SF),
					getOperandValue(CE->getOperand(1), SF),
					CE->getOperand(0)->getType());
	case Instruction::Select:
		return executeSelectInst(getOperandValue(CE->getOperand(0), SF),
					getOperandValue(CE->getOperand(1), SF),
					getOperandValue(CE->getOperand(2), SF),
					CE->getOperand(0)->getType());
	default :
		break;
	}

	// The cases below here require a GenericValue parameter for the result
	// so we initialize one, compute it and then return it.
	GenericValue Op0 = getOperandValue(CE->getOperand(0), SF);
	GenericValue Op1 = getOperandValue(CE->getOperand(1), SF);
	GenericValue Dest;
	Type * Ty = CE->getOperand(0)->getType();
	switch (CE->getOpcode()) {
	case Instruction::Add:  Dest.IntVal = Op0.IntVal + Op1.IntVal; break;
	case Instruction::Sub:  Dest.IntVal = Op0.IntVal - Op1.IntVal; break;
	case Instruction::Mul:  Dest.IntVal = Op0.IntVal * Op1.IntVal; break;
	case Instruction::FAdd: executeFAddInst(Dest, Op0, Op1, Ty); break;
	case Instruction::FSub: executeFSubInst(Dest, Op0, Op1, Ty); break;
	case Instruction::FMul: executeFMulInst(Dest, Op0, Op1, Ty); break;
	case Instruction::FDiv: executeFDivInst(Dest, Op0, Op1, Ty); break;
	case Instruction::FRem: executeFRemInst(Dest, Op0, Op1, Ty); break;
	case Instruction::SDiv: Dest.IntVal = Op0.IntVal.sdiv(Op1.IntVal); break;
	case Instruction::UDiv: Dest.IntVal = Op0.IntVal.udiv(Op1.IntVal); break;
	case Instruction::URem: Dest.IntVal = Op0.IntVal.urem(Op1.IntVal); break;
	case Instruction::SRem: Dest.IntVal = Op0.IntVal.srem(Op1.IntVal); break;
	case Instruction::And:  Dest.IntVal = Op0.IntVal & Op1.IntVal; break;
	case Instruction::Or:   Dest.IntVal = Op0.IntVal | Op1.IntVal; break;
	case Instruction::Xor:  Dest.IntVal = Op0.IntVal ^ Op1.IntVal; break;
	case Instruction::Shl:  
		Dest.IntVal = Op0.IntVal.shl(Op1.IntVal.getZExtValue());
		break;
	case Instruction::LShr: 
		Dest.IntVal = Op0.IntVal.lshr(Op1.IntVal.getZExtValue());
		break;
	case Instruction::AShr: 
		Dest.IntVal = Op0.IntVal.ashr(Op1.IntVal.getZExtValue());
		break;
	default:
		dbgs() << "Unhandled ConstantExpr: " << *CE << "\n";
		llvm_unreachable("Unhandled ConstantExpr");
	}
	return Dest;
}

GenericValue MyInterpreter::getOperandValue(Value *V, ExecutionContext &SF) {
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
		return getConstantExprValue(CE, SF);
	} else if (Constant *CPV = dyn_cast<Constant>(V)) {
		return getConstantValue(CPV);
	} else if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
		return PTOGV(getPointerToGlobal(GV));
	} else {
		return SF.Values[V];
	}
}

GenericValue MyInterpreter::executeTruncInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	Type *SrcTy = SrcVal->getType();
	if (SrcTy->isVectorTy()) {
		Type *DstVecTy = DstTy->getScalarType();
		unsigned DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
		unsigned NumElts = Src.AggregateVal.size();
		// the sizes of src and dst vectors must be equal
		Dest.AggregateVal.resize(NumElts);
		for (unsigned i = 0; i < NumElts; i++)
			Dest.AggregateVal[i].IntVal = Src.AggregateVal[i].IntVal.trunc(DBitWidth);
	} else {
		IntegerType *DITy = cast<IntegerType>(DstTy);
		unsigned DBitWidth = DITy->getBitWidth();
		Dest.IntVal = Src.IntVal.trunc(DBitWidth);
	}
	return Dest;
}

GenericValue MyInterpreter::executeSExtInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	const Type *SrcTy = SrcVal->getType();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	if (SrcTy->isVectorTy()) {
		const Type *DstVecTy = DstTy->getScalarType();
		unsigned DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
		unsigned size = Src.AggregateVal.size();
		// the sizes of src and dst vectors must be equal.
		Dest.AggregateVal.resize(size);
		for (unsigned i = 0; i < size; i++)
			Dest.AggregateVal[i].IntVal = Src.AggregateVal[i].IntVal.sext(DBitWidth);
	} else {
		const IntegerType *DITy = cast<IntegerType>(DstTy);
		unsigned DBitWidth = DITy->getBitWidth();
		Dest.IntVal = Src.IntVal.sext(DBitWidth);
	}
	return Dest;
}

GenericValue MyInterpreter::executeZExtInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	const Type *SrcTy = SrcVal->getType();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	if (SrcTy->isVectorTy()) {
		const Type *DstVecTy = DstTy->getScalarType();
		unsigned DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();

		unsigned size = Src.AggregateVal.size();
		// the sizes of src and dst vectors must be equal.
		Dest.AggregateVal.resize(size);
		for (unsigned i = 0; i < size; i++)
			Dest.AggregateVal[i].IntVal = Src.AggregateVal[i].IntVal.zext(DBitWidth);
	} else {
		const IntegerType *DITy = cast<IntegerType>(DstTy);
		unsigned DBitWidth = DITy->getBitWidth();
		Dest.IntVal = Src.IntVal.zext(DBitWidth);
	}
	return Dest;
}

GenericValue MyInterpreter::executeFPTruncInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);

	if (SrcVal->getType()->getTypeID() == Type::VectorTyID) {
	assert(SrcVal->getType()->getScalarType()->isDoubleTy() && DstTy->getScalarType()->isFloatTy() && "Invalid FPTrunc instruction");

	unsigned size = Src.AggregateVal.size();
	// the sizes of src and dst vectors must be equal.
	Dest.AggregateVal.resize(size);
	for (unsigned i = 0; i < size; i++)
		Dest.AggregateVal[i].FloatVal = (float)Src.AggregateVal[i].DoubleVal;
	} else {
		assert(SrcVal->getType()->isDoubleTy() && DstTy->isFloatTy() && "Invalid FPTrunc instruction");
		Dest.FloatVal = (float)Src.DoubleVal;
	}

	return Dest;
}

GenericValue MyInterpreter::executeFPExtInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);

	if (SrcVal->getType()->getTypeID() == Type::VectorTyID) {
	assert(SrcVal->getType()->getScalarType()->isFloatTy() && DstTy->getScalarType()->isDoubleTy() && "Invalid FPExt instruction");

	unsigned size = Src.AggregateVal.size();
	// the sizes of src and dst vectors must be equal.
	Dest.AggregateVal.resize(size);
	for (unsigned i = 0; i < size; i++)
		Dest.AggregateVal[i].DoubleVal = (double)Src.AggregateVal[i].FloatVal;
	} else {
		assert(SrcVal->getType()->isFloatTy() && DstTy->isDoubleTy() && "Invalid FPExt instruction");
		Dest.DoubleVal = (double)Src.FloatVal;
	}

	return Dest;
}

GenericValue MyInterpreter::executeFPToUIInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	Type *SrcTy = SrcVal->getType();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);

	if (SrcTy->getTypeID() == Type::VectorTyID) {
		const Type *DstVecTy = DstTy->getScalarType();
		const Type *SrcVecTy = SrcTy->getScalarType();
		uint32_t DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
		unsigned size = Src.AggregateVal.size();
		// the sizes of src and dst vectors must be equal.
		Dest.AggregateVal.resize(size);

		if (SrcVecTy->getTypeID() == Type::FloatTyID) {
			assert(SrcVecTy->isFloatingPointTy() && "Invalid FPToUI instruction");
			for (unsigned i = 0; i < size; i++)
				Dest.AggregateVal[i].IntVal = APIntOps::RoundFloatToAPInt(Src.AggregateVal[i].FloatVal, DBitWidth);
		} else {
		for (unsigned i = 0; i < size; i++)
			Dest.AggregateVal[i].IntVal = APIntOps::RoundDoubleToAPInt(Src.AggregateVal[i].DoubleVal, DBitWidth);
		}
	} else {
		// scalar
		uint32_t DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
		assert(SrcTy->isFloatingPointTy() && "Invalid FPToUI instruction");

		if (SrcTy->getTypeID() == Type::FloatTyID)
			Dest.IntVal = APIntOps::RoundFloatToAPInt(Src.FloatVal, DBitWidth);
		else {
			Dest.IntVal = APIntOps::RoundDoubleToAPInt(Src.DoubleVal, DBitWidth);
		}
	}

	return Dest;
}

GenericValue MyInterpreter::executeFPToSIInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	Type *SrcTy = SrcVal->getType();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);

	if (SrcTy->getTypeID() == Type::VectorTyID) {
		const Type *DstVecTy = DstTy->getScalarType();
		const Type *SrcVecTy = SrcTy->getScalarType();
		uint32_t DBitWidth = cast<IntegerType>(DstVecTy)->getBitWidth();
		unsigned size = Src.AggregateVal.size();
		// the sizes of src and dst vectors must be equal
		Dest.AggregateVal.resize(size);

		if (SrcVecTy->getTypeID() == Type::FloatTyID) {
			assert(SrcVecTy->isFloatingPointTy() && "Invalid FPToSI instruction");
			for (unsigned i = 0; i < size; i++)
				Dest.AggregateVal[i].IntVal = APIntOps::RoundFloatToAPInt(Src.AggregateVal[i].FloatVal, DBitWidth);
		} else {
			for (unsigned i = 0; i < size; i++)
				Dest.AggregateVal[i].IntVal = APIntOps::RoundDoubleToAPInt(Src.AggregateVal[i].DoubleVal, DBitWidth);
		}
	} else {
		// scalar
		unsigned DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
		assert(SrcTy->isFloatingPointTy() && "Invalid FPToSI instruction");

		if (SrcTy->getTypeID() == Type::FloatTyID)
			Dest.IntVal = APIntOps::RoundFloatToAPInt(Src.FloatVal, DBitWidth);
		else {
			Dest.IntVal = APIntOps::RoundDoubleToAPInt(Src.DoubleVal, DBitWidth);
		}
	}
	return Dest;
}

GenericValue MyInterpreter::executeUIToFPInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);

	if (SrcVal->getType()->getTypeID() == Type::VectorTyID) {
		const Type *DstVecTy = DstTy->getScalarType();
		unsigned size = Src.AggregateVal.size();
		// the sizes of src and dst vectors must be equal
		Dest.AggregateVal.resize(size);

		if (DstVecTy->getTypeID() == Type::FloatTyID) {
			assert(DstVecTy->isFloatingPointTy() && "Invalid UIToFP instruction");
			for (unsigned i = 0; i < size; i++)
				Dest.AggregateVal[i].FloatVal = APIntOps::RoundAPIntToFloat(Src.AggregateVal[i].IntVal);
		} else {
			for (unsigned i = 0; i < size; i++)
				Dest.AggregateVal[i].DoubleVal = APIntOps::RoundAPIntToDouble(Src.AggregateVal[i].IntVal);
		}
	} else {
		// scalar
		assert(DstTy->isFloatingPointTy() && "Invalid UIToFP instruction");
		if (DstTy->getTypeID() == Type::FloatTyID)
			Dest.FloatVal = APIntOps::RoundAPIntToFloat(Src.IntVal);
		else {
			Dest.DoubleVal = APIntOps::RoundAPIntToDouble(Src.IntVal);
		}
	}
	return Dest;
}

GenericValue MyInterpreter::executeSIToFPInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);

	if (SrcVal->getType()->getTypeID() == Type::VectorTyID) {
		const Type *DstVecTy = DstTy->getScalarType();
		unsigned size = Src.AggregateVal.size();
		// the sizes of src and dst vectors must be equal
		Dest.AggregateVal.resize(size);

		if (DstVecTy->getTypeID() == Type::FloatTyID) {
			assert(DstVecTy->isFloatingPointTy() && "Invalid SIToFP instruction");
			for (unsigned i = 0; i < size; i++)
				Dest.AggregateVal[i].FloatVal = APIntOps::RoundSignedAPIntToFloat(Src.AggregateVal[i].IntVal);
		} else {
			for (unsigned i = 0; i < size; i++)
				Dest.AggregateVal[i].DoubleVal = APIntOps::RoundSignedAPIntToDouble(Src.AggregateVal[i].IntVal);
		}
	} else {
		// scalar
		assert(DstTy->isFloatingPointTy() && "Invalid SIToFP instruction");

		if (DstTy->getTypeID() == Type::FloatTyID)
			Dest.FloatVal = APIntOps::RoundSignedAPIntToFloat(Src.IntVal);
		else {
			Dest.DoubleVal = APIntOps::RoundSignedAPIntToDouble(Src.IntVal);
		}
	}

	return Dest;
}

GenericValue MyInterpreter::executePtrToIntInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	uint32_t DBitWidth = cast<IntegerType>(DstTy)->getBitWidth();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	assert(SrcVal->getType()->isPointerTy() && "Invalid PtrToInt instruction");

	Dest.IntVal = APInt(DBitWidth, (intptr_t) Src.PointerVal);
	return Dest;
}

GenericValue MyInterpreter::executeIntToPtrInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);
	assert(DstTy->isPointerTy() && "Invalid PtrToInt instruction");

	uint32_t PtrSize = td.getPointerSizeInBits();
	if (PtrSize != Src.IntVal.getBitWidth())
		Src.IntVal = Src.IntVal.zextOrTrunc(PtrSize);

	Dest.PointerVal = PointerTy(intptr_t(Src.IntVal.getZExtValue()));
	return Dest;
}

GenericValue MyInterpreter::executeBitCastInst(Value *SrcVal, Type *DstTy, ExecutionContext &SF) {

	// This instruction supports bitwise conversion of vectors to integers and
	// to vectors of other types (as long as they have the same size)
	Type *SrcTy = SrcVal->getType();
	GenericValue Dest, Src = getOperandValue(SrcVal, SF);

	if ((SrcTy->getTypeID() == Type::VectorTyID) || (DstTy->getTypeID() == Type::VectorTyID)) {
		// vector src bitcast to vector dst or vector src bitcast to scalar dst or
		// scalar src bitcast to vector dst
		bool isLittleEndian = td.isLittleEndian();
		GenericValue TempDst, TempSrc, SrcVec;
		const Type *SrcElemTy;
		const Type *DstElemTy;
		unsigned SrcBitSize;
		unsigned DstBitSize;
		unsigned SrcNum;
		unsigned DstNum;

		if (SrcTy->getTypeID() == Type::VectorTyID) {
			SrcElemTy = SrcTy->getScalarType();
			SrcBitSize = SrcTy->getScalarSizeInBits();
			SrcNum = Src.AggregateVal.size();
			SrcVec = Src;
		} else {
			// if src is scalar value, make it vector <1 x type>
			SrcElemTy = SrcTy;
			SrcBitSize = SrcTy->getPrimitiveSizeInBits();
			SrcNum = 1;
			SrcVec.AggregateVal.push_back(Src);
		}

		if (DstTy->getTypeID() == Type::VectorTyID) {
			DstElemTy = DstTy->getScalarType();
			DstBitSize = DstTy->getScalarSizeInBits();
			DstNum = (SrcNum * SrcBitSize) / DstBitSize;
		} else {
			DstElemTy = DstTy;
			DstBitSize = DstTy->getPrimitiveSizeInBits();
			DstNum = 1;
		}

		if (SrcNum * SrcBitSize != DstNum * DstBitSize)
			llvm_unreachable("Invalid BitCast");

		// If src is floating point, cast to integer first.
		TempSrc.AggregateVal.resize(SrcNum);
		if (SrcElemTy->isFloatTy()) {
			for (unsigned i = 0; i < SrcNum; i++)
				TempSrc.AggregateVal[i].IntVal = APInt::floatToBits(SrcVec.AggregateVal[i].FloatVal);

		} else if (SrcElemTy->isDoubleTy()) {
			for (unsigned i = 0; i < SrcNum; i++)
				TempSrc.AggregateVal[i].IntVal = APInt::doubleToBits(SrcVec.AggregateVal[i].DoubleVal);
		} else if (SrcElemTy->isIntegerTy()) {
			for (unsigned i = 0; i < SrcNum; i++)
				TempSrc.AggregateVal[i].IntVal = SrcVec.AggregateVal[i].IntVal;
		} else {
			// Pointers are not allowed as the element type of vector.
			llvm_unreachable("Invalid Bitcast");
		}

		// now TempSrc is integer type vector
		if (DstNum < SrcNum) {
			// Example: bitcast <4 x i32> <i32 0, i32 1, i32 2, i32 3> to <2 x i64>
			unsigned Ratio = SrcNum / DstNum;
			unsigned SrcElt = 0;
			for (unsigned i = 0; i < DstNum; i++) {
				GenericValue Elt;
				Elt.IntVal = 0;
				Elt.IntVal = Elt.IntVal.zext(DstBitSize);
				unsigned ShiftAmt = isLittleEndian ? 0 : SrcBitSize * (Ratio - 1);
				for (unsigned j = 0; j < Ratio; j++) {
					APInt Tmp;
					Tmp = Tmp.zext(SrcBitSize);
					Tmp = TempSrc.AggregateVal[SrcElt++].IntVal;
					Tmp = Tmp.zext(DstBitSize);
					Tmp = Tmp.shl(ShiftAmt);
					ShiftAmt += isLittleEndian ? SrcBitSize : -SrcBitSize;
					Elt.IntVal |= Tmp;
				}
				TempDst.AggregateVal.push_back(Elt);
			}
		} else {
		  // Example: bitcast <2 x i64> <i64 0, i64 1> to <4 x i32>
			unsigned Ratio = DstNum / SrcNum;
			for (unsigned i = 0; i < SrcNum; i++) {
				unsigned ShiftAmt = isLittleEndian ? 0 : DstBitSize * (Ratio - 1);
				for (unsigned j = 0; j < Ratio; j++) {
					GenericValue Elt;
					Elt.IntVal = Elt.IntVal.zext(SrcBitSize);
					Elt.IntVal = TempSrc.AggregateVal[i].IntVal;
					Elt.IntVal = Elt.IntVal.lshr(ShiftAmt);
					// it could be DstBitSize == SrcBitSize, so check it
					if (DstBitSize < SrcBitSize)
						Elt.IntVal = Elt.IntVal.trunc(DstBitSize);
					ShiftAmt += isLittleEndian ? DstBitSize : -DstBitSize;
					TempDst.AggregateVal.push_back(Elt);
				}
			}
		}

		// convert result from integer to specified type
		if (DstTy->getTypeID() == Type::VectorTyID) {
			if (DstElemTy->isDoubleTy()) {
			Dest.AggregateVal.resize(DstNum);
			for (unsigned i = 0; i < DstNum; i++)
				Dest.AggregateVal[i].DoubleVal = TempDst.AggregateVal[i].IntVal.bitsToDouble();
			} else if (DstElemTy->isFloatTy()) {
				Dest.AggregateVal.resize(DstNum);
				for (unsigned i = 0; i < DstNum; i++)
					Dest.AggregateVal[i].FloatVal = TempDst.AggregateVal[i].IntVal.bitsToFloat();
			} else {
				Dest = TempDst;
			}
		} else {
			if (DstElemTy->isDoubleTy())
				Dest.DoubleVal = TempDst.AggregateVal[0].IntVal.bitsToDouble();
			else if (DstElemTy->isFloatTy()) {
				Dest.FloatVal = TempDst.AggregateVal[0].IntVal.bitsToFloat();
			} else {
				Dest.IntVal = TempDst.AggregateVal[0].IntVal;
			}
		}
	} else {
		// scalar src bitcast to scalar dst
		if (DstTy->isPointerTy()) {
			assert(SrcTy->isPointerTy() && "Invalid BitCast");
			Dest.PointerVal = Src.PointerVal;
		} else if (DstTy->isIntegerTy()) {
			if (SrcTy->isFloatTy())
				Dest.IntVal = APInt::floatToBits(Src.FloatVal);
			else if (SrcTy->isDoubleTy()) {
				Dest.IntVal = APInt::doubleToBits(Src.DoubleVal);
			} else if (SrcTy->isIntegerTy()) {
				Dest.IntVal = Src.IntVal;
			} else {
				llvm_unreachable("Invalid BitCast");
			}
		} else if (DstTy->isFloatTy()) {
			if (SrcTy->isIntegerTy())
				Dest.FloatVal = Src.IntVal.bitsToFloat();
			else {
				Dest.FloatVal = Src.FloatVal;
			}
		} else if (DstTy->isDoubleTy()) {
			if (SrcTy->isIntegerTy())
				Dest.DoubleVal = Src.IntVal.bitsToDouble();
			else {
				Dest.DoubleVal = Src.DoubleVal;
			}
		} else {
			llvm_unreachable("Invalid Bitcast");
		}
	}

	return Dest;
}

GenericValue MyInterpreter::executeGEPOperation(Value *Ptr, gep_type_iterator I, gep_type_iterator E,ExecutionContext &SF) {
  assert(Ptr->getType()->isPointerTy() && "Cannot getElementOffset of a nonpointer type!");

  uint64_t Total = 0;

	for (; I != E; ++I) {
		if (StructType *STy = dyn_cast<StructType>(*I)) {
			const StructLayout *SLO = td.getStructLayout(STy);

			const ConstantInt *CPU = cast<ConstantInt>(I.getOperand());
			unsigned Index = unsigned(CPU->getZExtValue());

			Total += SLO->getElementOffset(Index);
		} else {
			SequentialType *ST = cast<SequentialType>(*I);
			// Get the index number for the array... which must be long type...
			GenericValue IdxGV = getOperandValue(I.getOperand(), SF);

			int64_t Idx;
			unsigned BitWidth = cast<IntegerType>(I.getOperand()->getType())->getBitWidth();
			if (BitWidth == 32)
				Idx = (int64_t)(int32_t)IdxGV.IntVal.getZExtValue();
			else {
				assert(BitWidth == 64 && "Invalid index type for getelementptr");
				Idx = (int64_t)IdxGV.IntVal.getZExtValue();
			}
			Total += td.getTypeAllocSize(ST->getElementType())*Idx;
		}
	}

	GenericValue Result;
	Result.PointerVal = ((char*)getOperandValue(Ptr, SF).PointerVal) + Total;
	DEBUG(dbgs() << "GEP Index " << Total << " bytes.\n");
	return Result;
}

void MyInterpreter::run()
{
	while (!ECStack.empty()) {
		// Interpret a single instruction & increment the "PC".
		ExecutionContext &SF = ECStack.back();  // Current stack frame
		Instruction &I = *SF.CurInst++;         // Increment before execute

		// Track the number of dynamic instructions executed.
		//++NumDynamicInsts;

		errs() << "About to interpret: " << I << "\n";
		visit(I);
		ptsGraph.printPtsSets();
		errs() << "\n";
	}
}

void MyInterpreter::visitICmpInst(ICmpInst &I) {
	ExecutionContext &SF = ECStack.back();
	Type *Ty    = I.getOperand(0)->getType();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue R;   // Result

	switch (I.getPredicate()) {
	case ICmpInst::ICMP_EQ:  R = executeICMP_EQ(Src1,  Src2, Ty); break;
	case ICmpInst::ICMP_NE:  R = executeICMP_NE(Src1,  Src2, Ty); break;
	case ICmpInst::ICMP_ULT: R = executeICMP_ULT(Src1, Src2, Ty); break;
	case ICmpInst::ICMP_SLT: R = executeICMP_SLT(Src1, Src2, Ty); break;
	case ICmpInst::ICMP_UGT: R = executeICMP_UGT(Src1, Src2, Ty); break;
	case ICmpInst::ICMP_SGT: R = executeICMP_SGT(Src1, Src2, Ty); break;
	case ICmpInst::ICMP_ULE: R = executeICMP_ULE(Src1, Src2, Ty); break;
	case ICmpInst::ICMP_SLE: R = executeICMP_SLE(Src1, Src2, Ty); break;
	case ICmpInst::ICMP_UGE: R = executeICMP_UGE(Src1, Src2, Ty); break;
	case ICmpInst::ICMP_SGE: R = executeICMP_SGE(Src1, Src2, Ty); break;
	default:
		dbgs() << "Don't know how to handle this ICmp predicate!\n-->" << I;
		llvm_unreachable(0);
	}

	SetValue(&I, R, SF);
}

void MyInterpreter::visitFCmpInst(FCmpInst &I) {
	ExecutionContext &SF = ECStack.back();
	Type *Ty    = I.getOperand(0)->getType();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue R;   // Result

	switch (I.getPredicate()) {
	default:
		dbgs() << "Don't know how to handle this FCmp predicate!\n-->" << I;
		llvm_unreachable(0);
		break;
	case FCmpInst::FCMP_FALSE: R = executeFCMP_BOOL(Src1, Src2, Ty, false); break;
	case FCmpInst::FCMP_TRUE:  R = executeFCMP_BOOL(Src1, Src2, Ty, true);  break;
	case FCmpInst::FCMP_ORD:   R = executeFCMP_ORD(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_UNO:   R = executeFCMP_UNO(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_UEQ:   R = executeFCMP_UEQ(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_OEQ:   R = executeFCMP_OEQ(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_UNE:   R = executeFCMP_UNE(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_ONE:   R = executeFCMP_ONE(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_ULT:   R = executeFCMP_ULT(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_OLT:   R = executeFCMP_OLT(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_UGT:   R = executeFCMP_UGT(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_OGT:   R = executeFCMP_OGT(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_ULE:   R = executeFCMP_ULE(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_OLE:   R = executeFCMP_OLE(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_UGE:   R = executeFCMP_UGE(Src1, Src2, Ty); break;
	case FCmpInst::FCMP_OGE:   R = executeFCMP_OGE(Src1, Src2, Ty); break;
	}

	SetValue(&I, R, SF);
}

void MyInterpreter::visitBinaryOperator(BinaryOperator &I) {
	ExecutionContext &SF = ECStack.back();
	Type *Ty    = I.getOperand(0)->getType();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue R;   // Result

	// First process vector operation
	if (Ty->isVectorTy()) {
		assert(Src1.AggregateVal.size() == Src2.AggregateVal.size());
		R.AggregateVal.resize(Src1.AggregateVal.size());

		// Macros to execute binary operation 'OP' over integer vectors
#define INTEGER_VECTOR_OPERATION(OP)                               \
	for (unsigned i = 0; i < R.AggregateVal.size(); ++i)           \
		R.AggregateVal[i].IntVal = Src1.AggregateVal[i].IntVal OP Src2.AggregateVal[i].IntVal;

		// Additional macros to execute binary operations udiv/sdiv/urem/srem since
		// they have different notation.
#define INTEGER_VECTOR_FUNCTION(OP)                                \
	for (unsigned i = 0; i < R.AggregateVal.size(); ++i)           \
		R.AggregateVal[i].IntVal =  Src1.AggregateVal[i].IntVal.OP(Src2.AggregateVal[i].IntVal);

		// Macros to execute binary operation 'OP' over floating point type TY
		// (float or double) vectors
#define FLOAT_VECTOR_FUNCTION(OP, TY)                               \
	for (unsigned i = 0; i < R.AggregateVal.size(); ++i)          \
		R.AggregateVal[i].TY = Src1.AggregateVal[i].TY OP Src2.AggregateVal[i].TY;

    // Macros to choose appropriate TY: float or double and run operation
    // execution
#define FLOAT_VECTOR_OP(OP) {                                         \
	if (dyn_cast<VectorType>(Ty)->getElementType()->isFloatTy())        \
		FLOAT_VECTOR_FUNCTION(OP, FloatVal)                               \
	else {                                                              \
		if (dyn_cast<VectorType>(Ty)->getElementType()->isDoubleTy())     \
			FLOAT_VECTOR_FUNCTION(OP, DoubleVal)                            \
		else {                                                            \
			dbgs() << "Unhandled type for OP instruction: " << *Ty << "\n"; \
			llvm_unreachable(0);                                            \
		}                                                                 \
	}                                                                   \
}

		switch(I.getOpcode()){
		default:
			dbgs() << "Don't know how to handle this binary operator!\n-->" << I;
			llvm_unreachable(0);
			break;
		case Instruction::Add:   INTEGER_VECTOR_OPERATION(+) break;
		case Instruction::Sub:   INTEGER_VECTOR_OPERATION(-) break;
		case Instruction::Mul:   INTEGER_VECTOR_OPERATION(*) break;
		case Instruction::UDiv:  INTEGER_VECTOR_FUNCTION(udiv) break;
		case Instruction::SDiv:  INTEGER_VECTOR_FUNCTION(sdiv) break;
		case Instruction::URem:  INTEGER_VECTOR_FUNCTION(urem) break;
		case Instruction::SRem:  INTEGER_VECTOR_FUNCTION(srem) break;
		case Instruction::And:   INTEGER_VECTOR_OPERATION(&) break;
		case Instruction::Or:    INTEGER_VECTOR_OPERATION(|) break;
		case Instruction::Xor:   INTEGER_VECTOR_OPERATION(^) break;
		case Instruction::FAdd:  FLOAT_VECTOR_OP(+) break;
		case Instruction::FSub:  FLOAT_VECTOR_OP(-) break;
		case Instruction::FMul:  FLOAT_VECTOR_OP(*) break;
		case Instruction::FDiv:  FLOAT_VECTOR_OP(/) break;
		case Instruction::FRem:
			if (dyn_cast<VectorType>(Ty)->getElementType()->isFloatTy())
			for (unsigned i = 0; i < R.AggregateVal.size(); ++i)
				R.AggregateVal[i].FloatVal = fmod(Src1.AggregateVal[i].FloatVal, Src2.AggregateVal[i].FloatVal);
			else {
				if (dyn_cast<VectorType>(Ty)->getElementType()->isDoubleTy())
					for (unsigned i = 0; i < R.AggregateVal.size(); ++i)
						R.AggregateVal[i].DoubleVal = fmod(Src1.AggregateVal[i].DoubleVal, Src2.AggregateVal[i].DoubleVal);
				else {
					dbgs() << "Unhandled type for Rem instruction: " << *Ty << "\n";
					llvm_unreachable(0);
				}
			}
			break;
		}
	} else {
		switch (I.getOpcode()) {
		default:
			dbgs() << "Don't know how to handle this binary operator!\n-->" << I;
			llvm_unreachable(0);
			break;
		case Instruction::Add:   R.IntVal = Src1.IntVal + Src2.IntVal; break;
		case Instruction::Sub:   R.IntVal = Src1.IntVal - Src2.IntVal; break;
		case Instruction::Mul:   R.IntVal = Src1.IntVal * Src2.IntVal; break;
		case Instruction::FAdd:  executeFAddInst(R, Src1, Src2, Ty); break;
		case Instruction::FSub:  executeFSubInst(R, Src1, Src2, Ty); break;
		case Instruction::FMul:  executeFMulInst(R, Src1, Src2, Ty); break;
		case Instruction::FDiv:  executeFDivInst(R, Src1, Src2, Ty); break;
		case Instruction::FRem:  executeFRemInst(R, Src1, Src2, Ty); break;
		case Instruction::UDiv:  R.IntVal = Src1.IntVal.udiv(Src2.IntVal); break;
		case Instruction::SDiv:  R.IntVal = Src1.IntVal.sdiv(Src2.IntVal); break;
		case Instruction::URem:  R.IntVal = Src1.IntVal.urem(Src2.IntVal); break;
		case Instruction::SRem:  R.IntVal = Src1.IntVal.srem(Src2.IntVal); break;
		case Instruction::And:   R.IntVal = Src1.IntVal & Src2.IntVal; break;
		case Instruction::Or:    R.IntVal = Src1.IntVal | Src2.IntVal; break;
		case Instruction::Xor:   R.IntVal = Src1.IntVal ^ Src2.IntVal; break;
		}
	}
	SetValue(&I, R, SF);
}

void MyInterpreter::visitSelectInst(SelectInst &I) {
	ExecutionContext &SF = ECStack.back();
	const Type * Ty = I.getOperand(0)->getType();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
	GenericValue R = executeSelectInst(Src1, Src2, Src3, Ty);
	SetValue(&I, R, SF);

	// pts-to analysis
	if (!isa<PointerType>(Ty))
		return;
	if (Ty->isVectorTy())
		llvm_unreachable("Oops, I can't handle vector condition yet!");
	Value* srcValue0 = I.getFalseValue();
	Value* srcValue1 = I.getTrueValue();
	Value* srcValue = (Src1.IntVal == 0) ? srcValue0 : srcValue1;
	Variable* dst = variableFactory.getMappedVar(&I);
	Variable* src = ptrToVariable(srcValue);
	PtsSet srcTgt = ptsGraph.lookup(src);
	ptsGraph.update(dst, srcTgt);
}

void MyInterpreter::visitReturnInst(ReturnInst &I) {
	ExecutionContext &SF = ECStack.back();
	Type *RetTy = Type::getVoidTy(I.getContext());
	GenericValue Result;

	// Save away the return value... (if we are not 'ret void')
	if (I.getNumOperands()) {
		RetTy  = I.getReturnValue()->getType();
		Result = getOperandValue(I.getReturnValue(), SF);
	}

	// pts-to analysis
	// Using global variable to pass information across functions is really ugly. However, this is the only way I can think of that does not mess the existing codebase.
	if (isa<PointerType>(RetTy))
	{
		retVar = variableFactory.getMappedVar(I.getReturnValue());
		if (retVar == NULL)
			llvm_unreachable("retVar is null?");
	}

	popStackAndReturnValueToCaller(RetTy, Result);
}

void MyInterpreter::visitUnreachableInst(UnreachableInst &I) {
	report_fatal_error("Program executed an 'unreachable' instruction!");
}

void MyInterpreter::visitBranchInst(BranchInst &I) {
	ExecutionContext &SF = ECStack.back();
	BasicBlock *Dest;

	Dest = I.getSuccessor(0);          // Uncond branches have a fixed dest...
	if (!I.isUnconditional()) {
		Value *Cond = I.getCondition();
		if (getOperandValue(Cond, SF).IntVal == 0) // If false cond...
			Dest = I.getSuccessor(1);
	}
	SwitchToNewBasicBlock(Dest, SF);
}

void MyInterpreter::visitSwitchInst(SwitchInst &I) {
	ExecutionContext &SF = ECStack.back();
	Value* Cond = I.getCondition();
	Type *ElTy = Cond->getType();
	GenericValue CondVal = getOperandValue(Cond, SF);

	// Check to see if any of the cases match...
	BasicBlock *Dest = 0;
	for (SwitchInst::CaseIt i = I.case_begin(), e = I.case_end(); i != e; ++i) {
		GenericValue CaseVal = getOperandValue(i.getCaseValue(), SF);
		if (executeICMP_EQ(CondVal, CaseVal, ElTy).IntVal != 0) {
			Dest = cast<BasicBlock>(i.getCaseSuccessor());
			break;
		}
	}
	if (!Dest) Dest = I.getDefaultDest();   // No cases matched: use default
	SwitchToNewBasicBlock(Dest, SF);
}

void MyInterpreter::visitIndirectBrInst(IndirectBrInst &I) {
	ExecutionContext &SF = ECStack.back();
	void *Dest = GVTOP(getOperandValue(I.getAddress(), SF));
	SwitchToNewBasicBlock((BasicBlock*)Dest, SF);
}

void MyInterpreter::visitAllocaInst(AllocaInst &I) {
	ExecutionContext &SF = ECStack.back();

	Type *Ty = I.getType()->getElementType();  // Type to be allocated

	// Get the number of elements being allocated by the array...
	unsigned NumElements = 
	getOperandValue(I.getOperand(0), SF).IntVal.getZExtValue();

	unsigned TypeSize = (size_t)td.getTypeAllocSize(Ty);

	// Avoid malloc-ing zero bytes, use max()...
	unsigned MemToAlloc = std::max(1U, NumElements * TypeSize);

	// Allocate enough memory to hold the type...
	void *Memory = malloc(MemToAlloc);

	DEBUG(dbgs() << "Allocated Type: " << *Ty << " (" << TypeSize << " bytes) x " 
	           << NumElements << " (Total: " << MemToAlloc << ") at "
	           << uintptr_t(Memory) << '\n');

	GenericValue Result = PTOGV(Memory);
	assert(Result.PointerVal != 0 && "Null pointer returned by malloc!");
	SetValue(&I, Result, SF);

	if (I.getOpcode() == Instruction::Alloca)
		ECStack.back().Allocas.add(Memory);

	// pts-to analysis
	Variable* ptr = variableFactory.getMappedVar(&I);
	Variable* tgt = ptsInit[ptr];
	ptsGraph.update(ptr, tgt);

	if (tgt->getType() != ADDR_TAKEN)
		llvm_unreachable("Pointer to a top-level var?");
	unsigned bound = ((AddrTakenVar*)tgt)->getOffsetBound();
	for (unsigned i = 0; i < bound; ++i)
	{
		Variable* field = variableFactory.getVariable(ptr->getIndex() + i + 1);
		Variable* fieldTgt = variableFactory.getVariable(tgt->getIndex() + i + 1);
		ptsGraph.update(field, fieldTgt);
	}

}

void MyInterpreter::visitGetElementPtrInst(GetElementPtrInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeGEPOperation(I.getPointerOperand(), gep_type_begin(I), gep_type_end(I), SF), SF);

	// pts-to analysis
	Variable* dst = variableFactory.getMappedVar(&I);
	Value* srcValue = I.getPointerOperand();
	Variable* src = ptrToVariable(srcValue);
	unsigned offset = GEPtoOffset(&I);
	PtsSet srcTgt = ptsGraph.lookup(src);

	std::vector<unsigned> indexVec;
	srcTgt.toIndexVector(indexVec);
	for (std::vector<unsigned>::iterator itr = indexVec.begin(), ite = indexVec.end(); itr != ite; ++itr)
	{
		Variable* v = variableFactory.getVariable(*itr);
		if (v->getType() != ADDR_TAKEN)
			llvm_unreachable("GEP src points to a top-level?");
		AddrTakenVar* atSrcTgt = (AddrTakenVar*)v;
		if (offset < atSrcTgt->getOffsetBound())
			ptsGraph.update(dst, variableFactory.getVariable(atSrcTgt->getIndex() + offset), true);
		else if (atSrcTgt->isArray())
			ptsGraph.update(dst, atSrcTgt, true);
	}
}

void MyInterpreter::visitLoadInst(LoadInst &I) {
	ExecutionContext &SF = ECStack.back();
	GenericValue SRC = getOperandValue(I.getPointerOperand(), SF);
	GenericValue *Ptr = (GenericValue*)GVTOP(SRC);
	GenericValue Result;
	LoadValueFromMemory(Result, Ptr, I.getType());
	SetValue(&I, Result, SF);

	// pts-to analysis
	if (!isa<PointerType>(I.getType()))
		return;

	Variable* dst = variableFactory.getMappedVar(&I);
	Value* srcValue = I.getPointerOperand();
	Variable* src = ptrToVariable(srcValue);

	PtsSet srcTgt = ptsGraph.lookup(src);
	PtsSet srcTgtTgt = ptsGraph.lookup(srcTgt);
	ptsGraph.update(dst, srcTgtTgt);
}

void MyInterpreter::visitStoreInst(StoreInst &I) {
	ExecutionContext &SF = ECStack.back();
	GenericValue Val = getOperandValue(I.getOperand(0), SF);
	GenericValue SRC = getOperandValue(I.getPointerOperand(), SF);
	StoreValueToMemory(Val, (GenericValue *)GVTOP(SRC), I.getOperand(0)->getType());
	
	// pts-to analysis
	Value* dstValue = I.getPointerOperand();
	Value* srcValue = I.getValueOperand();
	if (!isa<PointerType>(srcValue->getType()))
		return;
	Variable* src = ptrToVariable(srcValue);
	Variable* dst = ptrToVariable(dstValue);
	if (dstValue == NULL)
		llvm_unreachable("Store into a null pointer");
	PtsSet dstTgt = ptsGraph.lookup(dst);
	PtsSet srcTgt = ptsGraph.lookup(src);
	ptsGraph.update(dstTgt, srcTgt);
}

void MyInterpreter::visitCallSite(CallSite CS) {
	ExecutionContext &SF = ECStack.back();

	// Check to see if this is an intrinsic function call...
	Function *F = CS.getCalledFunction();
	if (F && F->isDeclaration())
		switch (F->getIntrinsicID()) {
		case Intrinsic::not_intrinsic:
			break;
		case Intrinsic::vastart: { // va_start
			GenericValue ArgIndex;
			ArgIndex.UIntPairVal.first = ECStack.size() - 1;
			ArgIndex.UIntPairVal.second = 0;
			SetValue(CS.getInstruction(), ArgIndex, SF);
			return;
		}
		case Intrinsic::vaend:    // va_end is a noop for the interpreter
			return;
		case Intrinsic::vacopy:   // va_copy: dest = src
			SetValue(CS.getInstruction(), getOperandValue(*CS.arg_begin(), SF), SF);
			return;
		default:
			// If it is an unknown intrinsic function, use the intrinsic lowering
			// class to transform it into hopefully tasty LLVM code.
			//
			BasicBlock::iterator me(CS.getInstruction());
			BasicBlock *Parent = CS.getInstruction()->getParent();
			bool atBegin(Parent->begin() == me);
			if (!atBegin)
			--me;
			il->LowerIntrinsicCall(cast<CallInst>(CS.getInstruction()));

			// Restore the CurInst pointer to the first instruction newly inserted, if
			// any.
			if (atBegin) {
				SF.CurInst = Parent->begin();
			} else {
				SF.CurInst = me;
				++SF.CurInst;
			}
			return;
		}


	SF.Caller = CS;
	std::vector<GenericValue> ArgVals;
	const unsigned NumArgs = SF.Caller.arg_size();
	ArgVals.reserve(NumArgs);
	uint16_t pNum = 1;
	for (CallSite::arg_iterator i = SF.Caller.arg_begin(), e = SF.Caller.arg_end(); i != e; ++i, ++pNum) {
		Value *V = *i;
		ArgVals.push_back(getOperandValue(V, SF));
	}

	// To handle indirect calls, we must get the pointer value from the argument
	// and treat it as a function pointer.
	GenericValue SRC = getOperandValue(SF.Caller.getCalledValue(), SF);
	callFunction((Function*)GVTOP(SRC), ArgVals, &CS);
}

// auxilary function for shift operations
static unsigned getShiftAmount(uint64_t orgShiftAmount, llvm::APInt valueToShift) {
	unsigned valueWidth = valueToShift.getBitWidth();
	if (orgShiftAmount < (uint64_t)valueWidth)
		return orgShiftAmount;
	// according to the llvm documentation, if orgShiftAmount > valueWidth,
	// the result is undfeined. but we do shift by this rule:
	return (NextPowerOf2(valueWidth-1) - 1) & orgShiftAmount;
}


void MyInterpreter::visitShl(BinaryOperator &I) {
	ExecutionContext &SF = ECStack.back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest;
	const Type *Ty = I.getType();

	if (Ty->isVectorTy()) {
		uint32_t src1Size = uint32_t(Src1.AggregateVal.size());
		assert(src1Size == Src2.AggregateVal.size());
		for (unsigned i = 0; i < src1Size; i++) {
			GenericValue Result;
			uint64_t shiftAmount = Src2.AggregateVal[i].IntVal.getZExtValue();
			llvm::APInt valueToShift = Src1.AggregateVal[i].IntVal;
			Result.IntVal = valueToShift.shl(getShiftAmount(shiftAmount, valueToShift));
			Dest.AggregateVal.push_back(Result);
		}
	} else {
	// scalar
		uint64_t shiftAmount = Src2.IntVal.getZExtValue();
		llvm::APInt valueToShift = Src1.IntVal;
		Dest.IntVal = valueToShift.shl(getShiftAmount(shiftAmount, valueToShift));
	}

	SetValue(&I, Dest, SF);
}

void MyInterpreter::visitLShr(BinaryOperator &I) {
	ExecutionContext &SF = ECStack.back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest;
	const Type *Ty = I.getType();

	if (Ty->isVectorTy()) {
		uint32_t src1Size = uint32_t(Src1.AggregateVal.size());
		assert(src1Size == Src2.AggregateVal.size());
		for (unsigned i = 0; i < src1Size; i++) {
			GenericValue Result;
			uint64_t shiftAmount = Src2.AggregateVal[i].IntVal.getZExtValue();
			llvm::APInt valueToShift = Src1.AggregateVal[i].IntVal;
			Result.IntVal = valueToShift.lshr(getShiftAmount(shiftAmount, valueToShift));
			Dest.AggregateVal.push_back(Result);
		}
	} else {
	// scalar
		uint64_t shiftAmount = Src2.IntVal.getZExtValue();
		llvm::APInt valueToShift = Src1.IntVal;
		Dest.IntVal = valueToShift.lshr(getShiftAmount(shiftAmount, valueToShift));
	}

	SetValue(&I, Dest, SF);
}

void MyInterpreter::visitAShr(BinaryOperator &I) {
	ExecutionContext &SF = ECStack.back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest;
	const Type *Ty = I.getType();

	if (Ty->isVectorTy()) {
		size_t src1Size = Src1.AggregateVal.size();
		assert(src1Size == Src2.AggregateVal.size());
		for (unsigned i = 0; i < src1Size; i++) {
			GenericValue Result;
			uint64_t shiftAmount = Src2.AggregateVal[i].IntVal.getZExtValue();
			llvm::APInt valueToShift = Src1.AggregateVal[i].IntVal;
			Result.IntVal = valueToShift.ashr(getShiftAmount(shiftAmount, valueToShift));
			Dest.AggregateVal.push_back(Result);
		}
	} else {
	// scalar
		uint64_t shiftAmount = Src2.IntVal.getZExtValue();
		llvm::APInt valueToShift = Src1.IntVal;
		Dest.IntVal = valueToShift.ashr(getShiftAmount(shiftAmount, valueToShift));
	}

	SetValue(&I, Dest, SF);
}

void MyInterpreter::visitTruncInst(TruncInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeTruncInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitSExtInst(SExtInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeSExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitZExtInst(ZExtInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeZExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitFPTruncInst(FPTruncInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeFPTruncInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitFPExtInst(FPExtInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeFPExtInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitUIToFPInst(UIToFPInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeUIToFPInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitSIToFPInst(SIToFPInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeSIToFPInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitFPToUIInst(FPToUIInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeFPToUIInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitFPToSIInst(FPToSIInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeFPToSIInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitPtrToIntInst(PtrToIntInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executePtrToIntInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitIntToPtrInst(IntToPtrInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeIntToPtrInst(I.getOperand(0), I.getType(), SF), SF);
}

void MyInterpreter::visitBitCastInst(BitCastInst &I) {
	ExecutionContext &SF = ECStack.back();
	SetValue(&I, executeBitCastInst(I.getOperand(0), I.getType(), SF), SF);

	// pts-to analysis
	const Type* ty = I.getType();
	if (!isa<PointerType>(ty))
		return;
	Variable* src = NULL;
	Value* srcValue = I.getOperand(0);
	while (ConstantExpr* constExp = dyn_cast<ConstantExpr>(srcValue))
	{
		switch (constExp->getOpcode())
		{
			case Instruction::BitCast:
				srcValue = constExp->getOperand(0);
				break;
			case Instruction::GetElementPtr:
				src = ConstGEPtoVariable(constExp);
				break;
			default:
				assert(false && "Other types of constExpr not supported here");
		}
		if (src != NULL)
			break;
	}
	if (src == NULL)
		src = variableFactory.getMappedVar(srcValue);
	if (src == NULL)
		llvm_unreachable("bitcast missing operand");
	Variable* dst = variableFactory.getMappedVar(&I);
	PtsSet srcTgt = ptsGraph.lookup(src);
	ptsGraph.update(dst, srcTgt);
}

#define IMPLEMENT_VAARG(TY) \
	case Type::TY##TyID: Dest.TY##Val = Src.TY##Val; break

void MyInterpreter::visitVAArgInst(VAArgInst &I) {
	ExecutionContext &SF = ECStack.back();

	// Get the incoming valist parameter.  LLI treats the valist as a
	// (ec-stack-depth var-arg-index) pair.
	GenericValue VAList = getOperandValue(I.getOperand(0), SF);
	GenericValue Dest;
	GenericValue Src = ECStack[VAList.UIntPairVal.first].VarArgs[VAList.UIntPairVal.second];
	Type *Ty = I.getType();
	switch (Ty->getTypeID()) {
	case Type::IntegerTyID:
		Dest.IntVal = Src.IntVal;
		break;
	IMPLEMENT_VAARG(Pointer);
	IMPLEMENT_VAARG(Float);
	IMPLEMENT_VAARG(Double);
	default:
		dbgs() << "Unhandled dest type for vaarg instruction: " << *Ty << "\n";
		llvm_unreachable(0);
	}

	// Set the Value of this Instruction.
	SetValue(&I, Dest, SF);

	// Move the pointer to the next vararg.
	++VAList.UIntPairVal.second;
}

void MyInterpreter::visitExtractElementInst(ExtractElementInst &I) {
	ExecutionContext &SF = ECStack.back();
	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest;

	Type *Ty = I.getType();
	const unsigned indx = unsigned(Src2.IntVal.getZExtValue());

	if(Src1.AggregateVal.size() > indx) {
		switch (Ty->getTypeID()) {
		default:
			dbgs() << "Unhandled destination type for extractelement instruction: "
			<< *Ty << "\n";
			llvm_unreachable(0);
			break;
		case Type::IntegerTyID:
			Dest.IntVal = Src1.AggregateVal[indx].IntVal;
			break;
		case Type::FloatTyID:
			Dest.FloatVal = Src1.AggregateVal[indx].FloatVal;
			break;
		case Type::DoubleTyID:
			Dest.DoubleVal = Src1.AggregateVal[indx].DoubleVal;
			break;
		}
	} else {
		dbgs() << "Invalid index in extractelement instruction\n";
	}

	SetValue(&I, Dest, SF);
}

void MyInterpreter::visitInsertElementInst(InsertElementInst &I) {
	ExecutionContext &SF = ECStack.back();
	Type *Ty = I.getType();

	if(!(Ty->isVectorTy()) )
		llvm_unreachable("Unhandled dest type for insertelement instruction");

	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
	GenericValue Dest;

	Type *TyContained = Ty->getContainedType(0);

	const unsigned indx = unsigned(Src3.IntVal.getZExtValue());
	Dest.AggregateVal = Src1.AggregateVal;

	if(Src1.AggregateVal.size() <= indx)
		llvm_unreachable("Invalid index in insertelement instruction");
	switch (TyContained->getTypeID()) {
	default:
		llvm_unreachable("Unhandled dest type for insertelement instruction");
	case Type::IntegerTyID:
		Dest.AggregateVal[indx].IntVal = Src2.IntVal;
		break;
	case Type::FloatTyID:
		Dest.AggregateVal[indx].FloatVal = Src2.FloatVal;
		break;
	case Type::DoubleTyID:
		Dest.AggregateVal[indx].DoubleVal = Src2.DoubleVal;
		break;
	}
	SetValue(&I, Dest, SF);
}

void MyInterpreter::visitShuffleVectorInst(ShuffleVectorInst &I){
	ExecutionContext &SF = ECStack.back();

	Type *Ty = I.getType();
	if(!(Ty->isVectorTy()))
	llvm_unreachable("Unhandled dest type for shufflevector instruction");

	GenericValue Src1 = getOperandValue(I.getOperand(0), SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Src3 = getOperandValue(I.getOperand(2), SF);
	GenericValue Dest;

	// There is no need to check types of src1 and src2, because the compiled
	// bytecode can't contain different types for src1 and src2 for a
	// shufflevector instruction.

	Type *TyContained = Ty->getContainedType(0);
	unsigned src1Size = (unsigned)Src1.AggregateVal.size();
	unsigned src2Size = (unsigned)Src2.AggregateVal.size();
	unsigned src3Size = (unsigned)Src3.AggregateVal.size();

	Dest.AggregateVal.resize(src3Size);

	switch (TyContained->getTypeID()) {
	default:
		llvm_unreachable("Unhandled dest type for insertelement instruction");
		break;
	case Type::IntegerTyID:
		for( unsigned i=0; i<src3Size; i++) {
		unsigned j = Src3.AggregateVal[i].IntVal.getZExtValue();
		if(j < src1Size)
			Dest.AggregateVal[i].IntVal = Src1.AggregateVal[j].IntVal;
		else if(j < src1Size + src2Size)
			Dest.AggregateVal[i].IntVal = Src2.AggregateVal[j-src1Size].IntVal;
		else
		  // The selector may not be greater than sum of lengths of first and
		  // second operands and llasm should not allow situation like
		  // %tmp = shufflevector <2 x i32> <i32 3, i32 4>, <2 x i32> undef,
		  //                      <2 x i32> < i32 0, i32 5 >,
		  // where i32 5 is invalid, but let it be additional check here:
			llvm_unreachable("Invalid mask in shufflevector instruction");
		}
		break;
	case Type::FloatTyID:
		for( unsigned i=0; i<src3Size; i++) {
			unsigned j = Src3.AggregateVal[i].IntVal.getZExtValue();
			if(j < src1Size)
				Dest.AggregateVal[i].FloatVal = Src1.AggregateVal[j].FloatVal;
			else if(j < src1Size + src2Size)
				Dest.AggregateVal[i].FloatVal = Src2.AggregateVal[j-src1Size].FloatVal;
			else
				llvm_unreachable("Invalid mask in shufflevector instruction");
		}
		break;
	case Type::DoubleTyID:
		for( unsigned i=0; i<src3Size; i++) {
			unsigned j = Src3.AggregateVal[i].IntVal.getZExtValue();
			if(j < src1Size)
				Dest.AggregateVal[i].DoubleVal = Src1.AggregateVal[j].DoubleVal;
			else if(j < src1Size + src2Size)
				Dest.AggregateVal[i].DoubleVal = Src2.AggregateVal[j-src1Size].DoubleVal;
			else
				llvm_unreachable("Invalid mask in shufflevector instruction");
	  	}
	 	break;
	}
	SetValue(&I, Dest, SF);
}

void MyInterpreter::visitExtractValueInst(ExtractValueInst &I) {
	ExecutionContext &SF = ECStack.back();
	Value *Agg = I.getAggregateOperand();
	GenericValue Dest;
	GenericValue Src = getOperandValue(Agg, SF);

	ExtractValueInst::idx_iterator IdxBegin = I.idx_begin();
	unsigned Num = I.getNumIndices();
	GenericValue *pSrc = &Src;

	for (unsigned i = 0 ; i < Num; ++i) {
		pSrc = &pSrc->AggregateVal[*IdxBegin];
		++IdxBegin;
	}

	Type *IndexedType = ExtractValueInst::getIndexedType(Agg->getType(), I.getIndices());
	switch (IndexedType->getTypeID()) {
	default:
	  llvm_unreachable("Unhandled dest type for extractelement instruction");
		break;
	case Type::IntegerTyID:
		Dest.IntVal = pSrc->IntVal;
		break;
	case Type::FloatTyID:
		Dest.FloatVal = pSrc->FloatVal;
		break;
	case Type::DoubleTyID:
		Dest.DoubleVal = pSrc->DoubleVal;
		break;
	case Type::ArrayTyID:
	case Type::StructTyID:
	case Type::VectorTyID:
		Dest.AggregateVal = pSrc->AggregateVal;
		break;
	case Type::PointerTyID:
		Dest.PointerVal = pSrc->PointerVal;
		break;
	}

	SetValue(&I, Dest, SF);
}

void MyInterpreter::visitInsertValueInst(InsertValueInst &I) {

	ExecutionContext &SF = ECStack.back();
	Value *Agg = I.getAggregateOperand();

	GenericValue Src1 = getOperandValue(Agg, SF);
	GenericValue Src2 = getOperandValue(I.getOperand(1), SF);
	GenericValue Dest = Src1; // Dest is a slightly changed Src1

	ExtractValueInst::idx_iterator IdxBegin = I.idx_begin();
	unsigned Num = I.getNumIndices();

	GenericValue *pDest = &Dest;
	for (unsigned i = 0 ; i < Num; ++i) {
		pDest = &pDest->AggregateVal[*IdxBegin];
		++IdxBegin;
	}
	// pDest points to the target value in the Dest now

	Type *IndexedType = ExtractValueInst::getIndexedType(Agg->getType(), I.getIndices());

	switch (IndexedType->getTypeID()) {
	default:
		llvm_unreachable("Unhandled dest type for insertelement instruction");
		break;
	case Type::IntegerTyID:
		pDest->IntVal = Src2.IntVal;
		break;
	case Type::FloatTyID:
		pDest->FloatVal = Src2.FloatVal;
		break;
	case Type::DoubleTyID:
		pDest->DoubleVal = Src2.DoubleVal;
		break;
	case Type::ArrayTyID:
	case Type::StructTyID:
	case Type::VectorTyID:
		pDest->AggregateVal = Src2.AggregateVal;
		break;
	case Type::PointerTyID:
		pDest->PointerVal = Src2.PointerVal;
		break;
	}

	SetValue(&I, Dest, SF);
}