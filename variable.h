#ifndef VARIABLE_H
#define VARIABLE_H

#include "util.h"
#include "main.h"

// Variable definition
typedef enum {
	TOP_LEVEL, ADDR_TAKEN, ARGUMENT
} VariableType;

class Variable
{
private:
	VariableType type;
	unsigned index;
	bool global;
protected:
	Variable(VariableType t, bool g): type(t), global(g) {}
public:
	VariableType getType() { return type; }
	unsigned getIndex() { return index; }
	void setIndex(unsigned i) { index = i; }
	bool isGlobal() { return global; }
	friend llvm::raw_ostream& operator<< (llvm::raw_ostream& stream, const Variable& v);
	
	std::string name;
};

class TopLevelVar: public Variable
{
private:
	unsigned offsetBound;
public:
	TopLevelVar(bool g, unsigned o): Variable(TOP_LEVEL, g), offsetBound(o) {}
	unsigned getOffsetBound() { return offsetBound; }
	void setOffsetBound(unsigned o) { offsetBound = o; }
};

class ArgumentVar: public Variable
{
public:
	ArgumentVar(): Variable(ARGUMENT, false) {}
};

class AddrTakenVar: public Variable
{
private:
	unsigned offsetBound;
public:
	AddrTakenVar(bool g, unsigned o): Variable(ADDR_TAKEN, g), offsetBound(o) {}
	unsigned getOffsetBound() { return offsetBound; }
};

// Variable can only be generated from this factory
// Basically all information on variables can be found here
class VariableFactory
{
private:
	std::vector<Variable*> allocatedVars;
	llvm::DenseMap<llvm::Value*, Variable*> varMap;
	llvm::DenseMap<AddrTakenVar*, llvm::Value*> allocSiteMap;
	TopLevelVar *p2unknown, *nullPtr;
	AddrTakenVar* unknown;
	unsigned numAddrTakenVar, numTopLevelVar;
	bool sorted;
public:
	typedef std::vector<Variable*>::iterator iterator;
	VariableFactory();
	
	// Variable constructor
	TopLevelVar* getNextTopLevelVar(bool g = false, unsigned offsetBound = 0);
	AddrTakenVar* getNextAddrTakenVar(bool g = false, unsigned offsetBound = 0);
	ArgumentVar* getNextArgumentVar();
	unsigned getNumTopLevelVar() { return numTopLevelVar; }
	unsigned getNumAddrTakenVar() { return numAddrTakenVar; }
	unsigned getNumVar() { return allocatedVars.size(); }
	
	// Sort created variables: addr-taken first, then top-level
	void sortVariables();
	
	// Value => Variable Mapping
	void setMappedVar(llvm::Value* val, Variable* var);
	Variable* getMappedVar(llvm::Value* v);		// Return NULL if value not found
	void setAllocSite(AddrTakenVar* var, llvm::Value* val);
	llvm::Value* getAllocSite(AddrTakenVar* v);	// Return NULL if var not found
	
	// accessor to private fields
	TopLevelVar* getNullPointer() { return nullPtr; }
	TopLevelVar* getPointerToUnknown() { return p2unknown; }
	AddrTakenVar* getUnknownTarget() { return unknown; }
	Variable* getVariable(unsigned idx)
	{
		assert(idx < allocatedVars.size() && "getVariable out of range!");
		return allocatedVars[idx];
	}
	
	// Iterators (must be called after variables are sorted)
	iterator addr_begin();
	iterator addr_end();
	iterator top_begin();
	iterator top_end();
	
	// For easy debug
	void printFactoryInfo();
	
	~VariableFactory();
};

// We only have one global variable factory
extern VariableFactory variableFactory;

// Point-to set
class PtsSet
{
private:
	// Point-to set implementation is reference-counted
	struct PtsSetImpl: public RefCountObject
	{
		// Right now we are using sparse bit vectors
		llvm::SparseBitVector<128> ptsTo;
		// We use the default copy constructor, assignement operator, and destructor
	};
	RefCountPtr<PtsSetImpl> value;
	typedef llvm::SparseBitVector<128>::iterator iterator;
public:
	
	PtsSet(): value(new PtsSetImpl()) {}
	
	// This is an expensive operation
	bool operator==(const PtsSet& other) const { return value->ptsTo == (other.value)->ptsTo; }
	
	// Return true iff the variable is found
	bool find(Variable* v) const
	{
		return ((value->ptsTo).test(v->getIndex()));
	}
	// insert, remove and unionWith: return true iff the set is changed
	bool insert(Variable* v);
	bool remove(Variable* v);
	bool unionWith(const PtsSet other)
	{
		return ((value->ptsTo) |= (other.value)->ptsTo);
	}
	
	bool contains(const PtsSet other) const
	{
		return ((value->ptsTo).contains((other.value)->ptsTo));
	}
	
	unsigned getSize() const
	{
		return (value->ptsTo).count();		// NOT a constant time operation!
	}
	bool isEmpty() const		// Always prefer using this function to perform empty test 
	{
		return (value->ptsTo).empty();
	}
	
	void toIndexVector(std::vector<unsigned>& vec) const;
};

class PtsGraph
{
private:
	llvm::DenseMap<Variable*, Variable*> store;
public:
	typedef llvm::DenseMap<Variable*, Variable*>::iterator iterator;
	PtsGraph() {}
	Variable* lookup(Variable* v)
	{
		if (v == NULL)
			return NULL;

		iterator itr = store.find(v);
		if (itr == store.end())
			return NULL;
		else
			return itr->second;
	}
	void update(Variable* ptr, Variable* obj)
	{
		iterator itr = store.find(ptr);
		if (itr ==  store.end())
		{
			if (obj != NULL)
				store.insert(std::make_pair(ptr, obj));
		}
		else
		{
			if (obj != NULL)
				itr->second = obj;
			else
				store.erase(itr);
		}
	}
	void printPtsSets()
	{
		llvm::errs() << "----Print Pts-to set ----\n";
		for (iterator itr = store.begin(), ite = store.end(); itr != ite; ++itr)
		{
			Variable* v = itr->first;
			Variable* obj = itr->second;
			llvm::errs() << *v << "  ==>>  " << *obj;
		}
		llvm::errs() << "----End of Print----\n";
	}
};

#endif
