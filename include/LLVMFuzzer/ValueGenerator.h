#ifndef LLVM_FUZZER_VALUE_POOL_H
#define LLVM_FUZZER_VALUE_POOL_H

#include <vector>
#include <unordered_set>

namespace llvm
{
	class Value;
	class Constant;
	class Type;
	class LLVMContext;
}

namespace llvm_fuzzer
{

class Random;
class GeneratorEnvironment;

class ValueGenerator
{
private:
	GeneratorEnvironment& env;

	Random& randomGenerator;
public:
	ValueGenerator(GeneratorEnvironment& e, Random& r): env(e),randomGenerator(r) {}
	~ValueGenerator() = default;

	llvm::Value* getRandomValue() const;
	llvm::Value* getRandomPointer(bool mustValid = false) const;
	llvm::Value* getRandomScalar() const;
	llvm::Constant* getRandomConstantNumber(llvm::Type* type) const;
	llvm::Value* getRandomValueOfType(llvm::Type* type) const;

	llvm::Type* getRandomType() const;
	llvm::Type* getRandomScalarType() const;
};

}

#endif
