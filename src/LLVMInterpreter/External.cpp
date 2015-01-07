#include "Interpreter.h"

#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

#include <boost/format.hpp>
#include <unordered_map>

using namespace llvm;
using namespace llvm_interpreter;

// This file contains all codes necessary for dealing with external function calls

enum class ExternalCallType
{
	NOOP,
	PRINTF,
	MEMCPY,
	MEMSET,
	MALLOC,
	FREE,
};

// getMallocType - Returns the PointerType resulting from the malloc call.
// The PointerType depends on the number of bitcast uses of the malloc call:
//   0: PointerType is the calls' return type.
//   1: PointerType is the bitcast's result type.
//  >1: Unique PointerType cannot be determined, return NULL.
static PointerType* getMallocType(const Instruction* inst)
{
	assert(isa<CallInst>(inst) && "getMallocType and not a call");

	PointerType* mallocType = nullptr;
	unsigned numBitCastUses = 0;

	// Determine if CallInst has a bitcast use.
	for (auto itr = inst->user_begin(), ite = inst->user_end(); itr != ite; ++itr)
	{
		if (auto bitCastInst = dyn_cast<BitCastInst>(*itr))
		{
			mallocType = cast<PointerType>(bitCastInst->getDestTy());
			++numBitCastUses;
		}
	}

	if (numBitCastUses == 1)
		return mallocType;
	else if (numBitCastUses == 0)
		return cast<PointerType>(inst->getType());
	else
		return nullptr;
}

static void fillMemoryImpl(MemorySection& mem, Address addr, unsigned fillInt, const DynamicValue& val)
{
	if (val.isIntValue())
	{
		auto fillVal = val;
		mem.write(addr, std::move(fillVal));
	}
	else if (val.isFloatValue())
	{
		if (fillInt == 0)
			mem.write(addr, DynamicValue::getFloatValue(0));
		else
			llvm_unreachable("memset() floats to an int value?");
	}
	else if (val.isPointerValue())
	{
		if (fillInt == 0)
			mem.write(addr, DynamicValue::getPointerValue(PointerAddressSpace::GLOBAL_SPACE, 0));
		else
			llvm_unreachable("memset() fabricates pointers from int");
	}
	else if (val.isArrayValue())
	{
		auto& arrayVal = val.getAsArrayValue();
		for (auto i = 0u, e = arrayVal.getNumElements(); i < e; ++i)
			fillMemoryImpl(mem, addr + i * arrayVal.getElementSize(), fillInt, arrayVal.getElementAtIndex(i));
	}
	else if (val.isStructValue())
	{
		auto& stVal = val.getAsStructValue();
		for (auto i = 0u, e = stVal.getNumElements(); i < e; ++i)
		{
			fillMemoryImpl(mem, addr, fillInt, stVal.getFieldAtNum(i));
			addr += stVal.getOffsetAtNum(i);
		}
	}
};

std::string Interpreter::ptrToString(DynamicValue& v)
{
	auto retStr = std::string();
	auto& ptrVal = v.getAsPointerValue();

	while (true)
	{
		auto readInt = readFromPointer(ptrVal).getAsIntValue().getInt().getZExtValue();
		if (readInt == 0)
			break;
		retStr.push_back(static_cast<char>(readInt));
		ptrVal.setAddress(ptrVal.getAddress() + 1);
	}

	return retStr;
}

