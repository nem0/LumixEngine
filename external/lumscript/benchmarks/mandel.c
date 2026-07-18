/* Mandelbrot benchmark
 * C port of mandel.ls
 */

#include <math.h>
#include <stdio.h>
#include <windows.h>

static double now_ms(void) {
	LARGE_INTEGER freq, counter;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static double norm2(double re, double im) {
	return re * re - im * (-im);
}

static double cabs_(double re, double im) {
	return sqrt(norm2(re, im));
}

static int level(double x, double y) {
	double zre = x;
	double zim = y;

	for (int l = 0; l < 255; l++) {
		const double tre = zre * zre - zim * zim;
		const double tim = zre * zim + zim * zre;

		zre = tre + x;
		zim = tim + y;

		if (cabs_(zre, zim) > 2.0) {
			return l;
		}
	}

	return 255;
}

static int run(void) {
	const double xmin = -2.0;
	const double ymin = -2.0;
	const int n = 256;

	const double dx = 4.0 / n;
	const double dy = 4.0 / n;

	int result = 0;
	for (int i = 0; i < n; i++) {
		const double x = xmin + i * dx;
		for (int j = 0; j < n; j++) {
			const double y = ymin + j * dy;
			result += level(x, y);
		}
	}

	return result;
}

int main(void) {
	const double start = now_ms();
	const int result = run();
	const double elapsed = now_ms() - start;
	fprintf(stderr, "Time: %.2f ms\n", elapsed);
	printf("%d\n", result);
	return 0;
}
