#include <Kokkos_Core.hpp>
#include <cstdio>
#include <iostream>

// Declare a 5 ∗ 7 ∗ 12 ∗ n View

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
    int n = 10;
  // Make View
  Kokkos::View<int****> view("view", 5, 7, 12, n);
  
  // print name
  std::cout << "View label: " << view.label() << std::endl;
  }
  Kokkos::finalize();
}
