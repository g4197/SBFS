all: main

main: main.o
	g++ *.cpp -o main -lglog