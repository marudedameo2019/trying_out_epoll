CXXFLAGS = -g -Wall -pedantic -std=c++17
all: input_timeout event_loop_example
input_timeout: input_timeout.cpp
event_loop_example: event_loop_example.cpp
clean:
	rm -f input_timeout event_loop_example