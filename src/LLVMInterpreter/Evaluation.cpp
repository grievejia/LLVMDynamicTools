#include "Interpreter.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

using namespace llvm;
using namespace llvm_interpreter;

static Value* findBasePointer(const Value* val)
{
	if (auto itp = cast<IntToPtrInst>(val))
	{
		Value* srcValue = nullptr;
		auto op = itp->getOperand(0);
		if (PatternMatch::match(op, PatternMatch::m_PtrToInt(PatternMatch::m_Value(srcValue))))
		{
			return srcValue->stripPointerCasts();
		}
		else if (PatternMatch::match(op,
				PatternMatch::m_Add(
					PatternMatch::m_PtrToInt(
					PatternMatch::m_Value(srcValue)),
				PatternMatch::m_Value())))
		{
			return srcValue->stripPointerCasts();
		}
	}
	
	llvm_unreachable("Unhandled case for findBasePointer()");
}

const DynamicValue& Interpreter::readFromPointer(const PointerValue& ptr)
{
	switch (ptr.getAddressSpace())
	{
		case PointerAddressSpace::GLOBAL_SPACE:
			return globalMem.read(ptr.getAddress());
		case PointerAddressSpace::STACK_SPACE:
			return stackMem.read(ptr.getAddress());
		case PointerAddressSpace::HEAP_SPACE:
			return heapMem.read(ptr.getAddress());
	}
}

void Interpreter::writeToPointer(const PointerValue& ptr, DynamicValue&& val)
{
	switch (ptr.getAddressSpace())
	{
		case PointerAddressSpace::GLOBAL_SPACE:
			return globalMem.write(ptr.getAddress(), std::move(val));
		case PointerAddressSpace::STACK_SPACE:
			return stackMem.write(ptr.getAddress(), std::move(val));
		case PointerAddressSpace::HEAP_SPACE:
			return heapMem.write(ptr.getAddress(), std::move(val));
	}
}

DynamicValue Interpreter::evaluateConstant(const llvm::Constant* cv)
{
	switch (cv->getValueID())
	{
		case Value::UndefValueVal:
		{
			auto type = cv->getType();
			if (type->isStructTy())
			{
				auto stType = cast<StructType>(type);
				auto stLayout = dataLayout.getStructLayout(stType);

				auto retVal = DynamicValue::getStructValue(dataLayout.getTypeAllocSize(stType));
				auto& structVal = retVal.getAsStructValue();
				for (auto i = 0u, e = stType->getNumElements(); i < e; ++i)
				{
					auto elemType = stType->getElementType(i);
					auto offset = stLayout->getElementOffset(i);
					if (!elemType->isAggregateType())
						structVal.addField(offset, DynamicValue::getUndefValue());
					else
						structVal.addField(offset, evaluateConstant(UndefValue::get(elemType)));
				}
				return std::move(retVal);
			}
			else if (type->isArrayTy())
			{
				auto arrayType = cast<ArrayType>(type);
				auto elemType = arrayType->getElementType();
				auto arraySize = arrayType->getNumElements();

				auto retVal = DynamicValue::getArrayValue(arraySize, dataLayout.getTypeAllocSize(elemType));
				auto& arrayVal = retVal.getAsArrayValue();
				for (unsigned i = 0; i < arraySize; ++i)
				{
					if (elemType->isAggregateType())
						arrayVal.setElementAtIndex(i, evaluateConstant(UndefValue::get(elemType)));
				}
				return std::move(retVal);
			}
			else if (type->isVectorTy())
				llvm_unreachable("Vector type not supported");
			else
				return DynamicValue::getUndefValue();
		}
		case Value::GlobalAliasVal:
		{
			auto glbAlias = cast<GlobalAlias>(cv);
			return evaluateConstant(glbAlias->getAliasee());
		}
		case Value::GlobalVariableVal:
		{
			auto glbVar = cast<GlobalValue>(cv);
			auto globalAddr = globalEnv.at(glbVar);
			return DynamicValue::getPointerValue(PointerAddressSpace::GLOBAL_SPACE, globalAddr);
		}
		case Value::ConstantAggregateZeroVal:
		{
			auto caz = cast<ConstantAggregateZero>(cv);
			auto type = cv->getType();
			if (type->isStructTy())
			{
				auto stType = cast<StructType>(type);
				auto stLayout = dataLayout.getStructLayout(stType);

				auto retVal = DynamicValue::getStructValue(dataLayout.getTypeAllocSize(stType));
				auto& structVal = retVal.getAsStructValue();
				for (auto i = 0u, e = stType->getNumElements(); i < e; ++i)
				{
					auto offset = stLayout->getElementOffset(i);
					structVal.addField(offset, evaluateConstant(caz->getStructElement(i)));
				}
				return std::move(retVal);
			}
			else if (type->isArrayTy())
			{
				auto arrayType = cast<ArrayType>(type);
				auto elemType = arrayType->getElementType();
				auto arraySize = arrayType->getNumElements();

				auto retVal = DynamicValue::getArrayValue(arraySize, dataLayout.getTypeAllocSize(elemType));
				auto& arrayVal = retVal.getAsArrayValue();
				for (unsigned i = 0; i < arraySize; ++i)
				{
					arrayVal.setElementAtIndex(i, evaluateConstant(caz->getSequentialElement()));
				}
				return std::move(retVal);
			}
			else
				llvm_unreachable("ConstantAggregateZero not an array or a struct?");
		}
		case Value::ConstantDataArrayVal:
		{
			auto cda = cast<ConstantDataArray>(cv);
			auto arraySize = cda->getNumElements();

			auto retVal = DynamicValue::getArrayValue(arraySize, dataLayout.getTypeAllocSize(cda->getType()->getElementType()));
			auto& arrayVal = retVal.getAsArrayValue();
			for (unsigned i = 0; i < arraySize; ++i)
				arrayVal.setElementAtIndex(i, evaluateConstant(cda->getElementAsConstant(i)));
			return retVal;
		}
		case Value::ConstantIntVal:
		{
			auto cInt = cast<ConstantInt>(cv);
			return DynamicValue::getIntValue(cInt->getValue());
		}
		case Value::ConstantFPVal:
		{
			auto cFloat = cast<ConstantFP>(cv);
			return DynamicValue::getFloatValue(cFloat->getValueAPF().convertToDouble());
		}
		case Value::ConstantArrayVal:
		{
			auto cArray = cast<ConstantArray>(cv);
			auto arraySize = cArray->getType()->getNumElements();

			auto retVal = DynamicValue::getArrayValue(arraySize, dataLayout.getTypeAllocSize(cArray->getType()->getElementType()));
			auto& arrayVal = retVal.getAsArrayValue();
			for (unsigned i = 0; i < arraySize; ++i)
				arrayVal.setElementAtIndex(i, evaluateConstant(cArray->getOperand(i)));
			return retVal;
		}
		case Value::ConstantStructVal:
		{
			auto cStruct = cast<ConstantStruct>(cv);
			auto stSize = cStruct->getType()->getNumElements();
			auto stLayout = dataLayout.getStructLayout(cStruct->getType());

			auto retVal = DynamicValue::getStructValue(dataLayout.getTypeAllocSize(cStruct->getType()));
			auto& structVal = retVal.getAsStructValue();
			for (unsigned i = 0; i < stSize; ++i)
			{
				auto offset = stLayout->getElementOffset(i);
				structVal.addField(offset, evaluateConstant(cStruct->getOperand(i)));
			}
			return retVal;
		}
		case Value::ConstantPointerNullVal:
			return DynamicValue::getPointerValue(PointerAddressSpace::GLOBAL_SPACE, 0);
		case Value::ConstantExprVal:
		{
			auto cExpr = cast<ConstantExpr>(cv);
			return evaluateConstantExpr(cExpr);
		}
		case Value::FunctionVal:
		{
			auto fun = cast<Function>(cv);
			auto funAddr = globalEnv.at(fun);
			return DynamicValue::getPointerValue(PointerAddressSpace::GLOBAL_SPACE, funAddr);
		}
	}

	llvm_unreachable("unsupported constant for evaluateConstant()");
}

