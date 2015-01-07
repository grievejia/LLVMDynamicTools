# Make sure the compiler can find include files from our library. 
include_directories (${dynamic_pts_SOURCE_DIR}/include/LLVMFuzzer)

set (FuzzerSourceFiles ValueGenerator.cpp GeneratorEnvironment.cpp BlockGenerator.cpp ProgramGenerator.cpp main.cpp)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g")
add_executable(llvm-fuzzer ${FuzzerSourceFiles}) 

# Find the libraries that correspond to the LLVM components that we wish to use
llvm_map_components_to_libnames(FuzzerReferencedLLVMLibs core ipa support)

# Link against LLVM libraries
target_link_libraries(llvm-fuzzer ${FuzzerReferencedLLVMLibs})