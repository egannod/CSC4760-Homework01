#include <Kokkos_Core.hpp>
#include <cstdio>
#include <iostream>

// Do simple parallel reduce to output the maximum element in a View

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // Make View and create values
  int lval = 0;
  Kokkos::View<int*> view_4("view_4", 10);

  for(int i=0; i<view_4.extent(0); i++){
    view_4(i) = i*i;
  }
  
  // Do a parallel reduction
  Kokkos::parallel_reduce("largest value", view_4.extent(0), KOKKOS_LAMBDA(const int& i, int& val){
    if(view_4(i) > val){
      val = view_4(i);
    }
  }, lval);
  std::cout << "The largest value in the view: " << lval << std::endl;
  }
  Kokkos::finalize();
}