DynamicValue Interpreter::evaluateConstantExpr(const llvm::ConstantExpr* cexpr)
{
	// Local helper function to avoid code duplication
	auto evaluateConstantIntUnOp = [this, cexpr] (auto unOp) -> DynamicValue
	{
		auto srcVal = evaluateConstant(cexpr->getOperand(0));
		auto& srcIntVal = srcVal.getAsIntValue();
		// Heap memory reuse
		srcIntVal.setInt(unOp(srcIntVal.getInt()));
		return std::move(srcVal);
	};

	auto evaluateConstantIntBinOp = [this, cexpr] (auto binOp) -> DynamicValue
	{
		auto srcVal0 = evaluateConstant(cexpr->getOperand(0));
		auto srcVal1 = evaluateConstant(cexpr->getOperand(1));

		auto& srcIntVal0 = srcVal0.getAsIntValue();
		auto& srcIntVal1 = srcVal1.getAsIntValue();
		// Heap memory reuse
		srcIntVal0.setInt(binOp(srcIntVal0.getInt(), srcIntVal1.getInt()));
		return std::move(srcVal0);
	};

	auto evaluateConstantFloatBinOp = [this, cexpr] (auto binOp) -> DynamicValue
	{
		auto srcVal0 = evaluateConstant(cexpr->getOperand(0));
		auto srcVal1 = evaluateConstant(cexpr->getOperand(1));

		auto& srcFloatVal0 = srcVal0.getAsFloatValue();
		auto& srcFloatVal1 = srcVal1.getAsFloatValue();
		// Heap memory reuse
		srcFloatVal0.setFloat(binOp(srcFloatVal0.getFloat(), srcFloatVal1.getFloat()));
		return std::move(srcVal0);
	};

	switch (cexpr->getOpcode())
	{
		case Instruction::Trunc:
		{
			auto intWidth = cast<IntegerType>(cexpr->getType())->getBitWidth();
			return evaluateConstantIntUnOp(
				[intWidth] (const llvm::APInt& intVal)
				{
					return intVal.trunc(intWidth);
				}
			);
		}
		case Instruction::ZExt:
		{
			auto intWidth = cast<IntegerType>(cexpr->getType())->getBitWidth();
			return evaluateConstantIntUnOp(
				[intWidth] (const llvm::APInt& intVal)
				{
					return intVal.zext(intWidth);
				}
			);
		}
		case Instruction::SExt:
		{
			auto intWidth = cast<IntegerType>(cexpr->getType())->getBitWidth();
			return evaluateConstantIntUnOp(
				[intWidth] (const llvm::APInt& intVal)
				{
					return intVal.sext(intWidth);
				}
			);
		}
		case Instruction::FPTrunc:
		{
			auto srcType = cexpr->getOperand(0)->getType();
			auto dstType = cexpr->getType();
			if (!(srcType->isDoubleTy() && dstType->isFloatTy()))
				llvm_unreachable("Invalid FPTrunc instruction");

			auto srcVal = evaluateConstant(cexpr->getOperand(0));
			auto& srcFloatVal = srcVal.getAsFloatValue();
			// Heap memory reuse
			srcFloatVal.setFloat(static_cast<float>(srcFloatVal.getFloat()));
			return std::move(srcVal);
		}
		case Instruction::FPExt:
		{
			auto srcType = cexpr->getOperand(0)->getType();
			auto dstType = cexpr->getType();
			if (!(dstType->isDoubleTy() && srcType->isFloatTy()))
				llvm_unreachable("Invalid FPExt instruction");

			auto srcVal = evaluateConstant(cexpr->getOperand(0));
			// FPExt is a noop in this implementation
			return std::move(srcVal);
		}
		case Instruction::UIToFP:
		case Instruction::SIToFP:
		{
			auto srcVal = evaluateConstant(cexpr->getOperand(0));
			return DynamicValue::getFloatValue(APIntOps::RoundAPIntToDouble(srcVal.getAsIntValue().getInt()));
		}
		case Instruction::FPToUI:
		case Instruction::FPToSI:
		{
			auto intWidth = cast<IntegerType>(cexpr->getType())->getBitWidth();

			auto srcVal = evaluateConstant(cexpr->getOperand(0));
			return DynamicValue::getIntValue(APIntOps::RoundDoubleToAPInt(srcVal.getAsFloatValue().getFloat(), intWidth));
		}
		case Instruction::PtrToInt:
		{
			auto intWidth = cast<IntegerType>(cexpr->getType())->getBitWidth();
			auto srcVal = evaluateConstant(cexpr->getOperand(0)).getAsPointerValue().getAddress();
			return DynamicValue::getIntValue(APInt(intWidth, srcVal));
		}
		case Instruction::IntToPtr:
		{
			auto srcVal = evaluateConstant(cexpr->getOperand(0));
			auto ptrSize = dataLayout.getPointerSizeInBits();
			return DynamicValue::getPointerValue(PointerAddressSpace::GLOBAL_SPACE, srcVal.getAsIntValue().getInt().zextOrTrunc(ptrSize).getZExtValue());
		}
		case Instruction::BitCast:
		{
			auto op0 = cexpr->getOperand(0);
			auto srcType = op0->getType();
			auto dstType = cexpr->getType();
			auto srcVal = evaluateConstant(op0);

			if (dstType->isPointerTy())
			{
				if (!srcType->isPointerTy())
					llvm_unreachable("Invalid Bitcast");
				return std::move(srcVal);
			}
			else if (dstType->isIntegerTy())
			{
				if (srcType->isFloatTy() || srcType->isDoubleTy())
				{
					return DynamicValue::getIntValue(APInt::doubleToBits(srcVal.getAsFloatValue().getFloat()));
				}
				else if (srcType->isIntegerTy())
				{
					return std::move(srcVal);
				}
				else
					llvm_unreachable("Invalid BitCast");
			}
			else if (dstType->isFloatTy() || dstType->isDoubleTy())
			{
				if (srcType->isIntegerTy())
				{
					return DynamicValue::getFloatValue(srcVal.getAsIntValue().getInt().bitsToDouble());
				}
				else
				{
					return std::move(srcVal);
				}
			}
			else
				llvm_unreachable("Invalid Bitcast");
		}
		case Instruction::Add:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 + i1;
				}
			);
		}
		case Instruction::Sub:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 - i1;
				}
			);
		}
		case Instruction::Mul:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 * i1;
				}
			);
		}
		case Instruction::UDiv:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.udiv(i1);
				}
			);
		}
		case Instruction::SDiv:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.sdiv(i1);
				}
			);
		}
		case Instruction::URem:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.urem(i1);
				}
			);
		}
		case Instruction::SRem:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.srem(i1);
				}
			);
		}
		case Instruction::And:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 & i1;
				}
			);
		}
		case Instruction::Or:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 | i1;
				}
			);
		}
		case Instruction::Xor:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 ^ i1;
				}
			);
		}
		case Instruction::Shl:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& value, const APInt& shift)
				{
					auto shiftAmount = shift.getZExtValue();
					auto valueWidth = value.getBitWidth();
					if (shiftAmount > valueWidth)
						llvm_unreachable("Illegal shift amount");
					return value.shl(shiftAmount);
				}
			);
		}
		case Instruction::LShr:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& value, const APInt& shift)
				{
					auto shiftAmount = shift.getZExtValue();
					auto valueWidth = value.getBitWidth();
					if (shiftAmount > valueWidth)
						llvm_unreachable("Illegal shift amount");
					return value.lshr(shiftAmount);
				}
			);
		}
		case Instruction::AShr:
		{
			return evaluateConstantIntBinOp(
				[] (const APInt& value, const APInt& shift)
				{
					auto shiftAmount = shift.getZExtValue();
					auto valueWidth = value.getBitWidth();
					if (shiftAmount > valueWidth)
						llvm_unreachable("Illegal shift amount");
					return value.ashr(shiftAmount);
				}
			);
		}
		case Instruction::ICmp:
		{
			auto cmp = [cexpr] (const APInt& i0, const APInt& i1)
			{
				switch (cexpr->getPredicate())
				{
					case CmpInst::ICMP_EQ:
						return APInt(1, i0 == i1);
					case CmpInst::ICMP_NE:
						return APInt(1, i0 != i1);
					case CmpInst::ICMP_UGT:
						return APInt(1, i0.ugt(i1));
					case CmpInst::ICMP_UGE:
						return APInt(1, i0.uge(i1));
					case CmpInst::ICMP_ULT:
						return APInt(1, i0.ult(i1));
					case CmpInst::ICMP_ULE:
						return APInt(1, i0.ule(i1));
					case CmpInst::ICMP_SGT:
						return APInt(1, i0.sgt(i1));
					case CmpInst::ICMP_SGE:
						return APInt(1, i0.sge(i1));
					case CmpInst::ICMP_SLT:
						return APInt(1, i0.slt(i1));
					case CmpInst::ICMP_SLE:
						return APInt(1, i0.sle(i1));
					default:
						llvm_unreachable("Illegal icmp predicate");
				}
			};

			// ICmp can compare both integers and pointers, so we cannot just use evaluateIntBinOp
			auto val0 = evaluateConstant(cexpr->getOperand(0));
			auto val1 = evaluateConstant(cexpr->getOperand(1));
			if (val0.isIntValue() && val1.isIntValue())
			{
				auto& intVal0 = val0.getAsIntValue();
				auto res = cmp(intVal0.getInt(), val1.getAsIntValue().getInt());
				intVal0.setInt(res);
				return std::move(val0);
			}
			else if (val0.isPointerValue() && val1.isPointerValue())
			{
				auto ptrSize = dataLayout.getPointerSizeInBits();
				auto addr0 = val0.getAsPointerValue().getAddress();
				auto addr1 = val1.getAsPointerValue().getAddress();
				auto res = cmp(APInt(ptrSize, addr0), APInt(ptrSize, addr1));
				return DynamicValue::getIntValue(res);
			}
			else
				llvm_unreachable("Illegal icmp compare types");
		}
		case Instruction::FAdd:
		{
			return evaluateConstantFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 + f1;
				}
			);
		}
		case Instruction::FSub:
		{
			return evaluateConstantFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 - f1;
				}
			);
		}
		case Instruction::FMul:
		{
			return evaluateConstantFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 * f1;
				}
			);
		}
		case Instruction::FDiv:
		{
			return evaluateConstantFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 / f1;
				}
			);
		}
		case Instruction::FRem:
		{
			return evaluateConstantFloatBinOp(
				[] (double f0, double f1)
				{
					return std::remainder(f0, f1);
				}
			);
		}
		case Instruction::FCmp:
		{
			auto srcVal0 = evaluateConstant(cexpr->getOperand(0));
			auto srcVal1 = evaluateConstant(cexpr->getOperand(1));

			auto f0 = srcVal0.getAsFloatValue().getFloat();
			auto f1 = srcVal1.getAsFloatValue().getFloat();
			auto isF0Nan = std::isnan(f0);
			auto isF1Nan = std::isnan(f1);
			auto bothNotNan = !isF0Nan && !isF1Nan;
			auto eitherIsNan = isF0Nan || isF1Nan;

			switch (cexpr->getPredicate())
			{
				case CmpInst::FCMP_FALSE:
					return DynamicValue::getIntValue(APInt(1, false));
				case CmpInst::FCMP_OEQ:
					return DynamicValue::getIntValue(APInt(1, bothNotNan && f0 == f1));
				case CmpInst::FCMP_OGT:
					return DynamicValue::getIntValue(APInt(1, bothNotNan && f0 > f1));
				case CmpInst::FCMP_OGE:
					return DynamicValue::getIntValue(APInt(1, bothNotNan && f0 >= f1));
				case CmpInst::FCMP_OLT:
					return DynamicValue::getIntValue(APInt(1, bothNotNan && f0 < f1));
				case CmpInst::FCMP_OLE:
					return DynamicValue::getIntValue(APInt(1, bothNotNan && f0 <= f1));
				case CmpInst::FCMP_ONE:
					return DynamicValue::getIntValue(APInt(1, bothNotNan && f0 != f1));
				case CmpInst::FCMP_ORD:
					return DynamicValue::getIntValue(APInt(1, bothNotNan));
				case CmpInst::FCMP_UEQ:
					return DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 == f1));
				case CmpInst::FCMP_UGT:
					return DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 > f1));
				case CmpInst::FCMP_UGE:
					return DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 >= f1));
				case CmpInst::FCMP_ULT:
					return DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 < f1));
				case CmpInst::FCMP_ULE:
					return DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 <= f1));
				case CmpInst::FCMP_UNE:
					return DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 != f1));
				case CmpInst::FCMP_UNO:
					return DynamicValue::getIntValue(APInt(1, eitherIsNan));
				case CmpInst::FCMP_TRUE:
					return DynamicValue::getIntValue(APInt(1, false));
				default:
					llvm_unreachable("Illegal fcmp predicate");
			}
		}
		case Instruction::Select:
		{
			auto condVal = evaluateConstant(cexpr->getOperand(0));
			auto condInt = condVal.getAsIntValue().getInt().getBoolValue();
			if (condInt)
				return evaluateConstant(cexpr->getOperand(1));
			else
				return evaluateConstant(cexpr->getOperand(2));
		}
		case Instruction::GetElementPtr:
		{
			auto baseVal = evaluateConstant(cexpr->getOperand(0));
			auto offsetInt = APInt(dataLayout.getPointerSizeInBits(), 0);
			cast<GEPOperator>(cexpr)->accumulateConstantOffset(dataLayout, offsetInt);

			auto& basePtrVal = baseVal.getAsPointerValue();
			basePtrVal.setAddress(basePtrVal.getAddress() + offsetInt.getZExtValue());
			return std::move(baseVal);
		}
		case Instruction::ExtractValue:
		{
			auto baseVal = evaluateConstant(cexpr->getOperand(0));
			for (auto i = 1u, e = cexpr->getNumOperands(); i < e; ++i)
			{
				auto idxVal = evaluateConstant(cexpr->getOperand(i));
				auto seqNum = idxVal.getAsIntValue().getInt().getZExtValue();
				if (baseVal.isStructValue())
				{
					auto fieldVal = baseVal.getAsStructValue().getFieldAtNum(seqNum);
					baseVal = std::move(fieldVal);
				}
				else if (baseVal.isArrayValue())
				{
					auto elemVal = baseVal.getAsArrayValue().getElementAtIndex(seqNum);
					baseVal = std::move(elemVal);
				}
				else
					llvm_unreachable("extractvalue into a non-aggregate value!");
			}
			return std::move(baseVal);
		}
		case Instruction::InsertValue:
		{
			auto baseVal = evaluateConstant(cexpr->getOperand(0));
			auto& tmpBaseVal = baseVal;
			for (auto i = 2u, e = cexpr->getNumOperands(); i < e; ++i)
			{
				auto idxVal = evaluateConstant(cexpr->getOperand(i));
				auto seqNum = idxVal.getAsIntValue().getInt().getZExtValue();
				if (baseVal.isStructValue())
				{
					auto fieldVal = baseVal.getAsStructValue().getFieldAtNum(seqNum);
					baseVal = std::move(fieldVal);
				}
				else if (baseVal.isArrayValue())
				{
					auto elemVal = baseVal.getAsArrayValue().getElementAtIndex(seqNum);
					baseVal = std::move(elemVal);
				}
				else
					llvm_unreachable("insertvalue into a non-aggregate value!");
			}
			baseVal = evaluateConstant(cexpr->getOperand(1));

			return std::move(tmpBaseVal);
		}

		case Instruction::InsertElement:
		case Instruction::ExtractElement:
		case Instruction::ShuffleVector:
		case Instruction::AddrSpaceCast:
		default:
			llvm_unreachable("unsupported constant expr for evaluateConstantExpr()");
	}
}

