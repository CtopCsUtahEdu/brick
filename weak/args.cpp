//
// Created by Tuowen Zhao on 9/8/19.
//

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <omp.h>
#include <mpi.h>
#include <unistd.h>
#include "args.h"
// for TILE
#include "stencils/stencils.h"

namespace {
  const char *const shortopt = "d:s:I:hb";
  const char *help =
      "Running MPI with %s\n\n"
      "Program options\n"
      "  -h: show help (this message)\n"
      "  MPI downsizing:\n"
      "  -b: MPI downsize to 2-exponential "
      "  Domain size, pick either one, in array order contiguous first\n"
      "  -d: comma separated Int[3], overall domain size\n"
      "  -s: comma separated Int[3], per-process domain size\n"
      "  Benchmark control:\n"
      "  -I: number of iterations, default %d\n"
      "Example usage:\n"
      "  %s -d 2048,2048,2048\n";

  void parse3Tuple(std::string istr, std::vector<unsigned> &out) {
    for (int i = 0; i < 3; ++i) {
      int r = istr.find(',');
      r = r < 0 ? istr.length() : r;
      out[i] = std::stoi(istr.substr(0, r));
      if (r != istr.length())
        istr = istr.substr(r + 1);
    }
  }
}

std::vector<unsigned> dim_size, dom_size;
size_t tot_elems;
int MPI_ITER;

MPI_Comm parseArgs(int argc, char **argv, const char *program) {
  int c;
  bool bin = false;
  int sel = 0;
  dom_size.resize(3);
  dim_size.resize(3);
  while ((c = getopt(argc, argv, shortopt)) != -1) {
    switch (c) {
      case 'b':
        bin = true;
        break;
      case 'd':
        parse3Tuple(optarg, dom_size);
        sel = sel != 0 ? -2 : 1;
        break;
      case 's':
        parse3Tuple(optarg, dom_size);
        sel = sel != 0 ? -2 : 2;
        break;
      case 'I':
        MPI_ITER = std::stoi(optarg);
        break;
      default:
        printf("Unknown options %c\n", c);
      case 'h':
        printf(help, program, MPI_ITER, argv[0]);
        sel = sel != 0 ? -2 : -1;
    }
  }

  if (sel == -2)
    printf("Contradicting options\n");
  if (sel < 0) {
    MPI_Finalize();
    exit(0);
  }

  if (sel == 0) {
    sel = 2;
    dom_size[0] = dom_size[1] = dom_size[2] = 128;
  }

  MPI_Comm comm = MPI_COMM_WORLD;

  int size, rank;

  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  if (bin) {
    int b = 0, s = 1;
    for (int i = 0; i < 3; ++i)
      dim_size[i] = 1;
    while (s * 2 < size) {
      dim_size[b % 3] *= 2;
      ++b;
      s *= 2;
    }
  } else
    MPI_Dims_create(size, 3, (int *) dim_size.data());

  int period[3] = {1, 1, 1};
  MPI_Cart_create(MPI_COMM_WORLD, 3, (int *) dim_size.data(), period, true, &comm);

  if (comm != MPI_COMM_NULL) {
    MPI_Comm_size(comm, &size);
    MPI_Comm_rank(comm, &rank);

    int coo[3];
    MPI_Cart_get(comm, 3, (int *) dim_size.data(), period, coo);

    // Split domain size
    if (sel == 1) {
      tot_elems = 1;
      for (int i = 0; i < 3; ++i) {
        tot_elems *= dom_size[i];
        dom_size[i] = dom_size[i] / TILE;
        unsigned s = (unsigned) dom_size[i] % dim_size[2 - i];
        dom_size[i] = (dom_size[i] / dim_size[2 - i] + (s > coo[i] ? 1 : 0)) * TILE;
      }
    } else {
      tot_elems = size;
      for (int i = 0; i < 3; ++i)
        tot_elems *= dom_size[i];
    }

    if (rank == 0) {
      int numthreads;
#pragma omp parallel shared(numthreads) default(none)
      numthreads = omp_get_num_threads();
      long page_size = sysconf(_SC_PAGESIZE);
      std::cout << "Pagesize " << page_size << "; MPI Size " << size << " * OpenMP threads " << numthreads << std::endl;
      std::cout << "Domain size of " << tot_elems << " split among" << std::endl;
      std::cout << "A total of " << size << " processes "
                << dim_size[0] << "x" << dim_size[1] << "x" << dim_size[2] << std::endl;
    }
  }

  return comm;
}
