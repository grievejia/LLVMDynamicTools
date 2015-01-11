#ifndef DYNPTS_DYNAMIC_VALUE_H
#define DYNPTS_DYNAMIC_VALUE_H

#include "llvm/ADT/APInt.h"

#include <cstdint>
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
	UNDEF_VALUE
};

// Integer value
class IntValue
{
private:
	llvm::APInt intVal;

	explicit IntValue(const llvm::APInt& i): intVal(i) {}

	std::string toString() const;
public:
	const llvm::APInt& getInt() const { return intVal; }

	friend class DynamicValue;
};

// Floating point value
class FloatValue
{
private:
	double fpVal;
	bool doubleFlag;

	explicit FloatValue(double f, bool i): fpVal(f), doubleFlag(i) {}

	std::string toString() const;
public:
	double getFloat() const { return fpVal; }
	bool isDouble() const { return doubleFlag; }

	friend class DynamicValue;
};

// Pointer value
enum class PointerAddressSpace: std::uint8_t
{
	GLOBAL_SPACE,
	STACK_SPACE,
	HEAP_SPACE
};

class PointerValue
{
private:
	static size_t PointerSize;

	PointerAddressSpace addrSpace;
	Address ptr;

	PointerValue(PointerAddressSpace s, Address a): addrSpace(s), ptr(a) {}

	std::string toString() const;
public:
	Address getAddress() const { return ptr; }
	PointerAddressSpace getAddressSpace() const { return addrSpace; }

	static size_t getPointerSize() { return PointerSize; }
	static void setPointerSize(size_t sz) { PointerSize = sz; }

	friend class DynamicValue;
};

class DynamicValue;

class ArrayValue
{
private:
	using ArrayType = std::vector<DynamicValue>;
	ArrayType array;
	unsigned elemSize;

	ArrayValue(const ArrayType& a, unsigned e): array(a), elemSize(e) {}
	ArrayValue(unsigned elemCnt, unsigned elemSize);

	std::string toString() const;
public:
	void setElementAtIndex(unsigned idx, DynamicValue&& val);
	DynamicValue getElementAtIndex(unsigned idx) const;

	unsigned getElementSize() const { return elemSize; }
	unsigned getNumElements() const { return array.size(); }

	friend class DynamicValue;
};

class StructValue
{
private:
	using StructMapType = std::map<unsigned, DynamicValue>;
	StructMapType structMap;
	unsigned structSize;

	StructValue(const StructMapType& s, unsigned sz): structMap(s), structSize(sz) {}
	StructValue(unsigned sz): structSize(sz) {}

	std::string toString() const;
public:
	void addField(unsigned offset, DynamicValue&& val);
	DynamicValue getFieldAtNum(unsigned num) const;
	unsigned getOffsetAtNum(unsigned num) const;

	unsigned getNumElements() const { return structMap.size(); }

	friend class DynamicValue;
};

// We have two choices to make DynamicValue a polymorphic value type: using union, or using concept-based polymorphism. We choose the former here because of its efficiency
class DynamicValue
{
private:
	DynamicValueType type;
	
	// Since C++11, union may contain non-POD data members
	// However, care must be taken when those member contains nontrivial special member functions. We define the constructor/destructor of Data to do nothing, but instead do all the work in the special member function of DynamicValue
	union ValueData
	{
		IntValue intVal;
		FloatValue floatVal;
		PointerValue ptrVal;
		ArrayValue arrayVal;
		StructValue structVal;
		uint8_t placeHolder;

		ValueData(): placeHolder(0) {}
		~ValueData() {}
	} data;

	// Destruct any existing data values. Must be called if the data member is a pointer to somewhere else
	void clear();

	DynamicValue();	// Undef constructor
	DynamicValue(IntValue&& intVal);	// Int constructor
	DynamicValue(FloatValue&& floatVal);	// Float constructor
	DynamicValue(PointerValue&& ptrVal);	// Pointer constructor
	DynamicValue(ArrayValue&& arrayVal);	// Array constructor
	DynamicValue(StructValue&& structVal);	// Struct constructor

	inline void copyFrom(const DynamicValue& other);
	inline void moveFrom(DynamicValue&& other);
public:
	~DynamicValue();
	DynamicValue(const DynamicValue&);
	DynamicValue(DynamicValue&&);
	DynamicValue& operator=(const DynamicValue&);
	DynamicValue& operator=(DynamicValue&&);

	std::string toString() const;
	DynamicValueType getType() const { return type; }

	bool isUndefValue() const
	{
		return type == DynamicValueType::UNDEF_VALUE;
	}
	bool isIntValue() const
	{
		return type == DynamicValueType::INT_VALUE;
	}
	bool isFloatValue() const
	{
		return type == DynamicValueType::FLOAT_VALUE;
	}
	bool isPointerValue() const
	{
		return type == DynamicValueType::POINTER_VALUE;
	}
	bool isArrayValue() const
	{
		return type == DynamicValueType::ARRAY_VALUE;
	}
	bool isStructValue() const
	{
		return type == DynamicValueType::STRUCT_VALUE;
	}
	bool isAggregateValue() const
	{
		return isArrayValue() || isStructValue();
	}

	const IntValue& getAsIntValue() const;
	const FloatValue& getAsFloatValue() const;
	const PointerValue& getAsPointerValue() const;
	ArrayValue& getAsArrayValue();
	const ArrayValue& getAsArrayValue() const;
	StructValue& getAsStructValue();
	const StructValue& getAsStructValue() const;

	static DynamicValue getUndefValue();
	static DynamicValue getIntValue(const llvm::APInt& i);
	static DynamicValue getFloatValue(double f, bool i);
	static DynamicValue getPointerValue(PointerAddressSpace s, Address a);
	static DynamicValue getArrayValue(unsigned elemCnt, unsigned elemSize);
	static DynamicValue getStructValue(unsigned sz);
};

}

#endif
