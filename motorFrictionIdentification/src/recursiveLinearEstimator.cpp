/* 
 * Copyright (C) 2013 CoDyCo
 * Author: Andrea Del Prete
 * email:  andrea.delprete@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <motorFrictionIdentification/recursiveLinearEstimator.h>
#include <cstdio>

using namespace std;
using namespace Eigen;
using namespace motorFrictionIdentification;

RecursiveLinearEstimator::RecursiveLinearEstimator(unsigned int nParam) 
    : n(nParam), R(n), sampleCount(0)
{ 
    resizeAllVariables();
}

/*************************************************************************************************/
void RecursiveLinearEstimator::feedSample(const VectorXd &input, const double &output)
{
    assert(checkDomainSize(input));
    ///< update the Cholesky decomposition of the inverse covariance matrix
    R.rankUpdate(input);
    ///< update the right hand side of the equation
    b += input*output;
    sampleCount++;
}

/*************************************************************************************************/
void RecursiveLinearEstimator::predictOutput(const VectorXd &input, double &output) const
{
    assert(checkDomainSize(input));
    output = input.dot(x);
}

/*************************************************************************************************/
void RecursiveLinearEstimator::getParameterEstimate(VectorXd &xEst) const
{
    assert(checkDomainSize(xEst));
    xEst = x;
}

/*************************************************************************************************/
void RecursiveLinearEstimator::getCovarianceMatrix(MatrixXd &sigma) const
{
    assert(sigma.cols()==n && sigma.rows()==n);
    ///< if there are not enough sample to perform the estimation set covariance a very high value
    ///< @todo Rather than checking the # of sample I should check the rank of the inverse covariance matrix A
    if(sampleCount<(int)n)  
    {
        sigma = MatrixXd::Constant(n,n,1e10);
        return;
    }
    ///< Invert A by solving n times the system: A*x=e_i, 
    ///< where e_i is a vector with all elements equal to 0, except for the i-th element, which is equal to 1
    VectorXd e_i(n);
    for(unsigned int i=0; i<n; i++)
    {
        e_i.setZero();
        e_i[i] = 1.0;
        if(!R.solveInPlace(e_i))
            printf("Error while computing covariance matrix in loop %d\n", i);
        sigma.col(i) = e_i;

        // If the covariance is exactly zero it means that there are not enough samples to estimate
        // the relative parameter, so actually the covariance is infinite
        if(sigma(i,i)==0.0)
            sigma(i,i) = 1e10;
    }
}

/*************************************************************************************************/
void RecursiveLinearEstimator::getParameterEstimate(VectorXd &xEst, MatrixXd &sigma) const
{
    assert(sigma.cols()==n && sigma.rows()==n);
    getParameterEstimate(xEst);
    getCovarianceMatrix(sigma);
}

/*************************************************************************************************/
void RecursiveLinearEstimator::getEstimationState(MatrixXd &A, VectorXd &bOut) const
{
    assert(A.cols()==n && A.rows()==n);
    assert(b.size()==n);
    A = R.matrixLDLT();
    bOut = b;
}

/*************************************************************************************************/
void RecursiveLinearEstimator::setEstimationState(const MatrixXd &A, const VectorXd &bNew)
{
    assert(A.cols()==n && A.rows()==n);
    assert(b.size()==n);
    R.compute(A);
    b = bNew;
}

/*************************************************************************************************/
void RecursiveLinearEstimator::updateParameterEstimate()
{
    x = b;
    bool res = R.solveInPlace(x);
    assert(res);
}

/*************************************************************************************************/
void RecursiveLinearEstimator::resizeAllVariables()
{
    R.setZero();
    R.compute(MatrixXd::Zero(n,n));
    b.resize(n);
    b.setZero();
    x.resize(n);
    x.setZero();
}
