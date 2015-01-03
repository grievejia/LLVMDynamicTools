# Make sure the compiler can find include files from our library. 
include_directories (${dynamic_pts_SOURCE_DIR}/include/LLVMInterpreter)

set (SourceFiles DynamicValue.cpp Evaluation.cpp External.cpp Interpreter.cpp main.cpp)

add_executable(llvm-interpreter ${SourceFiles}) 

# Find the libraries that correspond to the LLVM components that we wish to use
llvm_map_components_to_libnames(ReferencedLLVMLibs core executionengine irreader instrumentation interpreter object support native)

# Find the FFI library
find_library(LibFFI NAMES ffi)

# Link against LLVM libraries
target_link_libraries(llvm-interpreter ${ReferencedLLVMLibs} ${LibFFI})