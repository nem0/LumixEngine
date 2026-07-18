// Recursion: Fibonacci benchmark
// Port of https://github.com/Beariish/bolt/blob/main/benchmarks/recursion.bolt

fn fib(n : i32) : i32 {
	if n <= 1 {
		return n;
	}
	return fib(n - 1) + fib(n - 2);
}

fn main() : i32 {
	return fib(30);
}

