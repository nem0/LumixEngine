// Fannkuch benchmark
// Port of https://github.com/Beariish/bolt/blob/main/benchmarks/fannkuch.bolt
//
// Differences from the bolt original:
// - fixed-size scratch arrays passed as slices instead of dynamic arrays
// - `for i in r to 0 by -1` and `for k != 0` loops are written as while loops
// - `for i in k / 2` on bolt's float numbers iterates ceil(k / 2) times, which
//   is (k + 1) / 2 in integer arithmetic
// - runs a single fannkuch(9) instead of the bench() harness

fn main() : i32 {
	const n = 9;
	var perm1 : [9]i32 = undefined;
	var perm : [9]i32 = undefined;
	var count : [9]i32 = undefined;

	for i in 0..n {
		perm1[i] = i;
	}

	var flips = 0;
	var nperm = 0;
	var checksum = 0;
	var r = n;

	while r > 0 {
		var i = r;
		while i > 0 {
			count[i - 1] = i;
			i -= 1;
		}

		for j in 0..n {
			perm[j] = perm1[j];
		}

		var f = 0;
		var k = perm[0];

		while k != 0 {
			for j in 0..(k + 1) / 2 {
				const t = perm[j];
				perm[j] = perm[k - j];
				perm[k - j] = t;
			}

			k = perm[0];
			f += 1;
		}

		if f > flips {
			flips = f;
		}
		if nperm % 2 == 0 {
			checksum += f;
		} else {
			checksum -= f;
		}

		r = 1;
		while true {
			if r == n {
				return flips;
			}

			const p0 = perm1[0];
			for j in 0..r {
				perm1[j] = perm1[j + 1];
			}

			perm1[r] = p0;
			count[r] -= 1;
			if count[r] > 0 {
				break;
			}

			r += 1;
		}

		nperm += 1;
	}

	return flips;
}
