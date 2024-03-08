#include <Kokkos_Core.hpp>
#include <iostream>

// Create a program that does matrix multiply between a 2D View and a 1D View with at least one loop of parallelism.
// For a test case:
// a = [130, 147, 115]   b = [221]
//     [224, 158, 187]       [12]
//     [ 54, 158, 120]       [157]
// Extra credit: make a function and check for correct shape/dimensions

int main(int argc, char* argv[]) {
  Kokkos::initialize(argc, argv);
  {
  // Make View and add values
    Kokkos::View<int**> a("a", 3, 3);
    a(0,0) = 130;
    a(0,1) = 147;
    a(0,2) = 115;
    a(1,0) = 224;
    a(1,1) = 158;
    a(1,2) = 187;
    a(2,0) = 54;
    a(2,1) = 158;
    a(2,2) = 120;
    Kokkos::View<int*> b("b", 3);
    b(0) = 221;
    b(1) = 12;
    b(2) = 157;
    Kokkos::View<int*> soln("soln", 3);
    // soln = [39896, 59189, 46499]
  
  // Do a matrix multiply
  for(int i=0; i<a.extent(0); i++){
    Kokkos::parallel_for("matrix multiply", a.extent(1), KOKKOS_LAMBDA(const int& j){
      soln(i) += a(j,i) * b(j);
    });
  }
  
  // Output multiplication
  for (int i=0; i<a.extent(0); i++){
    for (int j=0; j<a.extent(1); j++){
      std::cout << a(i,j) << " ";
    }
    std::cout << std::endl;
  }
  std::cout << "   *   " << std::endl;
  for (int i=0; i<b.extent(0); i++){
    std::cout << b(i) << " ";
  }
  std::cout << std::endl << "   =   " << std::endl;
  for(int i=0; i<soln.extent(0); i++){
    std::cout << soln(i) << " ";
  }
  }
  Kokkos::finalize();
}
