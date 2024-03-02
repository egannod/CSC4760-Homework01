#include <Kokkos_Core.hpp>
#include <cstdio>
#include <iostream>

// Problem: Make an n ∗ m View where each index equals 1000 ∗ i ∗ j

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // set n and m, you can change these values
  int n,m = 4;
  // Make View
  Kokkos::View<int*> prob_2("prob_2", n);
  // set values to 1000 * i * j;
  Kokkos::parallel_for("prob_2", n, KOKKOS_LAMBDA(const int i) {
    prob_2(i) = 1000 * i;
  });
  std::cout << prob_2(1) << endl;
  }

  Kokkos::finalize();
}
