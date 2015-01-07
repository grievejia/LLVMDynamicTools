#include "BlockGenerator.h"
#include "GeneratorEnvironment.h"
#include "Random.h"

#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm_fuzzer;

Value* BlockGenerator::getRandomOperand()
{
	auto randVal = valueGenerator.getRandomValue();
	if (randVal->getType()->isPointerTy() && env.getPointsToTarget(randVal).isValid())
	{
		// With 1/3 probability, dereference that pointer
		if (randomGenerator.getRandomBool(3))
		{
			randVal = builder.CreateLoad(randVal, "ldvar");
			env.addValue(randVal);
		}
	}
	return randVal;
}

Value* BlockGenerator::getRandomOperandOfType(Type* type)
{
	auto opSize = env.getNumValueOfType(type);
	auto ptrType = PointerType::getUnqual(type);
	auto ptrSize = env.getNumValueOfType(ptrType);

	if (opSize + ptrSize == 0)
		return valueGenerator.getRandomValueOfType(type);

	auto idx = randomGenerator.getRandomUInt32(0, opSize + ptrSize - 1);
	if (idx < opSize)
		return valueGenerator.getRandomValueOfType(type);

	idx -= opSize;
	auto ptr = valueGenerator.getRandomValueOfType(ptrType);
	if (env.getPointsToTarget(ptr).isValid())
		return builder.CreateLoad(ptr);
	else
		return valueGenerator.getRandomValueOfType(type);
}

Value* BlockGenerator::addAllocaInst()
{
	auto randType = valueGenerator.getRandomScalarType();
	// FIXME: create array-based alloca
	auto allocInst = builder.CreateAlloca(randType, nullptr, "avar");
	env.addValue(allocInst);
	builder.CreateStore(valueGenerator.getRandomValueOfType(randType), allocInst);
	env.recordStore(allocInst);

	// With 1/3 probability, create a pointer on top of the allocated value
	// FIXME: make that prob. tunable
	auto retInst = allocInst;
	while (randomGenerator.getRandomBool(3))
	{
		auto allocPtrInst = builder.CreateAlloca(PointerType::getUnqual(randType), nullptr, "aptr");
		builder.CreateStore(allocInst, allocPtrInst);
		env.addValue(allocPtrInst);
		env.recordStore(allocInst, allocPtrInst);
		randType = allocPtrInst->getAllocatedType();
		allocInst = allocPtrInst;
	}
	return retInst;
}

Value* BlockGenerator::addStoreInst()
{
	auto randPtr = valueGenerator.getRandomPointer();

	auto elemType = cast<PointerType>(randPtr->getType())->getElementType();
	auto randVal = getRandomOperandOfType(elemType);

	auto storeInst = builder.CreateStore(randVal, randPtr);
	env.recordStore(randVal, randPtr);
	return storeInst;
}

Value* BlockGenerator::addBinaryInst()
{
	auto op0 = valueGenerator.getRandomScalar();
	auto op1 = getRandomOperandOfType(op0->getType());

	auto randOpInt = randomGenerator.getRandomUInt32() % 17u;
	Value* retInst = nullptr;
	switch (randOpInt)
	{
		case 0:
		case 1:
			retInst = builder.CreateAdd(op0, op1, "add");
			break;
		case 2:
		case 3:
			retInst = builder.CreateSub(op0, op1, "sub");
			break;
		case 4:
		case 5:
			retInst = builder.CreateMul(op0, op1, "mul");
			break;
		case 6:
		case 7:
			retInst = builder.CreateAnd(op0, op1, "and");
			break;
		case 8:
		case 9:
			retInst = builder.CreateOr(op0, op1, "or");
			break;
		case 10:
		case 11:
			retInst = builder.CreateXor(op0, op1, "xor");
			break;
		// The problem with div and rem is that the second operand should not be zero.
		case 12:
		{
			auto isZeroPredicate = builder.CreateICmpEQ(op1, Constant::getNullValue(op1->getType()));

			auto randNonZeroVal = ConstantInt::get(op1->getType(), randomGenerator.getRandomUInt32());
			while (randNonZeroVal->isNullValue())
				randNonZeroVal = ConstantInt::get(op1->getType(), randomGenerator.getRandomUInt32());
			auto op2 = builder.CreateSelect(isZeroPredicate, randNonZeroVal, op1);

			auto opTypeInt = randomGenerator.getRandomUInt32() % 4;
			switch (opTypeInt)
			{
				case 0:
					retInst = builder.CreateUDiv(op0, op2, "udiv");
					break;
				case 1:
					retInst = builder.CreateSDiv(op0, op2, "sdiv");
					break;
				case 2:
					retInst = builder.CreateURem(op0, op2, "urem");
					break;
				default:
					retInst = builder.CreateSRem(op0, op2, "srem");
					break;
			}
			
			break;
		}
		case 13:
		case 14:
		case 15:
		{
			auto intType = cast<IntegerType>(op0->getType());
			auto width = intType->getBitWidth();
			auto shiftAmount = randomGenerator.getRandomUInt32() % width + 1;
			if (randOpInt == 16)
				retInst = builder.CreateShl(op0, ConstantInt::get(intType, shiftAmount), "shl");
			else if (randOpInt == 17)
				retInst = builder.CreateLShr(op0, ConstantInt::get(intType, shiftAmount), "lshr");
			else
				retInst = builder.CreateAShr(op0, ConstantInt::get(intType, shiftAmount), "ashr");

			break;
		}
		case 16:
		{
			auto condVal = valueGenerator.getRandomScalar();
			auto boolType = Type::getInt1Ty(bb->getContext());
			if (condVal->getType() != boolType)
				condVal = addCastInst(condVal, boolType);
			retInst = builder.CreateSelect(condVal, op0, op1, "sel");
			break;
		}
		default:
			llvm_unreachable("Unhandled binary inst");
	}

	env.addValue(retInst);
	return retInst;
}

