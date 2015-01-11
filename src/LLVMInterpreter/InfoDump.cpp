#include "Memory.h"
#include "Interpreter.h"

#include "llvm/Support/raw_ostream.h"

#include <sstream>

using namespace llvm;
using namespace llvm_interpreter;

// This file contains all the logic about dumping the internal state of interpreter to the console

std::string IntValue::toString() const
{
	std::ostringstream ss;
	ss << "<INT" << intVal.getBitWidth() << " " << intVal.toString(10, false) << ">";
	return ss.str();
}

std::string FloatValue::toString() const
{
	std::ostringstream ss;
	if (doubleFlag)
		ss << "<DOUBLE ";
	else
		ss << "<FLOAT";
	ss << fpVal;
	return ss.str();
}

std::string PointerValue::toString() const
{
	std::ostringstream ss;
	switch (addrSpace)
	{
		case PointerAddressSpace::GLOBAL_SPACE:
			ss << "<G_PTR ";
			break;
		case PointerAddressSpace::STACK_SPACE:
			ss << "<S_PTR ";
			break;
		case PointerAddressSpace::HEAP_SPACE:
			ss << "<H_PTR ";
			break;
	}

	ss << ptr << ">";
	return ss.str();
}

std::string ArrayValue::toString() const
{
	std::ostringstream ss;
	ss << "[ ";
	for (auto const& elem: array)
		ss << elem.toString() << " ";
	ss << "]";
	return ss.str();
}

std::string StructValue::toString() const
{
	std::ostringstream ss;
	ss << "{ ";
	for (auto const& mapping: structMap)
		ss << "(" << mapping.first << ", " << mapping.second.toString() << ") ";
	ss << "}";
	return ss.str();
}

std::string DynamicValue::toString() const
{
	switch (type)
	{
		case DynamicValueType::INT_VALUE:
			return data.intVal.toString();
		case DynamicValueType::FLOAT_VALUE:
			return data.floatVal.toString();
		case DynamicValueType::POINTER_VALUE:
			return data.ptrVal.toString();
		case DynamicValueType::ARRAY_VALUE:
			return data.arrayVal.toString();
		case DynamicValueType::STRUCT_VALUE:
			return data.structVal.toString();
		case DynamicValueType::UNDEF_VALUE:
			return "<undef>";
	}
}

void StackFrame::dumpFrame() const
{
	errs() << "--- Stack Frame Dump ---\n";

	errs() << "Current Function = " << curFunction->getName() << "\n";
	errs() << "Current Frame Size = " << allocSize << "\n";
	errs() << "Bindings: \n";
	for (auto const& mapping: vRegs)
	{
		errs() << mapping.first->getName() << "  -->>  " << mapping.second.toString() << "\n";
	}

	errs() << "---        End       ---\n";
}

void StackFrames::dumpContext() const
{
	errs() << "Context = [ ";
	for (auto const& frame: frames)
	{
		errs() << frame->getFunction()->getName() << " ";
	}
	errs() << "]\n";
}

void MemorySection::dumpMemory(Address startAddr, unsigned size) const
{
	errs() << "--- Memory Dump ---\n";

	errs() << "Total Memory Size = " << totalSize << "\n";
	errs() << "Allocated Memory Size = " << usedSize << "\n";
	errs() << "Data Dump:\n";
	
	auto currAddr = startAddr;
	auto step = 8;
	auto endAddr = (size == 0) ? usedSize : startAddr + size;
	while (currAddr < endAddr)
	{
		errs() << "Addr " << currAddr;
		if (currAddr < 100)
			errs() << "\t\t| ";
		else
			errs() << "\t| ";
		auto outputPtr = static_cast<const unsigned char*>(mem + currAddr);
		for (auto i = 0; i < step; ++i)
		{
			if (currAddr + i >= endAddr)
				break;
			errs().write_hex(outputPtr[i]) << "\t";
		}
		errs() << "\n";
		currAddr += step;
	}

	errs() << "---     End    ---\n";
}
