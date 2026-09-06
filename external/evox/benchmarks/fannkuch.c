/* Fannkuch benchmark
 * C port of fannkuch.evox
 */

#include <stdio.h>
#include "../platform.h"

static int fannkuch(int n, int* perm1, int* perm, int* count) {
	for (int i = 0; i < n; i++) {
		perm1[i] = i;
	}

	int flips = 0;
	int nperm = 0;
	int checksum = 0;
	int r = n;

	while (r > 0) {
		int i = r;
		while (i > 0) {
			count[i - 1] = i;
			i -= 1;
		}

		for (int j = 0; j < n; j++) {
			perm[j] = perm1[j];
		}

		int f = 0;
		int k = perm[0];

		while (k != 0) {
			const int half = (k + 1) / 2;
			for (int j = 0; j < half; j++) {
				const int t = perm[j];
				perm[j] = perm[k - j];
				perm[k - j] = t;
			}

			k = perm[0];
			f += 1;
		}

		if (f > flips) {
			flips = f;
		}
		if (nperm % 2 == 0) {
			checksum += f;
		} else {
			checksum -= f;
		}

		r = 1;
		for (;;) {
			if (r == n) {
				return flips;
			}

			const int p0 = perm1[0];
			for (int j = 0; j < r; j++) {
				perm1[j] = perm1[j + 1];
			}

			perm1[r] = p0;
			count[r] -= 1;
			if (count[r] > 0) {
				break;
			}

			r += 1;
		}

		nperm += 1;
	}

	return flips;
}

int main(void) {
	int perm1[9];
	int perm[9];
	int count[9];

	const double start = ex_platform_now_ms();
	const int result = fannkuch(9, perm1, perm, count);
	const double elapsed = ex_platform_now_ms() - start;
	fprintf(stderr, "Time: %.2f ms\n", elapsed);
	printf("%d\n", result);
	return 0;
}
