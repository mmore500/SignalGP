#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"

TEST_CASE( "Hello World", "[general]" ) {
  std::cout << "Hello tests!" << std::endl;
}