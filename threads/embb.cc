
/* embb.c: thread spawning via EMBB  */

#include "threads.h"

#include <embb/algorithms/for_each.h>

class ParallelInvokeOp {
  spawn_function fun;
  void * data;
  int block_size;
  int loop_max;
public:
  explicit ParallelInvokeOp(
    spawn_function f,
    void * argData,
    int blockSize,
    int loopMax)
    : fun(f),
    data(argData),
    block_size(blockSize),
    loop_max(loopMax) { }
  inline void operator()(int it) {
    spawn_data d;
    d.max = (d.min = it * block_size) + block_size;
    if (d.max > loop_max) {
      d.max = loop_max;
    }
    d.thr_num = it;
    d.data = data;
    fun(&d);
  }
};

int X(ithreads_init)(void)
{
  return 0; /* no error */
}

/*
Distribute a loop from 0 to loopmax-1 over nthreads threads.
proc(d) is called to execute a block of iterations from d->min
to d->max-1.  d->thr_num indicate the number of the thread
that is executing proc (from 0 to nthreads-1), and d->data is
the same as the data parameter passed to X(spawn_loop).
This function returns only after all the threads have completed.
*/
void X(spawn_loop)(int loopmax, int nthr, spawn_function proc, void *data)
{
  int block_size;
  spawn_data d;
  int i;

  A(loopmax >= 0);
  A(nthr > 0);
  A(proc);

  if (!loopmax) return;

  /*
  Choose the block size and number of threads in order to (1)
  minimize the critical path and (2) use the fewest threads that
  achieve the same critical path (to minimize overhead).
  e.g. if loopmax is 5 and nthr is 4, we should use only 3
  threads with block sizes of 2, 2, and 1.
  */
  block_size = (loopmax + nthr - 1) / nthr;
  nthr = (loopmax + block_size - 1) / block_size;

  THREAD_ON; /* prevent debugging mode from failing under threads */
  ParallelInvokeOp op(proc, data, block_size, loopmax);
  int * threadIt = new int[loopmax];
  try {
    embb::algorithms::ForEach(threadIt, threadIt + loopmax, op);
    delete[] threadIt;
  }
  catch (...) {
    delete[] threadIt;
  }
  THREAD_OFF; /* prevent debugging mode from failing under threads */
}

void X(threads_cleanup)(void)
{
}
