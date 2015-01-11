#include "DynamicValue.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

#include <iterator>

using namespace llvm;
using namespace llvm_interpreter;

size_t PointerValue::PointerSize = 8u;

ArrayValue::ArrayValue(unsigned eCount, unsigned eSize): array(eCount, DynamicValue::getUndefValue()), elemSize(eSize) {}

void ArrayValue::setElementAtIndex(unsigned idx, DynamicValue&& val)
{
	assert(idx < array.size());
	array[idx] = std::move(val);
}

DynamicValue ArrayValue::getElementAtIndex(unsigned idx) const
{
	return array.at(idx);
}

void StructValue::addField(unsigned offset, DynamicValue&& val)
{
	assert(!structMap.count(offset));
	structMap.insert(std::make_pair(offset, std::move(val)));
}

DynamicValue StructValue::getFieldAtNum(unsigned num) const
{
	if (structMap.size() <= num)
		llvm_unreachable("Out-of-bound struct access");

	return std::next(structMap.begin(), num)->second;
}

unsigned StructValue::getOffsetAtNum(unsigned num) const
{
	if (structMap.size() <= num)
		llvm_unreachable("Out-of-bound struct access");

	return std::next(structMap.begin(), num)->first;
}

DynamicValue::DynamicValue(): type(DynamicValueType::UNDEF_VALUE) {}
DynamicValue::DynamicValue(IntValue&& intVal): type(DynamicValueType::INT_VALUE)
{
	new (&data.intVal) IntValue(std::move(intVal));
}
DynamicValue::DynamicValue(FloatValue&& floatVal): type(DynamicValueType::FLOAT_VALUE)
{
	new (&data.floatVal) FloatValue(std::move(floatVal));
}
DynamicValue::DynamicValue(PointerValue&& ptrVal): type(DynamicValueType::POINTER_VALUE)
{
	new (&data.ptrVal) PointerValue(std::move(ptrVal));
}
DynamicValue::DynamicValue(ArrayValue&& arrayVal): type(DynamicValueType::ARRAY_VALUE)
{
	new (&data.arrayVal) ArrayValue(std::move(arrayVal));
}
DynamicValue::DynamicValue(StructValue&& structVal): type(DynamicValueType::STRUCT_VALUE)
{
	new (&data.structVal) StructValue(std::move(structVal));
}
DynamicValue::~DynamicValue() { clear(); }

void DynamicValue::clear()
{
	switch (type)
	{
		case DynamicValueType::INT_VALUE:
			data.intVal.~IntValue();
			break;
		case DynamicValueType::FLOAT_VALUE:
			data.floatVal.~FloatValue();
			break;
		case DynamicValueType::POINTER_VALUE:
			data.ptrVal.~PointerValue();
			break;
		case DynamicValueType::ARRAY_VALUE:
			data.arrayVal.~ArrayValue();
			break;
		case DynamicValueType::STRUCT_VALUE:
			data.structVal.~StructValue();
			break;
		case DynamicValueType::UNDEF_VALUE:
			break;
	}
}

void DynamicValue::copyFrom(const DynamicValue& other)
{
	type = other.type;
	switch (type)
	{
		case DynamicValueType::INT_VALUE:
			new (&data.intVal) IntValue(other.data.intVal);
			break;
		case DynamicValueType::FLOAT_VALUE:
			new (&data.floatVal) FloatValue(other.data.floatVal);
			break;
		case DynamicValueType::POINTER_VALUE:
			new (&data.ptrVal) PointerValue(other.data.ptrVal);
			break;
		case DynamicValueType::ARRAY_VALUE:
			new (&data.arrayVal) ArrayValue(other.data.arrayVal);
			break;
		case DynamicValueType::STRUCT_VALUE:
			new (&data.structVal) StructValue(other.data.structVal);
			break;
		case DynamicValueType::UNDEF_VALUE:
			break;
	}
}

void DynamicValue::moveFrom(DynamicValue&& other)
{
	type = other.type;
	switch (type)
	{
		case DynamicValueType::INT_VALUE:
			new (&data.intVal) IntValue(std::move(other.data.intVal));
			break;
		case DynamicValueType::FLOAT_VALUE:
			new (&data.floatVal) FloatValue(std::move(other.data.floatVal));
			break;
		case DynamicValueType::POINTER_VALUE:
			new (&data.ptrVal) PointerValue(std::move(other.data.ptrVal));
			break;
		case DynamicValueType::ARRAY_VALUE:
			new (&data.arrayVal) ArrayValue(std::move(other.data.arrayVal));
			break;
		case DynamicValueType::STRUCT_VALUE:
			new (&data.structVal) StructValue(std::move(other.data.structVal));
			break;
		case DynamicValueType::UNDEF_VALUE:
			break;
	}
}

DynamicValue::DynamicValue(const DynamicValue& other)
{
	copyFrom(other);
}
DynamicValue& DynamicValue::operator=(const DynamicValue& other)
{
	if (type != other.type)
		clear();
	copyFrom(other);
	return *this;
}

DynamicValue::DynamicValue(DynamicValue&& other)
{
	moveFrom(std::move(other));
}
DynamicValue& DynamicValue::operator=(DynamicValue&& other)
{
	if (type != other.type)
		clear();
	moveFrom(std::move(other));
	return *this;
}

const IntValue& DynamicValue::getAsIntValue() const
{
	assert(type == DynamicValueType::INT_VALUE);
	return data.intVal;
}

const FloatValue& DynamicValue::getAsFloatValue() const
{
	assert(type == DynamicValueType::FLOAT_VALUE);
	return data.floatVal;
}

const PointerValue& DynamicValue::getAsPointerValue() const
{
	assert(type == DynamicValueType::POINTER_VALUE);
	return data.ptrVal;
}

ArrayValue& DynamicValue::getAsArrayValue()
{
	assert(type == DynamicValueType::ARRAY_VALUE);
	return data.arrayVal;
}

const ArrayValue& DynamicValue::getAsArrayValue() const
{
	assert(type == DynamicValueType::ARRAY_VALUE);
	return data.arrayVal;
}

StructValue& DynamicValue::getAsStructValue()
{
	assert(type == DynamicValueType::STRUCT_VALUE);
	return data.structVal;
}

const StructValue& DynamicValue::getAsStructValue() const
{
	assert(type == DynamicValueType::STRUCT_VALUE);
	return data.structVal;
}

DynamicValue DynamicValue::getUndefValue()
{
	return DynamicValue();
}

DynamicValue DynamicValue::getIntValue(const llvm::APInt& i)
{
	return DynamicValue(IntValue(i));
}

DynamicValue DynamicValue::getFloatValue(double f, bool i)
{
	return DynamicValue(FloatValue(f, i));
}

DynamicValue DynamicValue::getPointerValue(PointerAddressSpace s, Address a)
{
	return DynamicValue(PointerValue(s, a));
}

DynamicValue DynamicValue::getArrayValue(unsigned elemCnt, unsigned elemSize)
{
	return DynamicValue(ArrayValue(elemCnt, elemSize));
}

DynamicValue DynamicValue::getStructValue(unsigned sz)
{
	return DynamicValue(StructValue(sz));
}
