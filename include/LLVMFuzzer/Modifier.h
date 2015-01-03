#ifndef LLVM_FUZZER_MODIFIER_H
#define LLVM_FUZZER_MODIFIER_H

#include "ValuePool.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm_fuzzer
{

class Random;

// A base class, implementing utilities needed for modifying and adding new random instructions.
class Modifier
{
private:
	// Basic block to populate
	llvm::BasicBlock* bb;
	// Random number generator
	Random& randomGenerator;
	// IR Builder
	llvm::IRBuilder<> builder;
	// Piece table
	ValuePool valuePool;

	llvm::Value* addCastInst(llvm::Value* val, llvm::Type* tgtType);
public:
	Modifier(llvm::BasicBlock* b, Random& r): bb(b), randomGenerator(r), builder(bb), valuePool(randomGenerator, bb->getContext()) {}
	~Modifier() = default;

	void addAllocaInst();
	void addLoadInst();
	void addStoreInst();
	void addBinaryInst();
	void addCmpInst();
	void addReturnInst();

	// Batch add
	void addAllocations(unsigned numAlloc);
	void addOperations(unsigned numOps);
};

}

#endif
