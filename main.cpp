#include <glog/logging.h>
#include <gflags/gflags.h>
#include <iostream>

#include "Solitaire.h"
#include "Solver.h"

DEFINE_uint64(timeout, 30, "Solver timeout in seconds.");

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  while (true) {
    solitaire::Solitaire game;
    std::cout << game << std::endl;

    solitaire::Solver solver(game, std::chrono::seconds(FLAGS_timeout));
    auto result = solver.solve();
    switch (result.status) {
    case solitaire::SolverStatus::SOLVED:
      std::cout << "Found solution in " << result.moves.size() << " moves."
		<< std::endl;
      break;
    case solitaire::SolverStatus::TIMEOUT:
      std::cout << "Solver timed out, unknown if solution exists." << std::endl;
      break;
    case solitaire::SolverStatus::NO_SOLUTION:
      std::cout << "No solution exists." << std::endl;
      break;
    }
    std::cout << "Time elapsed: " << result.elapsed.count()
	      << " seconds" << std::endl;
  }

  return 0;
}
