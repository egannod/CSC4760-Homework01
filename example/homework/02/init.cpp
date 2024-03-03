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
    for(int j=0; j<myView.extent(1); j++){
      myView(i,j) = 1000 * i * j;
    }
  }
  Kokkos::fence();
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < m; j++) {
      std::cout << "View at index " << i << "," << j << ": " << myView(i,j) << std::endl;
    }
  }
  Kokkos::finalize();
}
