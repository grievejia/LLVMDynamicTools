#include "ProgramGenerator.h"
#include "Random.h"
#include "Modifier.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace llvm;
using namespace llvm_fuzzer;

ProgramGenerator::ProgramGenerator(std::unique_ptr<llvm::Module> m, Random& r): module(std::move(m)), randomGenerator(r)
{
	assert(module->empty() && "ProgramGenerator only receives empty module!");
}
ProgramGenerator::~ProgramGenerator() = default;

Function* ProgramGenerator::getEmptyFunction(StringRef funName)
{
	auto argTypes = std::vector<Type*>();

	auto funType = FunctionType::get(Type::getInt32Ty(module->getContext()), argTypes, 0);
	auto retFun = Function::Create(funType, GlobalValue::ExternalLinkage, funName, module.get());

	return retFun;
}

void ProgramGenerator::fillFunction(Function* f)
{
	// Create a legal entry block.
	auto bb = BasicBlock::Create(f->getContext(), "bb", f);
	auto instGenerator = Modifier(bb, randomGenerator);

	instGenerator.addAllocations(10);
	instGenerator.addOperations(50);
	instGenerator.addReturnInst();
}

void ProgramGenerator::introduceControlFlow(Function* f)
{
	auto boolInsts = std::vector<Instruction*>();
	auto boolType = IntegerType::getInt1Ty(f->getContext());
	for (auto itr = inst_begin(f), ite = inst_end(f); itr != ite; ++itr)
	{
		if (itr->getType() == boolType)
			boolInsts.push_back(itr.getInstructionIterator());
	}

	std::random_shuffle(boolInsts.begin(), boolInsts.end(),
		[this] (auto n)
		{
			return randomGenerator.getRandomUInt64(0, n - 1);
		}
	);

	auto entryBlock = &f->getEntryBlock();
	for (auto inst: boolInsts)
	{
		auto srcBB = inst->getParent();
		auto dstBB = srcBB->splitBasicBlock(inst, "bb_split");
		inst->moveBefore(srcBB->getTerminator());
		if (isa<BranchInst>(srcBB->getTerminator()) && srcBB != entryBlock)
		{
			BranchInst::Create(srcBB, dstBB, inst, srcBB->getTerminator());
			srcBB->getTerminator()->eraseFromParent();
			
		}
		//	branchInst->setCondition(inst);
	}
}

Function* ProgramGenerator::generateRandomFunction(StringRef funName)
{
	auto f = getEmptyFunction(funName);

	// Generate lots of random instructions inside a single basic block
	fillFunction(f);

	// Break the basic block into many loops
	//introduceControlFlow(f);
	
	return f;
}

void ProgramGenerator::generateMainFunction(Function* entryFunc)
{
	auto mainFun = getEmptyFunction("main");

	// The main function just do one thing: call the generated entry function
	auto bb = BasicBlock::Create(mainFun->getContext(), "bb", mainFun);
	IRBuilder<> builder(bb);
	auto callInst = builder.CreateCall(entryFunc, "main.ret");
	builder.CreateRet(callInst);
}

void ProgramGenerator::generateRandomProgram()
{
	auto entryFunc = generateRandomFunction("autogen_func");
	generateMainFunction(entryFunc);
}
