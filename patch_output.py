
with open('/workspace/solver/src/OutputManager.cpp', 'r') as f:
    content = f.read()

# Find where restart_final_rank*.bin is written and add the manifest after
old_rm = '''    // Also write restart file (binary dump of states)
    if (step_id < 0) {
        std::string restart_file = output_dir + "/restart_final_rank" + std::to_string(rank) + ".bin";
        std::ofstream rf(restart_file, std::ios::binary);
        rf.write(reinterpret_cast<const char*>(&n_owned), sizeof(int));
        for (int i = 0; i < n_owned; i++)
            rf.write(reinterpret_cast<const char*>(states[i].data()), 4*sizeof(double));
    }'''

new_rm = '''    // Also write restart file (binary dump of states)
    if (step_id < 0) {
        std::string restart_file = output_dir + "/restart_final_rank" + std::to_string(rank) + ".bin";
        std::ofstream rf(restart_file, std::ios::binary);
        rf.write(reinterpret_cast<const char*>(&n_owned), sizeof(int));
        for (int i = 0; i < n_owned; i++)
            rf.write(reinterpret_cast<const char*>(states[i].data()), 4*sizeof(double));
        // Write restart manifest (rank 0 only, after barrier to ensure all bins are written)
        MPI_Barrier(comm);
        if (rank == 0) {
            json manifest;
            manifest["type"] = "restart_manifest";
            manifest["mpi_ranks"] = n_ranks;
            json files_arr = json::array();
            for (int r = 0; r < n_ranks; r++)
                files_arr.push_back("restart_final_rank" + std::to_string(r) + ".bin");
            manifest["files"] = files_arr;
            std::ofstream mf(output_dir + "/restart_final.json");
            mf << manifest.dump(2) << "\n";
        }
    }'''

if old_rm in content:
    content = content.replace(old_rm, new_rm, 1)
    print('OutputManager restart_final.json: OK')
else:
    print('restart block: NOT FOUND')
    idx = content.find('restart_final_rank')
    print(f'  idx={idx}, context={repr(content[max(0,idx-50):idx+100])}')

with open('/workspace/solver/src/OutputManager.cpp', 'w') as f:
    f.write(content)
print('OutputManager.cpp saved')

