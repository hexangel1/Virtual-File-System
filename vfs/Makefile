LIBNAME = vfs
SOURCES = $(wildcard *.cpp)
HEADERS = $(SOURCES:.cpp=.hpp)
OBJECTS = $(SOURCES:.cpp=.o)
CXX = g++
CXXFLAGS = -Wall -Wextra --std=c++98 -pedantic -g
CTAGS = ctags
AR = ar

lib$(LIBNAME).a: $(OBJECTS)
	$(AR) crs $@ $^

lib$(LIBNAME).tar: $(SOURCES) $(HEADERS) Makefile
	tar -cvf $@ $(SOURCES) $(HEADERS) Makefile

%.o: %.cpp %.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

deps.mk: $(SOURCES) Makefile
	$(CXX) -MM $(SOURCES) > $@

tags: $(SOURCES) $(HEADERS)
	$(CTAGS) $(SOURCES) $(HEADERS)

tar: lib$(LIBNAME).tar

clean:
	rm -f lib$(LIBNAME).a *.o *.a *.bin deps.mk tags

ifneq (clean, $(MAKECMDGOALS))
ifneq (tags, $(MAKECMDGOALS))
ifneq (tar, $(MAKECMDGOALS))
-include deps.mk
endif
endif
endif

