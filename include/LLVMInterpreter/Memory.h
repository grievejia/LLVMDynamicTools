#ifndef DYNPTS_MEMORY_H
#define DYNPTS_MEMORY_H

#include "DynamicValue.h"

#include <cstdint>
#include <cstdlib>
#include <map>

namespace llvm_interpreter
{

class MemorySection
{
private:
	Address nextAddr;

	std::map<Address, DynamicValue> mem;

	void insertOrAssign(Address addr, DynamicValue&& val)
	{
		auto itr = mem.find(addr);
		if (itr == mem.end())
			mem.insert(std::make_pair(addr, std::move(val)));
		else
			itr->second = std::move(val);
	}
public:
	MemorySection(): nextAddr(1) {}

	void clear()
	{
		nextAddr = 1;
		mem.clear();
	}

	DynamicValue exactRead(Address addr) const
	{
		// If addr is not mapped, let the exception get thrown
		return mem.at(addr);
	}

	DynamicValue& read(Address addr)
	{
		auto itr = mem.lower_bound(addr);
		if (itr == mem.end())
			throw std::out_of_range("read() accesses an unmapped address");

		if (itr->first == addr)
			return itr->second;
		else if (!itr->second.isAggregateValue())
			throw std::out_of_range("read() offsets into a scalar value");
		else
			return itr->second.getValueAtOffset(addr - itr->first);
	}

	DynamicValue readInitialize(Address addr, DynamicValue&& val)
	{
		auto itr = mem.find(addr);
		if (itr == mem.end())
			itr = mem.insert(itr, std::make_pair(addr, std::move(val)));
		return itr->second;
	}

	void exactWrite(Address addr, DynamicValue&& val)
	{
		insertOrAssign(addr, std::move(val));
	}

	void write(Address addr, DynamicValue&& val)
	{
		auto itr = mem.lower_bound(addr);
		if (itr == mem.end())
			throw std::out_of_range("write() accesses an unmapped address");

		if (itr->first == addr)
			insertOrAssign(addr, std::move(val));
		else if (itr->second.isAggregateValue())
			throw std::out_of_range("read() offsets into a scalar value");
		else
			itr->second.getValueAtOffset(addr - itr->first) = std::move(val);
	}

	Address allocate(unsigned size)
	{
		auto allocatedAddr = nextAddr;
		nextAddr += size;

		insertOrAssign(allocatedAddr, DynamicValue::getUndefValue());
		return allocatedAddr;
	}

	void deallocate(unsigned size)
	{
		nextAddr -= size;
		mem.erase(mem.lower_bound(nextAddr), mem.end());
	}

	Address allocateAndInitialize(unsigned size, DynamicValue&& val)
	{
		auto allocatedAddr = nextAddr;
		nextAddr += size;

		insertOrAssign(allocatedAddr, std::move(val));
		return allocatedAddr;
	}
};

}

#endif
