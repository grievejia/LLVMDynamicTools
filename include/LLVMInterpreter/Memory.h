#ifndef DYNPTS_MEMORY_H
#define DYNPTS_MEMORY_H

#include "DynamicValue.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace llvm_interpreter
{

// In LLVM IR, memory is modeled as an untyped byte array.
// Therefore we also implement MemorySection as a raw byte array that can automatically grow when the memory limit is reached
class MemorySection
{
private:
	// Default (starting) section size = 1MB
	static const size_t DEFAULT_SIZE = 0x100000;

	// The 2 msb of the address are reserved to mark the address space of the pointer:
	// 00 - Global
	// 01 - Stack
	// 10 - Heap
	// 11 - Undefined
	static const uint64_t AddressSpaceMask = 0xC000000000000000;
	static const uint64_t GlobalAddressSpaceTag = 0;
	static const uint64_t StackAddressSpaceTag = 0x4000000000000000;
	static const uint64_t HeapAddressSpaceTag = 0x8000000000000000;

	size_t totalSize, usedSize;
	uint8_t* mem;

	void grow()
	{
		auto newSize = totalSize * 2;
		auto newMem = new uint8_t[newSize];
		std::memcpy(newMem, mem, totalSize);
		delete[] mem;
		mem = newMem;
	}

	bool isAddressLegal(Address addr) const
	{
		return (addr < usedSize) && (addr != 0);
	}
public:
	MemorySection(): totalSize(DEFAULT_SIZE), usedSize(1), mem(nullptr)
	{
		// We use a little trick here: set usedSize = 1 so that valid address starts at 1. Address 0 is reserved for NULL pointer
		mem = new uint8_t[DEFAULT_SIZE];
	}
	~MemorySection()
	{
		delete[] mem;
	}

	// Allocate (size) bypes of memory and return the allocated addr
	Address allocate(unsigned size)
	{
		if (usedSize + size >= totalSize)
			grow();

		assert(usedSize + size < totalSize);

		auto retAddr = usedSize;
		usedSize += size;
		return retAddr;
	}

	// Deallocate (size) bytes of allocated memory. This function is used to model stack deallocation
	void deallocate(unsigned size)
	{
		usedSize -= size;
	}

	// Deallocate the memory at address (addr). This function is used to model heap deallocation. Currently it does nothing, which might now satisfy the needs of applications with heavy heap traffic. A memory management algorithm has to be implemented in the future.
	void free(Address addr)
	{
	}

	// Reads an integer from memory at address (addr).
	DynamicValue readAsInt(Address addr, unsigned bitWidth) const
	{
		assert(bitWidth <= 64 && "No support for >64-bit int read");
		if (!isAddressLegal(addr))
			throw std::out_of_range("readAsInt() accesses unallocated memory");
		uint64_t val = 0;
		std::memcpy(&val, mem + addr, bitWidth / 8u);
		return DynamicValue::getIntValue(llvm::APInt(bitWidth, val));
	}

	DynamicValue readAsFloat(Address addr, bool isDouble = true) const
	{
		if (!isAddressLegal(addr))
			throw std::out_of_range("readAsFloat() accesses unallocated memory");
		if (isDouble)
		{
			double val = 0;
			std::memcpy(&val, mem + addr, sizeof(double));
			return DynamicValue::getFloatValue(val, true);
		}
		else
		{
			float val = 0;
			std::memcpy(&val, mem + addr, sizeof(float));
			return DynamicValue::getFloatValue(val, false);
		}
	}

	DynamicValue readAsPointer(Address addr) const
	{
		if (!isAddressLegal(addr))
			throw std::out_of_range("readAsPointer() accesses unallocated memory");
		Address retAddr = 0;
		std::memcpy(&retAddr, mem + addr, PointerValue::getPointerSize());
		
		auto addrSpace = PointerAddressSpace::GLOBAL_SPACE;
		switch (retAddr & AddressSpaceMask)
		{
			case GlobalAddressSpaceTag:
				addrSpace = PointerAddressSpace::GLOBAL_SPACE;
				break;
			case StackAddressSpaceTag:
				addrSpace = PointerAddressSpace::STACK_SPACE;
				break;
			case HeapAddressSpaceTag:
				addrSpace = PointerAddressSpace::HEAP_SPACE;
				break;
			default:
				throw std::runtime_error("readAsPointer() reads illegal pointer tag");
		}

		return DynamicValue::getPointerValue(addrSpace, retAddr & ~AddressSpaceMask);
	}

	void write(Address addr, const DynamicValue& val)
	{
		if (!isAddressLegal(addr))
			throw std::out_of_range("write() accesses unallocated memory");
		switch (val.getType())
		{
			case DynamicValueType::INT_VALUE:
			{
				auto& intVal = val.getAsIntValue().getInt();
				assert(intVal.getBitWidth() <= 64 && ">64-bit integer write not supported");
				auto rawData = intVal.getRawData();
				std::memcpy(mem + addr, rawData, intVal.getBitWidth() / 8);
				break;
			}
			case DynamicValueType::FLOAT_VALUE:
			{
				auto& fpVal = val.getAsFloatValue();
				if (fpVal.isDouble())
				{
					double f = fpVal.getFloat();
					std::memcpy(mem + addr, &f, sizeof(double));
				}
				else
				{
					float f = fpVal.getFloat();
					std::memcpy(mem + addr, &f, sizeof(float));
				}
				break;
			}
			case DynamicValueType::POINTER_VALUE:
			{
				auto& ptrVal = val.getAsPointerValue();
				auto ptrAddr = ptrVal.getAddress();
				switch (ptrVal.getAddressSpace())
				{
					case PointerAddressSpace::GLOBAL_SPACE:
						ptrAddr |= GlobalAddressSpaceTag;
						break;
					case PointerAddressSpace::STACK_SPACE:
						ptrAddr |= StackAddressSpaceTag;
						break;
					case PointerAddressSpace::HEAP_SPACE:
						ptrAddr |= HeapAddressSpaceTag;
						break;
				}
				std::memcpy(mem + addr, &ptrAddr, PointerValue::getPointerSize());
				break;
			}
			case DynamicValueType::ARRAY_VALUE:
			{
				auto& arrayVal = val.getAsArrayValue();
				for (auto i = 0u, e = arrayVal.getNumElements(); i < e; ++i)
				{
					write(addr, arrayVal.getElementAtIndex(i));
					addr += arrayVal.getElementSize();
				}
				break;
			}
			case DynamicValueType::STRUCT_VALUE:
			{
				auto& stVal = val.getAsStructValue();

				for (auto i = 0u, e = stVal.getNumElements(); i < e; ++i)
				{
					write(addr + stVal.getOffsetAtNum(i), stVal.getFieldAtNum(i));
				}
				break;
			}
			case DynamicValueType::UNDEF_VALUE:
				//throw std::runtime_error("Writing an undef value to memory?");
				break;
		}
	}

	// Be very careful when calling this function!
	void* getRawPointerAtAddress(Address addr)
	{
		return mem + addr;	
	}

	void dumpMemory(Address startAddr = 1u, unsigned size = 0) const;
};

}

#endif
