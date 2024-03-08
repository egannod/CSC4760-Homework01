#include <Kokkos_Core.hpp>
#include <cstdio>
#include <iostream>

// Declare a 5 ∗ 7 ∗ 12 ∗ n View

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
    int n;
    std::cout << "Enter nth dimension: ";
    std::cin >> n;
  // Make View
  Kokkos::View<int*[5][7][12]> view("view", n);
  
  // print name
  std::cout << "View label: " << view.label() << std::endl;
  // print dimensions
  std::cout << "View dimensions: " << view.extent(0) << "x" << view.extent(1) << "x" << view.extent(2) << "x" << view.extent(3) << std::endl;
  }
  Kokkos::finalize();
}
