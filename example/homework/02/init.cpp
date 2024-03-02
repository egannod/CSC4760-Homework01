#include <Kokkos_Core.hpp>
#include <cstdio>

// Problem: Make an n ∗ m View where each index equals 1000 ∗ i ∗ j

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // set n and m, you can change these values
  int n,m = 4;
  // Make View
  Kokkos::View<int*> myView("myView", n);
  // set values to 1000 * i * j;
  Kokkos::parallel_for("iterator", n, KOKKOS_LAMBDA(const int& i) {
    myView(i) = i;
  });
  Kokkos::parallel_for("m", n, KOKKOS_LAMBDA(const int& i) {
    printf("myView(%d) = %d\n", i, myView(i));
  });
  }
  Kokkos::finalize();
}
