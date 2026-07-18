// Mandelbrot benchmark
// Node.js port of mandel.ls

function norm2(re, im) {
	return re * re - im * (-im);
}

function cabs(re, im) {
	return Math.sqrt(norm2(re, im));
}

function level(x, y) {
	let zre = x;
	let zim = y;

	for (let l = 0; l < 255; l++) {
		const tre = zre * zre - zim * zim;
		const tim = zre * zim + zim * zre;

		zre = tre + x;
		zim = tim + y;

		if (cabs(zre, zim) > 2.0) {
			return l;
		}
	}

	return 255;
}

function main() {
	const xmin = -2.0;
	const ymin = -2.0;
	const n = 256;

	const dx = 4.0 / n;
	const dy = 4.0 / n;

	let result = 0;
	for (let i = 0; i < n; i++) {
		const x = xmin + i * dx;
		for (let j = 0; j < n; j++) {
			const y = ymin + j * dy;
			result += level(x, y);
		}
	}

	return result;
}

const start = performance.now();
const result = main();
const elapsed = performance.now() - start;
console.error(`Time: ${elapsed.toFixed(2)} ms`);
console.log(result);
