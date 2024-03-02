#include <Kokkos_Core.hpp>
#include <cstdio>

// Declare a 5 ∗ 7 ∗ 12 ∗ n View

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
    int n = 10;
  // Make View
  Kokkos::View<int[5][7][12]*> view("view", n);
  
  // print name
  printf("View name: %s\n", view.label()); 
  }
  Kokkos::finalize();
}
