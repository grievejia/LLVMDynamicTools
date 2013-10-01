#ifndef PTS_EXTERNAL_H
#define PTS_EXTERNAL_H

#include "main.h"

typedef enum {
  EFT_NOOP = 0,      //no effect on pointers
  EFT_ALLOC,        //returns a ptr to a newly allocated object
  EFT_REALLOC,      //like L_A0 if arg0 is a non-null ptr, else ALLOC
  EFT_NOSTRUCT_ALLOC, //like ALLOC but only allocates non-struct data
  EFT_STAT,         //retval points to an unknown static var X
  EFT_STAT2,        //ret -> X -> Y (X, Y - external static vars)
  EFT_L_A0,         //copies arg0, arg1, or arg2 into LHS
  EFT_L_A1,
  EFT_L_A2,
  EFT_L_A8,
  EFT_L_A0__A0R_A1R,  //copies the data that arg1 points to into the location
                      //  arg0 points to; note that several fields may be
                      //  copied at once if both point to structs.
                      //  Returns arg0.
  EFT_A1R_A0R,      //copies *arg0 into *arg1, with non-ptr return
  EFT_A3R_A1R_NS,   //copies *arg1 into *arg3 (non-struct copy only)
  EFT_A1R_A0,       //stores arg0 into *arg1
  EFT_A2R_A1,       //stores arg1 into *arg2
  EFT_A4R_A1,       //stores arg1 into *arg4
  EFT_L_A0__A2R_A0, //stores arg0 into *arg2 and returns it
  EFT_A0R_NEW,      //stores a pointer to an allocated object in *arg0
  EFT_A1R_NEW,      //as above, into *arg1, etc.
  EFT_A2R_NEW,
  EFT_A4R_NEW,
  EFT_A11R_NEW,
  EFT_OTHER         //not found in the list
  
} ExtFunctionType;

class ExternalInfo
{
private:
	llvm::StringMap<ExtFunctionType> info;
public:
	ExternalInfo();
	bool isExternal(llvm::Function* f);
  ExtFunctionType getType(llvm::Function* f);
};

extern ExternalInfo extInfo;

void ptsAnalyzeExtFunction(llvm::CallSite* cs);

#endif