DynamicValue Interpreter::callExternalFunction(ImmutableCallSite cs, const llvm::Function* f, std::vector<DynamicValue>&& argValues)
{
	static std::unordered_map<std::string, ExternalCallType> externalFuncMap =
	{
		{ "printf", ExternalCallType::PRINTF },
		{ "memcpy", ExternalCallType::MEMCPY },
		{ "memmove", ExternalCallType::MEMCPY },
		{ "llvm.memcpy.p0i8.p0i8.i32", ExternalCallType::MEMCPY },
		{ "llvm.memcpy.p0i8.p0i8.i64", ExternalCallType::MEMCPY },
		{ "memset", ExternalCallType::MEMSET },
		{ "llvm.memset.p0i8.i32", ExternalCallType::MEMSET },
		{ "llvm.memset.p0i8.i64", ExternalCallType::MEMSET },
		{ "malloc", ExternalCallType::MALLOC },
		{ "free", ExternalCallType::FREE },
	};

	auto readMemoryRegion = [this] (const PointerValue& ptr, unsigned size)
	{
		switch (ptr.getAddressSpace())
		{
			case PointerAddressSpace::GLOBAL_SPACE:
				return globalMem.readMemoryRegion(ptr.getAddress(), size);
			case PointerAddressSpace::STACK_SPACE:
				return stackMem.readMemoryRegion(ptr.getAddress(), size);
			case PointerAddressSpace::HEAP_SPACE:
				return heapMem.readMemoryRegion(ptr.getAddress(), size);
		}
	};

	auto writeMemoryRegion = [this] (const PointerValue& ptr, MemoryRegion&& reg)
	{
		switch (ptr.getAddressSpace())
		{
			case PointerAddressSpace::GLOBAL_SPACE:
				return globalMem.writeMemoryRegion(ptr.getAddress(), std::move(reg));
			case PointerAddressSpace::STACK_SPACE:
				return stackMem.writeMemoryRegion(ptr.getAddress(), std::move(reg));
			case PointerAddressSpace::HEAP_SPACE:
				return heapMem.writeMemoryRegion(ptr.getAddress(), std::move(reg));
		}
	};

	auto fillMemory = [this] (const PointerValue& ptr, unsigned fillInt, const DynamicValue& val)
	{
		switch (ptr.getAddressSpace())
		{
			case PointerAddressSpace::GLOBAL_SPACE:
				fillMemoryImpl(globalMem, ptr.getAddress(), fillInt, val);
				break;
			case PointerAddressSpace::STACK_SPACE:
				fillMemoryImpl(stackMem, ptr.getAddress(), fillInt, val);
				break;
			case PointerAddressSpace::HEAP_SPACE:
				fillMemoryImpl(heapMem, ptr.getAddress(), fillInt, val);
				break;
		}
	};

	auto itr = externalFuncMap.find(f->getName());
	if (itr == externalFuncMap.end())
	{
		errs() << "Unknown external function: " << f->getName() << "\n";
		llvm_unreachable("");
	}

	switch (itr->second)
	{
		case ExternalCallType::NOOP:
			return DynamicValue::getUndefValue();
		case ExternalCallType::PRINTF:
		{
			assert(argValues.size() >= 1);

			// Use boost::format to output the format string
			auto fmt = boost::format(ptrToString(argValues.at(0)));
			for (auto i = std::size_t(1), e = argValues.size(); i < e; ++i)
			{
				auto& argVal = argValues[i];
				if (argVal.isUndefValue())
					llvm_unreachable("Passing undef value into printf?");
				else if (argVal.isIntValue())
					fmt % argVal.getAsIntValue().getInt().getZExtValue();
				else if (argVal.isFloatValue())
					fmt % argVal.getAsFloatValue().getFloat();
				else if (argVal.isPointerValue())
					fmt % ptrToString(argVal);
				else
					llvm_unreachable("Passing an array or struct to printf?");
			}

			outs() << fmt.str();

			return DynamicValue::getIntValue(APInt(32, fmt.size()));
		}
		case ExternalCallType::MEMCPY:
		{
			assert(argValues.size() >= 3);

			auto& destPtr = argValues.at(0).getAsPointerValue();
			auto& srcPtr = argValues.at(1).getAsPointerValue();
			auto size = argValues.at(2).getAsIntValue().getInt().getZExtValue();

			writeMemoryRegion(destPtr, readMemoryRegion(srcPtr, size));
			
			return DynamicValue::getUndefValue();
		}
		case ExternalCallType::MEMSET:
		{
			assert(argValues.size() >= 3);

			auto& destPtr = argValues.at(0).getAsPointerValue();
			auto fillInt = argValues.at(1).getAsIntValue().getInt().getZExtValue();
			auto size = argValues.at(2).getAsIntValue().getInt().getZExtValue();
			
			auto memRegion = readMemoryRegion(destPtr, size);
			if (memRegion.getNumEntry() > 1)
				llvm_unreachable("Memset() gets an awesome memory region");
			auto& sketchVal = memRegion.begin()->second;
			fillMemory(destPtr, fillInt, sketchVal);

			return DynamicValue::getUndefValue();
		}
		case ExternalCallType::MALLOC:
		{
			assert(argValues.size() >= 1);

			auto mallocType = getMallocType(cs.getInstruction());
			if (mallocType == nullptr)
				llvm_unreachable("Untyped malloc() not supported");

			auto mallocSize = argValues.at(0).getAsIntValue().getInt().getZExtValue();
			auto mallocTypeSize = dataLayout.getTypeAllocSize(mallocType);
			if (mallocSize != mallocTypeSize)
				llvm_unreachable("malloc size != malloc type size?");

			auto retAddr = 0;
			if (auto arrayType = dyn_cast<ArrayType>(mallocType))
				retAddr = heapMem.allocateAndInitialize(mallocSize, evaluateConstant(UndefValue::get(arrayType)));
			else if (auto structType = dyn_cast<StructType>(mallocType))
			{
				retAddr = heapMem.allocateAndInitialize(mallocSize, evaluateConstant(UndefValue::get(structType)));
			}
			else
				retAddr = heapMem.allocate(mallocSize);

			return DynamicValue::getPointerValue(PointerAddressSpace::HEAP_SPACE, retAddr);
		}
		case ExternalCallType::FREE:
		{
			assert(argValues.size() >= 1);

			auto& ptrVal = argValues.at(0).getAsPointerValue();
			if (ptrVal.getAddressSpace() != PointerAddressSpace::HEAP_SPACE)
				llvm_unreachable("Trying to free a non-heap pointer?");

			heapMem.free(ptrVal.getAddress());
			return DynamicValue::getUndefValue();
		}
	}

	llvm_unreachable("Should not reach here");
}
