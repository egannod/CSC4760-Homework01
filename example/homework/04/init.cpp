#include <Kokkos_Core.hpp>
#include <cstdio>
#include <iostream>

// Do simple parallel reduce to output the maximum element in a View

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // Make View and create values
  int lval = 1;
  Kokkos::View<int*> view_4("view_4", 10);

  for(int i=0; i<view_4.extent(0); i++){
    view_4(i) = i*i;
  }
  // print out view
  for(int i=0; i<view_4.extent(0); i++){
    std::cout << "view_4(" << i << ") = " << view_4(i) << std::endl;
  }
  
  // Do a parallel reduction
  Kokkos::parallel_reduce("largest value", view_4.extent(0), KOKKOS_LAMBDA(const int& i, int& val){
    val = val > view_4(i) ? val : view_4(i);
  }, lval);
  Kokkos::fence();
  std::cout << "The largest value in the view: " << lval << std::endl;
  }
  Kokkos::finalize();
}