#ifndef DYNPTS_MEMORY_H
#define DYNPTS_MEMORY_H

#include "DynamicValue.h"

#include <cstdint>
#include <cstdlib>
#include <map>

namespace llvm_interpreter
{

class MemoryRegion
{
private:
	using MemPair = std::pair<Address, DynamicValue>;
	using MemPairVec = std::vector<MemPair>;

	MemPairVec vec;

	MemoryRegion() = default;
public:
	using const_iterator = MemPairVec::const_iterator;

	unsigned getNumEntry() const { return vec.size(); }
	unsigned isEmpty() const { return vec.empty(); }

	const_iterator begin() const { return vec.begin(); }
	const_iterator end() const { return vec.end(); }

	friend class MemorySection;
};

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
		auto itr = std::prev(mem.upper_bound(addr));

		// Aggregates(except for vectors, which we don't support) are not first-class values. Therefore, we cannot read any aggregates directly
		if (itr->second.isAggregateValue())
			return itr->second.getValueAtOffset(addr - itr->first);
		else if (itr->first != addr)
			throw std::out_of_range("read() offsets into a scalar value");
		else
			return itr->second;
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
		auto itr = std::prev(mem.upper_bound(addr));

		// Aggregates(except for vectors, which we don't support) are not first-class values. Therefore, we cannot overwrite any aggregates directly
		if (itr->second.isAggregateValue())
			itr->second.getValueAtOffset(addr - itr->first) = std::move(val);
		else if (itr->first != addr)
			throw std::out_of_range("write() offsets into a scalar value");
		else
			itr->second = std::move(val);
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

	void free(Address addr)
	{
		auto itr = mem.find(addr);
		if (itr == mem.end())
			throw std::out_of_range("free() received invalid address");
		mem.erase(itr);
	}

	Address allocateAndInitialize(unsigned size, DynamicValue&& val)
	{
		auto allocatedAddr = nextAddr;
		nextAddr += size;

		insertOrAssign(allocatedAddr, std::move(val));
		return allocatedAddr;
	}

	MemoryRegion readMemoryRegion(Address addr, unsigned size) const
	{
		assert(addr < nextAddr);

		auto retRegion = MemoryRegion();
		auto endAddr = addr + size;
		auto itr = std::prev(mem.upper_bound(addr));
		for (auto ite = mem.end(); itr != ite; ++itr)
		{
			if (itr->first >= endAddr)
				break;
			retRegion.vec.emplace_back(addr - itr->first, itr->second);
		}

		return retRegion;
	}

	void writeMemoryRegion(Address addr, MemoryRegion&& memRegion)
	{
		// Remove old mappings
		for (auto const& mapping: memRegion.vec)
			mem.erase(mapping.first);

		// Insert new mappings
		for (auto& mapping: memRegion.vec)
			insertOrAssign(mapping.first + addr, std::move(mapping.second));
	}

	void dumpMemory() const;
};

}

#endif
