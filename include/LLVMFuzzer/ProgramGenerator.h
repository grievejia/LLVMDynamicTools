#ifndef LLVM_FUZZER_PROGRAM_GENERATOR_H
#define LLVM_FUZZER_PROGRAM_GENERATOR_H

#include <memory>

namespace llvm
{
	class Module;
	class Function;
	class BasicBlock;
	class StringRef;
	class FunctionType;
}

namespace llvm_fuzzer
{

class Random;
class GeneratorEnvironment;

class ProgramGenerator
{
private:
	std::unique_ptr<llvm::Module> module;
	Random& randomGenerator;

	// Generate a new function with a single empty basic block
	llvm::Function* getEmptyFunction(llvm::StringRef funName, llvm::FunctionType* funType);

	// Fill a basic block with only stack allocations. Return a new basic block
	void generateAllocationBlock(llvm::BasicBlock* bb, GeneratorEnvironment& env);
	llvm::BasicBlock* generateFunctionBody(llvm::BasicBlock* bb, GeneratorEnvironment& env);
	//void generateSequentialBlock(llvm::BasicBlock* bb, BlockGenerator& bg);
	//void generateConditionalBlock(llvm::BasicBlock* bb, BlockGenerator& bg);
	//void generateLoopBlock(llvm::BasicBlock* bb, BlockGenerator& bg);

	// Fill random instructions inside the given function. No control flow is added

	llvm::Function* generateRandomFunction(llvm::StringRef funName, llvm::FunctionType* funType, GeneratorEnvironment& env);
	void generateMainFunction(llvm::Function* entryFunc);
public:
	ProgramGenerator(std::unique_ptr<llvm::Module> m, Random& r);
	~ProgramGenerator();

	void generateRandomProgram();

	llvm::Module& getModule() { return *module; }
};

}

#endif
