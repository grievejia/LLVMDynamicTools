#include "GeneratorEnvironment.h"
#include "Random.h"
#include "ValueGenerator.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

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

Value* ValueGenerator::getRandomValue() const
{
	if (env.isEmpty())
		llvm_unreachable("Trying to get a random value from an empty pool");
	return env.getValueAtIndex(randomGenerator.getRandomUInt32(0, env.getSize() - 1));
}

Value* ValueGenerator::getRandomPointer(bool mustValid) const
{
	if (!env.hasPointer())
		llvm_unreachable("Trying to get a random pointer from a pointer-free pool");

	auto numPtr = env.getNumPointer();
	auto ptrIdx = randomGenerator.getRandomUInt32(0, numPtr - 1);
	if (!mustValid)
	{
		// Easy case first
		return env.getPointerAtIndex(ptrIdx);
	}
	else
	{
		// We must scan through the pool, looking for a valid pointer
		for (unsigned i = 1; i < numPtr; ++i)
		{
			auto newPtr = env.getPointerAtIndex((ptrIdx + i) % numPtr);
			if (env.getPointsToTarget(newPtr).isValid())
				return newPtr;
		}
		llvm_unreachable("Unable to find a valid pointer");
	}
}

Value* ValueGenerator::getRandomScalar() const
{
	auto retType = getRandomScalarType();
	return getRandomValueOfType(retType);
}

Constant* ValueGenerator::getRandomConstantNumber(Type* type) const
{
	if (auto intType = dyn_cast<IntegerType>(type))
	{
		auto isSigned = randomGenerator.getRandomBool();
		return ConstantInt::get(intType, randomGenerator.getRandomUInt64(), isSigned);
	}
	else
		llvm_unreachable("getRandomConstantNumber() received an unsupported type");
}

Value* ValueGenerator::getRandomValueOfType(Type* type) const
{
	// Search the existing pool for the required type
	auto& valuePool = env.getAttachedValues(type);

	if (!valuePool.empty())
		return valuePool.at(randomGenerator.getRandomUInt32(0, valuePool.size() - 1));

	// If the requested type was not found, generate a constant value.
	if (type->isIntegerTy())
	{
		return getRandomConstantNumber(type);
	}
	else
	{
		errs() << "getRandomValueOfType() received " << *type << "\n";
		llvm_unreachable("Failed to generate value of a given type");
	}
}

Type* ValueGenerator::getRandomScalarType() const
{
	auto randInt = randomGenerator.getRandomUInt32(0, 4);
	switch (randInt)
	{
		case 0:
			return Type::getInt1Ty(env.getContext());
		case 1:
			return Type::getInt8Ty(env.getContext());
		case 2:
			return Type::getInt16Ty(env.getContext());
		case 3:
			return Type::getInt32Ty(env.getContext());
		case 4:
			return Type::getInt64Ty(env.getContext());
		default:
			llvm_unreachable("Unhandled case in getRandomScalarType()");
	}
}

Type* ValueGenerator::getRandomType() const
{
	return env.getTypeAtIndex(randomGenerator.getRandomUInt32(0, env.getNumType() - 1));
}
