#include <Kokkos_Core.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
    // Create Views
    int N = 20;
    int result;
    Kokkos::View<int*> A("A", N);
    Kokkos::View<int*> B("B", N);

    // Initialize Views
    Kokkos::parallel_for("init", N, KOKKOS_LAMBDA(const int& i){
      A(i) = 2*i;
    });

    // Initialize Timer
    Kokkos::Timer timer;
    double times[7];

    // Parallel Scan Several times for results
    for (int i=0; i<7; i++){
      timer.reset();
      Kokkos::parallel_scan("scan a", N, KOKKOS_LAMBDA(const int& i, int& update, const bool& final){
        update += A(i);
        if(final){
          B(i) = update;
        }
      }, result);
      Kokkos::fence();
      times[i] = timer.seconds();
    }

    // Print Results
    std::cout << "A: ";
    for(int i=0; i<N; i++){
      std::cout << A(i) << " ";
    }
    std::cout << std::endl;

    std::cout << "B: ";
    for(int i=0; i<N; i++){
      std::cout << B(i) << " ";
    }
    std::cout << std::endl;

    // Print Times of each Parallel scan
    for (int i=0; i<7; i++){
      std::cout << "Time " << i << ": " << times[i] << std::endl;
    }

  }
  Kokkos::finalize();
}
