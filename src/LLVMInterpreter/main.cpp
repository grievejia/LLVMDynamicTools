#include "Interpreter.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;
using namespace llvm_interpreter;

cl::opt<std::string> InputFile(cl::desc("<input bitcode>"), cl::Positional, cl::init("-"));

cl::list<std::string> InputArgv(cl::ConsumeAfter, cl::desc("<program arguments>..."));

// Main driver of the interpreter
int main(int argc, char** argv, char* const *envp)
{
	sys::PrintStackTraceOnErrorSignal();
	PrettyStackTraceProgram X(argc, argv);

	auto& context = getGlobalContext();

	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();
	InitializeNativeTargetAsmParser();

	cl::ParseCommandLineOptions(argc, argv, "llvm interpreter & dynamic compiler\n");

	// Disable core file
	sys::Process::PreventCoreFiles();

	// Read and parse the IR file
	SMDiagnostic err;
	auto module = std::unique_ptr<Module>(ParseIRFile(InputFile, err, context));
	if (!module)
	{
		err.print(argv[0], errs());
		std::exit(1);
	}

	// Load the whole bitcode file eagerly
	if (auto errCode = module->materializeAllPermanently())
	{
		errs() << argv[0] << ": bitcode didn't read correctly.\n";
		errs() << "Reason: " << errCode.message() << "\n";
		std::exit(1);
	}

	// Add the module's name to the start of the vector of arguments to main().
	InputArgv.insert(InputArgv.begin(), InputFile);
	auto entryFn = module->getFunction("main");
	if (entryFn == nullptr)
	{
		errs() << "\'main\' function not found in module.\n";
		return -1;
	}

	Interpreter interpreter(module.get());

	interpreter.evaluateGlobals();
	auto retInt = interpreter.runMain(entryFn, InputArgv);

	errs() << "Interpreter returns value " << retInt << "\n";

	return 0;
}