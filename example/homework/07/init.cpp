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
    Kokkos::View<int*> C("C", N);
    Kokkos::View<int*> D("D", N);

    // Initialize Views
    Kokkos::parallel_for("init", N, KOKKOS_LAMBDA(const int& i){
      A(i) = 2*i;
      B(i) = 2*i;
    });

    // Initialize Timer
    Kokkos::Timer timer;

    // Parallel Scan Prefix Sum
    Kokkos::parallel_scan("scan a", N, KOKKOS_LAMBDA(const int& i, int& update, const bool& final){
      update += A(i);
      if(final){
        C(i) = update;
      }
    }, result);
    double time0 = timer.seconds();

    Kokkos::fence();

    // Parallel Scan With B
    Kokkos::parallel_scan("scan b", N, KOKKOS_LAMBDA(const int& i, int& update, const bool& final){
      update += B(i);
      if(final){
        D(i) = update;
      }
    }, result);
    double time1 = timer.seconds();

    Kokkos::fence();

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

    std::cout << "C: ";
    for(int i=0; i<N; i++){
      std::cout << C(i) << " ";
    }
    std::cout << std::endl;

    std::cout << "D: ";
    for(int i=0; i<N; i++){
      std::cout << D(i) << " ";
    }
    std::cout << std::endl;

    std::cout << "Time0: " << time0 << " Time1: " << time1 << std::endl;

  }
  Kokkos::finalize();
}
