#include <string>
#include <upcxx/upcxx.hpp>

int main() {
  upcxx::init();

  std::string s;
  upcxx::reduce_all(s, upcxx::op_add).wait();
  
  upcxx::finalize();
}
