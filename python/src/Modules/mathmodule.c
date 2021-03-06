/* Math module -- standard C math library functions, pi and e */

/* Here are some comments from Tim Peters, extracted from the
   discussion attached to http://bugs.python.org/issue1640.  They
   describe the general aims of the math module with respect to
   special values, IEEE-754 floating-point exceptions, and Python
   exceptions.

These are the "spirit of 754" rules:

1. If the mathematical result is a real number, but of magnitude too
large to approximate by a machine float, overflow is signaled and the
result is an infinity (with the appropriate sign).

2. If the mathematical result is a real number, but of magnitude too
small to approximate by a machine float, underflow is signaled and the
result is a zero (with the appropriate sign).

3. At a singularity (a value x such that the limit of f(y) as y
approaches x exists and is an infinity), "divide by zero" is signaled
and the result is an infinity (with the appropriate sign).  This is
complicated a little by that the left-side and right-side limits may
not be the same; e.g., 1/x approaches +inf or -inf as x approaches 0
from the positive or negative directions.  In that specific case, the
sign of the zero determines the result of 1/0.

4. At a point where a function has no defined result in the extended
reals (i.e., the reals plus an infinity or two), invalid operation is
signaled and a NaN is returned.

And these are what Python has historically /tried/ to do (but not
always successfully, as platform libm behavior varies a lot):

For #1, raise OverflowError.

For #2, return a zero (with the appropriate sign if that happens by
accident ;-)).

For #3 and #4, raise ValueError.  It may have made sense to raise
Python's ZeroDivisionError in #3, but historically that's only been
raised for division by zero and mod by zero.

*/

/*
   In general, on an IEEE-754 platform the aim is to follow the C99
   standard, including Annex 'F', whenever possible.  Where the
   standard recommends raising the 'divide-by-zero' or 'invalid'
   floating-point exceptions, Python should raise a ValueError.  Where
   the standard recommends raising 'overflow', Python should raise an
   OverflowError.  In all other circumstances a value should be
   returned.
 */

#include "Python.h"
#include "longintrepr.h" /* just for SHIFT */

#ifdef _OSF_SOURCE
/* OSF1 5.1 doesn't make this available with XOPEN_SOURCE_EXTENDED defined */
extern double copysign(double, double);
#endif

/* Call is_error when errno != 0, and where x is the result libm
 * returned.  is_error will usually set up an exception and return
 * true (1), but may return false (0) without setting up an exception.
 */
static int
is_error(double x)
{
	int result = 1;	/* presumption of guilt */
	assert(errno);	/* non-zero errno is a precondition for calling */
	if (errno == EDOM)
		PyErr_SetString(PyExc_ValueError, "math domain error");

	else if (errno == ERANGE) {
		/* ANSI C generally requires libm functions to set ERANGE
		 * on overflow, but also generally *allows* them to set
		 * ERANGE on underflow too.  There's no consistency about
		 * the latter across platforms.
		 * Alas, C99 never requires that errno be set.
		 * Here we suppress the underflow errors (libm functions
		 * should return a zero on underflow, and +- HUGE_VAL on
		 * overflow, so testing the result for zero suffices to
		 * distinguish the cases).
		 *
		 * On some platforms (Ubuntu/ia64) it seems that errno can be
		 * set to ERANGE for subnormal results that do *not* underflow
		 * to zero.  So to be safe, we'll ignore ERANGE whenever the
		 * function result is less than one in absolute value.
		 */
		if (fabs(x) < 1.0)
			result = 0;
		else
			PyErr_SetString(PyExc_OverflowError,
					"math range error");
	}
	else
                /* Unexpected math error */
		PyErr_SetFromErrno(PyExc_ValueError);
	return result;
}

/*
   wrapper for atan2 that deals directly with special cases before
   delegating to the platform libm for the remaining cases.  This
   is necessary to get consistent behaviour across platforms.
   Windows, FreeBSD and alpha Tru64 are amongst platforms that don't
   always follow C99.
*/

static double
m_atan2(double y, double x)
{
	if (Py_IS_NAN(x) || Py_IS_NAN(y))
		return Py_NAN;
	if (Py_IS_INFINITY(y)) {
		if (Py_IS_INFINITY(x)) {
			if (copysign(1., x) == 1.)
				/* atan2(+-inf, +inf) == +-pi/4 */
				return copysign(0.25*Py_MATH_PI, y);
			else
				/* atan2(+-inf, -inf) == +-pi*3/4 */
				return copysign(0.75*Py_MATH_PI, y);
		}
		/* atan2(+-inf, x) == +-pi/2 for finite x */
		return copysign(0.5*Py_MATH_PI, y);
	}
	if (Py_IS_INFINITY(x) || y == 0.) {
		if (copysign(1., x) == 1.)
			/* atan2(+-y, +inf) = atan2(+-0, +x) = +-0. */
			return copysign(0., y);
		else
			/* atan2(+-y, -inf) = atan2(+-0., -x) = +-pi. */
			return copysign(Py_MATH_PI, y);
	}
	return atan2(y, x);
}

