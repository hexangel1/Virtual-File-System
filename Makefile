PROJECT = prog
SOURCES = $(wildcard *.cpp)
HEADERS = $(filter-out main.hpp, $(SOURCES:.cpp=.hpp))
OBJECTS = $(SOURCES:.cpp=.o)
CXX = g++
CXXFLAGS = -Wall -g
LDLIBS = -lpthread -lvfs -Lvfs
LIBDEPEND = vfs/libvfs.a
CTAGS = ctags

$(PROJECT): $(OBJECTS) $(LIBDEPEND)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.cpp %.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

deps.mk: $(SOURCES) Makefile
	$(CXX) -MM $(SOURCES) > $@

vfs/libvfs.a:
	cd vfs && $(MAKE)

run: $(PROJECT)
	./$(PROJECT)

memcheck: $(PROJECT)
	valgrind -s --leak-check=full --track-origins=yes ./$(PROJECT)

vfstest: $(LIBDEPEND)
	$(CXX) $(CXXFLAGS) -o $@ test/$@.cpp $(LDLIBS)
#	cd test && ./build_tests.sh
	valgrind ./$@
	rm -f vfstest
	cd test && ./run_tests.sh

tags: $(SOURCES) $(HEADERS)
	$(CTAGS) $(SOURCES) $(HEADERS)
	cd vfs && $(MAKE) tags

clean:
	rm -f $(PROJECT) *.o *.a *.bin deps.mk tags
	cd vfs && $(MAKE) clean

ifneq (clean, $(MAKECMDGOALS))
ifneq (tags, $(MAKECMDGOALS))
-include deps.mk
endif
endif

