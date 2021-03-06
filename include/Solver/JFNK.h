#pragma once

#include "Solver/GMRES.h"
#include "Solver/Vector.h"
#include <memory>

namespace Solver {

/*
source:
Knoll, Keyes "Jacobian-Free Newton-Krylov Methods" 2003

LinearSolver is constructed with the createLinearSolver lambda
and must have a .solve() routine to solve for a single iteration
*/
template<typename real>
struct JFNK {

	using Func = std::function<void(real* y, const real* x)>;

	JFNK(
		size_t n,
		real* x,
		Func F,
		real stopEpsilon,
		int maxiter,
		std::function<std::shared_ptr<Krylov<real>>(size_t n, real* x, real* b, Func A)> createLinearSolver
		= [](size_t n, real* x, real* b, Func A) -> std::shared_ptr<Krylov<real>> {
			return std::make_shared<GMRES<real>>(n, x, b, A, 1e-20, 10 * n, n);
		});
	virtual ~JFNK();

	/*
	perform a single newton iteration
	newton = newton structure
	maxAlpha = scale applied to solved dx update step size
	lineSearchMaxIter = number of divisions to break maxAlpha * dx into when line searching
	*/
	void update();

	/*
	run all iterations until maxiter is reached or until stopEpsilon is reached
	*/
	void solve();

protected:
	size_t n;
	
	//external buffers for the caller to provide

	//current state / at which we are converging about
	//this is *not* the residual that the JFNK minimizes.  That is the private->dx vector.
	//size. equal to gmres.krylov.n. I'm keeping it separate for when gmres is disabled.
	real* x;

public:
	//function which we're minimizing wrt
	Func F;

protected:
	void krylovLinearFunc(real* y, const real* x);

public:
	/*
	don't do any extra searching -- just take the full step
	*/
	real lineSearch_none();

	/*
	assumes current step is in newton->private->dx
	finds best alpha along line, using no more than 'lineSearchMaxIter' iterations 
	reads and writes x, writes to x_plus_dx and F_of_x_plus_dx 
	*/
	real lineSearch_linear();

	/*
	successively subdivide line search to find best alpha
	*/	
	real lineSearch_bisect();

	//line search method
	real (JFNK::*lineSearch)();

	//line search scalar
	real maxAlpha;

	//line search max iter
	int lineSearchMaxIter;

	//epsilon for computing jacobian
	real jacobianEpsilon;

	//stop epsilon
	real stopEpsilon;

	//stop max iter
	int maxiter;

protected:
	virtual real calcResidual(const real* x, real alpha) const;
	
	real residualAtAlpha(real alpha);
	
	//step to solve (df/du)^-1 * du via GMRES
	real* dx;

	//function value at that point
	real* F_of_x;
	
	//temporary buffers
	real* x_plus_dx;
	real* F_of_x_plus_dx;

	real* x_minus_dx;
	real* F_of_x_minus_dx;

public:
	real getResidual() const { return residual; }
	real getAlpha() const { return alpha; }
	int getIter() const { return iter; }
	std::shared_ptr<Krylov<real>> getLinearSolver() const { return linearSolver; }
protected:
	//residual of best solution along the line search
	real residual;

	//alpha of best solution along the line search
	real alpha;

	//current iteration
	int iter;

	std::shared_ptr<Krylov<real>> linearSolver;

public:
	std::function<bool()> stopCallback;
};

}


#include "Solver/Vector.h"
#include <limits>
#include <string.h>	//memcpy
#include <cmath>	//isfinite
#include <assert.h>

