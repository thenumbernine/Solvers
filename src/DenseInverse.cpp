#include "Solvers/DenseInverse.h"
#include "Solvers/Vector.h"
#include <string.h>	//memset
#include <math.h>
#include <cassert>

namespace Solvers {

template<typename real>
void DenseInverse<real>::backSubstituteUpperTriangular(size_t m, size_t n, real* x, const real* a, const real* const b) {
	assert(m >= n);
	for (int i = n-1; i >= 0; --i) {
		real sum = 0;
		for (int j = i+1; j < n; ++j) {
			sum += a[i+m*j] * x[j];
		}
		x[i] = (b[i] - sum) / a[i+m*i];
	}
}

template<typename real>
void HouseholderQR<real>::applyQ(real* a, int m, int k, int jmin, int jmax, real* v) {
	for (int j = jmin; j < jmax; ++j) {
		real vDotMj = 0;
		for (int i = k; i < m; ++i) {
			vDotMj += v[i-k] * a[i + m * j];
		}
		for (int i = k; i < m; ++i) {
			a[i + m * j] -= 2. * vDotMj * v[i-k];
		}
	}
}

template<typename real>
void HouseholderQR<real>::householderQR(size_t m, size_t n, real* qt, real* a) {
	assert(m >= n);

	real v[m];

	for (int i = 0; i < m; ++i) {
		for (int j = 0; j < m; ++j) {
			qt[i+m*j] = i == j ? 1 : 0;
		}
	}

	for (int k = 0; k < n; ++k) {
		//v[i-k] = a[i,k], k<=i<m
		memcpy(v, a + k + m * k, sizeof(real) * (m - k));
		real vLen = Vector<real>::normL2(m-k, v);
		v[0] += vLen * (v[0] < 0 ? -1 : 1);
		vLen = Vector<real>::normL2(m-k, v);
		if (vLen > 1e-10) {
			for (int i = 0; i < m-k; ++i) {
				v[i] /= vLen;
			}
		}
		applyQ(a, m, k, k, n, v);
		applyQ(qt, m, k, 0, m, v);
	}
}

template<typename real>
void HouseholderQR<real>::solveLinear_leastSquares(size_t m, size_t n, real* x, const real* a, const real* b) {

	real r[m * n];
	memcpy(r, a, sizeof(real) * m * n);
	
	real qt[m * m];
	householderQR(m, n, qt, r);

	//if x == b are matching pointers then I wouldn't be able to write qtb to x without corrupting it
	//so I'll use an extra buffer to store the intermediate value
	real qtb[m];
	for (int i = 0; i < m; ++i) {
		real sum = 0;
		for (int j = 0; j < m; ++j) {
			sum += qt[i + m * j] * b[j];
		}
		qtb[i] = sum;
	}
	Super::backSubstituteUpperTriangular(m, n, x, r, qtb);
}

template<typename real>
void HouseholderQR<real>::solveLinear(size_t n, real* x, const real* a, const real *b) {
	solveLinear_leastSquares(n, n, x, a, b);
}

template<typename real>
void HouseholderQR<real>::matrixInverse(size_t n, real* ainv, const real* a) {
	real r[n * n];
	memcpy(r, a, sizeof(real) * n * n);

	real qt[n * n];
	householderQR(n, n, qt, r);
		
	real qty[n];

	for (int i = 0; i < n; ++i) {
		for (int j = 0; j < n; ++j) {
			ainv[i+n*j] = i == j ? 1 : 0;
		}
	}
	for (int j = 0; j < n; ++j) {
		//solve for x in a x = y
		//let a = q r
		//q r x = y
		//r x = q^t y
		for (int i = 0; i < n; ++i) {
			real sum = 0;
			for (int k = 0; k < n; ++k) {
				sum = sum + qt[i + n * k] * ainv[k + n * j];
			}
			qty[i] = sum;
		}
		Super::backSubstituteUpperTriangular(n, n, ainv + n * j, r, qty);
	}
}

template struct DenseInverse<float>;
template struct DenseInverse<double>;

template struct HouseholderQR<float>;
template struct HouseholderQR<double>;

}