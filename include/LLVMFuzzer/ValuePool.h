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

class ValuePool
{
private:
	using PoolType = std::vector<llvm::Value*>;
	PoolType pool;

	using TypeList = std::vector<llvm::Type*>;
	TypeList typePool;
	static const unsigned NumScalarTypes = 5;

	std::unordered_set<const llvm::Value*> definedPtrs;

	Random& randomGenerator;

	void addType(llvm::Type* t);
public:
	using const_iterator = decltype(pool)::const_iterator;

	ValuePool(Random& r, llvm::LLVMContext& c);
	~ValuePool() = default;

	void addValue(llvm::Value* val, bool defined = true);

	llvm::Value* sampleValueFromPool() const;
	llvm::Value* samplePointerFromPool(bool mustValid = false) const;
	llvm::Value* sampleScalarFromPool() const;

	llvm::Constant* getRandomConstantNumber(llvm::Type* type) const;
	llvm::Value* getRandomValueOfType(llvm::Type* type) const;

	llvm::Type* getRandomType() const;
	llvm::Type* getRandomScalarType() const;

	const_iterator begin() const { return pool.begin(); }
	const_iterator end() const { return pool.end(); }
};

}

#endif
