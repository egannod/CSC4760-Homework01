#include <Kokkos_Core.hpp>
#include <cstdio>
#include <iostream>

// Problem: Make an n ∗ m View where each index equals 1000 ∗ i ∗ j

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // set n and m, you can change these values
  int n = 4;
  int m = 4;
  // Make View
  Kokkos::View<int**> myView("myView", n, m);
  // set values to 1000 * i * j;
  for(int i=0; i<myView.extent(0); i++){
    myView(i,0) = 1000 * i;
  }
  Kokkos::fence();
  std::cout << "View at index 0: " << myView(0,0) << std::endl;
  std::cout << "View at index 1: " << myView(1,0) << std::endl;
  std::cout << "View at index 2: " << myView(2,0) << std::endl;
  std::cout << "View at index 3: " << myView(3,0) << std::endl;
  }
  Kokkos::finalize();
}
