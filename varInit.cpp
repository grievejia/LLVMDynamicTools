#include "varInit.h"
#include "main.h"

#include <sstream>

using namespace llvm;

// May not need this map. We'll see
DenseMap<Function*, TopLevelVar*> retPtrMap;
// Hold initial pts-to info before all variables are discovered	
DenseMap<Variable*, Variable*> ptsInit;
// Map llvm type to corresponding StructInfo
DenseMap<const StructType*, StructInfo*> structInfoMap;
// Initialize max struct info
const StructType* StructInfo::maxStruct = NULL;
unsigned StructInfo::maxStructSize = 0;
// Store all address-taken functions. This is useful when we build the interprocedural CFG, where each function pointer is assumed to target any address-taken function
std::vector<Function*> addressTakenFunctions;

extern DataLayout* layoutInfo;

// Compute the corresponding top-level var for a constant GEP expression
Variable* ConstGEPtoVariable(ConstantExpr* exp)
{
	Value* basePtr = exp->getOperand(0);
	Variable* baseVar = NULL;
	
	// First we need to find the starting top-level variable in that struct
	while (ConstantExpr* baseExp = dyn_cast<ConstantExpr>(basePtr))
	{
		// Strip bitcasts from the RHS, until we get to GEP, unknown, or non-CE value
		switch (baseExp->getOpcode())
		{
			case Instruction::BitCast:
			{
				ConstantExpr* cast = dyn_cast<ConstantExpr>(baseExp->getOperand(0));
				if (cast != NULL)
					baseVar = ConstGEPtoVariable(cast);
				else
					basePtr = baseExp->getOperand(0);
				break;
			}
			case Instruction::IntToPtr:
				return variableFactory.getPointerToUnknown();
			case Instruction::GetElementPtr:
				// We can have (gep (bitcast (gep X 1)) 1); the inner gep must be handled recursively.
				baseVar = ConstGEPtoVariable(baseExp);
				break;
			default:
				assert(false && "ConstGEPtoVariable: unexpected constant expr type");
		}
		
		if (baseVar != NULL)
			break;
	}
	
	if (baseVar == NULL)
		baseVar = variableFactory.getMappedVar(basePtr);
	
	assert(baseVar != NULL && "ConstGEPtoVariable: baseVar lookup failed");
	//errs() << "baseVar = " << *baseVar << "\n";
	assert(baseVar->getType() == TOP_LEVEL && "baseVar must be a top-level var");
	
	// Now compute the offset from base pointer (in bytes)
	unsigned offset = 0;	
	for (gep_type_iterator gi = gep_type_begin(*exp), ge= gep_type_end(*exp); gi != ge; ++gi)
	{
		ConstantInt* idx = cast<ConstantInt>(gi.getOperand());
		Type* type = *gi;
				
		if (isa<PointerType>(type))
		{
			// offset is the size of the type times idx
			Type* elemType = cast<PointerType>(type)->getElementType();
			offset += layoutInfo->getTypeAllocSize(elemType) * idx->getZExtValue();
		}
		else if (type->isArrayTy())
		{
			Type* elemType = cast<ArrayType>(type)->getElementType();
			offset += layoutInfo->getTypeAllocSize(elemType) * idx->getZExtValue();
		}
		else if (type->isStructTy())
		{
			offset += layoutInfo->getStructLayout(cast<StructType>(type))->getElementOffset(idx->getZExtValue());
		}
		else
			assert(false && "GEP into unsupported data type");
	}
	errs() << "offset = " << offset << "\n";
	
	// Finally, use that offset to select the corresponding variable
	PointerType* trueType = cast<PointerType>(basePtr->getType());
	Type* trueElemType = trueType->getElementType();
	unsigned fieldNum = 0;
	while (offset > 0)
	{
		// Collapse array type
		while(const ArrayType *arrayType= dyn_cast<ArrayType>(trueElemType))
			trueElemType = arrayType->getElementType();
		offset %= layoutInfo->getTypeAllocSize(trueElemType);
		if (trueElemType->isStructTy())
		{
			StructType* stType = cast<StructType>(trueElemType);
			const StructLayout* stLayout = layoutInfo->getStructLayout(stType);
			unsigned idx = stLayout->getElementContainingOffset(offset);
			fieldNum += idx;
			offset -= stLayout->getElementOffset(idx);
			trueElemType = stType->getElementType(idx);
		}
		else
		{
			// Just some sanity checks
			assert(offset == 0);
		}
	}
	
	errs() << "fieldNum = " << fieldNum << "\n";
	assert((fieldNum == 0 || fieldNum < ((TopLevelVar*)baseVar)->getOffsetBound()) && "struct access out of bound!");
	Variable* ret = variableFactory.getVariable(baseVar->getIndex() + fieldNum);
	errs() << "return " << *ret << "\n";

	return ret;
}

