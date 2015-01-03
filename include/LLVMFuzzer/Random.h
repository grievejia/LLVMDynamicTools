#ifndef LLVM_FUZZER_RANDOM_H
#define LLVM_FUZZER_RANDOM_H

#include <cstdint>
#include <random>

namespace llvm_fuzzer
{

// A utility class to provide a (cryptographically insecure) pseudo-random number generator
// It is implemented as a thin wrapper around std::uniform_int_distribution from C++11 random library
class Random
{
private:
	std::mt19937 gen;
public:
	Random(uint64_t seed): gen(seed) {}
 
	// Return a random 32 bit integer.
	uint32_t getRandomUInt32(uint32_t lo = 0, uint32_t hi = std::numeric_limits<uint32_t>::max())
	{
		std::uniform_int_distribution<uint32_t> distr(lo, hi);
		return distr(gen);
	}
 
	// Return a random 64 bit integer.
	uint64_t getRandomUInt64(uint64_t lo = 0, uint64_t hi = std::numeric_limits<uint64_t>::max())
	{
		std::uniform_int_distribution<uint64_t> distr(lo, hi);
		return distr(gen);
	}

	// Return a random bool
	bool getRandomBool()
	{
		return static_cast<bool>(getRandomUInt32());
	}

	// Return a random bool (with 1/n probability to be true)
	bool getRandomBool(uint32_t n)
	{
		auto val = getRandomUInt32() % n;
		if (val == 0)
			return true;
		else
			return false;
	}
};

}

#endif
