parser:parser.cc
	g++ -o $@ $^ -std=c++11 -lboost_filesystem -lboost_system

.PHONY:clean
clean:
	rm -f test