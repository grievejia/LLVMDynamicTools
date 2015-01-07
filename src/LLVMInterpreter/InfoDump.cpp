#include "Memory.h"
#include "Interpreter.h"

#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm_interpreter;

// This file contains all the logic about dumping the internal state of interpreter to the console

std::string IntValue::toString() const
{
	return intVal.toString(10, true);
}

std::string FloatValue::toString() const
{
	return std::to_string(fpVal);
}

std::string PointerValue::toString() const
{
	auto retStr = std::string("<");
	if (addrSpace == PointerAddressSpace::GLOBAL_SPACE)
		retStr += "G ";
	else if (addrSpace == PointerAddressSpace::STACK_SPACE)
		retStr += "S ";
	else
		retStr += "H ";
	return retStr + std::to_string(ptr) + ">";
}

std::string ArrayValue::toString() const
{
	auto retStr = std::string("[ ");
	for (auto const& elem: array)
		retStr += elem.toString() + " ";
	retStr += "]";
	return retStr;
}

std::string StructValue::toString() const
{
	auto retStr = std::string("{ ");
	for (auto const& mapping: structMap)
		retStr += "(" + std::to_string(mapping.first) + ", " + mapping.second.toString() + ") ";
	retStr += "}";
	return retStr;
}

std::string DynamicValue::toString() const
{
	if (impl)
		return impl->toString();
	else
		return "<undef>";
}

void MemorySection::dumpMemory() const
{
	errs() << "--- Memory Dump ---\n";

	errs() << "Next Available Address = " << nextAddr << "\n";
	errs() << "Memory Mapping: \n";
	for (auto const& mapping: mem)
	{
		errs() << mapping.first << "  -->>  " << mapping.second.toString() << "\n";
	}

	errs() << "---      End    ---\n";
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
