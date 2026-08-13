#include "case_config.hpp"
#include "cli.hpp"

#include <cassert>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect_throw(const std::function<void()> &operation) {
  bool threw = false;
  try {
    operation();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);
}

aerofv::CliOptions parse(std::initializer_list<const char *> arguments) {
  std::vector<char *> argv;
  argv.reserve(arguments.size());
  for (const char *argument : arguments) {
    argv.push_back(const_cast<char *>(argument));
  }
  return aerofv::parse_cli(static_cast<int>(argv.size()), argv.data());
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path root = fs::path(__FILE__).parent_path().parent_path();
  const fs::path cases = root / "inputs" / "cases";

  std::size_t parsed_case_count = 0;
  for (const auto &entry : fs::directory_iterator(cases)) {
    if (entry.path().extension() == ".json") {
      const aerofv::CaseConfig parsed = aerofv::load_case_config(entry.path());
      assert(parsed.schema_version == 1);
      assert(!parsed.case_id.empty());
      assert(fs::exists(parsed.mesh.file));
      ++parsed_case_count;
    }
  }
  assert(parsed_case_count == 8);

  const aerofv::CaseConfig inviscid = aerofv::load_case_config(cases / "naca0012_m080_inviscid.json");
  assert(inviscid.schema_version == 1);
  assert(inviscid.physics.mode == aerofv::PhysicsMode::inviscid);
  assert(inviscid.mesh.file == fs::absolute(root / "inputs" / "meshes" / "NACA0012_H2.cgns").lexically_normal());
  assert(inviscid.boundary_conditions.at("bc-2") == aerofv::BoundaryType::farfield);
  assert(inviscid.boundary_conditions.at("bc-4") == aerofv::BoundaryType::slip_wall);
  assert(inviscid.run_control.type == aerofv::RunType::steady);
  assert(inviscid.run_control.max_steps && *inviscid.run_control.max_steps == 30000);

  const aerofv::CaseConfig transient = aerofv::load_case_config(cases / "cylinder_m010_laminar_re200.json");
  assert(transient.physics.mode == aerofv::PhysicsMode::laminar);
  assert(transient.physics.reynolds && *transient.physics.reynolds == 200.0);
  assert(transient.run_control.type == aerofv::RunType::transient);
  assert(transient.run_control.time_step && *transient.run_control.time_step == 0.01);
  assert(transient.outputs.recommended_vorticity_clip_range);
  assert(transient.outputs.recommended_vorticity_clip_range->first == -5.0);
  assert(transient.outputs.recommended_vorticity_clip_range->second == 5.0);

  const aerofv::CliOptions cli = parse({"solver", "solve", "--case", "case.json", "--output", "results",
                                        "--restart", "restart.bin", "--report-level", "full"});
  assert(cli.action == aerofv::CliAction::solve);
  assert(cli.solve.case_file == "case.json");
  assert(cli.solve.output_dir == "results");
  assert(cli.solve.restart_file && *cli.solve.restart_file == "restart.bin");
  assert(cli.solve.report_level == aerofv::ReportLevel::full);
  assert(parse({"solver", "solve", "--help"}).action == aerofv::CliAction::help);
  expect_throw([] { (void)parse({"solver", "solve", "--case", "case.json"}); });
  expect_throw([] { (void)parse({"solver", "solve", "--case", "a", "--case", "b", "--output", "out"}); });
  expect_throw([] { (void)parse({"solver", "solve", "--case", "a", "--output", "out", "--report-level", "verbose"}); });

  std::cout << "case_config and cli tests passed\n";
}
