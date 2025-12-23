PARSER=parser
DEBUG=debug
HTTP_SERVER=http_server
cc=g++

.PHONY:all
all:$(PARSER) $(DEBUG) $(HTTP_SERVER)

$(PARSER):parser.cc
	$(cc) -o $@ $^ -std=c++14 -lboost_filesystem -lboost_system -I./cppjieba/include
$(DEBUG):debug.cc
	$(cc) -o $@ $^ -std=c++14  -I./cppjieba/include -ljsoncpp
$(HTTP_SERVER):http_server.cc
	$(cc) -o $@ $^ -std=c++14  -I./cppjieba/include -ljsoncpp -lpthread

.PHONY:clean
clean:
	rm -f $(PARSER) $(DEBUG) $(HTTP_SERVER)