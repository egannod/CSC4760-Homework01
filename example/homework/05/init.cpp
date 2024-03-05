#include <Kokkos_Core.hpp>
#include <iostream>

// Create a program that compares a parallel for loop and a standard for loop for summing rows of a View with Kokkos Timer.

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // Make View and create values
  Kokkos::View<int*> view_5("view_5", 10000);
  for(int i=0; i<view_5.extent(0); i++){
    view_5(i) = 100*i;
  }

  // Sum Loops
  int sum = 0;
  int parallel_sum = 0;

  Kokkos::Timer timer;
  Kokkos::parallel_reduce("parallel sum", view_5.extent(0), KOKKOS_LAMBDA(const int& i, int& lsum){
    lsum+=view_5(i);
  }, parallel_sum);
  double time = timer.seconds();

  timer.reset();
  for(int i=0; i<view_5.extent(0); i++){
    sum += view_5(i);
  }
  double parallel_time = timer.seconds();
  
  // Output times
  std::cout << "Sum: " << sum << " Time: " << time << std::endl;
  std::cout << "Parallel Sum: " << parallel_sum << " Time: " << parallel_time << std::endl;
  }
  Kokkos::finalize();
}
