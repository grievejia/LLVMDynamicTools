This project consists of several useful tool for dealing with LLVM IR runtime behaviors. Currently it consists of two parts, a custom-written LLVM IR interpreter, and an LLVM IR fuzzer (abandoned).

The LLVMInterpreter project is, as its name suggests, an interpreter for LLVM IR. The LLVM codebase has already included an existing interpreter called lli, but I'd like to write my own one for the following reasons:
- If you look at the source code for lli, you'll find that it is essentially a combination of a pure interpreter and a JIT. The JIT is less flexible for my purpose anyway so what I want is a cleaner codebase that contains an interpreter only.
- Personally I don't like the coding of lli. To me there are just so many places where I think I can write up something better. (I'm NOT blaming the LLVM developers. Generally speaking codes in the LLVM repository are of very high quality. My guess is that the lli interpreter codes are something that were hacked up some time in the history, but due to the lack of users they were not as well polished and maintained as other parts of the codebase). 
- One final goal of this project is to build a dynamic pointer analysis engine on top of this interpreter. Whenever I want to extend something, I am more comfortable when I have a fairly thourough understanding and total control of the basis of my work. In that sense, lli is probably not my best choice

My interpreter implementation has cleaner structure than lli. It also has a pretty good coverage of the langugage features of LLVM IR. Some notable unsupported language features are:
- Vector types and the corresponding instructions (insertelement, extractelement, shufflevector)
- External function call
- Atomic instructions (fence, atomicrmw, atomiccmpxchg)
- Indirect jumps (blockaddr, switch)
- Exceptions (invoke, landingpad)

The latter three restrictions can all be removed by running the -loweratomic, -lowerswitch, and -lowerinvoke prepasses. Vector types can be avoided by carefully picking what transformation passes you would like to run on an unoptimized piece of code.

Handling of the external function calls is a task left for the future work. Look for External.cpp if you want to figure out what library functions are supported. I suspect that I can use FFI to support lots of (relatively uninteresting) external calls, but this has not been done yet.

Building the project requires CMake (>2.8.8), Boost (>1.57), and a compiler that supports C++14 (g++>4.9 or clang++>3.4). Currently it builds on LLVM 3.5, but this may change if new version of LLVM library is available.
