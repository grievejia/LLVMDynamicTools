#ifndef DYNPTS_STACKFRAME_H
#define DYNPTS_STACKFRAME_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Function.h"

#include <unordered_map>

namespace llvm_interpreter
{

// StackFrame - This struct represents one stack frame currently executing.
class StackFrame
{
private:
	const llvm::Function* curFunction;// The currently executing function

	unsigned allocSize;

	std::unordered_map<const llvm::Value*, DynamicValue> vRegs;
	std::vector<DynamicValue> varArgs; // Values passed through an ellipsis
public:
	using const_vararg_iterator = decltype(varArgs)::const_iterator;
	using const_iterator = decltype(vRegs)::const_iterator;

	StackFrame(const llvm::Function* f): curFunction(f), allocSize(0) {}

	StackFrame(StackFrame&& rhs) = default;
	StackFrame& operator=(StackFrame&& rhs) = default;

	const llvm::Function* getFunction() const { return curFunction; }
	unsigned getAllocationSize() const { return allocSize; }
	void increaseAllocationSize(unsigned sz) { allocSize += sz; }

	void insertBinding(const llvm::Value* v, const DynamicValue& val)
	{
		//assert(!vRegs.count(v) && "Duplicate entries in env!");
		auto itr = vRegs.find(v);
		if (itr == vRegs.end())
			vRegs.insert(std::make_pair(v, val));
		else
			itr->second = std::move(val);
	}

	DynamicValue& lookup(const llvm::Value* val)
	{
		return vRegs.at(val);
	}
	DynamicValue lookup(const llvm::Value* val) const
	{
		return vRegs.at(val);
	}
	bool hasBinding(const llvm::Value* val) const
	{
		auto itr = vRegs.find(val);
		return itr != vRegs.end();
	}

	void insertVararg(DynamicValue&& val)
	{
		varArgs.push_back(std::move(val));
	}

	const_iterator begin() const { return vRegs.begin(); }
	const_iterator end() const { return vRegs.end(); }

	const_vararg_iterator vararg_begin() const { return varArgs.begin(); }
	const_vararg_iterator vararg_end() const { return varArgs.end(); }
	llvm::iterator_range<const_vararg_iterator> varargs() const
	{
		return llvm::iterator_range<const_vararg_iterator>(vararg_begin(), vararg_end());
	}

	void dumpFrame() const;
};

class StackFrames
{
private:
	std::vector<std::unique_ptr<StackFrame>> frames;
public:
	StackFrames() = default;

	StackFrame& createFrame(const llvm::Function* f)
	{
		frames.emplace_back(std::make_unique<StackFrame>(f));
		return *frames.back();
	}

	StackFrame& getCurrentFrame()
	{
		return *frames.back();
	}

	void popFrame()
	{
		assert(!frames.empty());
		frames.pop_back();
	}

	void dumpContext() const;
};

}

#endif