// Get the corresponding variable for a given pointer, which may be a top-level variable or a const GEP expression
Variable* ptrToVariable(Value* v)
{
	if (isa<ConstantPointerNull>(v))
		return variableFactory.getNullPointer();
	else if (ConstantExpr* exp = dyn_cast<ConstantExpr>(v))
		return ConstGEPtoVariable(exp);
	else
		return variableFactory.getMappedVar(v);
}

// Compute the corresponding offset for a GEP instruction
unsigned GEPtoOffset(GetElementPtrInst* inst)
{
	unsigned ret = 0;
	for (gep_type_iterator gi = gep_type_begin(*inst), ge= gep_type_end(*inst); gi != ge; ++gi)
	{
		ConstantInt* idxOp = dyn_cast<ConstantInt>(gi.getOperand());
		// idxOp may not be a constant for arrays
		unsigned idx = (idxOp == NULL? 0: idxOp->getZExtValue());
		const StructType* stType = dyn_cast<StructType>(*gi);
		
		// Not a struct: either a single-valued type or an array
		if (stType == NULL)
			continue;
		else
		{
			assert(structInfoMap.count(stType) && "structInfoMap should have info for all structs!");
			StructInfo* stInfo = structInfoMap[stType];
			assert(idx < stInfo->getSize() && "struct access out of bound!");
			ret += stInfo->getOffset(idx);
		}
	}
	
	return ret;
}

