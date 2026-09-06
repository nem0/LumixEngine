// Recursion: Fibonacci benchmark
// Node.js port of recursion.evox

function fib(n) {
	if (n <= 1) {
		return n;
	}
	return fib(n - 1) + fib(n - 2);
}

const start = performance.now();
const result = fib(30);
const elapsed = performance.now() - start;
console.error(`Time: ${elapsed.toFixed(2)} ms`);
console.log(result);