namespace Solver {

template<typename real>
JFNK<real>::JFNK(
	size_t n_,
	real* x_,
	Func F_,
	real stopEpsilon_,
	int maxiter_,
	std::function<std::shared_ptr<Krylov<real>>(size_t n, real* x, real* b, Func linearFunc)> createLinearSolver)
: n(n_)
, x(x_)
, F(F_)
, lineSearch(&JFNK::lineSearch_bisect)
, maxAlpha(1)
, lineSearchMaxIter(20)
, jacobianEpsilon(1e-6)
, stopEpsilon(stopEpsilon_)
, maxiter(maxiter_)
, dx(new real[n])
, F_of_x(new real[n])
, x_plus_dx(new real[n])
, F_of_x_plus_dx(new real[n])
, x_minus_dx(new real[n])
, F_of_x_minus_dx(new real[n])
, residual(0)
, alpha(0)
, iter(0)
, linearSolver(createLinearSolver(n, dx, F_of_x, [&](real* y, const real* x) {
	return this->krylovLinearFunc(y, x);
}))
{
	//assume x has the initial content
	//use x as the initial dx
	memcpy(dx, x, sizeof(real) * n);
}

template<typename real>
JFNK<real>::~JFNK() {
	delete[] dx;
	delete[] F_of_x;
	delete[] x_plus_dx;
	delete[] F_of_x_plus_dx;
	delete[] x_minus_dx;
	delete[] F_of_x_minus_dx;
}

//solve dF(x[n])/dx[n] x = F(x[n]) for x
template<typename real>
void JFNK<real>::krylovLinearFunc(real* y, const real* dx) {
#if 0
	// https://en.wikipedia.org/wiki/Machine_epsilon
	// machine epsilon for double precision is 2^-53 ~ 1.11e-16
	// sqrt machine epsilon is ~ 1e-8
	//but the Knoll, Keyes 2003 paper says they use 1e-6
	// the paper doesn't say the norm for this particular equation, but in all other equations they use a L2 norm
	real sqrtMachineEpsilon = 1e-6;
	real xNorm = vec_normL2(x);
	real dxNorm = vec_normL2(dx);
	real epsilon = sqrt( (1 + xNorm) / dxNorm ) * sqrtMachineEpsilon;
#else
	//looks like in my config I'm using 1e-6, which is the sqrt(machine epsilon) that the paper describes
	real epsilon = jacobianEpsilon;
#endif

	for (int i = 0; i < (int)n; ++i) {
		x_plus_dx[i] = x[i] + dx[i] * epsilon;
		x_minus_dx[i] = x[i] - dx[i] * epsilon;
	}
	
	F(F_of_x_plus_dx, x_plus_dx);	//F(x + dx * epsilon)
	F(F_of_x_minus_dx, x_minus_dx);	//F(x - dx * epsilon)

	/*
	Knoll, Keyes "Jacobian-Free JFNK-Krylov Methods" 2003 
	 shows "(f(x+epsilon v) - f(x)) / epsilon" for first order
	 and "(f(x+epsilon v) - f(x-epxilon v)) / epsilon" for second order
	  shouldn't the latter have "2 epsilon" on the bottom?
	 shouldn't they both have "|v|" on the bottom?
	
	using epsilon converges in 30 iterations, using 2*epsilon converges in 10
	*/
	real denom = 2. * epsilon;	//jacobianEpsilon;// * vec_normL2((vec_t){.v=dx, .n=n});
	
	//TODO shouldn't this be divided by epsilon times |dx| ?
	//(F(x + dx * epsilon) - F(x - dx * epsilon)) / (2 * |dx| * epsilon)
	for (int i = 0; i < (int)n; ++i) {
		y[i] = (F_of_x_plus_dx[i] - F_of_x_minus_dx[i]) / denom;		//F(x + dx * epsilon) - F(x - dx * epsilon)
	}
}

template<typename real>
real JFNK<real>::calcResidual(const real* x, real alpha) const {
	return Vector<real>::normL2(n, x) / (real)n;
}

template<typename real>
real JFNK<real>::residualAtAlpha(real alpha) {
	
	//advance by fraction along dx
	for (int i = 0; i < (int)n; ++i) {
		x_plus_dx[i] = x[i] - dx[i] * alpha;
	}
	
	//calculate residual at x
	F(F_of_x_plus_dx, x_plus_dx);
	
	//divide by n to normalize, so errors remain the same despite vector size
	real stepResidual = calcResidual(F_of_x_plus_dx, alpha);
	
	//for comparison's sake, convert nans to flt_max's
	//this will still fail the std::isfinite() conditions, *and* it will correctly compare when searching for minimas
	if (stepResidual != stepResidual) stepResidual = std::numeric_limits<real>::max();

	return stepResidual;
}

template<typename real>
real JFNK<real>::lineSearch_none() {
	residual = residualAtAlpha(maxAlpha);
	return maxAlpha;
}

template<typename real>
real JFNK<real>::lineSearch_linear() {
	real alpha = 0;
	residual = std::numeric_limits<real>::max();
	
	for (int i = 0; i <= lineSearchMaxIter; ++i) {
		real stepAlpha = maxAlpha * (real)i / (real)lineSearchMaxIter;
		real stepResidual = residualAtAlpha(stepAlpha);
		if (stepResidual < residual) {
			residual = stepResidual;
			alpha = stepAlpha;
		}
	}

	return alpha;
}

template<typename real>
real JFNK<real>::lineSearch_bisect() {
	real alphaL = 0;
	real alphaR = maxAlpha;
	real residualL = residualAtAlpha(alphaL);
	real residualR = residualAtAlpha(alphaR);

	for (int i = 0; i < lineSearchMaxIter; ++i) {
		real alphaMid = .5 * (alphaL + alphaR);
		real residualMid = residualAtAlpha(alphaMid);
		if (residualMid > residualL && residualMid > residualR) break;
		if (residualMid < residualL && residualMid < residualR) {	//better than both?  see which edge has the lead
			if (residualL <= residualR) {	//<= to prefer sticking closer to the origin in case of equality
				alphaR = alphaMid;
				residualR = residualMid;
			} else {
				alphaL = alphaMid;
				residualL = residualMid;
			}
		} else if (residualMid < residualL) {
			alphaL = alphaMid;
			residualL = residualMid;
		} else {
			alphaR = alphaMid;
			residualR = residualMid;
		}
	}
	
	residual = fmin(residualL, residualR);
	return residualL < residualR ? alphaL : alphaR;
}

/*
performs update of iteration x[n+1] = x[n] - ||dF/dx||^-1 F(x[n])
*/
template<typename real>
void JFNK<real>::update() {	

	//first calc F(x[n])
	F(F_of_x, x);	

	//solve dF(x[n])/dx[n] dx[n] = F(x[n]) for dx[n]
	//treating dF(x[n])/dx[n] = I gives us the (working) explicit version
	linearSolver->solve();

//the next step in matching the implicit to the explicit (whose results are good) is making sure the line search is going the correct distance 
	//update x[n] = x[n] - alpha * dx[n] for some alpha
	alpha = (this->*lineSearch)();

	if (!alpha) {
		//fail code? one will be set in the sim_t calling function at least.
	} else if (!std::isfinite(residual)) {
		//fail code as well?  likewise, one will be set in the caller.
	} else {

		//don't error here -- instead let the outer loop handle this 
		//if (private->alpha == 0) errorStr("stuck"); 

		//set x[n+1] = x[n] - alpha * dx[n]
		for (int i = 0; i < (int)n; ++i) {
			x[i] -= dx[i] * alpha;
		}
	}
}

template<typename real>
void JFNK<real>::solve() {
	for (; iter < maxiter; ++iter) {
		update();
		if (stopCallback && stopCallback()) break;
		if (!alpha) break;
		if (!std::isfinite(residual)) break;
		if (residual < stopEpsilon) break;
	}
}

}
