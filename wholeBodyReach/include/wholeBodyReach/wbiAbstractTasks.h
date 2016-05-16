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

#ifndef WBI_ABSTRACT_TASK
#define WBI_ABSTRACT_TASK

#include <wbi/wholeBodyInterface.h>
#include <wbi/wbiUtil.h>
#include <wholeBodyReach/wholeBodyReachConstants.h>
#include <wholeBodyReach/wholeBodyReachUtils.h>
#include <paramHelp/paramHelperServer.h>
#include <Eigen/Core>               // import most common Eigen types
#include <string>


namespace wholeBodyReach
{
  
static const double NO_LOWER_BOUND = -std::numeric_limits<double>::infinity();
static const double NO_UPPER_BOUND =  std::numeric_limits<double>::infinity();

struct RobotState
{
    Eigen::VectorXd     torques;    // joint torques in Nm (n)
    Eigen::VectorXd     qJ;         // joint positions in rad (n)
    Eigen::VectorXd     dqJ;        // joint velocities in rad/s (n)
    wbi::Frame          xBase;      // homogeneous transformation from world to base frame
    Eigen::Vector6d     vBase;      // base velocities (6)
    Eigen::VectorXd     dq;         // base and joint velocities (6+n)
    Eigen::Vector3d     g;          // gravity acceleration in m/s^2 (3)
    
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    
    /** Resize all the data of this object and initialize them to zero.
      * Moreover it initializes the gravity acceleration to [0 0 -9.8]. */
    bool init(int n)
    {
        if(n<0)
            return false;
        torques.setZero(n);
        qJ.setZero(n);     // joint positions (rad)
        dqJ.setZero(n);    // joint velocities (rad/s)
        dq.setZero(n+6);   // base+joint velocities
        g(0) = g(1) = 0.0;
        g(2) = -9.81;
        return true;
    }
};
    
    
/** An abstract task for a robot. */
class WbiAbstractTask
{
protected:
    int                         _m;         /// Size of the task
    std::string                 _name;      /// Name of the task
    wbi::wholeBodyInterface*    _robot;     /// Interface to read robot sensors and perform geometric/dynamic computations
    
public:
    /** Constructor. 
      * @param taskName The name associated to this task (just an identifier).
      * @param taskSize The size of this task.
      * @param robot An instance of wholeBodyInterface to interact with the robot.
     */
    WbiAbstractTask(std::string taskName, int taskSize, wbi::wholeBodyInterface* robot)
    : _name(taskName), _m(taskSize), _robot(robot) {}
    
    /** Update all the quantity related to the task. 
      * @return True if the operation succeeded, false otherwise.
      */
    virtual bool update(RobotState& state) = 0;
    
    /** Get the size of the task. */
    virtual int getSize()
    { return _m; }

    /** Get the name of the task. */
    virtual const std::string& getName()
    { return _name; }
    
    /** Get the name of the task. */
    virtual void setName(const std::string &newTaskName)
    { _name = newTaskName; }
};


/** A task described by an affine equality:
  * A x = a
  */
class WbiEqualityTask
{
protected:
    Eigen::MatrixRXd   _A_eq;      /// Equality matrix
    Eigen::VectorXd    _a_eq;      /// Equality vector
    
public:
    /** Constructor.
     * @param taskSize The size of this task.
     * @param n Number of DoFs of the robot
     */
    WbiEqualityTask(int taskSize, int n)
    {
        _A_eq.setZero(taskSize, n);
        _a_eq.setZero(taskSize);
    }
    
    /** Get the current matrix of the equality. */
    virtual void getEqualityMatrix(Eigen::MatrixRef A)
    {
        assert(A.cols()==_A_eq.cols());
        assert(A.rows()==_A_eq.rows());
        A = _A_eq;
    }
    
    /** Get the current matrix of the equality. */
    virtual const Eigen::MatrixRXd& getEqualityMatrix()
    { return _A_eq; }
    
    /** Get the current vector of the equality. */
    virtual void getEqualityVector(Eigen::VectorRef a)
    {
        assert(a.size()==_a_eq.size());
        a = _a_eq;
    }
    
    /** Get the current vector of the equality. */
    virtual const Eigen::VectorXd& getEqualityVector()
    { return _a_eq; }
};

    
/** A task described by an affine inequality:
  * A x + a >= 0
  */
class WbiInequalityTask
{
protected:
    Eigen::MatrixRXd   _A_in;      /// Inequality matrix
    Eigen::VectorXd    _a_in;      /// Inequality upper bound vector
    
public:
    /** Constructor.
     * @param taskSize The size of this task.
     */
    WbiInequalityTask(int numberOfInequalities, int n)
    {
        _A_in.setZero(numberOfInequalities, n);
        _a_in.setZero(numberOfInequalities);
    }
    
    virtual int getNumberOfInequalities()
    { return _a_in.size(); }
    
    /** Get the current matrix of the inequality. */
    virtual void getInequalityMatrix(Eigen::MatrixRef A)
    {
        assert(A.cols()==_A_in.cols());
        assert(A.rows()==_A_in.rows());
        A = _A_in;
    }
    
    /** Get the current matrix of the inequality. */
    virtual const Eigen::MatrixRXd& getInequalityMatrix()
    { return _A_in; }

