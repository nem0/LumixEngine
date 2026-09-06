// Mandelbrot benchmark
// Port of https://github.com/Beariish/bolt/blob/main/benchmarks/mandel.bolt

import "std:math" as math

fn norm2(re : f64, im : f64) : f64 {
	return re * re - im * (-im);
}

fn cabs(re : f64, im : f64) : f64 {
	return math.sqrt_f64(norm2(re, im));
}

fn level(x : f64, y : f64) : i32 {
	var zre = x;
	var zim = y;

	for l in 0..255 {
		const tre = zre * zre - zim * zim;
		const tim = zre * zim + zim * zre;

		zre = tre + x;
		zim = tim + y;

		if cabs(zre, zim) > 2.0 {
			return l;
		}
	}

	return 255;
}

fn main() : i32 {
	const xmin = -2.0;
	const ymin = -2.0;
	const n = 256;

	const dx = 4.0 / (n as f64);
	const dy = 4.0 / (n as f64);

	var result : i32 = 0;
	for i in 0..n {
		const x = xmin + (i as f64) * dx;
		for j in 0..n {
			const y = ymin + (j as f64) * dy;
			result += level(x, y);
		}
	}

	return result;
}