/*
    Various platforms (Solaris, OpenBSD) do nonstandard things for log(0),
    log(-ve), log(NaN).  Here are wrappers for log and log10 that deal with
    special values directly, passing positive non-special values through to
    the system log/log10.
 */

static double
m_log(double x)
{
	if (Py_IS_FINITE(x)) {
		if (x > 0.0)
			return log(x);
		errno = EDOM;
		if (x == 0.0)
			return -Py_HUGE_VAL; /* log(0) = -inf */
		else
			return Py_NAN; /* log(-ve) = nan */
	}
	else if (Py_IS_NAN(x))
		return x; /* log(nan) = nan */
	else if (x > 0.0)
		return x; /* log(inf) = inf */
	else {
		errno = EDOM;
		return Py_NAN; /* log(-inf) = nan */
	}
}

static double
m_log10(double x)
{
	if (Py_IS_FINITE(x)) {
		if (x > 0.0)
			return log10(x);
		errno = EDOM;
		if (x == 0.0)
			return -Py_HUGE_VAL; /* log10(0) = -inf */
		else
			return Py_NAN; /* log10(-ve) = nan */
	}
	else if (Py_IS_NAN(x))
		return x; /* log10(nan) = nan */
	else if (x > 0.0)
		return x; /* log10(inf) = inf */
	else {
		errno = EDOM;
		return Py_NAN; /* log10(-inf) = nan */
	}
}


/*
   math_1 is used to wrap a libm function f that takes a double
   arguments and returns a double.

   The error reporting follows these rules, which are designed to do
   the right thing on C89/C99 platforms and IEEE 754/non IEEE 754
   platforms.

   - a NaN result from non-NaN inputs causes ValueError to be raised
   - an infinite result from finite inputs causes OverflowError to be
     raised if can_overflow is 1, or raises ValueError if can_overflow
     is 0.
   - if the result is finite and errno == EDOM then ValueError is
     raised
   - if the result is finite and nonzero and errno == ERANGE then
     OverflowError is raised

   The last rule is used to catch overflow on platforms which follow
   C89 but for which HUGE_VAL is not an infinity.

   For the majority of one-argument functions these rules are enough
   to ensure that Python's functions behave as specified in 'Annex F'
   of the C99 standard, with the 'invalid' and 'divide-by-zero'
   floating-point exceptions mapping to Python's ValueError and the
   'overflow' floating-point exception mapping to OverflowError.
   math_1 only works for functions that don't have singularities *and*
   the possibility of overflow; fortunately, that covers everything we
   care about right now.
*/

static PyObject *
math_1(PyObject *arg, double (*func) (double), int can_overflow)
{
	double x, r;
	x = PyFloat_AsDouble(arg);
	if (x == -1.0 && PyErr_Occurred())
		return NULL;
	errno = 0;
	PyFPE_START_PROTECT("in math_1", return 0);
	r = (*func)(x);
	PyFPE_END_PROTECT(r);
	if (Py_IS_NAN(r)) {
		if (!Py_IS_NAN(x))
			errno = EDOM;
		else
			errno = 0;
	}
	else if (Py_IS_INFINITY(r)) {
		if (Py_IS_FINITE(x))
			errno = can_overflow ? ERANGE : EDOM;
		else
			errno = 0;
	}
	if (errno && is_error(r))
		return NULL;
	else
		return PyFloat_FromDouble(r);
}

/*
   math_2 is used to wrap a libm function f that takes two double
   arguments and returns a double.

   The error reporting follows these rules, which are designed to do
   the right thing on C89/C99 platforms and IEEE 754/non IEEE 754
   platforms.

   - a NaN result from non-NaN inputs causes ValueError to be raised
   - an infinite result from finite inputs causes OverflowError to be
     raised.
   - if the result is finite and errno == EDOM then ValueError is
     raised
   - if the result is finite and nonzero and errno == ERANGE then
     OverflowError is raised

   The last rule is used to catch overflow on platforms which follow
   C89 but for which HUGE_VAL is not an infinity.

   For most two-argument functions (copysign, fmod, hypot, atan2)
   these rules are enough to ensure that Python's functions behave as
   specified in 'Annex F' of the C99 standard, with the 'invalid' and
   'divide-by-zero' floating-point exceptions mapping to Python's
   ValueError and the 'overflow' floating-point exception mapping to
   OverflowError.
*/

static PyObject *
math_2(PyObject *args, double (*func) (double, double), char *funcname)
{
	PyObject *ox, *oy;
	double x, y, r;
	if (! PyArg_UnpackTuple(args, funcname, 2, 2, &ox, &oy))
		return NULL;
	x = PyFloat_AsDouble(ox);
	y = PyFloat_AsDouble(oy);
	if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
		return NULL;
	errno = 0;
	PyFPE_START_PROTECT("in math_2", return 0);
	r = (*func)(x, y);
	PyFPE_END_PROTECT(r);
	if (Py_IS_NAN(r)) {
		if (!Py_IS_NAN(x) && !Py_IS_NAN(y))
			errno = EDOM;
		else
			errno = 0;
	}
	else if (Py_IS_INFINITY(r)) {
		if (Py_IS_FINITE(x) && Py_IS_FINITE(y))
			errno = ERANGE;
		else
			errno = 0;
	}
	if (errno && is_error(r))
		return NULL;
	else
		return PyFloat_FromDouble(r);
}

