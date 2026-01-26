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

**Signals** are the primary synchronization primitive in the Job System. A signal can be either **red** or **green**. A red signal blocks all `wait()` callers until it turns green. The four basic operations on signals are:

* `jobs::turnRed` - Turns the signal red. This can be called at any time and is a no-op if the signal is already red.
* `jobs::turnGreen` - Turns the signal green, scheduling all waiting fibers to execute. This can be called at any time and is a no-op if the signal is already green.
* `jobs::wait` - Waits until the signal turns green. If the signal is already green, it continues. While waiting, the job system runs other jobs.
* `jobs::waitAndTurnRed` - Waits until the signal turns green and then atomically turns it red. If the signal is already green, it is equivalent to `jobs::turnRed`. If multiple fibers are `waitAndTurnRed`-ing on the same signal and it turns green, only one fiber proceeds with execution.

While **waiting** on a signal, a **worker thread** will attempt to **execute another job** from the queue. If no jobs are available, the thread will go to sleep to conserve resources. Signals are not copyable. This means the following code is invalid:

```cpp
jobs::Signal other_signal = signal;
```

### Counters

Counters are a specialized type of signal that maintain a numeric value. They are considered green when this value is zero and red when it is non-zero. Counters are used to wait until one or more jobs are finished. The main operations involving counters are:

- `jobs::run` - can increment the value of a provided counter (pass the counter as the third parameter).
- `jobs::wait` - waits until the counter reaches zero / signal turns green.

```cpp
jobs::Counter counter;
// run some job
jobs::run(&data, function, &counter);

// wait till the job is finished
jobs::wait(&counter);
// no need to cleanup `counter`
```

The Job System retains pointers to counters until the associated jobs are completed. It is the **user's responsibility** to ensure that the **counter remains valid** until the job is finished. Consequently, counters **cannot be assigned** to another counter, making the following code invalid:

```cpp
jobs::Counter other_counter = counter; // invalid

void foo() {
    jobs::Counter counter;
    jobs::run(&data, fn, &counter);
    // `counter` gets destroyed here, but there's no guarantee the job is finished, so this is invalid
}
```

Multiple jobs can use the same counter:

```cpp
jobs::Counter counter;
// run two jobs
jobs::run(&dataA, fnA, &counter);
jobs::run(&dataB, fnB, &counter);
// wait till both jobs are finished
jobs::wait(&counter);
```

## Mutex

The Job System provides a mutex to protect shared resources from concurrent access by multiple jobs. Use the provided free functions `jobs::enter` / `jobs::exit` or the RAII `jobs::MutexGuard` to lock a `jobs::Mutex`.

```cpp
struct {
    jobs::Mutex m_mutex;
    Array<T> m_array;

    T pop() {
        jobs::MutexGuard guard(m_mutex);
        T result = m_array.back();
        m_array.pop();
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
            positions[i] = mtx * positions[i];
        }
    });
```

# Links

* [Marl](https://github.com/google/marl)
* [folly::fibers](https://github.com/facebook/folly/blob/main/folly/fibers/README.md)
