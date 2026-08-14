#pragma once
#include "types.hpp"
#include <string>

namespace omo {

class CGNSReader {
public:
    static MeshData read(const std::string& filename);

private:
    static void read_nodes(int file_id, int base_id, int zone_id, MeshData& mesh);
    static void read_elements(int file_id, int base_id, int zone_id, MeshData& mesh);
    static int  get_cgns_element_type(int npe);
};

} // namespace omo
