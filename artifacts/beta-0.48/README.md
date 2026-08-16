# SFBench beta 0.48 binaries

- `SFBench-beta-0.48-windows-x64.exe`: Windows x64 Release, MSVC 19.50,
  portable runtime-dispatch build with the static MSVC runtime.
- `SFBench-beta-0.48-linux-x86_64-v3`: Linux x86-64-v3 Release, GCC 15.2,
  `-O3 -march=native -mtune=native`, IPO/LTO, static libstdc++/libgcc.

The Linux binary requires an AVX2/FMA-capable x86-64 CPU and glibc 2.38 or
newer. Run `chmod +x SFBench-beta-0.48-linux-x86_64-v3` after downloading it
outside Git. Verify downloads with `SHA256SUMS.txt` before running them.
