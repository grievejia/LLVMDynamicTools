#ifndef LLVM_FUZZER_GENERATOR_ENVIRONMENT_H
#define LLVM_FUZZER_GENERATOR_ENVIRONMENT_H

#include <vector>
#include <unordered_map>

namespace llvm
{
	class Type;
	class Value;
	class LLVMContext;
}

namespace llvm_fuzzer
{

class MemoryLoc
{
private:
	const llvm::Value* ptr;
	bool valid;

	MemoryLoc(const llvm::Value* p): ptr(p), valid(false) {}
public:
	llvm::Type* getType() const;
	const llvm::Value* getPointer() const { return ptr; }

	bool isValid() const { return valid; }
	void setValid() { valid = true; }

	static MemoryLoc makeFromPointer(const llvm::Value* p) { return MemoryLoc(p); }
};

// The GeneratorEnvironment class contains all the dataflow information a random generator needs to know in order to produce legitimate and interesting result
// Another way of looking at it is that this is a powered-up version of a simple value pool. A value pool only tells you what values have been created, yet a environment also records, e.g. the points-to info.
class GeneratorEnvironment
{
private:
	llvm::LLVMContext& context;

	using ValueList = std::vector<llvm::Value*>;
	std::unordered_map<llvm::Type*, ValueList> typeMap;

	using PtrMapType = std::unordered_map<llvm::Value*, MemoryLoc>;
	PtrMapType ptrMap;

	unsigned numValue;

	GeneratorEnvironment(llvm::LLVMContext& c): context(c), numValue(0) {}

	void addType(llvm::Type* type);
	void initializePointsTo(llvm::Value* ptr);
public:
	GeneratorEnvironment(const GeneratorEnvironment& other);
	GeneratorEnvironment(GeneratorEnvironment&& other);
	GeneratorEnvironment& operator=(const GeneratorEnvironment& other) = delete;
	GeneratorEnvironment& operator=(GeneratorEnvironment&& other) = delete;

	llvm::LLVMContext& getContext() { return context; }
	unsigned getSize() const { return numValue; }
	bool isEmpty() const { return numValue == 0; }
	unsigned getNumPointer() const { return ptrMap.size(); }
	bool hasPointer() const { return !ptrMap.empty(); }

	unsigned getNumType() const { return typeMap.size(); }
	unsigned getNumValueOfType(llvm::Type*) const;
	const ValueList& getAttachedValues(llvm::Type*);

	void addValue(llvm::Value* val);
	llvm::Value* getValueAtIndex(unsigned idx) const;
	llvm::Type* getTypeAtIndex(unsigned idx) const;

	void recordStore(llvm::Value* ptr, llvm::Value* val);
	void recordStore(llvm::Value* ptr);
	MemoryLoc& getPointsToTarget(llvm::Value* ptr);
	llvm::Value* getPointerAtIndex(unsigned idx) const;

	static GeneratorEnvironment getEmptyEnvironment(llvm::LLVMContext& c);
};

}

#endif
