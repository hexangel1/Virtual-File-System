PROJECT = prog
SOURCES = $(wildcard *.cpp)
HEADERS = $(filter-out main.hpp, $(SOURCES:.cpp=.hpp))
OBJECTS = $(SOURCES:.cpp=.o)
CXX = g++
CXXFLAGS = -Wall -g -O0
LDLIBS = -lpthread -lvfs -Lvfs 
LIBDEPEND = vfs/libvfs.a
CTAGS = ctags

$(PROJECT): $(OBJECTS) $(LIBDEPEND)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDLIBS) -o $@

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

tags: $(SOURCES) $(HEADERS)
	$(CTAGS) $(SOURCES) $(HEADERS)
	cd vfs && $(MAKE) tags

clean:
	rm -f $(PROJECT) *.o *.a deps.mk tags
	cd vfs && $(MAKE) clean

ifneq (clean, $(MAKECMDGOALS))
ifneq (tags, $(MAKECMDGOALS))
-include deps.mk
endif
endif