namespace {

void processGlobalInitializer(Variable* ptr, Constant* initializer)
{
	assert(initializer != NULL && ptr != NULL);
	
	// Here we abuse the LLVM dynamic cast functions a little bit. Using Visitor might be a better choice but casting is simpler here
	if (isa<ConstantPointerNull>(initializer) || isa<UndefValue>(initializer))
	{
		//No constraint for null/undef init
		return;
	}
	else if (ConstantExpr* exp = dyn_cast<ConstantExpr>(initializer))
	{
		switch (exp->getOpcode())
		{
			case Instruction::BitCast:
				// BitCast is a no-op. Process its operand instead
				processGlobalInitializer(ptr, exp->getOperand(0));
				break;
			case Instruction::GetElementPtr:
			{
				Variable* other;
				other = ConstGEPtoVariable(exp);
				assert(ptsInit.count(other) && "initializing using an uninitialized variable");
				ptsInit[ptsInit[ptr]] = ptsInit[other];
				break;
			}
			case Instruction::IntToPtr:
				// var will point to unknown target
				ptsInit[ptsInit[ptr]] = variableFactory.getUnknownTarget();
				break;
			case Instruction::PtrToInt:
				// Do nothing
				break;
			default:
		   		assert(false && "unexpected global initializer constant expr type");
		}
		return;
	}
	else if (ConstantStruct* s = dyn_cast<ConstantStruct>(initializer))
	{
		assert(ptr->getType() == TOP_LEVEL);
		
		StructType* stType = cast<StructType>(s->getType());
		assert(structInfoMap.count(stType) && "structInfoMap should have info for all structs!");
		StructInfo* stInfo = structInfoMap[stType];
		
		// Sequentially initialize each field
		for (unsigned i = 0; i < s->getNumOperands(); ++i)
		{
			Variable* field = variableFactory.getVariable(ptr->getIndex() + stInfo->getOffset(i));
			Value* v = s->getOperand(i);
			Constant* cv = dyn_cast<Constant>(v);
			assert(cv != NULL && "fields of constant struct must be constant themselves");
			processGlobalInitializer(field, cv);
		}
		
		return;
	}
	else if (GlobalVariable* other = dyn_cast<GlobalVariable>(initializer))
	{
		DenseMap<Variable*, Variable*>::iterator itr = ptsInit.find(variableFactory.getMappedVar(other));
		assert(itr != ptsInit.end() && "Global initializer variable not initialized!");
		Variable* ptrTgt = ptsInit[ptr];
		assert(ptrTgt != NULL && "Global variable no tgt?");
		ptsInit[ptrTgt] = itr->second;
		return;
	}
	else if (ConstantArray* a = dyn_cast<ConstantArray>(initializer))
	{
		// Just take the first element of the array
		Value* op0 = a->getOperand(0);
		Constant* cop0 = dyn_cast<Constant>(op0);
		assert(cop0 != NULL);
		processGlobalInitializer(ptr, cop0);
		return;
	}
	else if (isa<BlockAddress>(initializer) || isa<ConstantVector>(initializer))
	{
		assert(false && "Unsupported initializer type");
	}
}

void processStruct(Value* value, const StructType* structType, Function* f = NULL)	// The struct is global when f == NULL
{
	// We cannot handle opaque type
	assert(!structType->isOpaque() && "Opaque type not supported");
	// Sanity check
	assert(structType != NULL && "structType is NULL");
	
	std::string baseName = "";
	if (f != NULL) { baseName += f->getName(); baseName += "_"; }
	if (value->hasName()) baseName += value->getName();
	// Nasty string operation to find lhs variable name
	else { std::string s; raw_string_ostream ss(s); value->print(ss); baseName += ss.str().substr(2, ss.str().find_first_of("=")-3);}
	
	// We construct a pointer variable for each field
	// A better approach is to collect all constant GEP instructions and construct variables only if they are used. We want to do the simplest thing first
	assert(structInfoMap.count(structType) && "structInfoMap should have info for all structs!");
	StructInfo* stInfo = structInfoMap[structType];
	
	// Empty struct has only one pointer that points to nothing
	if (stInfo->isEmpty())
	{
		TopLevelVar* topVar = variableFactory.getNextTopLevelVar(f == NULL, false);
		topVar->name = "[empty_struct]" + baseName;
		variableFactory.setMappedVar(value, topVar);
		return;
	}
	
	// Non-empty structs: create one pointer and one target for each field
	unsigned stSize = stInfo->getExpandedSize();
	std::vector<TopLevelVar*> fields;
	// First create all top-level vars so that they have contiguous indicies
	for (unsigned i = 0; i < stSize; ++i)
	{
		TopLevelVar* topVar = variableFactory.getNextTopLevelVar(f == NULL, stInfo->isFieldArray(i), stSize - i);
		std::stringstream ss;
		ss << i;
		topVar->name = baseName + "/" + ss.str();
		fields.push_back(topVar);
	}
	variableFactory.setMappedVar(value, fields[0]);
	
	// Next create all addr-taken vars
	for (unsigned i = 0; i < stSize; ++i)
	{
		AddrTakenVar* atVar = variableFactory.getNextAddrTakenVar(f == NULL, stInfo->isFieldArray(i), stSize - i);
		TopLevelVar* topVar = fields[i];
		ptsInit[topVar] = atVar;
		atVar->name = topVar->name + "_tgt";
		variableFactory.setAllocSite(atVar, value);
	}
}

void processGlobals(Module& M)
{
	for(Module::global_iterator itr = M.global_begin(), ite = M.global_end(); itr != ite; ++itr)
	{
//		errs() << *itr << "\n";
		
		// Examine the type of this varaible
		const Type *type = itr->getType()->getElementType();
//		errs() << "["; type->dump(); errs() << "]\n";
		
		// An array is considered a single variable of its type.
		bool isArray= false;
		while(const ArrayType *arrayType= dyn_cast<ArrayType>(type)){
			type = arrayType->getElementType();
			isArray= true;
		}

		// Now construct the pointer and memory object variable
		// It depends on whether the type of this variable is a struct or not
		if (const StructType *structType = dyn_cast<StructType>(type))
		{
			// Construct a stuctVar for the entire variable
			processStruct(itr, structType);
		}
		else
		{
			// Global variables are always a top-level pointer in LLVM IR
			TopLevelVar* topVar = variableFactory.getNextTopLevelVar(true, isArray);
			topVar->name = itr->getName();
			variableFactory.setMappedVar(itr, topVar);
			AddrTakenVar* atVar = variableFactory.getNextAddrTakenVar(true, isArray);
			atVar->name = topVar->name + "_tgt";
			variableFactory.setAllocSite(atVar, itr);
			ptsInit[topVar] = atVar;
		}
	}

	// Init globals here since an initializer may refer to a global below it
	for(Module::global_iterator itr = M.global_begin(), ite = M.global_end(); itr != ite; ++itr)
		if (itr->hasInitializer())
		{
			Variable* ptr = variableFactory.getMappedVar(itr);
			processGlobalInitializer(ptr, itr->getInitializer());
		}
}

void processFunctionPointer(Function* f)
{
	bool addrTaken = f->hasAddressTaken();
	if (addrTaken)
	{
		// Allocate a function object and a pointer to that object
		TopLevelVar* funPtr = variableFactory.getNextTopLevelVar(true, false);
		funPtr->name = f->getName();
		funPtr->name = "<func>" + funPtr->name;
		variableFactory.setMappedVar(f, funPtr);
		
		AddrTakenVar* funObj = variableFactory.getNextAddrTakenVar(true, false);
		funObj->name = funPtr->name + "_tgt";
		variableFactory.setAllocSite(funObj, f);
		
		ptsInit[funPtr] = funObj;
		addressTakenFunctions.push_back(f);
	}
	
	// External functions should not be analyzed (since they are handled at the call site)
	if (f->isDeclaration() || f->isIntrinsic())
		return;
	
	assert(!f->isVarArg() && "var_arg currently not supported");
	
	if (f->getName() == "main" && f->arg_size() > 1)
	{
		Argument* argv = ++f->arg_begin();
		// Argv is treated as global variable
		TopLevelVar* argvPtr = variableFactory.getNextTopLevelVar(true, true);
		argvPtr->name = "argv_ptr";
		variableFactory.setMappedVar(argv, argvPtr);
		
		AddrTakenVar* argvObj = variableFactory.getNextAddrTakenVar(true, true);
		argvObj->name = "argv_tgt";
		variableFactory.setAllocSite(argvObj, argv);
		
		ptsInit[argvPtr] = argvObj;
	}
	else
	{
		// Make a top-level var for each ptr argument
		for (Function::arg_iterator itr= f->arg_begin(), ite= f->arg_end(); itr != ite; ++itr)
		{
			if (isa<PointerType>(itr->getType()))
			{
				ArgumentVar* argPtr = variableFactory.getNextArgumentVar();
				argPtr->name = f->getName();
				argPtr->name += "/arg_";
				argPtr->name += itr->getName();
				variableFactory.setMappedVar(itr, argPtr);
			}
		}
		
		// Make a top-level var for ptr return value
		if (isa<PointerType>(f->getReturnType()))
		{
			TopLevelVar* retPtr = variableFactory.getNextTopLevelVar(true, false);
			retPtr->name = f->getName();
			retPtr->name += "/ret";
			retPtrMap[f] = retPtr;
		}
	}
}

void visitFunction(Function* f)
{
	
	std::string fname = f->getName();
	
	for (inst_iterator itr = inst_begin(f), ite = inst_end(f); itr != ite; ++itr)
	{
		Instruction* inst = &(*itr);
		
		if (inst->getOpcode() == Instruction::Alloca)
		{
			AllocaInst* allocaInst= cast<AllocaInst>(inst);
			const Type* type = allocaInst->getAllocatedType();
			bool isArray = false;
			while(const ArrayType *arrayType= dyn_cast<ArrayType>(type))
			{
				type = arrayType->getElementType();
				isArray= true;
			}
			
			if (const StructType *structType= dyn_cast<StructType>(type))
				processStruct(inst, structType, f);
			else
			{
				TopLevelVar* instPtr = variableFactory.getNextTopLevelVar(false, isArray);
				if (inst->hasName()) instPtr->name = inst->getName();
				else { std::string s; raw_string_ostream ss(s); inst->print(ss); instPtr->name = ss.str().substr(2, ss.str().find_first_of("=")-3);}
				instPtr->name = fname + "_" + instPtr->name;
				variableFactory.setMappedVar(inst, instPtr);
				
				AddrTakenVar* allocObj = variableFactory.getNextAddrTakenVar(false, isArray);
				allocObj->name = instPtr->name + "_tgt";
				variableFactory.setAllocSite(allocObj, inst);
				
				ptsInit[instPtr] = allocObj;
			}
		}
		else if (PointerType* ptrType = dyn_cast<PointerType>(inst->getType()))
		{
			TopLevelVar* instPtr = variableFactory.getNextTopLevelVar(false, ptrType->getElementType()->isArrayTy());
			if (inst->hasName()) instPtr->name = inst->getName();
			else { std::string s; raw_string_ostream ss(s); ss << *inst; instPtr->name = ss.str().substr(2, ss.str().find_first_of("=")-3); }
			instPtr->name = fname + "_" + instPtr->name;
			variableFactory.setMappedVar(inst, instPtr);
		}
	}
}

// Forward declaration
StructInfo* addStructInfo(const StructType* st);

StructInfo* getStructInfo(const StructType* st)
{
	DenseMap<const llvm::StructType*, StructInfo*>::iterator itr = structInfoMap.find(st);
	if (itr != structInfoMap.end())
		return itr->second;
	else
		return addStructInfo(st);
}

// Expand (or flatten) the specified StructType and produce StructInfo
StructInfo* addStructInfo(const StructType* st)
{
	unsigned numField = 0;
	StructInfo* stInfo = new StructInfo();
	for (StructType::element_iterator itr = st->element_begin(), ite = st->element_end(); itr != ite; ++itr)
	{
		const Type* subType = *itr;
		bool isArray = false;
		// Treat an array field as a single element of its type
		while (const ArrayType* arrayType = dyn_cast<ArrayType>(subType))
		{
			subType = arrayType->getElementType();
			isArray = true;
		}
		// The offset is where this element will be placed in the expanded struct
		stInfo->addOffsetMap(numField);
		
		// Nested struct
		if (const StructType* structType = dyn_cast<StructType>(subType))
		{
			StructInfo* subInfo = getStructInfo(structType);
			assert(subInfo != NULL);
			
			// Copy information from this substruct
			stInfo->appendFields(subInfo);
			
			numField += subInfo->getExpandedSize();
		}
		else
		{
			stInfo->addField(1, isArray);
			++numField;
		}
	}
	
	stInfo->finalize();
	if (numField > StructInfo::maxStructSize)
	{
		StructInfo::maxStruct = st;
		StructInfo::maxStructSize = numField;
	}
	structInfoMap[st] = stInfo;
	
	return stInfo;
}

// Process the structs defined in this program
// We adopt the approach proposed by Pearce et al. in the paper "efficient field-sensitive pointer analysis of C"
void analyzeStruct(Module& M)
{
	TypeFinder usedStructTypes;
	usedStructTypes.run(M, false);
	for (TypeFinder::iterator itr = usedStructTypes.begin(), ite = usedStructTypes.end(); itr != ite; ++itr)
	{

		const StructType* st = *itr;
		addStructInfo(st);
	}
/*	
	errs() << "----------Print StructInfo------------\n";
	for (DenseMap<const llvm::StructType*, StructInfo*>::iterator itr = structInfoMap.begin(), ite = structInfoMap.end(); itr != ite; ++itr)
	{
		errs() << "Struct ";
		itr->first->dump();
		errs() << ": sz < ";
		StructInfo* info = itr->second;
		for (StructInfo::iterator szItr = info->field_begin(), szIte = info->field_end(); szItr != szIte; ++szItr)
			errs() << *szItr << " ";
		errs() << ">, offset < ";
		for (StructInfo::iterator offItr = info->offset_begin(), offIte = info->offset_end(); offItr != offIte; ++offItr)
			errs() << *offItr << " ";
		errs() << ">\n";
	}
	errs() << "----------End of print------------\n";
*/
}

} // end of anonymous namespace

void variableInit(Module& M)
{	
	// Initialize unknow pointer
	ptsInit[variableFactory.getPointerToUnknown()] = variableFactory.getUnknownTarget();
	
	// Analyze all struct types in the program
	analyzeStruct(M);

	// Process all global vars and their initializers
	processGlobals(M);

	// Process all function pointers
	for (Module::iterator itr = M.begin(), ite = M.end(); itr != ite; ++itr)
		processFunctionPointer(&(*itr));

	// Visit all functions to discover all variables
	for (Module::iterator itr = M.begin(), ite = M.end(); itr != ite; ++itr)
		visitFunction(&(*itr));

/*	variableFactory.printFactoryInfo();
	errs() << "ptsInit mapping:\n";
	for (DenseMap<Variable*, Variable*>::iterator itr = ptsInit.begin(), ite = ptsInit.end(); itr != ite; ++itr)
	{
		errs() << *(itr->first) << "\t==>>\t" << *(itr->second) << "\n";
	}*/
}
