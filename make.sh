#! /bin/bash
set -ex
g++ --std=c++2a unittest.cc -lpthread -lgflags -lgtest -g -o unittest
g++ --std=c++2a stress_test.cc -lpthread -lgflags -g -o stress_test