#define FUNC1(funcname, func, can_overflow, docstring)			\
	static PyObject * math_##funcname(PyObject *self, PyObject *args) { \
		return math_1(args, func, can_overflow);		    \
	}\
        PyDoc_STRVAR(math_##funcname##_doc, docstring);

#define FUNC2(funcname, func, docstring) \
	static PyObject * math_##funcname(PyObject *self, PyObject *args) { \
		return math_2(args, func, #funcname); \
	}\
        PyDoc_STRVAR(math_##funcname##_doc, docstring);

FUNC1(acos, acos, 0,
      "acos(x)\n\nReturn the arc cosine (measured in radians) of x.")
FUNC1(acosh, acosh, 0,
      "acosh(x)\n\nReturn the hyperbolic arc cosine (measured in radians) of x.")
FUNC1(asin, asin, 0,
      "asin(x)\n\nReturn the arc sine (measured in radians) of x.")
FUNC1(asinh, asinh, 0,
      "asinh(x)\n\nReturn the hyperbolic arc sine (measured in radians) of x.")
FUNC1(atan, atan, 0,
      "atan(x)\n\nReturn the arc tangent (measured in radians) of x.")
FUNC2(atan2, m_atan2,
      "atan2(y, x)\n\nReturn the arc tangent (measured in radians) of y/x.\n"
      "Unlike atan(y/x), the signs of both x and y are considered.")
FUNC1(atanh, atanh, 0,
      "atanh(x)\n\nReturn the hyperbolic arc tangent (measured in radians) of x.")
FUNC1(ceil, ceil, 0,
      "ceil(x)\n\nReturn the ceiling of x as a float.\n"
      "This is the smallest integral value >= x.")
FUNC2(copysign, copysign,
      "copysign(x,y)\n\nReturn x with the sign of y.")
FUNC1(cos, cos, 0,
      "cos(x)\n\nReturn the cosine of x (measured in radians).")
FUNC1(cosh, cosh, 1,
      "cosh(x)\n\nReturn the hyperbolic cosine of x.")
FUNC1(exp, exp, 1,
      "exp(x)\n\nReturn e raised to the power of x.")
FUNC1(fabs, fabs, 0,
      "fabs(x)\n\nReturn the absolute value of the float x.")
FUNC1(floor, floor, 0,
      "floor(x)\n\nReturn the floor of x as a float.\n"
      "This is the largest integral value <= x.")
FUNC1(log1p, log1p, 1,
      "log1p(x)\n\nReturn the natural logarithm of 1+x (base e).\n\
      The result is computed in a way which is accurate for x near zero.")
FUNC1(sin, sin, 0,
      "sin(x)\n\nReturn the sine of x (measured in radians).")
FUNC1(sinh, sinh, 1,
      "sinh(x)\n\nReturn the hyperbolic sine of x.")
FUNC1(sqrt, sqrt, 0,
      "sqrt(x)\n\nReturn the square root of x.")
FUNC1(tan, tan, 0,
      "tan(x)\n\nReturn the tangent of x (measured in radians).")
FUNC1(tanh, tanh, 0,
      "tanh(x)\n\nReturn the hyperbolic tangent of x.")

/* Precision summation function as msum() by Raymond Hettinger in
   <http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/393090>,
   enhanced with the exact partials sum and roundoff from Mark
   Dickinson's post at <http://bugs.python.org/file10357/msum4.py>.
   See those links for more details, proofs and other references.

   Note 1: IEEE 754R floating point semantics are assumed,
   but the current implementation does not re-establish special
   value semantics across iterations (i.e. handling -Inf + Inf).

   Note 2:  No provision is made for intermediate overflow handling;
   therefore, sum([1e+308, 1e-308, 1e+308]) returns 1e+308 while
   sum([1e+308, 1e+308, 1e-308]) raises an OverflowError due to the
   overflow of the first partial sum.

   Note 3: The intermediate values lo, yr, and hi are declared volatile so
   aggressive compilers won't algebraically reduce lo to always be exactly 0.0.
   Also, the volatile declaration forces the values to be stored in memory as
   regular doubles instead of extended long precision (80-bit) values.  This
   prevents double rounding because any addition or subtraction of two doubles
   can be resolved exactly into double-sized hi and lo values.  As long as the 
   hi value gets forced into a double before yr and lo are computed, the extra
   bits in downstream extended precision operations (x87 for example) will be
   exactly zero and therefore can be losslessly stored back into a double,
   thereby preventing double rounding.

   Note 4: A similar implementation is in Modules/cmathmodule.c.
   Be sure to update both when making changes.

   Note 5: The signature of math.fsum() differs from __builtin__.sum()
   because the start argument doesn't make sense in the context of
   accurate summation.  Since the partials table is collapsed before
   returning a result, sum(seq2, start=sum(seq1)) may not equal the
   accurate result returned by sum(itertools.chain(seq1, seq2)).
*/

#define NUM_PARTIALS  32  /* initial partials array size, on stack */

/* Extend the partials array p[] by doubling its size. */
static int                          /* non-zero on error */
_fsum_realloc(double **p_ptr, Py_ssize_t  n,
             double  *ps,    Py_ssize_t *m_ptr)
{
	void *v = NULL;
	Py_ssize_t m = *m_ptr;

	m += m;  /* double */
	if (n < m && m < (PY_SSIZE_T_MAX / sizeof(double))) {
		double *p = *p_ptr;
		if (p == ps) {
			v = PyMem_Malloc(sizeof(double) * m);
			if (v != NULL)
				memcpy(v, ps, sizeof(double) * n);
		}
		else
			v = PyMem_Realloc(p, sizeof(double) * m);
	}
	if (v == NULL) {        /* size overflow or no memory */
		PyErr_SetString(PyExc_MemoryError, "math.fsum partials");
		return 1;
	}
	*p_ptr = (double*) v;
	*m_ptr = m;
	return 0;
}

/* Full precision summation of a sequence of floats.

   def msum(iterable):
       partials = []  # sorted, non-overlapping partial sums
       for x in iterable:
           i = 0
           for y in partials:
               if abs(x) < abs(y):
                   x, y = y, x
               hi = x + y
               lo = y - (hi - x)
               if lo:
                   partials[i] = lo
                   i += 1
               x = hi
           partials[i:] = [x]
       return sum_exact(partials)

   Rounded x+y stored in hi with the roundoff stored in lo.  Together hi+lo
   are exactly equal to x+y.  The inner loop applies hi/lo summation to each
   partial so that the list of partial sums remains exact.

   Sum_exact() adds the partial sums exactly and correctly rounds the final
   result (using the round-half-to-even rule).  The items in partials remain
   non-zero, non-special, non-overlapping and strictly increasing in
   magnitude, but possibly not all having the same sign.

   Depends on IEEE 754 arithmetic guarantees and half-even rounding.
*/

static PyObject*
math_fsum(PyObject *self, PyObject *seq)
{
	PyObject *item, *iter, *sum = NULL;
	Py_ssize_t i, j, n = 0, m = NUM_PARTIALS;
	double x, y, t, ps[NUM_PARTIALS], *p = ps;
	double xsave, special_sum = 0.0, inf_sum = 0.0;
	volatile double hi, yr, lo;

	iter = PyObject_GetIter(seq);
	if (iter == NULL)
		return NULL;

	PyFPE_START_PROTECT("fsum", Py_DECREF(iter); return NULL)

	for(;;) {           /* for x in iterable */
		assert(0 <= n && n <= m);
		assert((m == NUM_PARTIALS && p == ps) ||
		       (m >  NUM_PARTIALS && p != NULL));

		item = PyIter_Next(iter);
		if (item == NULL) {
			if (PyErr_Occurred())
				goto _fsum_error;
			break;
		}
		x = PyFloat_AsDouble(item);
		Py_DECREF(item);
		if (PyErr_Occurred())
			goto _fsum_error;

		xsave = x;
		for (i = j = 0; j < n; j++) {       /* for y in partials */
			y = p[j];
			if (fabs(x) < fabs(y)) {
				t = x; x = y; y = t;
			}
			hi = x + y;
			yr = hi - x;
			lo = y - yr;
			if (lo != 0.0)
				p[i++] = lo;
			x = hi;
		}

		n = i;                              /* ps[i:] = [x] */
		if (x != 0.0) {
			if (! Py_IS_FINITE(x)) {
				/* a nonfinite x could arise either as
				   a result of intermediate overflow, or
				   as a result of a nan or inf in the
				   summands */
				if (Py_IS_FINITE(xsave)) {
					PyErr_SetString(PyExc_OverflowError,
					      "intermediate overflow in fsum");
					goto _fsum_error;
				}
				if (Py_IS_INFINITY(xsave))
					inf_sum += xsave;
				special_sum += xsave;
				/* reset partials */
				n = 0;
			}
			else if (n >= m && _fsum_realloc(&p, n, ps, &m))
				goto _fsum_error;
			else
				p[n++] = x;
		}
	}

	if (special_sum != 0.0) {
		if (Py_IS_NAN(inf_sum))
			PyErr_SetString(PyExc_ValueError,
					"-inf + inf in fsum");
		else
			sum = PyFloat_FromDouble(special_sum);
		goto _fsum_error;
	}

	hi = 0.0;
	if (n > 0) {
		hi = p[--n];
		/* sum_exact(ps, hi) from the top, stop when the sum becomes
		   inexact. */
		while (n > 0) {
			x = hi;
			y = p[--n];
			assert(fabs(y) < fabs(x));
			hi = x + y;
			yr = hi - x;
			lo = y - yr;
			if (lo != 0.0)
				break;
		}
		/* Make half-even rounding work across multiple partials.
		   Needed so that sum([1e-16, 1, 1e16]) will round-up the last
		   digit to two instead of down to zero (the 1e-16 makes the 1
		   slightly closer to two).  With a potential 1 ULP rounding
		   error fixed-up, math.fsum() can guarantee commutativity. */
		if (n > 0 && ((lo < 0.0 && p[n-1] < 0.0) ||
			      (lo > 0.0 && p[n-1] > 0.0))) {
			y = lo * 2.0;
			x = hi + y;
			yr = x - hi;
			if (y == yr)
				hi = x;
		}
	}
	sum = PyFloat_FromDouble(hi);

_fsum_error:
	PyFPE_END_PROTECT(hi)
	Py_DECREF(iter);
	if (p != ps)
		PyMem_Free(p);
	return sum;
}

#undef NUM_PARTIALS

PyDoc_STRVAR(math_fsum_doc,
"sum(iterable)\n\n\
Return an accurate floating point sum of values in the iterable.\n\
Assumes IEEE-754 floating point arithmetic.");

static PyObject *
math_factorial(PyObject *self, PyObject *arg)
{
	long i, x;
	PyObject *result, *iobj, *newresult;

	if (PyFloat_Check(arg)) {
		double dx = PyFloat_AS_DOUBLE((PyFloatObject *)arg);
		if (dx != floor(dx)) {
			PyErr_SetString(PyExc_ValueError, 
				"factorial() only accepts integral values");
			return NULL;
		}
	}

	x = PyInt_AsLong(arg);
	if (x == -1 && PyErr_Occurred())
		return NULL;
	if (x < 0) {
		PyErr_SetString(PyExc_ValueError, 
			"factorial() not defined for negative values");
		return NULL;
	}

	result = (PyObject *)PyInt_FromLong(1);
	if (result == NULL)
		return NULL;
	for (i=1 ; i<=x ; i++) {
		iobj = (PyObject *)PyInt_FromLong(i);
		if (iobj == NULL)
			goto error;
		newresult = PyNumber_Multiply(result, iobj);
		Py_DECREF(iobj);
		if (newresult == NULL)
			goto error;
		Py_DECREF(result);
		result = newresult;
	}
	return result;

error:
	Py_DECREF(result);
	return NULL;
}

PyDoc_STRVAR(math_factorial_doc,
"factorial(x) -> Integral\n"
"\n"
"Find x!. Raise a ValueError if x is negative or non-integral.");

static PyObject *
math_trunc(PyObject *self, PyObject *number)
{
	return PyObject_CallMethod(number, "__trunc__", NULL);
}

PyDoc_STRVAR(math_trunc_doc,
"trunc(x:Real) -> Integral\n"
"\n"
"Truncates x to the nearest Integral toward 0. Uses the __trunc__ magic method.");

static PyObject *
math_frexp(PyObject *self, PyObject *arg)
{
	int i;
	double x = PyFloat_AsDouble(arg);
	if (x == -1.0 && PyErr_Occurred())
		return NULL;
	/* deal with special cases directly, to sidestep platform
	   differences */
	if (Py_IS_NAN(x) || Py_IS_INFINITY(x) || !x) {
		i = 0;
	}
	else {
		PyFPE_START_PROTECT("in math_frexp", return 0);
		x = frexp(x, &i);
		PyFPE_END_PROTECT(x);
	}
	return Py_BuildValue("(di)", x, i);
}

PyDoc_STRVAR(math_frexp_doc,
"frexp(x)\n"
"\n"
"Return the mantissa and exponent of x, as pair (m, e).\n"
"m is a float and e is an int, such that x = m * 2.**e.\n"
"If x is 0, m and e are both 0.  Else 0.5 <= abs(m) < 1.0.");

static PyObject *
math_ldexp(PyObject *self, PyObject *args)
{
	double x, r;
	PyObject *oexp;
	long exp;
	if (! PyArg_ParseTuple(args, "dO:ldexp", &x, &oexp))
		return NULL;

	if (PyLong_Check(oexp)) {
		/* on overflow, replace exponent with either LONG_MAX
		   or LONG_MIN, depending on the sign. */
		exp = PyLong_AsLong(oexp);
		if (exp == -1 && PyErr_Occurred()) {
			if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
				if (Py_SIZE(oexp) < 0) {
					exp = LONG_MIN;
				}
				else {
					exp = LONG_MAX;
				}
				PyErr_Clear();
			}
			else {
				/* propagate any unexpected exception */
				return NULL;
			}
		}
	}
	else if (PyInt_Check(oexp)) {
		exp = PyInt_AS_LONG(oexp);
	}
	else {
		PyErr_SetString(PyExc_TypeError,
				"Expected an int or long as second argument "
				"to ldexp.");
		return NULL;
	}

	if (x == 0. || !Py_IS_FINITE(x)) {
		/* NaNs, zeros and infinities are returned unchanged */
		r = x;
		errno = 0;
	} else if (exp > INT_MAX) {
		/* overflow */
		r = copysign(Py_HUGE_VAL, x);
		errno = ERANGE;
	} else if (exp < INT_MIN) {
		/* underflow to +-0 */
		r = copysign(0., x);
		errno = 0;
	} else {
		errno = 0;
		PyFPE_START_PROTECT("in math_ldexp", return 0);
		r = ldexp(x, (int)exp);
		PyFPE_END_PROTECT(r);
		if (Py_IS_INFINITY(r))
			errno = ERANGE;
	}

	if (errno && is_error(r))
		return NULL;
	return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_ldexp_doc,
"ldexp(x, i) -> x * (2**i)");

static PyObject *
math_modf(PyObject *self, PyObject *arg)
{
	double y, x = PyFloat_AsDouble(arg);
	if (x == -1.0 && PyErr_Occurred())
		return NULL;
	/* some platforms don't do the right thing for NaNs and
	   infinities, so we take care of special cases directly. */
	if (!Py_IS_FINITE(x)) {
		if (Py_IS_INFINITY(x))
			return Py_BuildValue("(dd)", copysign(0., x), x);
		else if (Py_IS_NAN(x))
			return Py_BuildValue("(dd)", x, x);
	}          

	errno = 0;
	PyFPE_START_PROTECT("in math_modf", return 0);
	x = modf(x, &y);
	PyFPE_END_PROTECT(x);
	return Py_BuildValue("(dd)", x, y);
}

PyDoc_STRVAR(math_modf_doc,
"modf(x)\n"
"\n"
"Return the fractional and integer parts of x.  Both results carry the sign\n"
"of x and are floats.");

/* A decent logarithm is easy to compute even for huge longs, but libm can't
   do that by itself -- loghelper can.  func is log or log10, and name is
   "log" or "log10".  Note that overflow isn't possible:  a long can contain
   no more than INT_MAX * SHIFT bits, so has value certainly less than
   2**(2**64 * 2**16) == 2**2**80, and log2 of that is 2**80, which is
   small enough to fit in an IEEE single.  log and log10 are even smaller.
*/

static PyObject*
loghelper(PyObject* arg, double (*func)(double), char *funcname)
{
	/* If it is long, do it ourselves. */
	if (PyLong_Check(arg)) {
		double x;
		int e;
		x = _PyLong_AsScaledDouble(arg, &e);
		if (x <= 0.0) {
			PyErr_SetString(PyExc_ValueError,
					"math domain error");
			return NULL;
		}
		/* Value is ~= x * 2**(e*PyLong_SHIFT), so the log ~=
		   log(x) + log(2) * e * PyLong_SHIFT.
		   CAUTION:  e*PyLong_SHIFT may overflow using int arithmetic,
		   so force use of double. */
		x = func(x) + (e * (double)PyLong_SHIFT) * func(2.0);
		return PyFloat_FromDouble(x);
	}

	/* Else let libm handle it by itself. */
	return math_1(arg, func, 0);
}

static PyObject *
math_log(PyObject *self, PyObject *args)
{
	PyObject *arg;
	PyObject *base = NULL;
	PyObject *num, *den;
	PyObject *ans;

	if (!PyArg_UnpackTuple(args, "log", 1, 2, &arg, &base))
		return NULL;

	num = loghelper(arg, m_log, "log");
	if (num == NULL || base == NULL)
		return num;

	den = loghelper(base, m_log, "log");
	if (den == NULL) {
		Py_DECREF(num);
		return NULL;
	}

	ans = PyNumber_Divide(num, den);
	Py_DECREF(num);
	Py_DECREF(den);
	return ans;
}

PyDoc_STRVAR(math_log_doc,
"log(x[, base]) -> the logarithm of x to the given base.\n\
If the base not specified, returns the natural logarithm (base e) of x.");

static PyObject *
math_log10(PyObject *self, PyObject *arg)
{
	return loghelper(arg, m_log10, "log10");
}

PyDoc_STRVAR(math_log10_doc,
"log10(x) -> the base 10 logarithm of x.");

static PyObject *
math_fmod(PyObject *self, PyObject *args)
{
	PyObject *ox, *oy;
	double r, x, y;
	if (! PyArg_UnpackTuple(args, "fmod", 2, 2, &ox, &oy))
		return NULL;
	x = PyFloat_AsDouble(ox);
	y = PyFloat_AsDouble(oy);
	if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
		return NULL;
	/* fmod(x, +/-Inf) returns x for finite x. */
	if (Py_IS_INFINITY(y) && Py_IS_FINITE(x))
		return PyFloat_FromDouble(x);
	errno = 0;
	PyFPE_START_PROTECT("in math_fmod", return 0);
	r = fmod(x, y);
	PyFPE_END_PROTECT(r);
	if (Py_IS_NAN(r)) {
		if (!Py_IS_NAN(x) && !Py_IS_NAN(y))
			errno = EDOM;
		else
			errno = 0;
	}
	if (errno && is_error(r))
		return NULL;
	else
		return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_fmod_doc,
"fmod(x,y)\n\nReturn fmod(x, y), according to platform C."
"  x % y may differ.");

static PyObject *
math_hypot(PyObject *self, PyObject *args)
{
	PyObject *ox, *oy;
	double r, x, y;
	if (! PyArg_UnpackTuple(args, "hypot", 2, 2, &ox, &oy))
		return NULL;
	x = PyFloat_AsDouble(ox);
	y = PyFloat_AsDouble(oy);
	if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
		return NULL;
	/* hypot(x, +/-Inf) returns Inf, even if x is a NaN. */
	if (Py_IS_INFINITY(x))
		return PyFloat_FromDouble(fabs(x));
	if (Py_IS_INFINITY(y))
		return PyFloat_FromDouble(fabs(y));
	errno = 0;
	PyFPE_START_PROTECT("in math_hypot", return 0);
	r = hypot(x, y);
	PyFPE_END_PROTECT(r);
	if (Py_IS_NAN(r)) {
		if (!Py_IS_NAN(x) && !Py_IS_NAN(y))
			errno = EDOM;
		else
			errno = 0;
	}
	else if (Py_IS_INFINITY(r)) {
		if (Py_IS_FINITE(x) && Py_IS_FINITE(y))
			errno = ERANGE;
		else
			errno = 0;
	}
	if (errno && is_error(r))
		return NULL;
	else
		return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_hypot_doc,
"hypot(x,y)\n\nReturn the Euclidean distance, sqrt(x*x + y*y).");

/* pow can't use math_2, but needs its own wrapper: the problem is
   that an infinite result can arise either as a result of overflow
   (in which case OverflowError should be raised) or as a result of
   e.g. 0.**-5. (for which ValueError needs to be raised.)
*/

static PyObject *
math_pow(PyObject *self, PyObject *args)
{
	PyObject *ox, *oy;
	double r, x, y;
	int odd_y;

	if (! PyArg_UnpackTuple(args, "pow", 2, 2, &ox, &oy))
		return NULL;
	x = PyFloat_AsDouble(ox);
	y = PyFloat_AsDouble(oy);
	if ((x == -1.0 || y == -1.0) && PyErr_Occurred())
		return NULL;

	/* deal directly with IEEE specials, to cope with problems on various
	   platforms whose semantics don't exactly match C99 */
	r = 0.; /* silence compiler warning */
	if (!Py_IS_FINITE(x) || !Py_IS_FINITE(y)) {
		errno = 0;
		if (Py_IS_NAN(x))
			r = y == 0. ? 1. : x; /* NaN**0 = 1 */
		else if (Py_IS_NAN(y))
			r = x == 1. ? 1. : y; /* 1**NaN = 1 */
		else if (Py_IS_INFINITY(x)) {
			odd_y = Py_IS_FINITE(y) && fmod(fabs(y), 2.0) == 1.0;
			if (y > 0.)
				r = odd_y ? x : fabs(x);
			else if (y == 0.)
				r = 1.;
			else /* y < 0. */
				r = odd_y ? copysign(0., x) : 0.;
		}
		else if (Py_IS_INFINITY(y)) {
			if (fabs(x) == 1.0)
				r = 1.;
			else if (y > 0. && fabs(x) > 1.0)
				r = y;
			else if (y < 0. && fabs(x) < 1.0) {
				r = -y; /* result is +inf */
				if (x == 0.) /* 0**-inf: divide-by-zero */
					errno = EDOM;
			}
			else
				r = 0.;
		}
	}
	else {
		/* let libm handle finite**finite */
		errno = 0;
		PyFPE_START_PROTECT("in math_pow", return 0);
		r = pow(x, y);
		PyFPE_END_PROTECT(r);
		/* a NaN result should arise only from (-ve)**(finite
		   non-integer); in this case we want to raise ValueError. */
		if (!Py_IS_FINITE(r)) {
			if (Py_IS_NAN(r)) {
				errno = EDOM;
			}
			/* 
			   an infinite result here arises either from:
			   (A) (+/-0.)**negative (-> divide-by-zero)
			   (B) overflow of x**y with x and y finite
			*/
			else if (Py_IS_INFINITY(r)) {
				if (x == 0.)
					errno = EDOM;
				else
					errno = ERANGE;
			}
		}
	}

	if (errno && is_error(r))
		return NULL;
	else
		return PyFloat_FromDouble(r);
}

PyDoc_STRVAR(math_pow_doc,
"pow(x,y)\n\nReturn x**y (x to the power of y).");

static const double degToRad = Py_MATH_PI / 180.0;
static const double radToDeg = 180.0 / Py_MATH_PI;

static PyObject *
math_degrees(PyObject *self, PyObject *arg)
{
	double x = PyFloat_AsDouble(arg);
	if (x == -1.0 && PyErr_Occurred())
		return NULL;
	return PyFloat_FromDouble(x * radToDeg);
}

PyDoc_STRVAR(math_degrees_doc,
"degrees(x) -> converts angle x from radians to degrees");

static PyObject *
math_radians(PyObject *self, PyObject *arg)
{
	double x = PyFloat_AsDouble(arg);
	if (x == -1.0 && PyErr_Occurred())
		return NULL;
	return PyFloat_FromDouble(x * degToRad);
}

PyDoc_STRVAR(math_radians_doc,
"radians(x) -> converts angle x from degrees to radians");

static PyObject *
math_isnan(PyObject *self, PyObject *arg)
{
	double x = PyFloat_AsDouble(arg);
	if (x == -1.0 && PyErr_Occurred())
		return NULL;
	return PyBool_FromLong((long)Py_IS_NAN(x));
}

PyDoc_STRVAR(math_isnan_doc,
"isnan(x) -> bool\n\
Checks if float x is not a number (NaN)");

static PyObject *
math_isinf(PyObject *self, PyObject *arg)
{
	double x = PyFloat_AsDouble(arg);
	if (x == -1.0 && PyErr_Occurred())
		return NULL;
	return PyBool_FromLong((long)Py_IS_INFINITY(x));
}

PyDoc_STRVAR(math_isinf_doc,
"isinf(x) -> bool\n\
Checks if float x is infinite (positive or negative)");

static PyMethodDef math_methods[] = {
	{"acos",	math_acos,	METH_O,		math_acos_doc},
	{"acosh",	math_acosh,	METH_O,		math_acosh_doc},
	{"asin",	math_asin,	METH_O,		math_asin_doc},
	{"asinh",	math_asinh,	METH_O,		math_asinh_doc},
	{"atan",	math_atan,	METH_O,		math_atan_doc},
	{"atan2",	math_atan2,	METH_VARARGS,	math_atan2_doc},
	{"atanh",	math_atanh,	METH_O,		math_atanh_doc},
	{"ceil",	math_ceil,	METH_O,		math_ceil_doc},
	{"copysign",	math_copysign,	METH_VARARGS,	math_copysign_doc},
	{"cos",		math_cos,	METH_O,		math_cos_doc},
	{"cosh",	math_cosh,	METH_O,		math_cosh_doc},
	{"degrees",	math_degrees,	METH_O,		math_degrees_doc},
	{"exp",		math_exp,	METH_O,		math_exp_doc},
	{"fabs",	math_fabs,	METH_O,		math_fabs_doc},
	{"factorial",	math_factorial,	METH_O,		math_factorial_doc},
	{"floor",	math_floor,	METH_O,		math_floor_doc},
	{"fmod",	math_fmod,	METH_VARARGS,	math_fmod_doc},
	{"frexp",	math_frexp,	METH_O,		math_frexp_doc},
	{"fsum",	math_fsum,	METH_O,		math_fsum_doc},
	{"hypot",	math_hypot,	METH_VARARGS,	math_hypot_doc},
	{"isinf",	math_isinf,	METH_O,		math_isinf_doc},
	{"isnan",	math_isnan,	METH_O,		math_isnan_doc},
	{"ldexp",	math_ldexp,	METH_VARARGS,	math_ldexp_doc},
	{"log",		math_log,	METH_VARARGS,	math_log_doc},
	{"log1p",	math_log1p,	METH_O,		math_log1p_doc},
	{"log10",	math_log10,	METH_O,		math_log10_doc},
	{"modf",	math_modf,	METH_O,		math_modf_doc},
	{"pow",		math_pow,	METH_VARARGS,	math_pow_doc},
	{"radians",	math_radians,	METH_O,		math_radians_doc},
	{"sin",		math_sin,	METH_O,		math_sin_doc},
	{"sinh",	math_sinh,	METH_O,		math_sinh_doc},
	{"sqrt",	math_sqrt,	METH_O,		math_sqrt_doc},
	{"tan",		math_tan,	METH_O,		math_tan_doc},
	{"tanh",	math_tanh,	METH_O,		math_tanh_doc},
	{"trunc",	math_trunc,	METH_O,		math_trunc_doc},
	{NULL,		NULL}		/* sentinel */
};


PyDoc_STRVAR(module_doc,
"This module is always available.  It provides access to the\n"
"mathematical functions defined by the C standard.");

PyMODINIT_FUNC
initmath(void)
{
	PyObject *m;

	m = Py_InitModule3("math", math_methods, module_doc);
	if (m == NULL)
		goto finally;

	PyModule_AddObject(m, "pi", PyFloat_FromDouble(Py_MATH_PI));
	PyModule_AddObject(m, "e", PyFloat_FromDouble(Py_MATH_E));

    finally:
	return;
}
