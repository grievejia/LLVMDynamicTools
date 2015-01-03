#include "ValuePool.h"
#include "Random.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

using namespace llvm;
using namespace llvm_fuzzer;

template <typename Filter>
static Value* sampleSpecialValueFromPool(const std::vector<Value*>& pool, Random& randomGenerator, Filter filter)
{
	auto idx = randomGenerator.getRandomUInt32();
	auto poolSize = pool.size();
	assert(poolSize != 0);

	for (unsigned i = 0; i < poolSize; ++i)
	{
		auto val = pool.at((idx + i) % poolSize);
		if (filter(val))
			return val;
	}
	return nullptr;
}

ValuePool::ValuePool(Random& r, LLVMContext& context): randomGenerator(r)
{
	// Initialize type pool to contain all scalar types
	typePool.push_back(Type::getInt1Ty(context));
	typePool.push_back(Type::getInt8Ty(context));
	typePool.push_back(Type::getInt16Ty(context));
	typePool.push_back(Type::getInt32Ty(context));
	typePool.push_back(Type::getInt64Ty(context));
}

void ValuePool::addType(llvm::Type* type)
{
	auto itr = std::find(typePool.begin(), typePool.end(), type);
	if (itr == typePool.end())
		return;
	typePool.insert(itr, type);
}

void ValuePool::addValue(llvm::Value* val, bool defined)
{
	if (isa<UndefValue>(val))
		return;
	pool.push_back(val);
	if (val->getType()->isPointerTy() && defined)
		definedPtrs.insert(val);
	addType(val->getType());
}

Value* ValuePool::sampleValueFromPool() const
{
	assert(!pool.empty());
	return pool.at(randomGenerator.getRandomUInt32() % pool.size());
}

Value* ValuePool::samplePointerFromPool(bool mustValid) const
{
	auto retVal = sampleSpecialValueFromPool(pool, randomGenerator, 
		[this, mustValid] (const Value* val)
		{
			return val->getType()->isPointerTy() && (!mustValid || definedPtrs.count(val));
		}
	);

	if (retVal == nullptr)
		llvm_unreachable("No valid pointer value in value pool");

	return retVal;
}

Value* ValuePool::sampleScalarFromPool() const
{
	auto retVal = sampleSpecialValueFromPool(pool, randomGenerator, 
		[] (const Value* val)
		{
			return !val->getType()->isPointerTy();
		}
	);

	if (retVal == nullptr)
		retVal = getRandomConstantNumber(getRandomScalarType());

	return retVal;
}

Constant* ValuePool::getRandomConstantNumber(Type* type) const
{
	if (auto intType = dyn_cast<IntegerType>(type))
	{
		auto isSigned = randomGenerator.getRandomBool();
		return ConstantInt::get(intType, randomGenerator.getRandomUInt64(), isSigned);
	}
	else
		llvm_unreachable("getRandomConstantNumber() received an unsupported type");
}

Value* ValuePool::getRandomValueOfType(Type* type) const
{
	// Search the existing pool for the required type
	auto typeEqPredicate = [type] (const Value* val)
	{
		return (val->getType() == type);
	};
	auto retVal = sampleSpecialValueFromPool(pool, randomGenerator, typeEqPredicate);
	if (retVal != nullptr)
		return retVal;

	// If the requested type was not found, generate a constant value.
	if (type->isIntegerTy())
	{
		return getRandomConstantNumber(type);
	}
	else
		llvm_unreachable("Failed to generate value of a given type");
}

Type* ValuePool::getRandomScalarType() const
{
	assert(typePool.size() >= NumScalarTypes);
	return typePool.at(randomGenerator.getRandomUInt32() % NumScalarTypes);
}

Type* ValuePool::getRandomType() const
{
	assert(typePool.size() >= NumScalarTypes);
	return typePool.at(randomGenerator.getRandomUInt32() % typePool.size());
}
