# Profiler

## Profiler Overview

The profiler in LumixEngine is a tool designed to help developers analyze and optimize the performance of their games. It provides detailed insights into CPU and GPU usage, memory consumption, and other critical performance metrics. 

The profiler is divided into two components: the Recorder and the Viewer. 

The **Recorder** is embedded within the engine and is responsible for capturing the performance data. You can find its source code in [src/core/profiler.cpp](../src/core/profiler.cpp). It's a tracing profiler - it only captures what it's told to capture. Recording can be started and paused at any time. Studio is started with recording paused, unless `-profile_start` is provided on command line. Profiling data is stored in thread-local ring buffers with a fixed size, which means that older data is overwritten by newer data as it is recorded. For more information, see the `default_context_size` in [profiler.cpp](../src/core/profiler.cpp). Some data, such as GPU data, are not tied to any specific thread and are stored in a special context.

The **Viewer** is integrated into the editor and enables developers to visualize and analyze the captured performance data. You can find its source code in [src/editor/profiler_ui.cpp](../src/editor/profiler_ui.cpp).

To access the Viewer user interface, navigate to **Main Menu -> View -> Profiler** in the editor. The Viewer UI consists of three tabs: **GPU / CPU, Memory and Resources**.

## CPU / GPU

### Blocks

Blocks are defined using `profiler::beginBlock` and `profiler::endBlock`. These blocks are visualized as rectangles with their names inside. They are stacked to create a flamegraph representation. Hover over a block with the mouse to see its duration.

![blocks](images/profiler/blocks.png)

```cpp
    #include "core/profiler.h"

    void function() {
        // this creates a block with function name as its name and
        // duration till the end of scope
        PROFILE_FUNCTION();
        ...
        if (condition) {
            // this creates a block with the provided name
            // the block ends at the end of scope
            PROFILE_BLOCK("block #0");
            
            // assign color to the current block (block #0)
            profiler::blockColor(0x60, 0x60, 0x60); 
        }

        ...
        // manual begin/end block pair
        profiler::beginBlock("block #1");
        ...
        profiler::endBlock();
    }
```

### Fiber switch

### Properties

You can associate additional data with a block, such as:

* A string
* A string and an integer

Hover over a block with the mouse to view this data.

![alt text](images/profiler/block_properties.png)

```cpp
{
    PROFILE_BLOCK("culling");
    ...
    // assign culled count to "culling" block so we can view it in profiler
    profiler::pushInt("count", total_count);
}
```
### Links

TODO

### Counters

Counters are used to track and visualize numerical values over time. They can be useful for monitoring metrics such as frame rates, memory usage, or any other custom values you wish to track.

To define a counter, use the `profiler::createCounter`. You can update the counter's value at any point in your code with `profiler::pushCounter`.

```cpp
void pushProcessMemory() {
    static u32 process_mem_counter = profiler::createCounter("Process Memory (MB)", 0);
    profiler::pushCounter(process_mem_counter, process_mem);
}
```

Counters are displayed as graphs in the profiler, allowing you to see how the values change over time.

![alt text](images/profiler/counters.png)


### Frames

Blue vertical lines indicate each call to `profiler::frame`, which is invoked in the "main thread" at the end of every frame.

#### Autopause

When enabled, the profiler will automatically pause recording if a frame exceeds a certain duration threshold. This allows developers to inspect the captured data in detail without it being overwritten in the ring buffer.

You can enable autopause in the profiler UI by clicking on the "cogs" button.

![alt text](images/profiler/autopause.png)

### Context switch

Context switches are represented by green lines above each thread, showing the active periods of the thread. Hover over these lines to see more details. To enable context switch recording, start the editor with administrative privileges and use the `-profile_cswitch` command line option. You may need to scroll back in the timeline to view context switches.

![alt text](images/profiler/context_switch.png)

### Mutex

TODO

### GPU

TODO

## Memory

TODO

## Resources

TODO

