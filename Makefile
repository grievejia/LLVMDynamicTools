CXX=clang++
CFLAGS=-Wall -fno-rtti -lffi `llvm-config --cppflags --ldflags --libs core engine interpreter irreader`
OBJS=main.o interpreter.o external.o variable.o varInit.o util.o pts-external.o
OPTFLAG=-O2

dynamic-pts: $(OBJS) Makefile
	$(CXX) $(OBJS) -o dynamic-pts $(CFLAGS)
main.o: main.cpp interpreter.h main.h variable.h varInit.h
	$(CXX) main.cpp -c -Wall -fno-rtti $(OPTFLAG) `llvm-config --cppflags`
interpreter.o: interpreter.cpp interpreter.h main.h pts-external.h
	$(CXX) interpreter.cpp -c -Wall -fno-rtti $(OPTFLAG) `llvm-config --cppflags`
external.o: external.cpp interpreter.h main.h
	$(CXX) external.cpp -c -Wall -fno-rtti $(OPTFLAG) `llvm-config --cppflags`
variable.o: variable.cpp variable.h util.h main.h
	$(CXX) variable.cpp -c -Wall -fno-rtti $(OPTFLAG) `llvm-config --cppflags`
varInit.o: varInit.cpp varInit.h variable.h main.h
	$(CXX) varInit.cpp -c -Wall -fno-rtti $(OPTFLAG) `llvm-config --cppflags`
pts-external.o: pts-external.cpp pts-external.h variable.h varInit.h main.h
	$(CXX) pts-external.cpp -c -Wall -Wno-implicit-fallthrough -fno-rtti $(OPTFLAG) `llvm-config --cppflags`
util.o: util.cpp util.h
	$(CXX) util.cpp -c -Wall -fno-rtti $(OPTFLAG) `llvm-config --cppflags`
clean:
	rm *.o
	rm dynamic-pts