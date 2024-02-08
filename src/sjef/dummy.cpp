#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
int main(int argc, char* argv[]) {
  if (argc > 1) {
    auto stem = fs::absolute(fs::path{argv[argc-1]}).parent_path() / fs::absolute(fs::path{argv[argc-1]}).stem();
//    std::cout << "stem " << stem << std::endl;
    std::ofstream(stem.string() + ".out") << "dummy" << std::endl;
    std::ofstream(stem.string() + ".xml") << "<?xml version=\"1.0\"?><root/>" << std::endl;
  }
  return 0;
}