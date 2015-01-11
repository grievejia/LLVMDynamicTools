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

	auto getRawPointer = [this] (const PointerValue& ptr)
	{
		switch (ptr.getAddressSpace())
		{
			case PointerAddressSpace::GLOBAL_SPACE:
				return globalMem.getRawPointerAtAddress(ptr.getAddress());
			case PointerAddressSpace::STACK_SPACE:
				return stackMem.getRawPointerAtAddress(ptr.getAddress());
			case PointerAddressSpace::HEAP_SPACE:
				return heapMem.getRawPointerAtAddress(ptr.getAddress());
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
			auto fmtStrPtr = argValues.at(0).getAsPointerValue();
			auto fmt = boost::format(static_cast<const char*>(getRawPointer(fmtStrPtr)));
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
					fmt % static_cast<const char*>(getRawPointer(argVal.getAsPointerValue()));
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

			std::memcpy(getRawPointer(destPtr), getRawPointer(srcPtr), size);
			
			return DynamicValue::getUndefValue();
		}
		case ExternalCallType::MEMSET:
		{
			assert(argValues.size() >= 3);

			auto& destPtr = argValues.at(0).getAsPointerValue();
			auto fillInt = argValues.at(1).getAsIntValue().getInt().getZExtValue();
			auto size = argValues.at(2).getAsIntValue().getInt().getZExtValue();
			
			std::memset(getRawPointer(destPtr), fillInt, size);

			return DynamicValue::getUndefValue();
		}
		case ExternalCallType::MALLOC:
		{
			assert(argValues.size() >= 1);

			auto mallocSize = argValues.at(0).getAsIntValue().getInt().getZExtValue();

			auto retAddr = heapMem.allocate(mallocSize);

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
