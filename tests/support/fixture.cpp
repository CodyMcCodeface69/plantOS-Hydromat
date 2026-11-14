#include "fixture.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#ifndef PLANTOS_SCHEDULE_JSON_PATH
#error "PLANTOS_SCHEDULE_JSON_PATH must be defined by the Makefile"
#endif

const std::string &production_schedule_json() {
  static std::string json = [] {
    std::ifstream f(PLANTOS_SCHEDULE_JSON_PATH);
    if (!f) {
      std::cerr << "cannot read " << PLANTOS_SCHEDULE_JSON_PATH << " (run via make)\n";
      exit(2);
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }();
  return json;
}
