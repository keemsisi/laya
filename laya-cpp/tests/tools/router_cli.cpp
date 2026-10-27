// Differential fuzz driver for the router: reads a JSON list of cases in the routes-golden format
// (written by laya-e2e/cpp-lane/fuzz-c8a/fuzz.py from the Python Router), routes each through the
// library and prints "<cases> cases, <mismatches> mismatches" plus the first few differences.
// Exit status 1 on any mismatch. Usage: laya_router_cli cases.json
#include <fstream>
#include <iostream>
#include <sstream>

#include "router_check.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: laya_router_cli cases.json\n";
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  auto cases = laya::pyvalue::Parse(ss.str());
  if (!cases.ok()) {
    std::cerr << "cannot parse " << argv[1] << ": " << cases.error() << "\n";
    return 2;
  }
  size_t bad = 0;
  const auto& rows = cases.value().items();
  for (size_t i = 0; i < rows.size(); i++) {
    std::string diff = router_check::CheckRoute(rows[i]);
    if (diff.empty()) continue;
    if (++bad <= 5) std::cout << "case " << i << ": " << diff << "\n";
  }
  std::cout << rows.size() << " cases, " << bad << " mismatches\n";
  return bad == 0 ? 0 : 1;
}
