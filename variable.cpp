#include "variable.h"

using namespace llvm;

VariableFactory variableFactory;
PtsGraph ptsGraph;

VariableFactory::VariableFactory(): numAddrTakenVar(0), numTopLevelVar(0), sorted(false)
{
	// Insert the top-level var that represent null pointer
	nullPtr = getNextTopLevelVar();
	nullPtr->name = "nullPtr";
	// Insert two special variable: unknown target and pointer to unknown target
	p2unknown = getNextTopLevelVar();
	p2unknown->name = "p2unknown";
	unknown = getNextAddrTakenVar();
	unknown->name = "unknownTarget";
}

llvm::raw_ostream& operator<< (llvm::raw_ostream& stream, const Variable& v)
{
	stream << v.name;
	return stream;
}

VariableFactory::~VariableFactory()
{
	for (VariableFactory::iterator itr = allocatedVars.begin(), ite = allocatedVars.end(); itr != ite; ++itr)
		delete (*itr);
	allocatedVars.clear();
}

TopLevelVar* VariableFactory::getNextTopLevelVar(bool g, bool a, unsigned bound)
{
	TopLevelVar* ret = new TopLevelVar(g, a, bound);
	ret->setIndex(allocatedVars.size());
	allocatedVars.push_back(ret);

	++numTopLevelVar;
	return ret;
}

AddrTakenVar* VariableFactory::getNextAddrTakenVar(bool g, bool a, unsigned bound)
{
	AddrTakenVar* ret = new AddrTakenVar(g, a, bound);
	ret->setIndex(allocatedVars.size());
	allocatedVars.push_back(ret);

	++numAddrTakenVar;
	return ret;
}

ArgumentVar* VariableFactory::getNextArgumentVar()
{
	ArgumentVar* ret = new ArgumentVar();
	ret->setIndex(allocatedVars.size());
	allocatedVars.push_back(ret);

	return ret;
}

void VariableFactory::setMappedVar(Value* val, Variable* var)
{
	assert(val != NULL && "setMappedVar to NULL");
	assert(!varMap.count(val) && "mapping already exists!");
	varMap[val] = var;
}

void VariableFactory::setAllocSite(AddrTakenVar* var, Value* val)
{
	assert(var != NULL && "setAllocSite to NULL");
	assert(!allocSiteMap.count(var) && "mapping already exists!");
	allocSiteMap[var] = val;
}

Variable* VariableFactory::getMappedVar(llvm::Value* v)
{
	assert(v != NULL && "getMappedVar from NULL");
	DenseMap<Value*, Variable*>::iterator itr = varMap.find(v);
	if (itr == varMap.end())
		return NULL;
	else
		return itr->second;
}

Value* VariableFactory::getAllocSite(AddrTakenVar* v)
{
	assert(v != NULL && "getAllocSite from NULL");
	DenseMap<AddrTakenVar*, Value*>::iterator itr = allocSiteMap.find(v);
	if (itr == allocSiteMap.end())
		return NULL;
	else
		return itr->second;
}

void VariableFactory::printFactoryInfo()
{
	errs() << "\n-------------------- variableFactory Info --------------------\n";
	errs() << "List of allocated variables:\n";
	for (VariableFactory::iterator itr = allocatedVars.begin(), ite = allocatedVars.end(); itr != ite; ++itr)
	{
		Variable* var = *itr;
		if (var->getType() == TOP_LEVEL)
		{
			errs() << "[top]  #" << var->getIndex() << "\t(" << var->name << ")";
			if (var->isGlobal())
				errs() << "\t\tGLOBAL";
			errs() << "\n";
		}
		else if (var->getType() == ADDR_TAKEN)
		{
			errs() << "[addr] #" << var->getIndex() << "\t(" << var->name << ")";
			if (var->isGlobal())
				errs() << "\t\tGLOBAL";
			errs() << "\n";
		}
		else
			errs() << "[arg] #" << var->getIndex() << "\t(" << var->name << ")\n";
	}
	
	errs() << "-------------------- End of variableFactory Info --------------------\n";
}

void VariableFactory::sortVariables()
{
	std::vector<Variable*> topVars, addrVars, argVars;

	for (VariableFactory::iterator itr = allocatedVars.begin(), ite = allocatedVars.end(); itr != ite; ++itr)
	{
		Variable* v = *itr;
		if (v->getType() == TOP_LEVEL)
			topVars.push_back(v);
		else if (v->getType() == ADDR_TAKEN)
			addrVars.push_back(v);
		else
			argVars.push_back(v);
	}

	allocatedVars.clear();
	allocatedVars.insert(allocatedVars.end(), addrVars.begin(), addrVars.end());
	allocatedVars.insert(allocatedVars.end(), topVars.begin(), topVars.end());
	allocatedVars.insert(allocatedVars.end(), argVars.begin(), argVars.end());
	topVars.clear();
	addrVars.clear();
	
	for (unsigned i = 0; i < allocatedVars.size(); ++i)
		allocatedVars[i]->setIndex(i);
	
	sorted = true;
}

VariableFactory::iterator VariableFactory::addr_begin()
{
	assert(sorted && "variables have not been sorted yet!");
	return allocatedVars.begin();
}

VariableFactory::iterator VariableFactory::addr_end()
{
	assert(sorted && "variables have not been sorted yet!");
	return allocatedVars.begin() + numAddrTakenVar;
}

VariableFactory::iterator VariableFactory::top_begin()
{
	assert(sorted && "variables have not been sorted yet!");
	return allocatedVars.begin() + numAddrTakenVar;
}

VariableFactory::iterator VariableFactory::top_end()
{
	assert(sorted && "variables have not been sorted yet!");
	return allocatedVars.begin() + numAddrTakenVar + numTopLevelVar;
}

bool PtsSet::insert(Variable* v)
{
	unsigned idx = v->getIndex();
	if ((value->ptsTo).test(idx))
		return false;
	else
	{
		if (value->isShared())
			value = new PtsSetImpl(*value);
		(value->ptsTo).set(idx);
		return true;
	}
}

bool PtsSet::remove(Variable* v)
{
	unsigned idx = v->getIndex();
	if ((value->ptsTo).test(idx))
	{
		if (value->isShared())
			value = new PtsSetImpl(*value);
		
		(value->ptsTo).reset(idx);
		return true;
	}
	else
		return false;
}

void PtsSet::toIndexVector (std::vector<unsigned>& vec) const
{
	for (PtsSet::iterator itr = (value->ptsTo).begin(), ite = (value->ptsTo).end(); itr != ite; ++itr)
		vec.push_back(*itr);
}

