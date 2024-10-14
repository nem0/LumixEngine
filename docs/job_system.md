# Job System
The primary goal of the Job System is to efficiently utilize multicore CPUs by distributing workloads across multiple worker threads. This system allows for the execution of small, independent units of work, known as jobs, which can run concurrently. If there is no work, worker threads are put to sleep to conserve resources (battery, etc.). For more details on the Job System API, refer to the [job_system.h](../src/core/job_system.h) header file.

## Job

The `jobs::run` function adds a task and a data pointer to the global work queue. A worker thread will eventually execute the task. It is the **user's responsibility** to ensure the **data remains valid** until the job is completed. 

**Avoid using OS-level synchronization primitives** within jobs, as they can block the worker thread. Instead, utilize the synchronization primitives provided by the Job System, which allow the worker thread to continue processing other tasks if available.

```cpp
struct MyJobData {
    float values[1024];
    float sum;
};

void sum(void* user_ptr) {
    MyJobData* data = (MyJobData*)user_ptr;
    float sum = 0;
    for (float f : data->values) {
        sum += f;
    }
    data->sum = sum;
}

MyJobData data = ...;
jobs::run(&data, sum, nullptr);
...
```

## Signal

**Signals** are one of the synchronization primitives of the Job System. They can be used to wait for two types of events:

### Job finished
```cpp
jobs::Signal signal;
// run some job
jobs::run(data, function, &signal);
// wait till the job is finished
jobs::wait(&signal);
// no need to cleanup `signal`
```
While **waiting** on a signal, a **worker thread** will attempt to **execute another job** from the queue. If no jobs are available, the thread will go to sleep to conserve resources. The Job System retains pointers to signals until the associated jobs are completed. It is the **user's responsibility** to ensure that the **signal remains valid** until the job is finished. Consequently, signals **cannot be assigned** to other signals, making the following code invalid:

```cpp
jobs::Signal other_signal = signal; // invalid

void foo() {
    jobs::Signal signal;
    jobs::run(&data, fn, &signal);
    // `signal` gets destroyed here, but there's no guarantee the job is finished, so this is invalid
}
```

Multiple jobs can use the same signal:

```cpp
jobs::Signal signal;
// run two jobs
jobs::run(&dataA, fnA, &signal);
jobs::run(&dataB, fnB, &signal);
// wait till both jobs are finished
jobs::wait(&signal);
```

### User triggered signals

Signals can also be used to trigger events based on user requests. It is **strongly advised not to mix** user-triggered signals with signals used as job counters. User-triggered signal can be in one of two states: green or red. Waiting on **green** signal is a **does not block**. Waiting on **red signal blocks** the caller until the signal turns green.

```cpp
jobs::Signal is_ready; // signal is green by default
jobs::setRed(&is_ready); // signal is red, any called of wait(&is_ready) is blocked
...
jobs::setRed(&is_ready); // this unblocks anybody waiting on `is_ready`
// from this point, wait(&is_ready) does not block the callers
```

## Mutex

The Job System provides a mutex to protect shared resources from concurrent access by multiple jobs.

```cpp
struct {
    jobs::Mutex m_mutex;
    Array<T> m_array;

    T pop() {
        m_mutex.enter();
        T result = m_array.back();
        m_array.pop();
        m_mutex.exit();
        return result;
    }
    ...
};
```

## For each

A common pattern is to parallelize a standard single-threaded `for` loop across multiple threads. The Job System facilitates this with the `jobs::forEach(N, step_length, function)` function. It splits the range from 0 to `N` into steps of `step_length` length and calls `function` for each step. The function is called in parallel. `jobs::forEach` **blocks**, till the whole array is processed.

```cpp
    Vec4 positions[particles_count];
    Matrix mtx;
    ...
    jobs::forEach(particles_count, 4096, [&](u32 from, u32 to){
        for (u32 i = from; i < to; ++i) {
            positons[i] = mtx * positions;
        }
    });
```

# Links

* [Marl](https://github.com/google/marl)
* [folly::fibers](https://github.com/facebook/folly/blob/main/folly/fibers/README.md)
