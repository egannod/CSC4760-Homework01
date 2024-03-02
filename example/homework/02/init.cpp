#include <Kokkos_Core.hpp>
#include <cstdio>
#include <iostream>

// Problem: Make an n ∗ m View where each index equals 1000 ∗ i ∗ j

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // set n and m, you can change these values
  int n,m = 16;
  // Make View
  Kokkos::View<int**> prob_2("prob_2", n, m);
  // set values to 1000 * i * j;
  for (int i=0; i < n; i++){
    for (int j=0; j < m; j++){
      prob_2(i,j) = 1000 * i * j;
    }
  }
  std::cout << "prob_2 at 16,16: " << prob_2(16,16) << std::endl;

  }

  Kokkos::finalize();
}
