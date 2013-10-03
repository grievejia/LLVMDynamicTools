#ifndef VARINIT_H
#define VARINIT_H

#include "variable.h"

// Every struct type T is mapped to the vectors fieldSize and offsetMap.
// If field [i] in the expanded struct T begins an embedded struct, fieldSize[i] is the # of fields in the largest such struct, else S[i] = 1.
// Also, if a field has index (j) in the original struct, it has index offsetMap[j] in the expanded struct.
class StructInfo
{
private:
	std::vector<bool> arrayFlags;
	std::vector<unsigned> fieldSize;
	std::vector<unsigned> offsetMap;
public:
	typedef std::vector<unsigned>::iterator iterator;
	unsigned getSize() { return offsetMap.size(); }
	unsigned getExpandedSize() { return fieldSize.size(); }
	
	void addOffsetMap(unsigned newOffsetMap) { offsetMap.push_back(newOffsetMap); }
	void addField(unsigned newFieldSize, bool isArray)
	{
		fieldSize.push_back(newFieldSize);
		arrayFlags.push_back(isArray);
	}
	void appendFields(const StructInfo* other)
	{
		fieldSize.insert(fieldSize.end(), (other->fieldSize).begin(), (other->fieldSize).end());
		arrayFlags.insert(arrayFlags.end(), (other->arrayFlags).begin(), (other->arrayFlags).end());
	}
	
	// Must be called after all fields have been analyzed
	void finalize()
	{
		unsigned numField = fieldSize.size();
		if (numField == 0)
			fieldSize.resize(1);
		fieldSize[0] = numField;
	}
	bool isEmpty() { return (fieldSize[0] == 0);}
	bool isFieldArray(unsigned field) { return arrayFlags.at(field); }
	unsigned getOffset(unsigned off) { return offsetMap.at(off); }
	
	iterator field_begin() { return fieldSize.begin(); }
	iterator field_end() { return fieldSize.end(); }
	iterator offset_begin() { return offsetMap.begin(); }
	iterator offset_end() { return offsetMap.end(); }
	
	static const llvm::StructType* maxStruct;
	static unsigned maxStructSize;
};

void variableInit(llvm::Module& M);
Variable* ConstGEPtoVariable(llvm::ConstantExpr* exp);
unsigned GEPtoOffset(llvm::GetElementPtrInst* inst);
Variable* ptrToVariable(llvm::Value* v);

// Set the global pointer point to a specific target. Weak update.
inline void globalPtsSet(Variable* ptr, Variable* tgt)
{
	PtsSet set;
	set.insert(tgt);
	ptsGraph.update(ptr, set, true);
}

// Copy from a global pointer to another
// Require that src must point to something. The update to dst is a weak update
inline void globalPtsCopy(Variable* dst, Variable* src)
{
	PtsSet srcSet = ptsGraph.lookup(src);
	ptsGraph.update(dst, srcSet, true);
}

// Get the target of a global pointer. Return NULL if target not found
// The pointer ptr must points to at most one target
// This is an expensive operation. Avoid using this function to do pointer copies (use globalPtsCopy instead).
inline Variable* globalPtsGet(Variable* ptr)
{
	PtsSet ret = ptsGraph.lookup(ptr);
	if (ret.isEmpty())
		return NULL;

	std::vector<unsigned> indexVec;
	ret.toIndexVector(indexVec);
	return variableFactory.getVariable(indexVec[0]);
}

extern llvm::DenseMap<Variable*, Variable*> allocMap;
extern llvm::DenseMap<llvm::Function*, TopLevelVar*> retPtrMap;
extern llvm::DenseMap<const llvm::StructType*, StructInfo*> structInfoMap;
extern std::vector<llvm::Function*> addressTakenFunctions;

#endif
