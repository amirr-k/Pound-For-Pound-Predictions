#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "usage: codewatch <index|query|bench>\n";
    return 1;
  }

  const std::string sub = argv[1];
  if (sub == "index") {
    std::cout << "index: not yet implemented\n";
    return 0;
  }
  if (sub == "query") {
    std::cout << "query: not yet implemented\n";
    return 0;
  }
  if (sub == "bench") {
    std::cout << "bench: not yet implemented\n";
    return 0;
  }

  std::cerr << "usage: codewatch <index|query|bench>\n";
  return 1;
}
