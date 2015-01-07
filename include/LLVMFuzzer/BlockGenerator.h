#ifndef LLVM_FUZZER_MODIFIER_H
#define LLVM_FUZZER_MODIFIER_H

#include "ValueGenerator.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm_fuzzer
{

class Random;
class GeneratorEnvironment;

// A base class, implementing utilities needed for modifying and adding new random instructions in a basic block
class BlockGenerator
{
private:
	// Basic block to populate
	llvm::BasicBlock* bb;
	// Random number generator
	Random& randomGenerator;
	// Environment
	GeneratorEnvironment& env;
	// Piece table
	ValueGenerator valueGenerator;
	// IR Builder
	llvm::IRBuilder<> builder;

	llvm::Value* addCastInst(llvm::Value* val, llvm::Type* tgtType);
	llvm::Value* getRandomOperand();
	llvm::Value* getRandomOperandOfType(llvm::Type*);
public:
	BlockGenerator(llvm::BasicBlock* b, GeneratorEnvironment& e, Random& r): bb(b), randomGenerator(r), env(e), valueGenerator(e, randomGenerator), builder(bb) {}
	~BlockGenerator() = default;

	llvm::Value* addAllocaInst();
	llvm::Value* addStoreInst();
	llvm::Value* addBinaryInst();
	llvm::Value* addCmpInst();
	llvm::Value* addReturnInst(llvm::Type* retType);

	// Batch add
	void addAllocations(unsigned numAlloc);
	void addOperations(unsigned numOps);
};

}

#endif
