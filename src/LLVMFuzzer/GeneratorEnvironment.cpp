#include "GeneratorEnvironment.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

#include <algorithm>

using namespace llvm;
using namespace llvm_fuzzer;

Type* MemoryLoc::getType() const
{
	return cast<PointerType>(ptr->getType())->getElementType();
}

GeneratorEnvironment::GeneratorEnvironment(const GeneratorEnvironment& other) = default;
GeneratorEnvironment::GeneratorEnvironment(GeneratorEnvironment&& other) = default;

GeneratorEnvironment GeneratorEnvironment::getEmptyEnvironment(LLVMContext& c)
{
	auto env = GeneratorEnvironment(c);

	// Initialize the type map with all integer types
	env.addType(Type::getInt1Ty(c));
	env.addType(Type::getInt8Ty(c));
	env.addType(Type::getInt16Ty(c));
	env.addType(Type::getInt32Ty(c));
	env.addType(Type::getInt64Ty(c));

	return env;
}

void GeneratorEnvironment::addType(Type* type)
{
	typeMap.insert(std::make_pair(type, ValueList()));
}

Type* GeneratorEnvironment::getTypeAtIndex(unsigned idx) const
{
	assert(idx < typeMap.size());

	auto itr = std::next(typeMap.begin(), idx);
	return itr->first;
}

unsigned GeneratorEnvironment::getNumValueOfType(Type* type) const
{
	auto itr = typeMap.find(type);
	if (itr == typeMap.end())
		return 0;
	else
		return itr->second.size();
}

const GeneratorEnvironment::ValueList& GeneratorEnvironment::getAttachedValues(Type* type)
{
	return typeMap[type];
}

void GeneratorEnvironment::addValue(Value* val)
{
	if (isa<UndefValue>(val))
		return;

	auto& valuePool = typeMap[val->getType()];
	if (std::find(valuePool.begin(), valuePool.end(), val) != valuePool.end())
		return;
	valuePool.push_back(val);
	++numValue;

	if (val->getType()->isPointerTy())
		initializePointsTo(val);
}

Value* GeneratorEnvironment::getValueAtIndex(unsigned idx) const
{
	assert(idx < numValue);

	for (auto const& mapping: typeMap)
	{
		auto sz = mapping.second.size();
		if (idx < sz)
			return mapping.second.at(idx);
		else
			idx -= sz;
	}

	llvm_unreachable("getValueAtIndex() out of bound");
}

Value* GeneratorEnvironment::getPointerAtIndex(unsigned idx) const
{
	assert(idx < ptrMap.size());

	auto itr = std::next(ptrMap.begin(), idx);
	return itr->first;
}

void GeneratorEnvironment::initializePointsTo(Value* ptr)
{
	assert(ptr->getType()->isPointerTy() && "Initializing a non-ptr?");
	assert(!ptrMap.count(ptr) && "Initializing an already existing ptr?");

	ptrMap.insert(std::make_pair(ptr, MemoryLoc::makeFromPointer(ptr)));
}

MemoryLoc& GeneratorEnvironment::getPointsToTarget(Value* ptr)
{
	assert(ptr->getType()->isPointerTy() && "getPointsToTarget() a non-ptr?");
	
	auto itr = ptrMap.find(ptr);
	if (itr == ptrMap.end())
		llvm_unreachable("Failed to getPointsToTarget()!");

	return itr->second;
}

void GeneratorEnvironment::recordStore(llvm::Value* ptr)
{
	assert(ptr->getType()->isPointerTy() && "Setting a non-ptr?");
	ptrMap.at(ptr).setValid();
}

void GeneratorEnvironment::recordStore(Value* val, Value* ptr)
{
	assert(ptr->getType()->isPointerTy() && "Setting a non-ptr?");
	if (!val->getType()->isPointerTy())
	{
		ptrMap.at(ptr).setValid();
		return;
	}

	auto& valMem = ptrMap.at(val);
	auto& ptrMem = ptrMap.at(ptr);

	if (!valMem.isValid())
		llvm_unreachable("Storing from an undefined ptr!");

	ptrMem.setValid();
}
