/* Recursion: Fibonacci benchmark
 * C port of recursion.ls
 */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static double now_ms(void) {
	LARGE_INTEGER freq, counter;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static int fib(int n) {
	if (n <= 1) {
		return n;
	}
	return fib(n - 1) + fib(n - 2);
}

int main(int argc, char** argv) {
	/* Read n from argv so the compiler cannot fold fib(30) to a constant. */
	const int n = argc > 1 ? atoi(argv[1]) : 30;
	const double start = now_ms();
	const int result = fib(n);
	const double elapsed = now_ms() - start;
	fprintf(stderr, "Time: %.2f ms\n", elapsed);
	printf("%d\n", result);
	return 0;
}
