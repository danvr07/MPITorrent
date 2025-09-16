CXX = mpicxx
CXXFLAGS = -std=c++11 -pthread -Wall

build: tema2.cpp
	$(CXX) $(CXXFLAGS) -o tema2 tema2.cpp

run:
	mpirun -np 4 ./tema2

clean:
	rm -rf tema2 client*