    /** Get the current vector of the inequality. */
    virtual void getInequalityVector(Eigen::VectorRef a)
    {
        assert(a.size()==_a_in.size());
        a = _a_in;
    }
    
    /** Get the current upper bound vector of the inequality. */
    virtual const Eigen::VectorXd& getInequalityVector()
    { return _a_in; }
};
    
    
/** An equality task with a proportional-derivative
  * control law. The proportional and derivative gains 
  * are supposed to be diagonal matrices, so they are
  * stored as vectors.
  */
class WbiPDTask : virtual public paramHelp::ParamValueObserver
{
    /* Use "virtual" inheritance to solve the diamond problem.
     * See http://www.deitel.com/articles/cplusplus_tutorials/20060225/virtualBaseClass/
     * for more information.
     */
protected:
    Eigen::VectorXd     _Kp;        /// Proportional gains
    Eigen::VectorXd     _Kd;        /// Derivative gains
    int                 _gainSize;  /// Size of the PD gain vectors
    bool                _automaticCriticallyDamped; /// if true, the gains are automatically adjusted so that
                                                    /// the system is critically damped. In other words,
                                                    /// when you set Kp, then Kd is automatically changed so that
                                                    /// Kd = 2*sqrt(Kp). Similarly, when you set Kd, then Kp is
                                                    /// automatically set so that Kp = 0.5*Kd^2
    int                 _paramId_Kp;    /// id of the paramater associated to the variable _Kp
    
    bool checkGainSize(Eigen::VectorConst v)
    { return v.size()==_gainSize; }
    
public:
    /** Constructor.
      * @param gainSize Size of the gain vectors.
      * @param automaticCriticallyDamped If true, gains are automatically adjusted to make system critically damped.
      */
    WbiPDTask(int gainSize, bool automaticCriticallyDamped=false)
    :   _gainSize(gainSize),
        _automaticCriticallyDamped(automaticCriticallyDamped),
        _paramId_Kp(-1)
    {
        _Kp.setZero(gainSize);
        _Kd.setZero(gainSize);
    }
    
    virtual ~WbiPDTask(){}
    
    /** Link the proportional gain of this object to a parameter managed by the specified 
      * instance of ParamHelperServer.
      */
    virtual void linkParameterKp(paramHelp::ParamHelperServer* paramHelper, int paramId);
    virtual void linkParameterKd(paramHelp::ParamHelperServer* paramHelper, int paramId);
    
    /** Method called every time a parameter (for which a callback is registered)
      * is changed. 
      */
    virtual void parameterUpdated(const paramHelp::ParamProxyInterface *pp);
    
    /** Get the current value of the proportional gains.
      * @param Kp Output vector to fill with the proportional gains.
     */
    virtual void getProportionalGain(Eigen::VectorRef Kp)
    { Kp = _Kp; }

    /** Get the current value of the proportional gains. */
    virtual const Eigen::VectorXd& getProportionalGain()
    { return _Kp; }
    
    /** Get the current value of the derivative gains.
     * @param Kd Output vector to fill with the derivative gains.
     */
    virtual void getDerivativeGain(Eigen::VectorRef Kd)
    { Kd = _Kd; }
    
    /** Get the current value of the derivative gains. */
    virtual const Eigen::VectorXd& getDerivativeGain()
    { return _Kd; }
    
    /** Get the expected size of the gain vectors. */
    virtual int getGainSize(){  return _gainSize; }
    
    /** Get the current value of the flag automaticCriticallyDamped.
      * If true, the gains are automatically adjusted so that
      * the system is critically damped. In other words,
      * when you set Kp, then Kd is automatically changed so that
      * Kd = 2*sqrt(Kp). Similarly, when you set Kd, then Kp is
      * automatically changed so that Kp = 0.5*Kd^2
      */
    virtual bool getAutomaticCriticallyDamped(){ return _automaticCriticallyDamped; }
    
    /** Set the current value of the proportional gains.
     * @param Kp Input vector containing the proportional gains.
     * @return True if the operation succeeded, false otherwise.
     */
    virtual bool setProportionalGain(Eigen::VectorConst Kp);
    
    /** Set the current value of the proportional gains.
     * @param Kd Input vector containing the derivative gains.
     * @return True if the operation succeeded, false otherwise.
     */
    virtual bool setDerivativeGain(Eigen::VectorConst Kd);

    /** Set the expected size of the gain vectors.
      * @return True if the operation succeeded, false otherwise.
      */
    virtual bool setGainSize(int gainSize);
    
    /** Set the current value of the flag automaticCriticallyDamped.
     * If true, the gains are automatically adjusted so that
     * the system is critically damped. In other words,
     * when you set Kp, then Kd is automatically changed so that
     * Kd = 2*sqrt(Kp). Similarly, when you set Kd, then Kp is
     * automatically changed so that Kp = 0.5*Kd^2
     */
    virtual void setAutomaticCriticallyDamped(bool value);
};
    
    
/** Compute the 6d error vector given the measured frame and the desired frame. */
void compute6DError(const wbi::Frame& H, const wbi::Frame& H_des, Eigen::VectorRef res);

/** Compute the 3d orientation error given the measured orientation and the desired orientation. */
void computeOrientationError(const wbi::Rotation& R, const wbi::Rotation& R_des, Eigen::VectorRef res);
    
} // end namespace wholeBodyReach


#endif
