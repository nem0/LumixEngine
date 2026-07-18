// Fannkuch benchmark
// Node.js port of fannkuch.ls

function fannkuch(n, perm1, perm, count) {
	for (let i = 0; i < n; i++) {
		perm1[i] = i;
	}

	let flips = 0;
	let nperm = 0;
	let checksum = 0;
	let r = n;

	while (r > 0) {
		let i = r;
		while (i > 0) {
			count[i - 1] = i;
			i -= 1;
		}

		for (let j = 0; j < n; j++) {
			perm[j] = perm1[j];
		}

		let f = 0;
		let k = perm[0];

		while (k !== 0) {
			const half = (k + 1) >> 1;
			for (let j = 0; j < half; j++) {
				const t = perm[j];
				perm[j] = perm[k - j];
				perm[k - j] = t;
			}

			k = perm[0];
			f += 1;
		}

		if (f > flips) {
			flips = f;
		}
		if (nperm % 2 === 0) {
			checksum += f;
		} else {
			checksum -= f;
		}

		r = 1;
		for (;;) {
			if (r === n) {
				return flips;
			}

			const p0 = perm1[0];
			for (let j = 0; j < r; j++) {
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

const start = performance.now();
const result = fannkuch(9, new Int32Array(9), new Int32Array(9), new Int32Array(9));
const elapsed = performance.now() - start;
console.error(`Time: ${elapsed.toFixed(2)} ms`);
console.log(result);
