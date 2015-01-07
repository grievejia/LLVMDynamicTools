#include "DynamicValue.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

#include <iterator>

using namespace llvm;
using namespace llvm_interpreter;

IntValue* IntValue::clone() const
{
	return new IntValue(intVal);
}

FloatValue* FloatValue::clone() const
{
	return new FloatValue(fpVal);
}

PointerValue* PointerValue::clone() const
{
	return new PointerValue(addrSpace, ptr);
}

ArrayValue* ArrayValue::clone() const
{
	return new ArrayValue(array, elemSize);
}

StructValue* StructValue::clone() const
{
	return new StructValue(structMap, structSize);
}

ArrayValue::ArrayValue(unsigned eCount, unsigned eSize): DynamicValueConcept(DynamicValueType::ARRAY_VALUE), array(eCount, DynamicValue::getUndefValue()), elemSize(eSize) {}

void ArrayValue::setElementAtIndex(unsigned idx, DynamicValue&& val)
{
	assert(idx < array.size());
	array[idx] = std::move(val);
}

DynamicValue ArrayValue::getElementAtIndex(unsigned idx) const
{
	return array.at(idx);
}

DynamicValue& ArrayValue::getValueAtOffset(unsigned offset)
{
	if (offset >= elemSize * array.size())
		llvm_unreachable("Out-of-bound array offset access");

	auto divQuot = offset / elemSize;
	auto divRem = offset % elemSize;
	auto& elem = array.at(divQuot);
	if (elem.isAggregateValue())
		return elem.getValueAtOffset(divRem);
	else
	{
		if (divRem != 0)
			llvm_unreachable("Trying to offset-access into a scalar value!");
		return elem;
	}
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

DynamicValue& StructValue::getValueAtOffset(unsigned offset)
{
	if (offset >= structSize)
		llvm_unreachable("Out-of-bound struct offset access");

	auto itr = std::prev(structMap.upper_bound(offset));

	auto& elem = itr->second;
	if (elem.isAggregateValue())
		return elem.getValueAtOffset(offset - itr->first);
	else
	{
		if (itr->first != offset)
			llvm_unreachable("Trying to offset-access into a scalar value!");
		return elem;
	}
}

DynamicValue::DynamicValue(DynamicValueConcept* c): impl(c) {}
DynamicValue::~DynamicValue() {}

DynamicValue::DynamicValue(DynamicValue&&) = default;
DynamicValue& DynamicValue::operator=(DynamicValue&&) = default;

DynamicValue::DynamicValue(const DynamicValue& other)
{
	if (other.impl)
		impl = std::unique_ptr<DynamicValueConcept>(other.impl->clone());
	else
		impl = nullptr;
}
DynamicValue& DynamicValue::operator=(const DynamicValue& other)
{
	if (other.impl)
		impl = std::unique_ptr<DynamicValueConcept>(other.impl->clone());
	else
		impl = nullptr;
	return *this;
}

IntValue& DynamicValue::getAsIntValue()
{
	assert(impl.get() != nullptr);
	return cast<IntValue>(*impl);
}

const IntValue& DynamicValue::getAsIntValue() const
{
	assert(impl.get() != nullptr);
	return cast<IntValue>(*impl);
}

FloatValue& DynamicValue::getAsFloatValue()
{
	assert(impl.get() != nullptr);
	return cast<FloatValue>(*impl);
}

PointerValue& DynamicValue::getAsPointerValue()
{
	assert(impl.get() != nullptr);
	return cast<PointerValue>(*impl);
}

const PointerValue& DynamicValue::getAsPointerValue() const
{
	assert(impl.get() != nullptr);
	return cast<PointerValue>(*impl);
}

ArrayValue& DynamicValue::getAsArrayValue()
{
	assert(impl.get() != nullptr);
	return cast<ArrayValue>(*impl);
}

const ArrayValue& DynamicValue::getAsArrayValue() const
{
	assert(impl.get() != nullptr);
	return cast<ArrayValue>(*impl);
}

StructValue& DynamicValue::getAsStructValue()
{
	assert(impl.get() != nullptr);
	return cast<StructValue>(*impl);
}

const StructValue& DynamicValue::getAsStructValue() const
{
	assert(impl.get() != nullptr);
	return cast<StructValue>(*impl);
}

bool DynamicValue::isIntValue() const
{
	return !isUndefValue() && isa<IntValue>(*impl);
}

bool DynamicValue::isFloatValue() const
{
	return !isUndefValue() && isa<FloatValue>(*impl);
}

bool DynamicValue::isPointerValue() const
{
	return !isUndefValue() && isa<PointerValue>(*impl);
}

bool DynamicValue::isArrayValue() const
{
	return !isUndefValue() && isa<ArrayValue>(*impl);
}

bool DynamicValue::isStructValue() const
{
	return !isUndefValue() && isa<StructValue>(*impl);
}

bool DynamicValue::isAggregateValue() const
{
	return !isUndefValue() && (isa<StructValue>(*impl) || isa<ArrayValue>(*impl));
}

DynamicValue DynamicValue::getUndefValue()
{
	return DynamicValue(nullptr);
}

DynamicValue DynamicValue::getIntValue(const llvm::APInt& i)
{
	return DynamicValue(new IntValue(i));
}

DynamicValue DynamicValue::getFloatValue(double f)
{
	return DynamicValue(new FloatValue(f));
}

DynamicValue DynamicValue::getPointerValue(PointerAddressSpace s, Address a)
{
	return DynamicValue(new PointerValue(s, a));
}

DynamicValue DynamicValue::getArrayValue(unsigned elemCnt, unsigned elemSize)
{
	return DynamicValue(new ArrayValue(elemCnt, elemSize));
}

DynamicValue DynamicValue::getStructValue(unsigned sz)
{
	return DynamicValue(new StructValue(sz));
}

DynamicValue& DynamicValue::getValueAtOffset(unsigned offset)
{
	if (isUndefValue())
		llvm_unreachable("Trying to offset-access into an undef value!");
	else if (auto arrayVal = dyn_cast<ArrayValue>(impl.get()))
		return arrayVal->getValueAtOffset(offset);
	else if (auto structVal = dyn_cast<StructValue>(impl.get()))
		return structVal->getValueAtOffset(offset);
	else
		llvm_unreachable("Trying to offset-access into a scalar value!");
}