Value* BlockGenerator::addCmpInst()
{
	auto op0 = valueGenerator.getRandomScalar();
	auto op1 = getRandomOperandOfType(op0->getType());

	auto predInt = randomGenerator.getRandomUInt32(CmpInst::FIRST_ICMP_PREDICATE, CmpInst::LAST_ICMP_PREDICATE);
	auto retInst = builder.CreateICmp(static_cast<CmpInst::Predicate>(predInt), op0, op1, "cmp");
	env.addValue(retInst);
	return retInst;
}

static bool isCastSafe(Type* srcType, Type* dstType)
{
	if (srcType == dstType)
		return true;

	if (isa<IntegerType>(srcType) && isa<IntegerType>(dstType))
		return true;

	return false;
}

Value* BlockGenerator::addCastInst(Value* val, Type* dstType)
{
	auto srcType = val->getType();
	if (srcType == dstType)
		return val;

	if (srcType->isPointerTy())
	{
		if (!dstType->isPointerTy())
			llvm_unreachable("Illegal cast from pointer to non-pointer");

		auto castInst = builder.CreateBitCast(val, dstType, val->getName() + ".cast");
		env.addValue(castInst);
		return castInst;
	}

	if (auto srcIntType = dyn_cast<IntegerType>(srcType))
	{
		if (auto dstIntType = dyn_cast<IntegerType>(dstType))
		{
			auto srcWidth = srcIntType->getBitWidth();
			auto dstWidth = dstIntType->getBitWidth();
			if (srcWidth > dstWidth)
			{
				auto castInst = builder.CreateTrunc(val, dstIntType, val->getName() + ".trunc");
				env.addValue(castInst);
				return castInst;
			}
			else
			{
				auto castInst = randomGenerator.getRandomBool() ? builder.CreateSExt(val, dstIntType, val->getName() + ".sext") : builder.CreateZExt(val, dstIntType, val->getName() + ".zext");
				env.addValue(castInst);
				return castInst;
			}
		}
		else if (dstType->isPointerTy())
			llvm_unreachable("Illegal cast from int to pointer");
	}

	llvm_unreachable("Unhandled cast case");
}

Value* BlockGenerator::addReturnInst(Type* retType)
{
	auto itr = bb->rbegin();
	for (auto ite = bb->rend(); itr != ite; ++itr)
	{
		auto instType = itr->getType();
		if (isCastSafe(instType, retType))
			return builder.CreateRet(addCastInst(&*itr, retType));
	}

	// If we can't find an instruction in the current block with type retType, randomly return a value of retType from the environment
	return builder.CreateRet(valueGenerator.getRandomValueOfType(retType));
}

void BlockGenerator::addAllocations(unsigned numAlloc)
{
	for (auto i = 0; i < numAlloc; ++i)
		addAllocaInst();
}

void BlockGenerator::addOperations(unsigned numOps)
{
	auto counter = 0u;
	while (counter < numOps)
	{
		auto opTypeInt = randomGenerator.getRandomUInt32(0, 3);
		switch (opTypeInt)
		{
			case 0:
			case 1:
				addBinaryInst();
				break;
			case 2:
				addStoreInst();
				break;
			case 3:
				addCmpInst();
				break;
			default:
				llvm_unreachable("addOperations() failed to handle all cases");
		}
		++counter;
	}
}
