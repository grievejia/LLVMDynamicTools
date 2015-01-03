#ifndef DYNPTS_DYNAMIC_VALUE_H
#define DYNPTS_DYNAMIC_VALUE_H

#include "llvm/ADT/APInt.h"

#include <cstdint>
#include <memory>
#include <map>
#include <string>

namespace llvm_interpreter
{

using Address = uint64_t;

enum class DynamicValueType
{
	INT_VALUE,
	FLOAT_VALUE,
	POINTER_VALUE,
	ARRAY_VALUE,
	STRUCT_VALUE,
};

// Abstract concept
class DynamicValueConcept
{
private:
	DynamicValueType type;
protected:
	virtual DynamicValueConcept* clone() const = 0;
	virtual std::string toString() const = 0;
public:
	DynamicValueConcept(DynamicValueType t): type(t) {}

	DynamicValueType getType() const
	{
		return type;
	}

	virtual ~DynamicValueConcept() = default;

	friend class DynamicValue;
};

class DynamicValue;

// Models of the general concepts

// Integer value
class IntValue: public DynamicValueConcept
{
private:
	llvm::APInt intVal;
	IntValue* clone() const override;

	explicit IntValue(const llvm::APInt& i): DynamicValueConcept(DynamicValueType::INT_VALUE), intVal(i) {}

	std::string toString() const override;
public:
	const llvm::APInt& getInt() const { return intVal; }
	void setInt(const llvm::APInt& other) { intVal = other; }

	static bool classof(const DynamicValueConcept* c)
	{
		return c->getType() == DynamicValueType::INT_VALUE;
	}

	friend class DynamicValue;
};

// Floating point value
class FloatValue: public DynamicValueConcept
{
private:
	double fpVal;
	FloatValue* clone() const override;

	explicit FloatValue(double f): DynamicValueConcept(DynamicValueType::FLOAT_VALUE), fpVal(f) {}

	std::string toString() const override;
public:
	double getFloat() const { return fpVal; }
	void setFloat(double other) { fpVal = other; }

	static bool classof(const DynamicValueConcept* c)
	{
		return c->getType() == DynamicValueType::FLOAT_VALUE;
	}

	friend class DynamicValue;
};

// Pointer value
class PointerValue: public DynamicValueConcept
{
private:
	Address ptr;
	PointerValue* clone() const override;

	explicit PointerValue(Address a): DynamicValueConcept(DynamicValueType::POINTER_VALUE), ptr(a) {}

	std::string toString() const override;
public:
	Address getAddress() const { return ptr; }
	void setAddress(Address other) { ptr = other; }

	static bool classof(const DynamicValueConcept* c)
	{
		return c->getType() == DynamicValueType::POINTER_VALUE;
	}

	friend class DynamicValue;
};

class ArrayValue: public DynamicValueConcept
{
private:
	using ArrayType = std::vector<DynamicValue>;
	ArrayType array;
	unsigned elemSize;

	ArrayValue* clone() const override;
	ArrayValue(const ArrayType& a, unsigned e): DynamicValueConcept(DynamicValueType::ARRAY_VALUE), array(a), elemSize(e) {}
	ArrayValue(unsigned elemCnt, unsigned elemSize);

	std::string toString() const override;
public:
	void setElementAtIndex(unsigned idx, DynamicValue&& val);
	DynamicValue getElementAtIndex(unsigned idx) const;

	DynamicValue& getValueAtOffset(unsigned offset);

	static bool classof(const DynamicValueConcept* c)
	{
		return c->getType() == DynamicValueType::ARRAY_VALUE;
	}

	friend class DynamicValue;
};

class StructValue: public DynamicValueConcept
{
private:
	using StructMapType = std::map<unsigned, DynamicValue>;
	StructMapType structMap;
	unsigned structSize;

	StructValue* clone() const override;

	StructValue(const StructMapType& s, unsigned sz): DynamicValueConcept(DynamicValueType::STRUCT_VALUE), structMap(s), structSize(sz) {}
	StructValue(unsigned sz): DynamicValueConcept(DynamicValueType::STRUCT_VALUE), structSize(sz) {}

	std::string toString() const override;
public:
	void addField(unsigned offset, DynamicValue&& val);
	DynamicValue getFieldAtNum(unsigned num) const;

	DynamicValue& getValueAtOffset(unsigned offset);

	static bool classof(const DynamicValueConcept* c)
	{
		return c->getType() == DynamicValueType::STRUCT_VALUE;
	}

	friend class DynamicValue;
};

class DynamicValue
{
private:
	std::unique_ptr<DynamicValueConcept> impl;

	DynamicValue(DynamicValueConcept* c);
public:
	~DynamicValue();
	DynamicValue(const DynamicValue&);
	DynamicValue(DynamicValue&&);
	DynamicValue& operator=(const DynamicValue&);
	DynamicValue& operator=(DynamicValue&&);

	std::string toString() const;

	bool isUndefValue() const { return impl.get() == nullptr; }
	bool isArrayValue() const;
	bool isStructValue() const;
	bool isAggregateValue() const;

	IntValue& getAsIntValue();
	FloatValue& getAsFloatValue();
	PointerValue& getAsPointerValue();
	ArrayValue& getAsArrayValue();
	StructValue& getAsStructValue();

	DynamicValue& getValueAtOffset(unsigned offset);

	static DynamicValue getUndefValue();
	static DynamicValue getIntValue(const llvm::APInt& i);
	static DynamicValue getFloatValue(double f);
	static DynamicValue getPointerValue(Address a);
	static DynamicValue getArrayValue(unsigned elemCnt, unsigned elemSize);
	static DynamicValue getStructValue(unsigned sz);
};

}

#endif
