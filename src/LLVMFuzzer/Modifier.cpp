#include "Modifier.h"
#include "Random.h"

#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm_fuzzer;

void Modifier::addAllocaInst()
{
	auto randType = valuePool.getRandomType();
	// FIXME: create array-based alloca
	auto allocInst = builder.CreateAlloca(randType, nullptr, "avar");
	valuePool.addValue(allocInst, false);

	// With 1/3 probability, create a pointer on top of the allocated value
	// FIXME: make that prob. tunable
	while (randomGenerator.getRandomBool(3))
	{
		auto allocPtrInst = builder.CreateAlloca(PointerType::getUnqual(randType), nullptr, "aptr");
		valuePool.addValue(allocPtrInst, false);
		randType = allocPtrInst->getAllocatedType();
	}
}

void Modifier::addStoreInst()
{
	auto randPtr = valuePool.samplePointerFromPool();

	auto elemType = cast<PointerType>(randPtr->getType())->getElementType();
	auto randVal = valuePool.getRandomValueOfType(elemType);

	builder.CreateStore(randVal, randPtr);
	valuePool.addValue(randPtr);
}

void Modifier::addLoadInst()
{
	auto randValidPtr = valuePool.samplePointerFromPool(true);
	auto loadInst = builder.CreateLoad(randValidPtr, "lvar");
	valuePool.addValue(loadInst, false);
}

void Modifier::addBinaryInst()
{
	auto op0 = valuePool.sampleScalarFromPool();
	auto op1 = valuePool.getRandomValueOfType(op0->getType());

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
			auto condVal = valuePool.sampleScalarFromPool();
			auto boolType = Type::getInt1Ty(bb->getContext());
			if (condVal->getType() != boolType)
				condVal = addCastInst(condVal, boolType);
			retInst = builder.CreateSelect(condVal, op0, op1, "sel");
			break;
		}
		default:
			llvm_unreachable("Unhandled binary inst");
	}

	valuePool.addValue(retInst);
}

void Modifier::addCmpInst()
{
	auto op0 = valuePool.sampleScalarFromPool();
	auto op1 = valuePool.getRandomValueOfType(op0->getType());

	auto predInt = randomGenerator.getRandomUInt32(CmpInst::FIRST_ICMP_PREDICATE, CmpInst::LAST_ICMP_PREDICATE);
	auto retInst = builder.CreateICmp(static_cast<CmpInst::Predicate>(predInt), op0, op1, "cmp");
	valuePool.addValue(retInst);
}

Value* Modifier::addCastInst(Value* val, Type* dstType)
{
	auto srcType = val->getType();
	if (srcType == dstType)
		return val;

	if (srcType->isPointerTy())
	{
		if (!dstType->isPointerTy())
			llvm_unreachable("Illegal cast from pointer to non-pointer");

		auto castInst = builder.CreateBitCast(val, dstType, val->getName() + ".cast");
		valuePool.addValue(castInst);
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
				valuePool.addValue(castInst);
				return castInst;
			}
			else
			{
				auto castInst = randomGenerator.getRandomBool() ? builder.CreateSExt(val, dstIntType, val->getName() + ".sext") : builder.CreateZExt(val, dstIntType, val->getName() + ".zext");
				valuePool.addValue(castInst);
				return castInst;
			}
		}
		else if (dstType->isPointerTy())
			llvm_unreachable("Illegal cast from int to pointer");
	}

	llvm_unreachable("Unhandled cast case");
}

void Modifier::addReturnInst()
{
	auto itr = bb->rbegin();
	for (auto ite = bb->rend(); itr != ite; ++itr)
	{
		auto instType = itr->getType();
		if (instType->isSingleValueType() && !instType->isPointerTy())
			break;
	}
	if (itr == bb->rend())
		builder.CreateRet(valuePool.getRandomValueOfType(Type::getInt32Ty(bb->getContext())));
	else
		builder.CreateRet(addCastInst(&*itr, Type::getInt32Ty(bb->getContext())));
}

void Modifier::addAllocations(unsigned numAlloc)
{
	for (auto i = 0; i < numAlloc; ++i)
		addAllocaInst();
}

void Modifier::addOperations(unsigned numOps)
{
	auto counter = 0u;
	auto numStore = 0u, numLoad = 0u;
	while (counter < numOps)
	{
		auto opTypeInt = randomGenerator.getRandomUInt32(0, 4);
		switch (opTypeInt)
		{
			case 0:
			case 1:
				addBinaryInst();
				break;
			case 2:
				addStoreInst();
				++numStore;
				break;
			case 3:
				if (numLoad < numStore)
				{
					++numLoad;
					addLoadInst();
				}
				else
					continue;
				break;
			case 4:
				addCmpInst();
				break;
			default:
				llvm_unreachable("addOperations() failed to handle all cases");
		}
		++counter;
	}
}
