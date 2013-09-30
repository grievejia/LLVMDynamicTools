CXX=clang++
CFLAGS=-Wall -fno-rtti -lffi `llvm-config --cppflags --ldflags --libs core engine interpreter irreader`
OBJS=main.o interpreter.o external.o variable.o varInit.o util.o

dynamic-pts: $(OBJS) Makefile
	$(CXX) $(OBJS) -o dynamic-pts $(CFLAGS)
main.o: main.cpp interpreter.h main.h variable.h varInit.h
	$(CXX) main.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
interpreter.o: interpreter.cpp interpreter.h main.h
	$(CXX) interpreter.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
external.o: external.cpp interpreter.h main.h
	$(CXX) external.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
variable.o: variable.cpp variable.h util.h main.h
	$(CXX) variable.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
varInit.o: varInit.cpp varInit.h variable.h main.h
	$(CXX) varInit.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
util.o: util.cpp util.h
	$(CXX) util.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
clean:
	rm *.o
	rm dynamic-pts