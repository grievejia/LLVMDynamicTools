#include "BlockGenerator.h"
#include "GeneratorEnvironment.h"
#include "ProgramGenerator.h"
#include "Random.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm_fuzzer;

static cl::opt<unsigned> FunctionAllocSize("asize", cl::desc("Approximately how many allocation instructions should be generated for each function"), cl::init(10));

ProgramGenerator::ProgramGenerator(std::unique_ptr<llvm::Module> m, Random& r): module(std::move(m)), randomGenerator(r)
{
	assert(module->empty() && "ProgramGenerator only receives empty module!");
}
ProgramGenerator::~ProgramGenerator() = default;

Function* ProgramGenerator::getEmptyFunction(StringRef funName, FunctionType* funType)
{
	auto retFun = Function::Create(funType, GlobalValue::ExternalLinkage, funName, module.get());
	BasicBlock::Create(retFun->getContext(), "entry", retFun);

	return retFun;
}

void ProgramGenerator::generateAllocationBlock(BasicBlock* bb, GeneratorEnvironment& env)
{
	auto numAlloc = std::lround(FunctionAllocSize * randomGenerator.getRandomDoubleWithNormalDist(1.0, 0.15));
	
	BlockGenerator blockGen(bb, env, randomGenerator);
	blockGen.addAllocations(numAlloc);
}

BasicBlock* ProgramGenerator::generateFunctionBody(BasicBlock* bb, GeneratorEnvironment& env)
{
	BlockGenerator blockGen(bb, env, randomGenerator);

	blockGen.addOperations(50);

	return bb;
}

Function* ProgramGenerator::generateRandomFunction(StringRef funName, FunctionType* funType, GeneratorEnvironment& env)
{
	auto f = getEmptyFunction(funName, funType);

	// Generate a bunch of stack allocations
	generateAllocationBlock(f->begin(), env);

	// Generate the function body
	auto bodyBlock = BasicBlock::Create(f->getContext(), "body", f);
	BranchInst::Create(bodyBlock, f->begin());
	auto finalBlock = generateFunctionBody(bodyBlock, env);

	// Generate return instruction
	BlockGenerator(finalBlock, env, randomGenerator).addReturnInst(funType->getReturnType());
	
	return f;
}

void ProgramGenerator::generateMainFunction(Function* entryFunc)
{
	auto mainType = FunctionType::get(Type::getInt32Ty(module->getContext()), false);
	auto mainFun = getEmptyFunction("main", mainType);

	// The main function just do one thing: call the generated entry function
	auto bb = mainFun->begin();
	IRBuilder<> builder(bb);
	auto callInst = builder.CreateCall(entryFunc, "main.ret");
	builder.CreateRet(callInst);
}

void ProgramGenerator::generateRandomProgram()
{
	auto entryType = FunctionType::get(Type::getInt32Ty(module->getContext()), false);

	auto globalEnv = GeneratorEnvironment::getEmptyEnvironment(module->getContext());
	auto entryFunc = generateRandomFunction("autogen_entry", entryType, globalEnv);
	generateMainFunction(entryFunc);
}