DynamicValue Interpreter::evaluateOperand(const StackFrame& frame, const llvm::Value* v)
{
	if (auto cv = dyn_cast<Constant>(v))
		return evaluateConstant(cv);
	else
		return frame.lookup(v);
}

void Interpreter::evaluateInstruction(StackFrame& frame, const llvm::Instruction* inst)
{
	//errs() << "Eval " << *inst << "\n";
	auto evaluateIntBinOp = [this, &frame, inst] (auto binOp)
	{
		auto val0 = evaluateOperand(frame, inst->getOperand(0));
		auto val1 = evaluateOperand(frame, inst->getOperand(1));
		auto& intVal0 = val0.getAsIntValue();
		auto& intVal1 = val1.getAsIntValue();

		intVal0.setInt(binOp(intVal0.getInt(), intVal1.getInt()));
		frame.insertBinding(inst, std::move(val0));
	};

	auto evaluateFloatBinOp = [this, &frame, inst] (auto binOp)
	{
		auto val0 = evaluateOperand(frame, inst->getOperand(0));
		auto val1 = evaluateOperand(frame, inst->getOperand(1));
		auto& fpVal0 = val0.getAsFloatValue();
		auto& fpVal1 = val1.getAsFloatValue();

		fpVal0.setFloat(binOp(fpVal0.getFloat(), fpVal1.getFloat()));
		frame.insertBinding(inst, std::move(val0));
	};

	auto evaluateIntUnOp = [this, &frame, inst] (auto unOp)
	{
		auto srcVal = evaluateOperand(frame, inst->getOperand(0));
		auto& srcIntVal = srcVal.getAsIntValue();

		srcIntVal.setInt(unOp(srcIntVal.getInt()));
		frame.insertBinding(inst, std::move(srcVal));
	};

	switch (inst->getOpcode())
	{
		// Standard binary operators...
		case Instruction::Add:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 + i1;
				}
			);
			break;
		}
		case Instruction::Sub:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 - i1;
				}
			);
			break;
		}
		case Instruction::Mul:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 * i1;
				}
			);
			break;
		}
		case Instruction::UDiv:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.udiv(i1);
				}
			);
			break;
		}
		case Instruction::SDiv:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.sdiv(i1);
				}
			);
			break;
		}
		case Instruction::URem:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.urem(i1);
				}
			);
			break;
		}
		case Instruction::SRem:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0.srem(i1);
				}
			);
			break;
		}
		case Instruction::FAdd:
		{
			evaluateFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 + f1;
				}
			);
			break;
		}
		case Instruction::FSub:
		{
			evaluateFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 - f1;
				}
			);
			break;
		}
		case Instruction::FMul:
		{
			evaluateFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 * f1;
				}
			);
			break;
		}
		case Instruction::FDiv:
		{
			evaluateFloatBinOp(
				[] (double f0, double f1)
				{
					return f0 / f1;
				}
			);
			break;
		}
		case Instruction::FRem:
		{
			evaluateFloatBinOp(
				[] (double f0, double f1)
				{
					return std::remainder(f0, f1);
				}
			);
			break;
		}
		// Logical operators...
		case Instruction::And:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 & i1;
				}
			);
			break;
		}
		case Instruction::Or:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 | i1;
				}
			);
			break;
		}
		case Instruction::Xor:
		{
			evaluateIntBinOp(
				[] (const APInt& i0, const APInt& i1)
				{
					return i0 ^ i1;
				}
			);
			break;
		}
		case Instruction::Shl:
		{
			evaluateIntBinOp(
				[] (const APInt& value, const APInt& shift)
				{
					auto shiftAmount = shift.getZExtValue();
					auto valueWidth = value.getBitWidth();
					if (shiftAmount > valueWidth)
						llvm_unreachable("Illegal shift amount");
					return value.shl(shiftAmount);
				}
			);

			break;
		}
		case Instruction::LShr:
		{
			evaluateIntBinOp(
				[] (const APInt& value, const APInt& shift)
				{
					auto shiftAmount = shift.getZExtValue();
					auto valueWidth = value.getBitWidth();
					if (shiftAmount > valueWidth)
						llvm_unreachable("Illegal shift amount");
					return value.lshr(shiftAmount);
				}
			);

			break;
		}
		case Instruction::AShr:
		{
			evaluateIntBinOp(
				[] (const APInt& value, const APInt& shift)
				{
					auto shiftAmount = shift.getZExtValue();
					auto valueWidth = value.getBitWidth();
					if (shiftAmount > valueWidth)
						llvm_unreachable("Illegal shift amount");
					return value.ashr(shiftAmount);
				}
			);

			break;
		}
		case Instruction::ICmp:
		{
			auto cmp = [inst] (const APInt& i0, const APInt& i1)
			{
				switch (cast<CmpInst>(inst)->getPredicate())
				{
					case CmpInst::ICMP_EQ:
						return APInt(1, i0 == i1);
					case CmpInst::ICMP_NE:
						return APInt(1, i0 != i1);
					case CmpInst::ICMP_UGT:
						return APInt(1, i0.ugt(i1));
					case CmpInst::ICMP_UGE:
						return APInt(1, i0.uge(i1));
					case CmpInst::ICMP_ULT:
						return APInt(1, i0.ult(i1));
					case CmpInst::ICMP_ULE:
						return APInt(1, i0.ule(i1));
					case CmpInst::ICMP_SGT:
						return APInt(1, i0.sgt(i1));
					case CmpInst::ICMP_SGE:
						return APInt(1, i0.sge(i1));
					case CmpInst::ICMP_SLT:
						return APInt(1, i0.slt(i1));
					case CmpInst::ICMP_SLE:
						return APInt(1, i0.sle(i1));
					default:
						llvm_unreachable("Illegal icmp predicate");
				}
			};

			// ICmp can compare both integers and pointers, so we cannot just use evaluateIntBinOp
			auto val0 = evaluateOperand(frame, inst->getOperand(0));
			auto val1 = evaluateOperand(frame, inst->getOperand(1));
			if (val0.isIntValue() && val1.isIntValue())
			{
				auto& intVal0 = val0.getAsIntValue();
				auto res = cmp(intVal0.getInt(), val1.getAsIntValue().getInt());
				intVal0.setInt(res);
				frame.insertBinding(inst, std::move(val0));
			}
			else if (val0.isPointerValue() && val1.isPointerValue())
			{
				auto ptrSize = dataLayout.getPointerSizeInBits();
				auto addr0 = val0.getAsPointerValue().getAddress();
				auto addr1 = val1.getAsPointerValue().getAddress();
				auto res = cmp(APInt(ptrSize, addr0), APInt(ptrSize, addr1));
				frame.insertBinding(inst, DynamicValue::getIntValue(res));
			}
			else
				llvm_unreachable("Illegal icmp compare types");

			break;
		}
		case Instruction::FCmp:
		{
			auto srcVal0 = evaluateOperand(frame, inst->getOperand(0));
			auto srcVal1 = evaluateOperand(frame, inst->getOperand(1));

			auto f0 = srcVal0.getAsFloatValue().getFloat();
			auto f1 = srcVal1.getAsFloatValue().getFloat();
			auto isF0Nan = std::isnan(f0);
			auto isF1Nan = std::isnan(f1);
			auto bothNotNan = !isF0Nan && !isF1Nan;
			auto eitherIsNan = isF0Nan || isF1Nan;

			switch (cast<CmpInst>(inst)->getPredicate())
			{
				case CmpInst::FCMP_FALSE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, false)));
				case CmpInst::FCMP_OEQ:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, bothNotNan && f0 == f1)));
				case CmpInst::FCMP_OGT:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, bothNotNan && f0 > f1)));
				case CmpInst::FCMP_OGE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, bothNotNan && f0 >= f1)));
				case CmpInst::FCMP_OLT:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, bothNotNan && f0 < f1)));
				case CmpInst::FCMP_OLE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, bothNotNan && f0 <= f1)));
				case CmpInst::FCMP_ONE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, bothNotNan && f0 != f1)));
				case CmpInst::FCMP_ORD:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, bothNotNan)));
				case CmpInst::FCMP_UEQ:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 == f1)));
				case CmpInst::FCMP_UGT:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 > f1)));
				case CmpInst::FCMP_UGE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 >= f1)));
				case CmpInst::FCMP_ULT:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 < f1)));
				case CmpInst::FCMP_ULE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 <= f1)));
				case CmpInst::FCMP_UNE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, eitherIsNan || f0 != f1)));
				case CmpInst::FCMP_UNO:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, eitherIsNan)));
				case CmpInst::FCMP_TRUE:
					frame.insertBinding(inst, DynamicValue::getIntValue(APInt(1, false)));
				default:
					llvm_unreachable("Illegal fcmp predicate");
			}

			break;
		}

		// Convert instructions...
		case Instruction::Trunc:
		{
			auto truncWidth = cast<IntegerType>(inst->getType())->getBitWidth();
			evaluateIntUnOp(
				[truncWidth] (const APInt& i0)
				{
					return i0.trunc(truncWidth);
				}
			);
			break;
		}
		case Instruction::ZExt:
		{
			auto extWidth = cast<IntegerType>(inst->getType())->getBitWidth();
			evaluateIntUnOp(
				[extWidth] (const APInt& i0)
				{
					return i0.zext(extWidth);
				}
			);
			break;
		}
		case Instruction::SExt:
		{
			auto extWidth = cast<IntegerType>(inst->getType())->getBitWidth();
			evaluateIntUnOp(
				[extWidth] (const APInt& i0)
				{
					return i0. sext(extWidth);
				}
			);
			break;
		}
		case Instruction::FPTrunc:
		{
			auto srcType = inst->getOperand(0)->getType();
			auto dstType = inst->getType();
			if (!(srcType->isDoubleTy() && dstType->isFloatTy()))
				llvm_unreachable("Invalid FPTrunc instruction");

			auto srcVal = evaluateOperand(frame, inst->getOperand(0));
			auto& srcFloatVal = srcVal.getAsFloatValue();
			srcFloatVal.setFloat(static_cast<float>(srcFloatVal.getFloat()));
			frame.insertBinding(inst, std::move(srcVal));
			break;
		}
		case Instruction::FPExt:
		{
			auto srcType = inst->getOperand(0)->getType();
			auto dstType = inst->getType();
			if (!(dstType->isDoubleTy() && srcType->isFloatTy()))
				llvm_unreachable("Invalid FPExt instruction");
			// Extention is a non-op for us
			auto srcVal = evaluateOperand(frame, inst->getOperand(0));
			frame.insertBinding(inst, std::move(srcVal));
			break;
		}
		// Since APInt can represent both UI and SI, we process them in the same way
		case Instruction::FPToUI:
		case Instruction::FPToSI:
		{
			auto intWidth = cast<IntegerType>(inst->getType())->getBitWidth();

			auto srcVal = evaluateOperand(frame, inst->getOperand(0));
			auto resVal = DynamicValue::getIntValue(APIntOps::RoundDoubleToAPInt(srcVal.getAsFloatValue().getFloat(), intWidth));
			frame.insertBinding(inst, std::move(resVal));
			break;
		}
		// Ditto
		case Instruction::UIToFP:
		case Instruction::SIToFP:
		{
			auto srcVal = evaluateOperand(frame, inst->getOperand(0));
			auto resVal = DynamicValue::getFloatValue(APIntOps::RoundAPIntToDouble(srcVal.getAsIntValue().getInt()));
			frame.insertBinding(inst, std::move(resVal));
			break;
		}
		case Instruction::IntToPtr:
		{
			auto srcVal = evaluateOperand(frame, inst->getOperand(0));
			auto ptrSize = dataLayout.getPointerSizeInBits();

			// Look for matching ptrtoint to decide what the address space should be
			auto addrSpace = PointerAddressSpace::GLOBAL_SPACE;
			auto matchingPtr = findBasePointer(inst);
			if (matchingPtr != nullptr && frame.hasBinding(matchingPtr))
			{
				auto& ptrVal = frame.lookup(matchingPtr).getAsPointerValue();
				addrSpace = ptrVal.getAddressSpace();
			}

			auto resVal = DynamicValue::getPointerValue(addrSpace, srcVal.getAsIntValue().getInt().zextOrTrunc(ptrSize).getZExtValue());
			frame.insertBinding(inst, std::move(resVal));
			break;
		}
		case Instruction::PtrToInt:
		{
			auto intWidth = cast<IntegerType>(inst->getType())->getBitWidth();
			auto srcVal = evaluateOperand(frame, inst->getOperand(0));
			auto resVal = DynamicValue::getIntValue(APInt(intWidth, srcVal.getAsPointerValue().getAddress()));
			frame.insertBinding(inst, std::move(resVal));
			break;
		}
		case Instruction::BitCast:
		{
			auto srcType = inst->getOperand(0)->getType();
			auto dstType = inst->getType();

			if (dstType->isPointerTy())
			{
				if (!srcType->isPointerTy())
					llvm_unreachable("Invalid Bitcast");
				auto resVal = evaluateOperand(frame, inst->getOperand(0));
				frame.insertBinding(inst, std::move(resVal));
			}
			else if (dstType->isIntegerTy())
			{
				if (srcType->isFloatTy() || srcType->isDoubleTy())
				{
					auto srcVal = evaluateOperand(frame, inst->getOperand(0));
					auto resVal = DynamicValue::getIntValue(APInt::doubleToBits(srcVal.getAsFloatValue().getFloat()));
					frame.insertBinding(inst, std::move(resVal));
				}
				else if (srcType->isIntegerTy())
				{
					auto resVal = evaluateOperand(frame, inst->getOperand(0));
					frame.insertBinding(inst, std::move(resVal));
				}
				else
					llvm_unreachable("Invalid BitCast");
			}
			else if (dstType->isFloatTy() || dstType->isDoubleTy())
			{
				if (srcType->isIntegerTy())
				{
					auto srcVal = evaluateOperand(frame, inst->getOperand(0));
					auto resVal = DynamicValue::getFloatValue(srcVal.getAsIntValue().getInt().bitsToDouble());
					frame.insertBinding(inst, std::move(resVal));
				}
				else
				{
					auto resVal = evaluateOperand(frame, inst->getOperand(0));
					frame.insertBinding(inst, std::move(resVal));
				}
			}
			else
				llvm_unreachable("Invalid Bitcast");

			break;
		}

		// Memory instructions...
		case Instruction::Alloca:
		{
			auto allocInst = cast<AllocaInst>(inst);

			auto allocElems = 1u;
			if (allocInst->isArrayAllocation())
			{
				auto sizeVal = evaluateOperand(frame, allocInst->getArraySize());
				allocElems = sizeVal.getAsIntValue().getInt().getZExtValue();
			}

			auto allocSize = dataLayout.getTypeAllocSize(allocInst->getType()->getElementType());
			auto retAddr = allocateStackMem(frame, allocSize, allocInst->getType()->getElementType());
			for (auto i = 1; i < allocElems; ++i)
				allocateStackMem(frame, allocSize, allocInst->getType()->getElementType());

			frame.insertBinding(inst, DynamicValue::getPointerValue(PointerAddressSpace::STACK_SPACE, retAddr));

			break;
		}
		case Instruction::Load:
		{
			auto loadInst = cast<LoadInst>(inst);

			auto loadSrc = evaluateOperand(frame, loadInst->getPointerOperand());
			auto& loadPtr = loadSrc.getAsPointerValue();

			auto resVal = readFromPointer(loadPtr);

			frame.insertBinding(inst, std::move(resVal));

			break;
		}
		case Instruction::Store:
		{
			auto storeInst = cast<StoreInst>(inst);

			auto storeSrc = evaluateOperand(frame, storeInst->getPointerOperand());
			auto storeVal = evaluateOperand(frame, storeInst->getValueOperand());
			auto& storePtr = storeSrc.getAsPointerValue();

			if (frame.getFunction()->getName() == "func_6")
			{
				errs() << "inst = " << *inst << '\n';
				errs() << "storePtr = " << storeSrc.toString() << "\n";
				errs() << "storeVal = " << storeVal.toString() << "\n";
			}

			writeToPointer(storePtr, std::move(storeVal));

			break;
		}
		case Instruction::GetElementPtr:
		{
			auto gepInst = cast<GetElementPtrInst>(inst);

			auto baseVal = evaluateOperand(frame, gepInst->getPointerOperand());
			auto& basePtrVal = baseVal.getAsPointerValue();
			auto baseAddr = basePtrVal.getAddress();

			for (auto itr = gep_type_begin(gepInst), ite = gep_type_end(gepInst); itr != ite; ++itr)
			{
				auto idxVal = evaluateOperand(frame, itr.getOperand());
				auto seqNum = idxVal.getAsIntValue().getInt().getSExtValue();

				if (auto structType = dyn_cast<StructType>(*itr))
				{
					baseAddr += dataLayout.getStructLayout(structType)->getElementOffset(seqNum);
				}
				else
				{
					auto seqType = cast<SequentialType>(*itr);
					baseAddr += seqNum * dataLayout.getTypeAllocSize(seqType->getElementType());
				}
			}

			basePtrVal.setAddress(baseAddr);
			frame.insertBinding(inst, std::move(baseVal));

			break;
		}

		// Other instructions...
		case Instruction::ExtractValue:
		{
			auto evInst = cast<ExtractValueInst>(inst);

			auto baseVal = evaluateOperand(frame, evInst->getAggregateOperand());

			for (auto itr = evInst->idx_begin(), ite = evInst->idx_end(); itr != ite; ++itr)
			{
				auto idx = *itr;
				if (baseVal.isStructValue())
				{
					auto fieldVal = baseVal.getAsStructValue().getFieldAtNum(idx);
					baseVal = std::move(fieldVal);
				}
				else if (baseVal.isArrayValue())
				{
					auto elemVal = baseVal.getAsArrayValue().getElementAtIndex(idx);
					baseVal = std::move(elemVal);
				}
				else
					llvm_unreachable("extractvalue into a non-aggregate type!");
			}
			
			frame.insertBinding(inst, std::move(baseVal));
			break;
		}
		case Instruction::InsertValue:
		{
			auto ivInst = cast<InsertValueInst>(inst);

			auto baseVal = evaluateOperand(frame, ivInst->getAggregateOperand());
			auto& tmpBaseVal = baseVal;
			for (auto itr = ivInst->idx_begin(), ite = ivInst->idx_end(); itr != ite; ++itr)
			{
				auto idx = *itr;
				if (baseVal.isStructValue())
				{
					auto fieldVal = baseVal.getAsStructValue().getFieldAtNum(idx);
					baseVal = std::move(fieldVal);
				}
				else if (baseVal.isArrayValue())
				{
					auto elemVal = baseVal.getAsArrayValue().getElementAtIndex(idx);
					baseVal = std::move(elemVal);
				}
				else
					llvm_unreachable("insertvalue into a non-aggregate type!");
			}
			baseVal = evaluateOperand(frame, ivInst->getInsertedValueOperand());
			
			frame.insertBinding(inst, std::move(tmpBaseVal));
			break;
		}
		case Instruction::Select:
		{
			auto selInst = cast<SelectInst>(inst);

			auto condVal = evaluateOperand(frame, selInst->getCondition());
			auto condInt = condVal.getAsIntValue().getInt().getBoolValue();
			if (condInt)
				frame.insertBinding(inst, evaluateOperand(frame, selInst->getTrueValue()));
			else
				frame.insertBinding(inst, evaluateOperand(frame, selInst->getFalseValue()));

			break;
		}
		case Instruction::Call:
		{
			ImmutableCallSite cs(inst);
			assert(cs);

			auto callTgt = cs.getCalledFunction();
			if (callTgt == nullptr)
			{
				auto funPtr = evaluateOperand(frame, cs.getCalledValue());
				auto funAddr = funPtr.getAsPointerValue().getAddress();
				callTgt = cast<Function>(funPtrMap.at(funAddr));
			}

			auto argVals = std::vector<DynamicValue>();
			for (auto itr = cs.arg_begin(), ite = cs.arg_end(); itr != ite; ++itr)
				argVals.push_back(evaluateOperand(frame, *itr));

			auto retVal = (callTgt->isDeclaration()) ? callExternalFunction(cs, callTgt, std::move(argVals)) : callFunction(callTgt, std::move(argVals));
			if (!callTgt->getReturnType()->isVoidTy())
				frame.insertBinding(inst, std::move(retVal));

			break;
		}

		// Instructions that should not be here
		case Instruction::PHI:
			llvm_unreachable("Illegal instruction type!");

		// Unimplemented instructions
		case Instruction::VAArg:
		case Instruction::Invoke:
		case Instruction::LandingPad:
			llvm_unreachable("Unimplemented instruction type!");

		// Unsupported instructions
		case Instruction::AddrSpaceCast:
		case Instruction::AtomicCmpXchg:
		case Instruction::AtomicRMW:
		case Instruction::Fence:
		case Instruction::ExtractElement:
		case Instruction::InsertElement:
		case Instruction::ShuffleVector:
			llvm_unreachable("Unsupported instruction type!");
	}
}
