#include "main.h"
#include "interpreter.h"
#include "variable.h"
#include "varInit.h"

#include <string>

using namespace llvm;

DataLayout* layoutInfo;
PtsGraph ptsGraph;

namespace {
	cl::opt<std::string> bitcodeFile(cl::desc("<input bitcode (*.ll or *.bc)>"), cl::Positional, cl::Required);
	cl::list<std::string>  Argv(cl::ConsumeAfter, cl::desc("<program arguments>..."));
}	// end of anonymous namespace

int main(int argc, char **argv, char * const *envp) {
	cl::ParseCommandLineOptions(argc, argv, "Jia's execution tracer\n");
	errs() << "Hello, " << bitcodeFile << " !\n";

	// Load the bitcode
	LLVMContext& context = getGlobalContext();
	SMDiagnostic errDiag;
	Module* module = ParseIRFile(bitcodeFile, errDiag, context);
	if (module == NULL)
	{
		errs() << "Bitcode parsing failed!\n";
		return -1;
	}

	// Check the bitcode
	std::string errMsg;
	if (module->MaterializeAllPermanently(&errMsg))
	{
		errs() << "bitcode read error: " << errMsg << "\n";
		return -1;
	}

	// Initialize variables
	layoutInfo = new DataLayout(module);
	variableInit(*module);
	variableFactory.sortVariables();
	//variableFactory.printFactoryInfo();
	delete layoutInfo;

	// Move pts-to info from ptsInit to ptsGraph
	for (DenseMap<Variable*, Variable*>::iterator itr = ptsInit.begin(), ite = ptsInit.end(); itr != ite; ++itr)
	{
		Variable* lhs = itr->first;
		if (lhs->getType() == ARGUMENT || (lhs->getType() == TOP_LEVEL && !((TopLevelVar*)lhs)->isGlobal()))
			continue;
		
		Variable* rhs = itr->second;
		ptsGraph.update(lhs, rhs);
	}

	// Do the interpretation
	MyInterpreter* interp = new MyInterpreter(module);
	Function* startFunc = module->getFunction("main");
	if (startFunc == NULL)
	{
		errs() << "Cannot find main function!\n";
		return -1;
	}
	Argv.insert(Argv.begin(), bitcodeFile);
	int retValue = interp->runFunctionAsMain(startFunc, Argv, envp);
	errs() << "Interpreter returns " << retValue << "\n";

	ptsGraph.printPtsSets();

	errs() << "Bye-bye!\n";

	return 0;
}

