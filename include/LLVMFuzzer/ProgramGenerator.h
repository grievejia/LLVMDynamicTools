#ifndef LLVM_FUZZER_PROGRAM_GENERATOR_H
#define LLVM_FUZZER_PROGRAM_GENERATOR_H

#include <memory>

namespace llvm
{
	class Module;
	class Function;
	class StringRef;
}

namespace llvm_fuzzer
{

class Random;

class ProgramGenerator
{
private:
	std::unique_ptr<llvm::Module> module;
	Random& randomGenerator;

	// Generate a new function with a single empty basic block
	llvm::Function* getEmptyFunction(llvm::StringRef funName);
	// Fill random instructions inside the given function. No control flow is added
	void fillFunction(llvm::Function* f);
	// Once the given function is filled, randomly turn the compare instructions into branches and loops
	void introduceControlFlow(llvm::Function* f);

	llvm::Function* generateRandomFunction(llvm::StringRef funName);
	void generateMainFunction(llvm::Function* entryFunc);
public:
	ProgramGenerator(std::unique_ptr<llvm::Module> m, Random& r);
	~ProgramGenerator();

	void generateRandomProgram();

	llvm::Module& getModule() { return *module; }
};

}

#endif
