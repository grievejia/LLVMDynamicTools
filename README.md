This project is essentially an LLVM interpreter. The current version builds on the latest LLVM SVN version (3.4)

Basically most of the interpreter implementation is a copy-paste from llvm codebase. Jason Wolfe used to have a working interpreter that is based on a giant hack (pointer-casting), which I don't really like. Also, the output format of his tool is somewhat incompatible to my analysis, and he used CMake to build the project, which is again incompatible with my GNU make. Therefore, I go ahead to build this project on my own without relying on the same hack.

The final goal of this project is to build a dynamic pointer analysis engine on top of this interpreter. This will allow us to compute the points-to sets dynamically. The output of this tool could become very, very helpful if we want to test the our own pointer analysis pass implementation.
