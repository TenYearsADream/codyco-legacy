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

#ifndef _MOTOR_FRICTION_EXCITATION_THREAD
#define _MOTOR_FRICTION_EXCITATION_THREAD

#include <sstream>
#include <iomanip>
#include <vector>

#include <yarp/os/RateThread.h>
#include <yarp/os/ResourceFinder.h>

#include <Eigen/Core>                               // import most common Eigen types
#include <Eigen/SVD>

#include <wbi/wbi.h>
#include <paramHelp/paramHelperServer.h>
#include <paramHelp/paramHelperClient.h>
#include <motorFrictionExcitation/motorFrictionExcitationConstants.h>


using namespace std;
using namespace Eigen;
using namespace yarp::os;
using namespace paramHelp;
using namespace wbi;
using namespace motorFrictionExcitation;

namespace motorFrictionExcitation
{


enum MotorFrictionExcitationStatus
{
    EXCITATION_CONTACT,                 ///< a contact excitation is going on
    EXCITATION_CONTACT_FINISHED,        ///< a contact excitation has finished
    EXCITATION_FREE_MOTION,             ///< a free motion excitation is going on
    EXCITATION_FREE_MOTION_FINISHED,    ///< a free motion excitation has just finished
    EXCITATION_OFF                      ///< controller off (either the user stopped it or all excitations have finished)
};


/**
 * MotorFrictionExcitation control thread.
 */
class MotorFrictionExcitationThread: public RateThread, public ParamValueObserver, public CommandObserver
{
    string              name;                   ///< name of this module instance
    string              robotName;              ///< name of the robot
    ParamHelperServer   *paramHelper;           ///< manager of the module parameters
    wholeBodyInterface  *robot;                 ///< interface to communicate with the robot
    ParamHelperClient   *identificationModule;  ///< client to communicate with the motorFrictionIdentification module

    // Member variables
    MotorFrictionExcitationStatus   status;             ///< thread status ("on" when controlling, off otherwise)
    MFE_MotorCommandMode            sendCmdToMotors;    ///< specify whether to send commands to motors
    bool                isFrictionStdDevBelowThreshold; ///< true if during the last freeMotionExcitation the std dev went below threhsold
    int                 printCountdown;         // every time this is 0 (i.e. every PRINT_PERIOD ms) print stuff
    int                 _n;                     // number of joints of the robot
    int                 freeExcCounter;         // counter of how many free motion excitations have been performed
    int                 contactExcCounter;      // counter of how many contact excitations have been performed
    double              excitationStartTime;    // timestamp taken at the beginning of the current excitation
    ArrayXd             pwmOffset;              // pwm to keep motor still in the starting position
    ArrayXd             pwmDes;                 // desired values of PWM for the controlled joints (variable size)
    ArrayXd             posIntegral;            // integral of (q-q0), where q0 is the initial joint position
    ArrayXd             dqJ;                    // joint velocities (size of vector: n)
    ArrayXd             ftSens;                 // ankle force/torque sensor readings (order is: left, right)
    ArrayXi             currentJointIndeces; // Indeces of the joints in the global joint list currently excited
    vector<ID>       currentJointWbiIds;     // Wbi IDs of the joints currently excited
    //ArrayXi             currentGlobalJointIds;  // global IDs of the joints currently excited
    ArrayXd              qMinOfCurrentJointFME;  // Joint limit of the current joint during free motion
    ArrayXd              qMaxOfCurrentJointFME;  // Joint limit of the current joint during free motion

    // Module parameters
    FreeMotionExcitationList    freeMotionExc;  ///< free motion excitations
    ContactExcitationList           contactExc;     ///< in contact excitations
    ArrayXd             qPosMin, qPosMax;                 // lower and upper joint bounds

    // Output streaming parameters
    ArrayXd             qDeg, qRad;             // measured positions

    ///< Output monitoring parameters
    double              pwmDesSingleJoint;      // value of desired pwm for the first controlled joint
    double              qDegMonitor;            // value of the measured joint angle for the first controlled joint
    double              ktStdDevThrMonitor;     ///< standard deviation threshold for the param kt of the currently excited joint
    double              fricStdDevThrMonitor;   ///< standard deviation threshold for the friction parameters of the currently excited joint

    ///< Identification module parameters
    ArrayXi             activeJoints;
    string              monitoredJoint;
    struct
    {
        ArrayXd kt, kvp, kvn, kcp, kcn;
    } stdDev;

    /************************************************* PRIVATE METHODS ******************************************************/
    void sendMsg(const string &msg, MsgType msgType=MSG_INFO);

    /** Read the robot sensors and compute forward kinematics and Jacobians. */
    bool readRobotStatus(bool blockingRead=false);

    /** Update the reference trajectories to track and compute the desired velocities for all tasks. */
    bool updateReferenceTrajectories();

    /** Return true if the desired motor PWM are too large. */
    bool areDesiredMotorPwmTooLarge();

    /** Return true if at least one of the stop conditions is verified (e.g. joint limit too close). */
    bool checkFreeMotionStopConditions();

    /** Return true if the current contact excitation phase is finished (e.g. std dev of parameters is below threshold). */
    bool checkContactStopConditions();

    /** Send commands to the motors. Return true if the operation succeeded, false otherwise. */
    bool sendMotorCommands();

    /** Perform all the operations needed just before starting the controller.
     * @return True iff all initialization operations went fine, false otherwise. */
    bool preStartOperations();

    bool initFreeMotionExcitation();

    bool initContactExcitation();

    /** Perform all the operations needed just before stopping the controller. */
    void preStopOperations();

public:

    /* If you define a structure having members of fixed-size vectorizable Eigen types, you must overload
     * its "operator new" so that it generates 16-bytes-aligned pointers. Fortunately, Eigen provides you
     * with a macro EIGEN_MAKE_ALIGNED_OPERATOR_NEW that does that for you. */
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    MotorFrictionExcitationThread(string _name, string _robotName, int _period, ParamHelperServer *_ph, wholeBodyInterface *_wbi,
                                    ResourceFinder &rf, ParamHelperClient *_identificationModule);

    bool threadInit();
    void run();
    void threadRelease();

    /** Callback function for parameter updates. */
    void parameterUpdated(const ParamProxyInterface *pd);
    /** Callback function for rpc commands. */
    void commandReceived(const CommandDescription &cd, const Bottle &params, Bottle &reply);

    /** Start the excitation process. */
    inline void startExcitation()
    {
        paramHelper->lock();
        if(!preStartOperations())
            preStopOperations();
        paramHelper->unlock();
    }

};

/** Command the motors to move to the specified joint configuration and then wait until the motion is finished. */
bool moveToJointConfigurationAndWaitMotionDone(wbi::wholeBodyInterface *robot, double *qDes_deg, const int nDoF,
    double tolerance_deg=0.1, double *qMaxDeg=0, double *qMinDeg=0);

/** Wait for the specified joint configuration to be reached. */
bool waitMotionDone(wbi::iWholeBodyStates *robot, double *qDes_deg, const int nDoF, double tolerance_deg=0.1);

bool waitMotionDone(wbi::iWholeBodyStates *robot, double qDes_deg, const int &jointId, double tolerance_deg=0.1);

} // end namespace

#endif
