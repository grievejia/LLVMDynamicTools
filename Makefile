CXX=clang++
CFLAGS=-Wall -fno-rtti -lffi `llvm-config --cppflags --ldflags --libs core engine interpreter irreader`

dynamic-pts: main.o interpreter.o external.o Makefile
	$(CXX) main.o interpreter.o external.o -o dynamic-pts $(CFLAGS)
main.o: main.cpp interpreter.h main.h
	$(CXX) main.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
interpreter.o: interpreter.cpp interpreter.h main.h
	$(CXX) interpreter.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
external.o: external.cpp interpreter.h main.h
	$(CXX) external.cpp -c -Wall -fno-rtti `llvm-config --cppflags`
clean:
	rm *.o
	rm dynamic-pts