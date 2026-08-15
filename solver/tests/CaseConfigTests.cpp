#include "cfd/CaseConfig.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

int main() {
  const std::filesystem::path cases_dir{CFD_CASES_DIR};
  std::size_t loaded = 0;
  cfd::CaseConfig representative_case;
  for (const auto& entry : std::filesystem::directory_iterator(cases_dir)) {
    if (entry.path().extension() != ".json") continue;
    const cfd::CaseConfig config = cfd::CaseConfig::load(entry.path());
    assert(config.schema_version == 1);
    assert(!config.case_id.empty());
    assert(config.mesh.file.is_absolute());
    assert(config.mesh.file.extension() == ".cgns");
    assert(!config.boundary_conditions.empty());
    if (config.run_control.type == "transient") {
      assert(config.run_control.time_step.has_value());
      assert(config.run_control.final_time.has_value());
      assert(config.numerics_required.transient_order == 2);
    }
    if (loaded == 0) representative_case = config;
    ++loaded;
  }
  assert(loaded == 8);

  cfd::CaseConfig unsupported = representative_case;
  unsupported.schema_version = 2;
  bool rejected_unsupported_schema = false;
  try {
    unsupported.validate();
  } catch (const cfd::CaseConfigError&) {
    rejected_unsupported_schema = true;
  }
  assert(rejected_unsupported_schema);

  std::cout << "Loaded " << loaded << " schema-v1 cases\n";
}
