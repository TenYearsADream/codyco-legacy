/* 
 * Copyright (C) 2012
 * Author: Silvio Traversaro
 * email:  pegua1@gmail.com
 * website: www.robotcub.org
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

/*
 * \todo add semaphores
 */

#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <yarp/dev/all.h>
#include <yarp/math/api.h>
#include <yarp/math/SVD.h>

#include <iCub/ctrl/math.h>
#include <iCub/ctrl/adaptWinPolyEstimator.h>
#include <iCub/iDyn/iDyn.h>
#include <iCub/iDyn/iDynBody.h>
#include <iCub/iDyn/iDynRegressor.h>

#include <iCub/learningMachine/MultiTaskLinearGPRLearner.h>
#include <iCub/learningMachine/MultiTaskLinearGPRLearnerFixedParameters.h>
#include <iCub/learningMachine/MultiTaskLinearFixedParameters.h>


#include <iostream>
#include <iomanip>
#include <string.h>
#include <algorithm>
#include <limits>
#include <fstream>


#include "observerThread.h"
#include "MatVetIO.h"

using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace yarp::dev;
using namespace iCub::ctrl;
using namespace iCub::iDyn;
using namespace iCub::iDyn::Regressor;
using namespace std;
using namespace iCub::learningmachine;



double lpf_ord1_3hz(double input, int j)
{ 
    if (j<0 || j>= MAX_JN)
    {
        cout<<"Received an invalid joint index to filter"<<endl;
        return 0;
    }

    static double xv[MAX_FILTER_ORDER][MAX_JN];
    static double yv[MAX_FILTER_ORDER][MAX_JN];
    xv[0][j] = xv[1][j] ; 
    xv[1][j] = input / 1.157889499e+01;
    yv[0][j] = yv[1][j] ; 
    yv[1][j] =   (xv[0][j]  + xv[1][j] ) + (  0.8272719460 * yv[0][j] );
    return (yv[1][j]);
}

void printVector_ofstream(ofstream & file, const Vector& vet, int precision)
{
        for(int c=0;c<(int)vet.size();c++)
            {
                file << vet(c) << endl;
            }
}

inertiaObserver_thread::inertiaObserver_thread(int _rate,int _rateEstimation, 
                                                string _robot_name, string _local_name, 
                                                version_tag _icub_type, string _data_path, 
                                                bool _autoconnect, bool _right_leg_enabled, 
                                                bool _left_leg_enabled, bool _right_arm_enabled, 
                                                bool _left_arm_enabled,bool _debug_out_enabled, 
                                                bool _dump_static, string _xml_yarpscope_file) : RateThread(_rate), rateEstimation(_rateEstimation), robot_name(_robot_name), local_name(_local_name), icub_type(_icub_type), data_path(_data_path), autoconnect(_autoconnect), right_leg_enabled(_right_leg_enabled), left_leg_enabled(_left_leg_enabled), right_arm_enabled(_right_arm_enabled), left_arm_enabled(_left_arm_enabled), debug_out_enabled(_debug_out_enabled), dump_static(_dump_static), xml_yarpscope_file(_xml_yarpscope_file)
{
    //ugly, change ASAP todo
    //Information on iCub/FT sensor structure
    vectorLimbs.push_back(ICUB_HEAD);
    vectorLimbs.push_back(ICUB_RIGHT_ARM);
    vectorLimbs.push_back(ICUB_LEFT_ARM);
    vectorLimbs.push_back(ICUB_TORSO);
    vectorLimbs.push_back(ICUB_RIGHT_LEG);
    vectorLimbs.push_back(ICUB_LEFT_LEG);
    
    limbNames[ICUB_HEAD] = "head";
    limbNames[ICUB_TORSO] = "torso";
    limbNames[ICUB_RIGHT_ARM] = "right_arm";
    limbNames[ICUB_LEFT_ARM] = "left_arm";
    limbNames[ICUB_RIGHT_LEG] = "right_leg";
    limbNames[ICUB_LEFT_LEG] = "left_leg";
        
    vectorFT.push_back(ICUB_FT_RIGHT_ARM);
    vectorFT.push_back(ICUB_FT_LEFT_ARM);
    vectorFT.push_back(ICUB_FT_RIGHT_LEG);
    vectorFT.push_back(ICUB_FT_LEFT_LEG);
    
    FTNames[ICUB_FT_RIGHT_ARM] = "right_arm";
    FTNames[ICUB_FT_LEFT_ARM] = "left_arm";
    FTNames[ICUB_FT_RIGHT_LEG] = "right_leg";
    FTNames[ICUB_FT_LEFT_LEG] = "left_leg";
    
    FTlimb[ICUB_FT_RIGHT_ARM] = ICUB_RIGHT_ARM;
    FTlimb[ICUB_FT_LEFT_ARM] = ICUB_LEFT_ARM;
    FTlimb[ICUB_FT_RIGHT_LEG] = ICUB_RIGHT_LEG;
    FTlimb[ICUB_FT_LEFT_LEG] = ICUB_LEFT_LEG;
        
    first = true;
    verbose = true;
    
    debug_out_parameters = false;
    
    produceAb = false;
    produceAb_contact = true;
    produceAb_motors = true;
    
    if( produceAb ) {
        Ab_sample_count = 0;
        local_Phi = Matrix(6,40);
        local_A = Matrix(6,46);
        local_b = Vector(6);
        
        A_file.open("A.csv");
        b_file.open("b.csv");
    }
    
    if( produceAb_contact ) {
        contact_file.open("contact.csv");
    }
    
    if( produceAb_motors ) {
        //Coupling matrix
        double r = 65/40;
        T_T = Matrix(3,3);
        T_T(0,0) = 1;
        T_T(0,1) = T_T(0,2) =-r;
        T_T(1,0) = T_T(2,0) = 0;
        T_T(1,1) = T_T(1,2) = T_T(2,2) = r;
        
        //Inertial parameters regressors
        Y_s_reduced = Matrix(6,40);
        Y_s_reduced.zero();
        Y_s_all = Matrix(6,70);
        Y_s_all.zero();
        Y_tau = Matrix(7,70);
        Y_tau.zero();
        Y_I = Matrix(3,70);
        Y_I.zero();
        Y_II = Matrix(2,70);
        Y_II.zero();
        
        //Friction regressors
        A_I = Matrix(3,12);
        A_I.zero();
        A_II = Matrix(1,4);
        A_II.zero();
        B_I = Matrix(3,12);
        B_I.zero();
        
        //PWM
        diagV_I = Matrix(3,3);
        diagV_I.zero();
        diagV_II = Matrix(1,1);
        diagV_II.zero();
        
        //everything
        megazord = Matrix(3+1+6,70+6+4*3+4*1+4*3+3+1);
        megazord.zero();
        
        A_file.open("A.csv");
        b_file.open("b.csv");
        

    }

    
    is_enabled[ICUB_HEAD] = true;
    if( left_leg_enabled || right_leg_enabled ) {
        is_enabled[ICUB_TORSO] = true;
    } else {
        is_enabled[ICUB_TORSO] = false;
    }
    is_enabled[ICUB_LEFT_ARM] = left_arm_enabled;
    is_enabled[ICUB_RIGHT_ARM] = right_arm_enabled;
    is_enabled[ICUB_LEFT_LEG] = left_leg_enabled;
    is_enabled[ICUB_RIGHT_LEG] = right_leg_enabled;
    
    //FT errors: 0.25 0.25 0.25 0.005 0.005 0.004
    //lin? FT  : 0.2  0.2  0.7  0.01  0.01  0.005
    ftStdDev[ICUB_FT_RIGHT_ARM] = Vector(6);
    
    /*
    ftStdDev[ICUB_FT_RIGHT_ARM][0] = 0.2;
    ftStdDev[ICUB_FT_RIGHT_ARM][1] = 0.2;
    ftStdDev[ICUB_FT_RIGHT_ARM][2] = 0.2;
    ftStdDev[ICUB_FT_RIGHT_ARM][3] = 0.01;
    ftStdDev[ICUB_FT_RIGHT_ARM][4] = 0.01;
    ftStdDev[ICUB_FT_RIGHT_ARM][5] = 0.005;
    */
    
    ftStdDev[ICUB_FT_RIGHT_ARM][0] = 0.0759966;
    ftStdDev[ICUB_FT_RIGHT_ARM][1] = 0.0893956;
    ftStdDev[ICUB_FT_RIGHT_ARM][2] = 0.1793871;
    ftStdDev[ICUB_FT_RIGHT_ARM][3] = 0.0019388; 
    ftStdDev[ICUB_FT_RIGHT_ARM][4] = 0.0028635; 
    ftStdDev[ICUB_FT_RIGHT_ARM][5] = 0.0014280;
    
    
    ftStdDev[ICUB_FT_LEFT_ARM] = ftStdDev[ICUB_FT_RIGHT_ARM];
    
    learning_enabled = false;

    
     

    //---------------------PORT--------------------------//
    port_inertial_thread=new BufferedPort<Vector>;
    
    for(vector<iCubLimb>::size_type i = 0; i != vectorLimbs.size(); i++) {
        if( is_enabled[vectorLimbs[i]] ) {
            port_q[vectorLimbs[i]] = new posCollector(vectorLimbs[i],&current_state_estimator);
            wasStill[vectorLimbs[i]] = false;
        }
    }
    
    for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
        if( is_enabled[FTlimb[vectorFT[i]]] ) {
            port_ft[vectorFT[i]] = new FTCollector(vectorFT[i],&current_state_estimator);
            if( dump_static ) {
                //onlineMeanFT[vectorFT[i]].reset();
                //onlineMeanRegr[vectorFT[i]].reset();
                static_dump_count[vectorFT[i]] = 0;
            }
        }
    }
    
    port_contact = new skinCollector(&current_state_estimator);
    
    port_pwm = new pwmCollector(ICUB_RIGHT_ARM,&current_state_estimator);

    port_inertial_thread->open(string("/"+local_name+"/inertial:i").c_str());
    
    
    for(vector<iCubLimb>::size_type i = 0; i != vectorLimbs.size(); i++) {
        if( is_enabled[vectorLimbs[i]] ) {
            port_q[vectorLimbs[i]]->useCallback();
            port_q[vectorLimbs[i]]->open(string("/"+local_name+"/"+limbNames[vectorLimbs[i]]+"/state:i").c_str());
        }
    }
    
    for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
        if( is_enabled[FTlimb[vectorFT[i]]] ) {
            port_ft[vectorFT[i]]->useCallback();
            port_ft[vectorFT[i]]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT:i").c_str());
        }
    }
    
    port_contact->useCallback();
    port_contact->open(string("/"+local_name+"/"+"skin_events:i").c_str());
    
    port_pwm->useCallback();
    port_pwm->open(string("/"+local_name+"/"+"pwm_monitor:i").c_str());
    

    if (autoconnect)
    {
        
        
        //from iCub to inertiaObserver
        
        
        //Network::connect(string("/"+local_name+"/filtered/inertial:o").c_str(),string("/"+local_name+"/inertial:i").c_str(),"tcp",false);			
        //Network::connect(string("/"+robot_name+"/inertial").c_str(),           string("/"+local_name+"/unfiltered/inertial:i").c_str(),"tcp",false);
            
        for(vector<iCubLimb>::size_type i = 0; i != vectorLimbs.size(); i++) {
            if( is_enabled[vectorLimbs[i]] ) {
                Network::connect(string("/"+robot_name+"/"+limbNames[vectorLimbs[i]]+"/state:o").c_str(), string("/"+local_name+"/"+limbNames[vectorLimbs[i]]+"/state:i").c_str(),"tcp",false);
            }
        }
    
        for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
            if( is_enabled[FTlimb[vectorFT[i]]] ) {
                Network::connect(string("/"+robot_name+"/"+FTNames[vectorFT[i]]+"/analog:o").c_str(),  string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT:i").c_str(),"tcp",false);
            }
        }
        
        Network::connect(string("/skinManager/skin_events:o").c_str(),string("/"+local_name+"/"+"skin_events:i").c_str(),"tcp",false);
        
        Network::connect(string("/torqueCtrlTest/monitor:o").c_str(),string("/"+local_name+"/"+"pwm_monitor:i").c_str(),"tcp",false);
    }
    
    for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
        if( is_enabled[FTlimb[vectorFT[i]]] ) {
            timestamp_lastFTsample_returned[vectorFT[i]] = -1.0;
        }
    }
    
    //----------INIT icub object--------------------//
    icub = new iCubWholeBody(icub_type, DYNAMIC);
    
    beta_cad_file.open("beta_cad.csv");
    Vector param_right_arm;
    iCubLimbGetBetaComplete(icub,"right_arm",param_right_arm);  
    YARP_ASSERT(param_right_arm.size() == 70);
    printVector_ofstream(beta_cad_file,param_right_arm,10);
    beta_cad_file.close();

}

bool inertiaObserver_thread::threadInit()
{
    fprintf(stderr,"threadInit: waiting for port connections... \n\n");
    
    fprintf(stderr,"threadInit: Initializing estimation... \n\n");
    initial_time = yarp::os::Time::now();
    
    N_samples = 0;
    
    if( debug_out_enabled ) {
        calibrateOffset();
    }

    fprintf(stderr,"threadInit: Calculating identifiable parameters \n\n");
    //Calculating identifiable parameters
    for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
        if( is_enabled[FTlimb[vectorFT[i]]] ) {
            identifiable_parameters[vectorFT[i]] = getiCubLimbIdentifiableSubspace(limbNames[FTlimb[vectorFT[i]]]);
            cerr    << "threadInit: FT " << limbNames[FTlimb[vectorFT[i]]] 
                    << ", identifiable parameters subspace size : " <<  identifiable_parameters[vectorFT[i]].cols() 
                    << " of " <<  identifiable_parameters[vectorFT[i]].rows() << endl;
            //if( dump_static ) {
                static_identifiable_parameters[vectorFT[i]] =  getiCubLimbIdentifiableSubspace(limbNames[FTlimb[vectorFT[i]]],true);
                cerr    << "threadInit: FT " << limbNames[FTlimb[vectorFT[i]]] 
                    << ", static identifiable parameters subspace size : " <<  static_identifiable_parameters[vectorFT[i]].cols() 
                    << " of " <<  static_identifiable_parameters[vectorFT[i]].rows() << endl; 
            //}
               dynamic_identifiable_parameters[vectorFT[i]] = getOnlyDynamicParam(identifiable_parameters[vectorFT[i]]);
                  cerr    << "threadInit: FT " << limbNames[FTlimb[vectorFT[i]]] 
                    << ", only dynamic identifiable parameters subspace size : " <<  dynamic_identifiable_parameters[vectorFT[i]].cols() 
                    << " of " <<  dynamic_identifiable_parameters[vectorFT[i]].rows() << endl; 
 
        }
    }
    
    //Initialing online estimation
    fprintf(stderr,"threadInit: Initializing online estimation \n\n");
    for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
        if( is_enabled[FTlimb[vectorFT[i]]] ) {
            IParameterLearner * param_learner;
            
            //Setting default method
            param_learner = new MultiTaskLinearGPRLearner(identifiable_parameters[vectorFT[i]].cols()+6,6);
            param_learner->setName("RLS");
            param_learner->setNoiseStandardDeviation(ftStdDev[vectorFT[i]]);
            //param_learner->setWeightsStandardDeviation(Vector(identifiable_parameters[vectorFT[i]].cols()+6,1.0));
            paramEstimators[vectorFT[i]].push_back(param_learner);
            params[vectorFT[i]].push_back(Vector());
            
            if( debug_out_enabled ) {
                /*
                //if in debug mode, install also other methods, first the one using cad models
                //CAD model 
                Vector cad_parameters;
                Vector cad_parameters_reduced;
                Vector cad_parameters_w_offset;
                iCubLimbGetBeta(icub,FTNames[vectorFT[i]],cad_parameters);
                cad_parameters_reduced = identifiable_parameters[vectorFT[i]].transposed()*cad_parameters;
                cad_parameters_w_offset = cat(cad_parameters_reduced,offset[vectorFT[i]]);
                //cout << "lalala " << identifiable_parameters[vectorFT[i]].cols()+6  <<  "  and " << cad_parameters_w_offset.size() << endl;
                param_learner = new MultiTaskLinearFixedParameters(identifiable_parameters[vectorFT[i]].cols()+6,6,cad_parameters_w_offset);
                param_learner->setName("CAD");
                paramEstimators[vectorFT[i]].push_back(param_learner);
                params[vectorFT[i]].push_back(Vector());
                
                //CAD model with learned offset
                param_learner = new MultiTaskLinearGPRLearnerFixedParameters(identifiable_parameters[vectorFT[i]].cols()+6,6,cad_parameters_reduced);
                param_learner->setName("CAD_LEARNED_OFFSET");
                paramEstimators[vectorFT[i]].push_back(param_learner);
                params[vectorFT[i]].push_back(Vector());                
                
                //debug
                //add others
                */

            }
            
        }
    }
    /*
    //Setting mixed estimation
    staticParamEstimator = new MultiTaskLinearGPRLearner(static_identifiable_parameters[ICUB_FT_RIGHT_ARM].cols()+6,6);
    staticParamEstimator->setName("STATIC_RLS");
    
    dynamicParamEstimator = new MultiTaskLinearGPRLearner(dynamic_identifiable_parameters[ICUB_FT_RIGHT_ARM].cols(),6);
    dynamicParamEstimator->setName("DYNAMIC_RLS");
    
    mixed_estimated_out_port = new BufferedPort<Vector>;
    mixed_estimated_out_port->open(string("/"+local_name+"/"+FTNames[ICUB_FT_RIGHT_ARM]+"/FT_mixed_RLS_estimated:o").c_str());
    
    static_estimated_out_port = new BufferedPort<Vector>;
    static_estimated_out_port->open(string("/"+local_name+"/"+FTNames[ICUB_FT_RIGHT_ARM]+"/FT_static_RLS_estimated:o").c_str());
    */
    
    //If debug is enabled, open output debug ports
    if( debug_out_enabled ) { 
        /*
        for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
            if( is_enabled[FTlimb[vectorFT[i]]] ) {
                measured_out_port[vectorFT[i]] = new BufferedPort<Vector>;
                measured_out_port[vectorFT[i]]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT_measured:o").c_str());
                for(unsigned int j = 0; j != paramEstimators[vectorFT[i]].size(); j++ ) {
                    estimated_out_port[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    estimated_out_port[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated:o").c_str());
                    //estimated_out_port_inc[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    //estimated_out_port_inc[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated_inc:o").c_str());
                    if( debug_out_parameters ) {
                        parameters_estimated_out_port[vectorFT[i]].push_back(new BufferedPort<Vector>);
                        parameters_estimated_out_port[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated_parameters:o").c_str());
                    }
                    estimated_forward_inertial_torques[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    estimated_forward_inertial_torques[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/forward_projected_torques_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated:o").c_str());
                    estimated_backward_inertial_torques[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    estimated_backward_inertial_torques[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/backward_projected_torques_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated:o").c_str());
                    estimated_FT_sens_torques[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    estimated_FT_sens_torques[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT_sens_torques_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated:o").c_str());
                    measured_FT_sens_torques[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    measured_FT_sens_torques[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/FT_sens_torques_"+paramEstimators[vectorFT[i]][j]->getName()+"_measured:o").c_str());
    
                    //estimated_projected_torques[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    //estimated_projected_torques[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/Projected_torques_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated:o").c_str());
                    estimated_projected_torques_inc[vectorFT[i]].push_back(new BufferedPort<Vector>);
                    estimated_projected_torques_inc[vectorFT[i]][j]->open(string("/"+local_name+"/"+FTNames[vectorFT[i]]+"/Projected_torques_"+paramEstimators[vectorFT[i]][j]->getName()+"_estimated_inc:o").c_str());
     
                }
            }
        }*/
    }

    debug_generate_yarpscope_xml(ICUB_FT_RIGHT_ARM);
    debug_generate_yarpscope_xml(ICUB_FT_RIGHT_ARM,true);
    debug_generate_yarpscope_xml_only_param(ICUB_FT_RIGHT_ARM);

    error_steps = 0;
    
    fprintf(stderr,"threadInit: Finish... \n\n");
    thread_status = STATUS_OK;
    return true;
}


void printMatrix_ofstream(ofstream & file, const Matrix& mat, int precision)
{
     for(int r=0;r<mat.rows();r++)
        {        
            for(int c=0;c<mat.cols();c++)
            {
                file << mat(r,c) << ",";
            }
            file << std::endl;
        }
}


double positive_part(double dq)
{
    if( dq > 0.0 ) return dq;
    return 0.0;
}

double negative_part(double dq)
{
    if( dq < 0.0 ) return -dq;
    return 0.0;
}
     
double damped_sgn(double dq, double tol)
{
    tol = fabs(tol);
    if( dq < tol && dq > -tol ) {
        return 0.0;
    }
    if( dq > tol ) {
        return 1.0;
    }
    if( dq < -tol ) {
        return -1.0;
    }
    return 0.0;
}

Matrix friction_regressor(double dq)
{
    double tol = CTRL_DEG2RAD*1;
    Matrix ret(1,4);
    ret(0,0) = positive_part(damped_sgn(dq,tol));
    ret(0,1) = -negative_part(damped_sgn(dq,tol));
    ret(0,2) = positive_part(dq);
    ret(0,3) = -negative_part(dq);
    return ret;
}


Matrix friction_regressor(double q0, double q1)
{
    Matrix ret(2,8);
    ret.zero();
    ret.setSubmatrix(friction_regressor(q0),0,0);
    ret.setSubmatrix(friction_regressor(q1),1,4);
    return ret;
}
    

Matrix friction_regressor(double q0, double q1, double q2)
{
    Matrix ret(3,12);
    ret.zero();
    ret.setSubmatrix(friction_regressor(q0,q1),0,0);
    ret.setSubmatrix(friction_regressor(q2),2,8);
    return ret;
}
     

void inertiaObserver_thread::run()
{
	static int call_count = 0;
    
    Matrix Phi, Phi_w_offset;
    Matrix Phi_static_w_offset;
    Matrix Phi_dynamic;
    
    map<iCubFT,double> W_timestamp;
    
    bool read_success;
    
    bool limbIsStill;
    
    bool oneReadWasSuccess = false;
    
    thread_status = STATUS_OK;
    
    double tic_run;
    double toc_run;
    //static double cond_num = 1e10;
    

    call_count++;
    
    tic_run = yarp::os::Time::now();


    if( call_count % 100 == 0) {
        //fprintf(stderr,"~~~~\nRunning run method\n");
    }
    
    if( right_arm_enabled ) {
        iCubLimb currLimb = ICUB_RIGHT_ARM;
        iCubFT currFT = ICUB_FT_RIGHT_ARM;
        limbIsStill = false;
        do {
            read_success = readAvailableFT(currFT,*icub,current_state_estimator,measuredW[currFT],W_timestamp[currFT]);
            //limbIsStill = current_state_estimator.isStill(currLimb) && current_state_estimator.isStill(ICUB_HEAD);
            
            if( read_success ) {
                
                
                oneReadWasSuccess = true;
                
                //If produceAb is enabled, produce the estimation matrix A and known terms b and dump them
                /*
                if( produceAb ) {
                    iCubLimbRegressorSensorWrench(icub,"right_arm",local_Phi);
                    local_A.setSubmatrix(local_Phi,0,0);
                    local_A.setSubmatrix(eye(6,6),0,local_Phi.cols());
                    local_b = measuredW[currFT];
                    YARP_ASSERT(local_Phi.rows() == 6 && local_Phi.cols() == 40);
                    
                    printMatrix_ofstream(A_file,local_A,10);
                    printVector_ofstream(b_file,local_b,10);
                    
                    fail = false; 
                    
                    Ab_sample_count++;
                }*/
                
     
                
                if( produceAb_motors ) {
                    
                    megazord.zero();
                    
                    //Inertial parameters regressor
                    iCubLimbRegressorSensorWrench(icub,"right_arm",Y_s_reduced);
                    iCubLimbRegressorTorqueFromBase(icub,"right_arm",Y_tau);
                
                    Y_s_all.zero();
                    Y_s_all.setSubmatrix(Y_s_reduced,0,30);
                
                    Y_I = Y_tau.submatrix(0,2,0,Y_tau.cols()-1);
                    Y_II = Y_tau.submatrix(3,3,0,Y_tau.cols()-1);
                    
                    megazord.setSubmatrix(Y_I,0,0);
                    megazord.setSubmatrix(Y_II,Y_I.rows(),0);
                    megazord.setSubmatrix(Y_s_all,Y_II.rows()+Y_I.rows(),0);
                    
                    assert(Y_II.rows()+Y_I.rows() == 4);
                    //FT offset 
                    megazord.setSubmatrix(eye(6,6),4,Y_s_all.cols());
                    
                    //friction stuff
                    Vector dq = icub->upperTorso->getDAng("right_arm");
                    Vector dq_I = dq.subVector(0,2);
                    Vector dq_II = dq.subVector(3,3);
                    
                    A_I = friction_regressor(dq_I(0),dq_I(1),dq_I(2));
                    A_II = friction_regressor(dq_II(0));
                    
                    //std::cout << "A_I: " << A_I.toString() << std::endl;
                    //std::cout << "A_II: " << A_II.toString() << std::endl;
                    
                    assert(Y_s_all.cols()+6 == 76);
                    
                    megazord.setSubmatrix(A_I,0,76);
                    megazord.setSubmatrix(A_II,A_I.rows(),76+A_I.cols());
                    
                    //motor friction stuff;
                    Vector dq_mI = T_T.transposed()*dq_I;
                    B_I = friction_regressor(dq_mI(0),dq_mI(1),dq_mI(2));
                    
                    assert(76+A_I.cols()+A_II.cols()==92);
                    megazord.setSubmatrix(T_T*B_I,0,92);
                    
                    //pwm stuff;
                    Vector pwm(5), pwm_I(3), pwm_II(1);
                    current_state_estimator.getPwm(ICUB_RIGHT_ARM,pwm,W_timestamp[ICUB_FT_RIGHT_ARM]);
                    #ifndef NDEBUG
                    //std::cout << "Pwm value readed:" << pwm.toString() << std::endl;
                    #endif
                    fail = false;
                    if( pwm.size() == 0 ) {
                        AWPolyList* pwmList = current_state_estimator.getPWMdeque(ICUB_RIGHT_ARM);
                        
                        std::cout << setprecision(20) << "Error: couldn't read pwm value, sample exctraction failed, requested timestamp "  << W_timestamp[ICUB_FT_RIGHT_ARM] << std::endl;
                        std::cout << "samples available from timestamp " << pwmList->front().time
<< " to timestamp " <<  pwmList->back().time
 << std::endl;
                        fail = true;
                    }
                    
                    pwm_I = pwm.subVector(0,2);
                    pwm_II = pwm.subVector(3,3);
                    
                    //std::cout << "pwm_II" << pwm_II.toString() << std::endl;
                    
                    diagV_I.diagonal(pwm_I);
                    diagV_II.diagonal(pwm_II);
                    
                    //std::cout << "diagV_II" << pwm_II.toString() << std::endl;

                    
                    megazord.setSubmatrix(-1*T_T*diagV_I,0,104);
                    
                    assert(A_I.rows() == 3);
                    megazord.setSubmatrix(-1*diagV_II,3,107);
                    
                    YARP_ASSERT(megazord.rows() == 10);
                    YARP_ASSERT(megazord.cols() == 108);
                    
                    local_b = Vector(10,0.0);
                    local_b.setSubvector(4,measuredW[currFT]);
                    
                    if( !fail ) {
                        printMatrix_ofstream(A_file,megazord,10);
                        printVector_ofstream(b_file,local_b,10);
                    }
                    
                    Ab_sample_count++;
                }
                
                           if( produceAb_contact ) {
                    skinContactList scl, ra_scl;
                    current_state_estimator.getContact(scl,W_timestamp[ICUB_FT_RIGHT_ARM]);
                    
                    ra_scl = scl.filterBodyPart(RIGHT_ARM);
                    
                    int num_taxels = 0;
                    for(int i=0;i<(int)ra_scl.size();i++ ) {
                        if( ra_scl[i].getSkinPart() != SKIN_RIGHT_HAND ) { 
                            num_taxels += ra_scl[i].getActiveTaxels();
                        }
                    }
                    if( !fail ) {
                        contact_file << num_taxels << std::endl;
                    }
                }
                
                
                if( learning_enabled ) {
                    for(unsigned int j=0; j < paramEstimators[currFT].size(); j++ ) {
                        //paramEstimators[currFT][j]->feedSample(Phi_w_offset,measuredW[currFT]);
                    }
                }
            }
                
        } while( read_success );
        
        
        //~~~~~~~~~~~~~~
        toc_run = yarp::os::Time::now();
        run_period.feedSample(toc_run-tic_run);
        if( call_count % 100 == 0 ) {
            //fprintf(stderr,"Correct run method, duration: %lf, mean %lf\n",toc_run-tic_run,run_period.getMean());
            //fprintf(stderr,"W_timestamp icub_right_arm %lf\n",W_timestamp[ICUB_FT_RIGHT_ARM]);
        }
        if( !oneReadWasSuccess ) {
           //fprintf(stderr,"No successful read in this run execution.\n"); 
        }
        //~~~~~~~~~~~~~~

    }
}


     

void inertiaObserver_thread::threadRelease()
{
    finalAnalysis();
    /*
    {
        ofstream beta_file;
        ofstream identifiable_param_file;
        
        beta_file.open("beta.csv");
        identifiable_param_file.open("identifiable_parameters.csv");
        
        printMatrix_ofstream(identifiable_param_file,identifiable_parameters[ICUB_FT_RIGHT_ARM],10);
        printVector_ofstream(beta_file,paramEstimators[ICUB_FT_RIGHT_ARM][0]->getParameters(),10);
        
        identifiable_param_file.close();
        beta_file.close();
    }
    */
	fprintf(stderr,"Closing the inertiaObserver thread\n");
    
    fprintf(stderr, "Closing inertial port\n");
    closePort(port_inertial_thread);
    
    
    for(vector<iCubLimb>::size_type i = 0; i != vectorLimbs.size(); i++) {
        if( is_enabled[vectorLimbs[i]] ) {
            cerr << "Closing port_q " << limbNames[vectorLimbs[i]] << endl;
            closePort(port_q[vectorLimbs[i]]);
        }
    }
    
    for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
        if( is_enabled[FTlimb[vectorFT[i]]] ) {
            cerr << "Closing port_ft " << FTNames[vectorFT[i]] << endl;
            closePort(port_ft[vectorFT[i]]);
            cerr << "Deleting online estimators " << FTNames[vectorFT[i]] << endl;
            for(unsigned int j=0; j < paramEstimators[vectorFT[i]].size(); j++ ) {
                delete paramEstimators[vectorFT[i]][j];
            } 
        }
    }
    
            closePort(port_contact);
            
            closePort(port_pwm);

    
    if( staticParamEstimator ) {
        delete staticParamEstimator;
    }
    
    if( dynamicParamEstimator ) {
        delete dynamicParamEstimator;
    }
    
    closePort(mixed_estimated_out_port);
    closePort(static_estimated_out_port);


    if( debug_out_enabled ) { 
        fprintf(stderr, "Closing debug output ports\n");
        for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
            if( is_enabled[FTlimb[vectorFT[i]]] ) {
                closePort(measured_out_port[vectorFT[i]]);
                for(unsigned int j = 0; j != estimated_out_port[vectorFT[i]].size(); j++ ) {
                    closePort(estimated_out_port[vectorFT[i]][j]);
                }
                for(unsigned int j = 0; j != estimated_out_port_inc[vectorFT[i]].size(); j++ ) {
                    //closePort(estimated_out_port_inc[vectorFT[i]][j]);
                }
                for(unsigned int j = 0; j != parameters_estimated_out_port[vectorFT[i]].size(); j++ ) {
                    closePort(parameters_estimated_out_port[vectorFT[i]][j]);
                }
                for(unsigned int j = 0; j != estimated_forward_inertial_torques[vectorFT[i]].size(); j++ ) {
                    closePort(estimated_forward_inertial_torques[vectorFT[i]][j]);
                }
                for(unsigned int j = 0; j != estimated_backward_inertial_torques[vectorFT[i]].size(); j++ ) {
                    closePort(estimated_backward_inertial_torques[vectorFT[i]][j]);
                }
                for(unsigned int j = 0; j != estimated_FT_sens_torques[vectorFT[i]].size(); j++ ) {
                    closePort(estimated_FT_sens_torques[vectorFT[i]][j]);
                }
                for(unsigned int j = 0; j != measured_FT_sens_torques[vectorFT[i]].size(); j++ ) {
                    closePort(measured_FT_sens_torques[vectorFT[i]][j]);
                }
                for(unsigned int j = 0; j != estimated_projected_torques_inc[vectorFT[i]].size(); j++ ) {
                    closePort(estimated_projected_torques_inc[vectorFT[i]][j]);
                }
            }
        }
    }
    
    /*
    if( produceAb ) {
        Matrix trainA, testA;
        Vector trainb, testb;
    
        int test_sample_count  = Ab_sample_count/2;
        int train_sample_count = Ab_sample_count - test_sample_count;
        
        trainA = A.submatrix(0,6*train_sample_count-1,0,A.cols()-1);
        trainb = b.subVector(0,6*train_sample_count-1);
        
        testA = A.submatrix(6*train_sample_count,A.rows()-1,0,A.cols()-1);
        testb = b.subVector(6*train_sample_count,b.size()-1);
        
        
        bool produce_binary = false;
        bool produce_ascii = false;
        produce_binary = true; 
        produce_ascii = true;
        if( produce_binary ) {
            std::cerr << "Dumping measures to A.ymt, b.yvc" << std::endl;
            Vector_write("b.yvc",b);
            Matrix_write("A.ymt",A);
            
            std::cerr << "Dumping training measures to trainA.ymt, trainb.yvc" << std::endl;
            Vector_write("trainb.yvc",trainb);
            Matrix_write("trainA.ymt",trainA);
            
            std::cerr << "Dumping training measures to testA.ymt, testb.yvc" << std::endl;
            Vector_write("testb.yvc",testb);
            Matrix_write("testA.ymt",testA);
        }
        if( produce_ascii ) {
            std::cerr << "Dumping measures to A.csv, b.csv" << std::endl;
            ofstream A_file;
            A_file.open("A.csv");
            printMatrix_ofstream(A_file,A,10);
            A_file.close();
            
            ofstream b_file;
            b_file.open("b.csv");
            printVector_ofstream(b_file,b,10);
            b_file.close();
            
            std::cerr << "Dumping measures to trainA.csv, trainb.csv" << std::endl;
            ofstream trainA_file;
            trainA_file.open("trainA.csv");
            printMatrix_ofstream(trainA_file,trainA,10);
            trainA_file.close();
            
            ofstream trainb_file;
            trainb_file.open("trainb.csv");
            printVector_ofstream(trainb_file,trainb,10);
            trainb_file.close();
            
            std::cerr << "Dumping measures to testA.csv, testb.csv" << std::endl;
            ofstream testA_file;
            testA_file.open("testA.csv");
            printMatrix_ofstream(testA_file,testA,10);
            testA_file.close();
            
            ofstream testb_file;
            testb_file.open("testb.csv");
            printVector_ofstream(trainb_file,trainb,10);
            testb_file.close();
        }
    }
    */
    
    if( produceAb ) {
        A_file.close();
        b_file.close();
    }
    
    if( produceAb_motors ) {
        A_file.close();
        b_file.close();
    }
    
    /*
    if( dump_static ) {
        std::cerr << "Dumping static measure to staticRegr.ymt, staticFT.yvc" << std::endl;
        for(vector<iCubFT>::size_type i = 0; i != vectorFT.size(); i++) {
            if( is_enabled[FTlimb[vectorFT[i]]] ) {
                Vector cad_parameters;
                std::cout << "Last col of regr " << allStaticRegr[vectorFT[i]].getCol(16).toString() << std::endl;
                std::cerr << "Saving allStaticRegr[vectorFT[i]] " << allStaticRegr[vectorFT[i]].rows() << " " << allStaticRegr[vectorFT[i]].cols() << std::endl;
                std::cerr << "Saving all allStaticFT[vectorFT[i]] " << allStaticFT[vectorFT[i]].size() << std::endl;
                Vector_write("staticFT_"+limbNames[FTlimb[vectorFT[i]]]+".yvc",allStaticFT[vectorFT[i]]);
                Matrix_write("staticRegr_"+limbNames[FTlimb[vectorFT[i]]]+".ymt",allStaticRegr[vectorFT[i]]);
                Matrix_write("staticIdentiableParameters_"+limbNames[FTlimb[vectorFT[i]]]+".ymt",static_identifiable_parameters[vectorFT[i]]);
                iCubLimbGetBeta(icub,FTNames[vectorFT[i]],cad_parameters);
                Vector_write("cad_parameters_"+limbNames[FTlimb[vectorFT[i]]]+".yvc",cad_parameters);
            }
        }
    }
    */
    if (icub)      {delete icub; icub=0;}
    /*
    if (icub_CAD) {delete icub_CAD; icub_CAD=0;}
    if (icub_col_scaling) { delete icub_col_scaling; icub_col_scaling=0;}
    */
}   

void inertiaObserver_thread::openPort(Contactable *_port, string portName)
{
}

Vector inertiaObserver_thread::all_masses_regressor(int n)
{
    Vector ret(n,0.0);
    for(int i=0; i < n-n%10; i = i + 10) {
        ret(i) = 1;
    }
    return ret;
}

void inertiaObserver_thread::closePort(Contactable *_port)
{
    if (_port)
    {
        _port->interrupt();
        _port->close();

        delete _port;
        _port = 0;
    }
}

/**
 * \todo make the function for all the limbs
 */
bool inertiaObserver_thread::estimateSensorWrench(iCubWholeBody &icub,const std::string limb,const Vector beta,Vector & wrench)
{
    Matrix F_ext_up;
    F_ext_up.resize(6,3);
    F_ext_up.zero();
    Vector old_beta;
    iCubLimbGetBeta(&icub,limb,old_beta);
    iCubLimbSetBeta(&icub,limb,beta);
    icub.upperTorso->solveWrench();
    Matrix F_sensor_up = icub.upperTorso->estimateSensorsWrench(F_ext_up,false);
    wrench = F_sensor_up.getCol(0);
    iCubLimbSetBeta(&icub,limb,old_beta);
    return true;
}

//return true if the ft measure was available, otherwise false, it there where problems or no ft measure with the right charcateristic (not previously used, not isulated) is available
bool inertiaObserver_thread::readAvailableFT(iCubFT ft, iCubWholeBody & icub, iCubStateEstimator & current_state_estimator, Vector & F_measured, double & F_timestamp )
{
    AWPolyList * p_ft_list; 
    int i;
    
    Vector q_limb,dq_limb,ddq_limb;
    Vector q_head, dq_head, ddq_head;
    Vector q_torso, dq_torso, ddq_torso;
    bool found_suitable_FT = false;
    
    //std::cerr << "readAvailableFT: started" << endl;

    
    current_state_estimator.waitOnFTMutex(ft);
    
    p_ft_list = current_state_estimator.getFTdeque(ft);

    
    
    if( p_ft_list->size() == 0 ) {
        //If there are no measures
        //return false;
    } else if( p_ft_list->front().time <= timestamp_lastFTsample_returned[ft] ) {
        //if all the available samples are oldest than the last returned is the rist
        //return false;
    } else {
        if( p_ft_list->back().time > timestamp_lastFTsample_returned[ft] ) {
            //if all the available samples are newer, then the last is returned
            i = p_ft_list->size()-1;
        } else { 
            //Find the oldest not already used FT measure
            for(i = 0; i != (int)p_ft_list->size(); i++ ) {
                if( (*p_ft_list)[i].time <= timestamp_lastFTsample_returned[ft] ) {
                    //found last delivered one, than the previous element was the oldest not already returned
                    i--;
                    break;
                }
    
            }
        }
        //size_t old_size = p_ft_list->size();
        if( i >= (int)p_ft_list->size() ) {
            std::cerr << " i " << i << " size: " << p_ft_list->size() << std::endl;
        }
        YARP_ASSERT(i < (int)p_ft_list->size());
        for( /* i as before */ ; i >= 0; i-- ) {
            //YARP_ASSERT(p_ft_list->size() == old_size );
            YARP_ASSERT(i >= 0);
            if( i >= (int)p_ft_list->size() ) {
                std::cerr << " i " << i << " size: " << p_ft_list->size() << std::endl;
            }
            YARP_ASSERT(i < (int)p_ft_list->size());
            current_state_estimator.getPos(ICUB_HEAD,q_head,(*p_ft_list)[i].time);
            if( q_head.size() == 0 ) continue;
            if( FTlimb[ft] == ICUB_RIGHT_LEG || FTlimb[ft] == ICUB_LEFT_LEG ) {
                current_state_estimator.getPos(ICUB_TORSO,q_torso,(*p_ft_list)[i].time);
                if( q_torso.size() == 0 ) continue;
            }
            current_state_estimator.getPos(FTlimb[ft],q_limb,(*p_ft_list)[i].time);
            if( q_limb.size() == 0 ) continue;
            
            current_state_estimator.getVel(ICUB_HEAD,dq_head,(*p_ft_list)[i].time);
            if( dq_head.size() == 0 ) continue;
            if( FTlimb[ft] == ICUB_RIGHT_LEG || FTlimb[ft] == ICUB_LEFT_LEG ) {
                current_state_estimator.getVel(ICUB_TORSO,dq_torso,(*p_ft_list)[i].time);
                if( dq_torso.size() == 0 ) continue;
            }
            current_state_estimator.getVel(FTlimb[ft],dq_limb,(*p_ft_list)[i].time);
            if( dq_limb.size() == 0 ) continue;
            
            current_state_estimator.getAcc(ICUB_HEAD,ddq_head,(*p_ft_list)[i].time);
            if( ddq_head.size() == 0 ) continue;
            if( FTlimb[ft] == ICUB_RIGHT_LEG || FTlimb[ft] == ICUB_LEFT_LEG ) {
                current_state_estimator.getAcc(ICUB_TORSO,ddq_torso,(*p_ft_list)[i].time);
                if( ddq_torso.size() == 0 ) continue;
            }
            current_state_estimator.getAcc(FTlimb[ft],ddq_limb,(*p_ft_list)[i].time);
            if( ddq_limb.size() == 0 ) continue;
            found_suitable_FT = true;
            break;
        }
    }
    
    current_state_estimator.postOnFTMutex(ft);

    
    if( found_suitable_FT ) {
         /**
         *\todo Add switch to use inertial measure
         * 
         */ 
        //set Inertial measurment!!!
        Vector F_up(6, 0.0);
        Vector init_w0(3), init_dw0(3), init_d2p0(3);
        init_w0.zero();
        init_dw0.zero();
        init_d2p0.zero();
        init_d2p0[2] = 9.78;
        icub.upperTorso->setInertialMeasure(init_w0,init_dw0,init_d2p0);
    
        icub.upperTorso->setSensorMeasurement(F_up,F_up,F_up);
        
        YARP_ASSERT(FTlimb[ft] == ICUB_RIGHT_ARM || FTlimb[ft] == ICUB_LEFT_ARM);
        
		icub.upperTorso->setAng(limbNames[FTlimb[ft]],CTRL_DEG2RAD * q_limb);
		icub.upperTorso->setDAng(limbNames[FTlimb[ft]],CTRL_DEG2RAD * dq_limb);
		icub.upperTorso->setD2Ang(limbNames[FTlimb[ft]],CTRL_DEG2RAD * ddq_limb);

		icub.upperTorso->setAng("head",CTRL_DEG2RAD * q_head);
		icub.upperTorso->setDAng("head",CTRL_DEG2RAD * dq_head);
		icub.upperTorso->setD2Ang("head",CTRL_DEG2RAD * ddq_head);
        
        icub.upperTorso->solveKinematics();
        
        /**
         * 
         * \todo add verbose parameter
         * 
         */

        F_timestamp = (*p_ft_list)[i].time;
		F_measured  = (*p_ft_list)[i].data;
        
        timestamp_lastFTsample_returned[ft] = F_timestamp;
        //std::cerr << "readAvailableFT: finished returning true" << endl;
        return true;
    } else {
        //std::cerr << "readAvailableFT: finished returning false" << endl;
        return false;
    }
}

//Should be improved to "readSuitableFT" based on some rules (for example for non causal speed estimation
bool inertiaObserver_thread::readLastSuitableFT(std::string limbName, iCubWholeBody & icub, iCubStateEstimator & current_state_estimator, Vector & F_measured, double & F_timestamp )
{
	//fprintf(stderr,"Called readLastSuitableFT\n");
	if( limbName == "right_arm" ) {
        //iCubLimb currLimb = ICUB_RIGHT_ARM;
        iCubFT currFT = ICUB_FT_RIGHT_ARM;
		AWPolyList * p_ft_list;
		Vector q_pos(0), q_vel(0), q_acc(0);
		Vector q_pos_head(0), q_vel_head(0), q_acc_head(0);
		p_ft_list = current_state_estimator.getFTdeque(currFT);
		AWPolyList::iterator ft_iter;
        //fprintf(stderr,"readLastSuitableFT: for position loop\n");
		for(ft_iter = p_ft_list->begin(); ft_iter != p_ft_list->end(); ft_iter++ ) {
			current_state_estimator.getPos(ICUB_RIGHT_ARM,q_pos,ft_iter->time);
            //fprintf(stderr,"readLastSuitableFT: got head position\n");
			current_state_estimator.getPos(ICUB_HEAD,q_pos_head,ft_iter->time);
            //fprintf(stderr,"readLastSuitableFT: got arm position\n");
			if( q_pos.size() > 0 && q_pos_head.size() > 0 ) break;
		}
        //fprintf(stderr,"readLastSuitableFT: for position loop ended\n");
		if( q_pos_head.size() == 0) { 
            cout << "Head position not arrived\n";
            return false; 
        }
		if( q_pos.size() == 0) { 
            cout << "arm position estimate error\n"; 
            return false; 
        }
        current_state_estimator.getVel(ICUB_RIGHT_ARM,q_vel,ft_iter->time);
        current_state_estimator.getVel(ICUB_HEAD,q_vel_head,ft_iter->time);
        //fprintf(stderr,"readLastSuitableFT: got velocities\n");
        if(q_vel.size() == 0 || q_vel_head.size() == 0) { 
            cout << "velocity estimate error\n"; 
            return false; 
        }
		current_state_estimator.getAcc(ICUB_RIGHT_ARM,q_acc,ft_iter->time);
		current_state_estimator.getAcc(ICUB_HEAD,q_acc_head,ft_iter->time);
        //fprintf(stderr,"readLastSuitableFT: got accelerations\n");
		if(q_acc.size() == 0) { 
            cout << "acceleration estimate error\n"; 
            return false; 
        }
		if(q_acc_head.size() == 0) { 
            cout << "head acceleration estimate error\n"; 
            return false; 
        }
        
        /**
         *\todo Add switch to use inertial measure
         * 
         */ 
    //set Inertial measurment!!!
        Vector F_up(6, 0.0);
        Vector init_w0(3), init_dw0(3), init_d2p0(3);
        init_w0.zero();
        init_dw0.zero();
        init_d2p0.zero();
        init_d2p0[2] = 9.78;
        icub.upperTorso->setInertialMeasure(init_w0,init_dw0,init_d2p0);
    
    
        icub.upperTorso->setSensorMeasurement(F_up,F_up,F_up);
		icub.upperTorso->setAng("right_arm",CTRL_DEG2RAD * q_pos);
		icub.upperTorso->setDAng("right_arm",CTRL_DEG2RAD * q_vel);
		icub.upperTorso->setD2Ang("right_arm",CTRL_DEG2RAD * q_acc);

		icub.upperTorso->setAng("head",CTRL_DEG2RAD * q_pos_head);
		icub.upperTorso->setDAng("head",CTRL_DEG2RAD * q_vel_head);
		icub.upperTorso->setD2Ang("head",CTRL_DEG2RAD * q_acc_head);
        
        icub.upperTorso->solveKinematics();
        
        /**
         * 
         * \todo add verbose parameter
         * 
         */
        
        F_timestamp = ft_iter->time;
		F_measured = ft_iter->data;

        
        //fprintf(stderr,"readLastSuitableFT: returing\n");
        if( F_timestamp == timestamp_lastFTsample_returned[currFT]) {
            cerr << "FT sample already returned" << endl;
            return false;
        } else {
            timestamp_lastFTsample_returned[currFT] = F_timestamp;
            return true;
        }
	}
    return false;
}

bool inertiaObserver_thread::calibrateOffset()
{
    int wait_count = 0;
    const int Nsamples = 100;
    const double approx_FT_sensor_period = 0.01;
    fprintf(stderr,"calibrateOffset: starting calibration... \n");
    
    int Ntrials = 0;
    
    
    //Assuming that the robot is still
    this->suspend();
    current_state_estimator.reset();
    
    //Busy waiting
    /**
     * 
     * \todo add calibration for all limbs
     * 
     */
    /**
     * \todo exit condition if no FT measurment are available!
     */
    if( right_arm_enabled ) {
        iCubFT currFT;
        iCubLimb currLimb;
        Vector FT_measure_sum(6,0.0);
        Vector iDyn_cad_estimation(6,0.0);
        Vector q_pos;
        Vector q_head_pos;
        currLimb = ICUB_RIGHT_ARM;
        currFT = ICUB_FT_RIGHT_ARM;
        offset[currFT] = Vector(6,0.0);
        AWPolyList * p_ft_list = current_state_estimator.getFTdeque(currFT);
        AWPolyList::iterator ft_iter;
        while( p_ft_list->size() < Nsamples + 2 ) {
            if( wait_count % 50 == 0 ) {
                fprintf(stderr,"calibrateOffset: Waiting for %d samples, currenly only %d received... \n",Nsamples+2,(int)p_ft_list->size());
            }
            yarp::os::Time::delay(Nsamples*approx_FT_sensor_period);
            wait_count++;
        } 
		for(ft_iter = p_ft_list->begin()+1; ft_iter != p_ft_list->end()-1; ft_iter++ ) {
            FT_measure_sum += ft_iter->data;
            Ntrials++;
        }
        //Take the position from the middle of the measurments
        ft_iter = p_ft_list->begin() + Ntrials/2;
        current_state_estimator.getPos(currLimb,q_pos,ft_iter->time);
        current_state_estimator.getPos(ICUB_HEAD,q_head_pos,ft_iter->time);
        if(q_pos.size() == 0 || q_head_pos.size() == 0 ) { 
            fprintf(stderr,"calibrateOffset: fatal error in data grabbing\n"); 
            this->resume(); 
            return false; 
        }
        /**
         *\todo Add switch to use inertial measure
         * 
         */ 
        Matrix F_ext_up;
        F_ext_up.resize(6,3);
        F_ext_up.zero();
        Vector F_up(6, 0.0);
        Vector init_w0(3), init_dw0(3), init_d2p0(3);
        init_w0.zero();
        init_dw0.zero();
        init_d2p0.zero();
        init_d2p0[2] = 9.78;
        icub->upperTorso->setInertialMeasure(init_w0,init_dw0,init_d2p0);
        icub->upperTorso->setSensorMeasurement(F_up,F_up,F_up);
         
        icub->upperTorso->setAng(limbNames[currLimb],CTRL_DEG2RAD * q_pos);
		icub->upperTorso->setDAng(limbNames[currLimb],CTRL_DEG2RAD * Vector(q_pos.size(),0.0) );
		icub->upperTorso->setD2Ang(limbNames[currLimb],CTRL_DEG2RAD * Vector(q_pos.size(),0.0) );

		icub->upperTorso->setAng("head",CTRL_DEG2RAD * q_head_pos);
		icub->upperTorso->setDAng("head",CTRL_DEG2RAD * Vector(q_head_pos.size(),0.0));
		icub->upperTorso->setD2Ang("head",CTRL_DEG2RAD * Vector(q_head_pos.size(),0.0));
        
        icub->upperTorso->solveKinematics();
        icub->upperTorso->solveWrench();
        
        Matrix F_sensor_up = icub->upperTorso->estimateSensorsWrench(F_ext_up,false);
        if( currLimb == ICUB_RIGHT_ARM ) {
            iDyn_cad_estimation = F_sensor_up.getCol(0);
        } else {
            YARP_ASSERT(currLimb == ICUB_LEFT_ARM);
            iDyn_cad_estimation = F_sensor_up.getCol(1);
        }
        offset[currFT] = (1.0/((double)Ntrials))*FT_measure_sum - iDyn_cad_estimation;
        /*
        std::cerr << "calibrateOffset debug information:" << std::endl;
        std::cerr << "q_head_pos           " << q_head_pos.toString() << std::endl;
        std::cerr << "q_pos                " << q_pos.toString() << std::endl;
        std::cerr << "iDyn_cad_estimation  " << iDyn_cad_estimation.toString() << std::endl;
        std::cerr << "Measured FT sensor   " << ((1.0/((double)Ntrials))*FT_measure_sum).toString() << std::endl;
        std::cerr << "Offset_RArm          " << Offset_RArm.toString() << std::endl;
        */
        std::cerr << "calibrateOffset: calibration success." << std::endl;
    }
    
    //for now, quick hack, replicating
    if( left_arm_enabled ) {
        iCubFT currFT;
        iCubLimb currLimb;
        Vector FT_measure_sum(6,0.0);
        Vector iDyn_cad_estimation(6,0.0);
        Vector q_pos;
        Vector q_head_pos;
        currLimb = ICUB_LEFT_ARM;
        currFT = ICUB_FT_LEFT_ARM;
        offset[currFT] = Vector(6,0.0);
        AWPolyList * p_ft_list = current_state_estimator.getFTdeque(currFT);
        AWPolyList::iterator ft_iter;
        while( p_ft_list->size() < Nsamples + 2 ) {
            fprintf(stderr,"calibrateOffset: Waiting for %d samples, currenly only %d received... \n",Nsamples+2,(int)p_ft_list->size());
            yarp::os::Time::delay(Nsamples*approx_FT_sensor_period);
        } 
		for(ft_iter = p_ft_list->begin()+1; ft_iter != p_ft_list->end()-1; ft_iter++ ) {
            FT_measure_sum += ft_iter->data;
            Ntrials++;
        }
        //Take the position from the middle of the measurments
        ft_iter = p_ft_list->begin() + Nsamples/2;
        current_state_estimator.getPos(currLimb,q_pos,ft_iter->time);
        current_state_estimator.getPos(ICUB_HEAD,q_head_pos,ft_iter->time);
        if(q_pos.size() == 0 || q_head_pos.size() == 0 ) { 
            fprintf(stderr,"calibrateOffset: fatal error in data grabbing\n"); 
            this->resume(); 
            return false; 
        }
        /**
         *\todo Add switch to use inertial measure
         * 
         */ 
        Matrix F_ext_up;
        F_ext_up.resize(6,3);
        F_ext_up.zero();
        Vector F_up(6, 0.0);
        Vector init_w0(3), init_dw0(3), init_d2p0(3);
        init_w0.zero();
        init_dw0.zero();
        init_d2p0.zero();
        init_d2p0[2] = 9.78;
        icub->upperTorso->setInertialMeasure(init_w0,init_dw0,init_d2p0);
        icub->upperTorso->setSensorMeasurement(F_up,F_up,F_up);
         
        icub->upperTorso->setAng(limbNames[currLimb],CTRL_DEG2RAD * q_pos);
		icub->upperTorso->setDAng(limbNames[currLimb],CTRL_DEG2RAD * Vector(q_pos.size(),0.0) );
		icub->upperTorso->setD2Ang(limbNames[currLimb],CTRL_DEG2RAD * Vector(q_pos.size(),0.0) );

		icub->upperTorso->setAng("head",CTRL_DEG2RAD * q_head_pos);
		icub->upperTorso->setDAng("head",CTRL_DEG2RAD * Vector(q_head_pos.size(),0.0));
		icub->upperTorso->setD2Ang("head",CTRL_DEG2RAD * Vector(q_head_pos.size(),0.0));
        
        icub->upperTorso->solveKinematics();
        icub->upperTorso->solveWrench();
        
        Matrix F_sensor_up = icub->upperTorso->estimateSensorsWrench(F_ext_up,false);
        if( currLimb == ICUB_RIGHT_ARM ) {
            iDyn_cad_estimation = F_sensor_up.getCol(0);
        } else {
            YARP_ASSERT(currLimb == ICUB_LEFT_ARM);
            iDyn_cad_estimation = F_sensor_up.getCol(1);
        }
        offset[currFT] = (1.0/((double)Ntrials))*FT_measure_sum - iDyn_cad_estimation;
        /*
        std::cerr << "calibrateOffset debug information:" << std::endl;
        std::cerr << "q_head_pos           " << q_head_pos.toString() << std::endl;
        std::cerr << "q_pos                " << q_pos.toString() << std::endl;
        std::cerr << "iDyn_cad_estimation  " << iDyn_cad_estimation.toString() << std::endl;
        std::cerr << "Measured FT sensor   " << ((1.0/((double)Ntrials))*FT_measure_sum).toString() << std::endl;
        std::cerr << "Offset_RArm          " << Offset_RArm.toString() << std::endl;
        */
    }
    
    this->resume();
    return true;
}


void inertiaObserver_thread::fillRandomMotion(iCubWholeBody * icub,string limbName)
{
    iDynChain * p_chain;
    iDynSensor * p_sensor;
    int virtual_link;
 
    iCubLimbGetData(icub,limbName,/*consider_virtual_link=*/false,p_chain,p_sensor,virtual_link);

    Vector q_min, q_max;
    q_min = p_chain->getJointBoundMin();
    q_max = p_chain->getJointBoundMax();
    unsigned int N = q_min.size();
    for(unsigned int i=0; i < N; i++ ) {
        p_chain->setAng(i,((q_max[i]-q_min[i])*yarp::os::Random::uniform() + q_min[i]));
    }
    
    for(unsigned int i=0; i < N; i++ ) {
            p_chain->setDAng(i,1*yarp::os::Random::uniform());
    }
    
    for(unsigned int i=0; i < N; i++ ) {
            p_chain->setD2Ang(i,0.5*yarp::os::Random::uniform());
    }
    Vector w0(3),dw0(3),ddp0(3);
    w0.zero();
    dw0.zero();
    ddp0.zero();
    ddp0[2] = 9.78;
    icub->upperTorso->setInertialMeasure(w0,dw0,ddp0);
}


void inertiaObserver_thread::fillRandomPosition(iCubWholeBody * icub,string limbName)
{
    iDynChain * p_chain;
    iDynSensor * p_sensor;
    int virtual_link;
 
    iCubLimbGetData(icub,limbName,/*consider_virtual_link=*/false,p_chain,p_sensor,virtual_link);

    Vector q_min, q_max;
    q_min = p_chain->getJointBoundMin();
    q_max = p_chain->getJointBoundMax();
    unsigned int N = q_min.size();
    for(unsigned int i=0; i < N; i++ ) {
        p_chain->setAng(i,((q_max[i]-q_min[i])*yarp::os::Random::uniform() + q_min[i]));
    }
    
    for(unsigned int i=0; i < N; i++ ) {
            p_chain->setDAng(i,0);
    }
    
    for(unsigned int i=0; i < N; i++ ) {
            p_chain->setD2Ang(i,0);
    }
    Vector w0(3),dw0(3),ddp0(3);
    w0.zero();
    dw0.zero();
    ddp0.zero();
    ddp0[2] = 9.81;
    icub->upperTorso->setInertialMeasure(w0,dw0,ddp0);
}

Matrix inertiaObserver_thread::getiCubLimbIdentifiableSubspace(string limb_name, bool only_static, int num_samples, double tol) {
    iCubWholeBody * icub_obs;
    Matrix A, allA;
    Matrix U,V;
    Vector S;
    size_t rank;
    icub_obs = new iCubWholeBody(icub_type, DYNAMIC);
    
    //seed yarp::os::Random::seed()
    for(int i = 0; i < num_samples; i++ ) {
        //if( i % 10 == 0 ) {
            //fprintf(stderr,"getiCubLimbIdentifiableSubspace: generating regressor for %d sample\n",i);
        //}
        if( !only_static ) {
            //fprintf(stderr,"fillRandomMotion");
            fillRandomMotion(icub_obs,limb_name);
        } else {
            fillRandomPosition(icub_obs,limb_name);
        }
        //Propagate kinematics on all icub
        //fprintf(stderr,"solveKineamtics");
        icub_obs->upperTorso->solveKinematics();
        
        //fprintf(stderr,"computeRegressor");
        iCubLimbRegressorSensorWrench(icub_obs,limb_name,A);
        
        if( i == 0 ) {
             allA = Matrix(A.rows()*(num_samples),A.cols());
        }
        
        //fprintf(stderr,"setSubmatrix");
        allA.setSubmatrix(A,A.rows()*(i),0);
    }
    
    SVD(allA,U,S,V);
    
    if( tol < 0 ) {
        tol = max(allA.rows(),allA.cols())*numeric_limits<double>::epsilon()*S[0];
    }
    
    for(rank = 0; rank < S.size(); rank++ ) {
        if( S[rank] < tol ) {
            break;
        }
    }
    
    
    Matrix V1;
    V1 = V.submatrix(0,V.rows()-1,0,rank-1);
    
    return V1;
}

void inertiaObserver_thread::debug_generate_yarpscope_xml(iCubFT ft, bool debug_out_parameters_yarpscope) {
    
    ofstream xml_file;
    vector<string> colors;
    vector<string> param_colors;
    colors.push_back("Red");
    colors.push_back("Green");
    colors.push_back("Blue");
    colors.push_back("gold01");
    colors.push_back("chocolate1");
    colors.push_back("pink01");
    colors.push_back("orchid1");
    colors.push_back("OliveDrab1");
    colors.push_back("12D4A1");
    colors.push_back("B25421");
    colors.push_back("246701");
    colors.push_back("2345B2");
    colors.push_back("khaki1");
    colors.push_back("DeepSkyBlue1");
    colors.push_back("turquoise1");
    colors.push_back("cyan2");
    colors.push_back("MistyRose1");
    colors.push_back("LightSlateGray");
    colors.push_back("lavander");
    colors.push_back("ForestGreen");
    colors.push_back("goldenrod");
    colors.push_back("gray95");
    colors.push_back("tomato1");
    colors.push_back("sienna1");
    colors.push_back("DodgerBlue1");
    colors.push_back("ivory1");
    colors.push_back("SpringGreen");
    colors.push_back("DarkMagenta");
    colors.push_back("tan1");
    colors.push_back("IndianRed1");
    colors.push_back("snow1");
    colors.push_back("MediumSeaGreen");
    colors.push_back("WhiteSmoke");
    colors.push_back("firebrick4");
    colors.push_back("RosyBrown1");
    colors.push_back("purple4");
    colors.push_back("DarkGrey");
    colors.push_back("burlywood1");
    colors.push_back("brown2");
    colors.push_back("DarkOrange3");
    colors.push_back("DeepPink4");
    colors.push_back("thistle4");



    
    printf("Generating yarpscope xml...\n");
    
    int gridx[] = { 0, 1, 2, 0, 1, 2 };
    int gridy[] = { 0, 0, 0, 1, 1, 1 };
    int minval[] = { -100, -100, -100, -5, -5, -5};
    int maxval[] = {100,    100,    100, 5, 5, 5};
    
    vector<string> title;
    title.resize(6);
    title[0] = "Fx";
    title[1] = "Fy";
    title[2] = "Fz";
    title[3] = "tx";
    title[4] = "ty";
    title[5] = "tz";
    
    if( !debug_out_parameters_yarpscope ) {
        xml_file.open ("debug_inertiaObserver.xml");
    } else {
        xml_file.open ("debug_inertiaObserver_param.xml");
    }
    xml_file << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
    if( !debug_out_parameters_yarpscope ) {
    xml_file << "<portscope rows=\"2\" columns=\"3\">" << endl;
    } else {
        xml_file << "<portscope rows=\"2\" columns=\"5\">" << endl;
    }
    for(int i = 0; i < 6; i++ ) {
        xml_file << "<plot gridx=\"" << gridx[i] << "\" " 
                       << "gridy=\"" << gridy[i] << "\" " 
                       << "title=\"" << title[i] << "\" "
                       << "minval=\"" << minval[i] << "\" "
                       << "maxval=\"" << maxval[i] << "\">" << endl;
        xml_file << "<graph remote=\"" << "/"+local_name+"/"+FTNames[ft]+"/FT_measured:o" << "\" " << "index = \"" << i << "\" " << "color = \"Yellow\" title=\"Measured\" /> " << endl;
        for(unsigned int j = 0; j != paramEstimators[ft].size(); j++ ) {
                    xml_file << "<graph remote=\"" << "/"+local_name+"/"+FTNames[ft]+"/FT_"+paramEstimators[ft][j]->getName()+"_estimated:o" << "\" " << "index = \"" << i << "\" " << "color = \"" << colors[j] << "\" title=\"" << paramEstimators[ft][j]->getName() << "\" /> " << endl;
        }
        xml_file << "</plot>" << endl;
    }
    
    if( debug_out_parameters_yarpscope ) {

    
    xml_file << "<plot gridx=\"" << 3 << "\" " 
                       << "gridy=\"" << 0 << "\" " 
                       << "hspan=\"" << 2 << "\" " 
                       << "vspan=\"" << 2 << "\" " 
                       << "title=\"" << "parameters" << "\">" << endl;
    for( unsigned int i = 0; i != (unsigned) identifiable_parameters[ft].cols(); i++ ) {
        for(unsigned int j = 0; j != paramEstimators[ft].size(); j++ ) {
            xml_file << "<graph remote=\"" << "/"+local_name+"/"+FTNames[ft]+"/FT_"+paramEstimators[ft][j]->getName()+"_estimated_parameters:o" << "\" " << "index = \"" << i << "\" " << "color = \"" << colors[(i+j)%colors.size()] << "\" title=\"" << paramEstimators[ft][j]->getName() << " param " << i <<  "\" /> " << endl;
        }
    }
            xml_file << "</plot>" << endl;

    }
    
    xml_file << "</portscope>" << endl;
    xml_file.close();
}

void inertiaObserver_thread::enableLearning() {
    learning_enabled = true;
}

void inertiaObserver_thread::disableLearning() {
    learning_enabled = false;
}

void inertiaObserver_thread::finalAnalysis() {
    double tol = 1e-2;
    /*
    cout << "~~~~~~~~~~~~~~~~~~~~~Torque BACKWARD analysis~~~~~~~~~~~~" << endl;
    {
        Matrix U_A,U_Tf,V_A,V_Tf;
        Matrix V1_A,V2_A,V1_Tf,V2_tf;
        Vector S_A, S_Tf;
        Vector S1_A, S1_Tf;
        int rank_A, rank_Tf;
        int l;
        
        SVD(ATA,U_A,S_A,V_A);
        SVD(TauTTau,U_Tf,S_Tf,V_Tf);
        for(l = S_A.size()-1; l >= 0; l-- )  {
            S_A[l] = sqrt(S_A[l]);
        }
        
        for(l = S_Tf.size()-1; l >= 0; l-- )  {
            S_Tf[l] = sqrt(S_Tf[l]);
        }
        
        for(l = S_A.size()-1; l >= 0; l-- )  {
            if( S_A[l] > tol ) {
                break;
            }
        }
        rank_A = l+1;
        S1_A = S_A.subVector(0,rank_A-1);
        Vector inv_S1_A = S1_A;
        for(l=0; l < inv_S1_A.size(); l++ ) {
            inv_S1_A[l] = 1/S1_A[l];
        }
        
        
        
        for(l = S_Tf.size()-1; l >= 0; l-- )  {
            if( S_Tf[l] > tol ) {
                break;
            }
        }
        rank_Tf = l+1;
        S1_Tf = S_Tf.subVector(0,rank_Tf-1);
        Vector inv_S1_Tf = S1_Tf;
        for(l=0; l < inv_S1_Tf.size(); l++ ) {
            inv_S1_Tf[l] = 1/S1_Tf[l];
        }
        
        Matrix SV_Tf_on_V_A = V_A.transposed()*V_Tf*diagonalMatrix(S_Tf);
        Vector S_Tf_on_V_A = S_Tf;
        
        cout << "V_Tf\n" <<  (V_A.transposed()*V_Tf).toString() << endl;
        cout << "You know" << endl;
        cout << S_Tf_on_V_A.toString() << endl;
        
        for(l=0; l < SV_Tf_on_V_A.cols(); l++ ) {
            S_Tf_on_V_A[l] = norm(SV_Tf_on_V_A.getCol(l));
        }
        
        printVector("Singular values of A:",S_A);
        printVector("Singular values of T:",S_Tf);
        //printMatrix("you know what this is:",);
        printVector("Singular values of Tau expressed on singular vectors of a",S_Tf_on_V_A);

        
        V1_A = V_A.submatrix(0,V_A.rows()-1,0,rank_A-1);
        //V2_A = V_A.submatrix(0,V_A.rows()-1,rank_A,V_A.cols()-1);
        
        V1_Tf = V_Tf.submatrix(0,V_Tf.rows()-1,0,rank_Tf-1);
        //V2_Tf = V_Tf.submatrix(0,V_Tf.rows()-1,rank_Tf,V_T.cols()-1);
        
        
        cout << "Rank A " << rank_A << " rank Tf " << rank_Tf << endl;
        
        

        
    }
    */
    /*
    cout << "~~~~~~~~~~~~~~~~~~~~~Torque FORWARD analysis~~~~~~~~~~~~" << endl;
    for(int joint_index = 3; joint_index < 7; joint_index++ ) {
        cout << "~~~~~~~~~~~~~~~~~~~~~~~~~Check identifiable  subspace joint " << joint_index << " ~~~~~~~~~~~~~~~" << endl;
        Matrix U_At,U_T,V_At,V_T,U_Af,V_Af;
        Matrix V1_At,V1_Af,V1_T;
        Vector S_Af,S_T,S_At;
        Vector S1_At, S1_Af, S1_T;
        int rank_Af,rank_T,rank_At,l;
        
        
        SVD(ATA_forces,U_Af,S_Af,V_Af);
        SVD(ATA_torques,U_At,S_At,V_At);
        SVD(TTT[joint_index],U_T,S_T,V_T);
        for(l = S_Af.size()-1; l >= 0; l-- )  {
            S_Af[l] = sqrt(S_Af[l]);
        }
        for(l = S_At.size()-1; l >= 0; l-- )  {
            S_At[l] = sqrt(S_At[l]);
        }
        
        for(l = S_T.size()-1; l >= 0; l-- )  {
            S_T[l] = sqrt(S_T[l]);
        }
        
        for(l = S_Af.size()-1; l >= 0; l-- )  {
            if( S_Af[l] > tol ) {
                break;
            }
        }
        rank_Af = l+1;
        
        for(l = S_At.size()-1; l >= 0; l-- )  {
            if( S_At[l] > tol ) {
                break;
            }
        }
        rank_At = l+1;
        
        cout << "rank_At" << rank_At << " rank_Af " << rank_Af << endl;
        
        for(l = S_T.size()-1; l >= 0; l-- )  {
            if( S_T[l] > tol ) {
                break;
            }
        }
        rank_T = l+1;
        
        Vector inv_S_Af = Vector(S_Af.size(),0.0);
        for(l=0; l < rank_Af; l++ ) {
            inv_S_Af[l] = 1/S_Af[l];
        }
        Vector inv_S_At = Vector(S_At.size(),0.0);
        for(l=0; l < rank_At; l++ ) {
            inv_S_At[l] = 1/S_At[l];
        }
        
        printVector("inv_S_At",inv_S_At);
        printVector("inv_S_Af",inv_S_Af);
        
        
        cout << "1659" << endl;
        Matrix CORE_f, CORE_t;
        
        CORE_f = diagonalMatrix(S_T)*V_T.transposed()*V_Af*diagonalMatrix(inv_S_Af);
        CORE_t = diagonalMatrix(S_T)*V_T.transposed()*V_At*diagonalMatrix(inv_S_At);
        
        cout << "1665" << endl;

        Matrix U_CORE_f, U_CORE_t, V_CORE_f, V_CORE_t;
        Vector S_CORE_f, S_CORE_t;
        
        SVD(CORE_f,U_CORE_f,S_CORE_f,V_CORE_f);
        
        SVD(CORE_t,U_CORE_t,S_CORE_t,V_CORE_t);
        
        cout << "N_samples " << N_samples << endl;

        cout << "S_CORE_f " << S_CORE_f[0] << endl;
        cout << "S_CORE_t " << S_CORE_t[0] << endl;
        
        cout << "error on forces " << norm((ftStdDev[ICUB_FT_RIGHT_ARM]).subVector(0,2)) << endl;
        cout << "error on torques " << norm((ftStdDev[ICUB_FT_RIGHT_ARM]).subVector(3,5)) << endl;
        
        cout << "error on forces total " << norm(ftStdDev[ICUB_FT_RIGHT_ARM].subVector(0,2))*sqrt(N_samples) << endl;
        cout << "error on torques total " << norm(ftStdDev[ICUB_FT_RIGHT_ARM].subVector(3,5))*sqrt(N_samples) << endl;
        
        cout << "error on estimated torques total " << endl;
    }
    */
    /*
    for(int joint_index = 3; joint_index < 7; joint_index++ ) { 
        Matrix U_A,U_T,V_A,V_T;
        Matrix V1_A,V2_A,V1_T,V2_T;
        Vector S_A,S_T;
        Vector S1_A, S2_A, S1_T;
        int rank_A,rank_T,l;
        
        
        SVD(ATA,U_A,S_A,V_A);
        SVD(TTT[joint_index],U_T,S_T,V_T);
        cout << "~~~~~~~~~~~~~~~~~~~~~~~~~Check identifiable  subspace joint " << joint_index << " ~~~~~~~~~~~~~~~" << endl;
        for(l = S_A.size()-1; l >= 0; l-- )  {
            S_A[l] = sqrt(S_A[l]);
        }
        
        for(l = S_T.size()-1; l >= 0; l-- )  {
            S_T[l] = sqrt(S_T[l]);
        }
        
        printVector("S for A",S_A);
        printVector("S for T",S_T);
        
        for(l = S_A.size()-1; l >= 0; l-- )  {
            if( S_A[l] > tol ) {
                break;
            }
        }
        rank_A = l+1;
        S1_A = S_A.subVector(0,rank_A-1);
            S1_A = S_A.subVector(0,rank_A-1);
        Vector inv_S1_A = Vector(S1_A.size(),0.0);
        Vector inv_S_A = Vector(S_A.size(),0.0);
        for(l=0; l < inv_S1_A.size(); l++ ) {
            inv_S1_A[l] = 1/S1_A[l];
            inv_S_A[l] = inv_S1_A[l];
        }
        
        
        for(l = S_T.size()-1; l >= 0; l-- )  {
            if( S_T[l] > tol ) {
                break;
            }
        }
        rank_T = l+1;
        S1_T = S_T.subVector(0,rank_T-1);
        
        V1_A = V_A.submatrix(0,V_A.rows()-1,0,rank_A-1);
        V2_A = V_A.submatrix(0,V_A.rows()-1,rank_A,V_A.cols()-1);
        
        V1_T = V_T.submatrix(0,V_T.rows()-1,0,rank_T-1);
        V2_T = V_T.submatrix(0,V_T.rows()-1,rank_T,V_T.cols()-1);
        
        Matrix CORE_regr_T_M =(diagonalMatrix(S_T)*V_T.transposed()*V_A*diagonalMatrix(inv_S_A));
        
        Matrix U_CORE, V_CORE;
        Vector S_CORE;
        
        SVD(CORE_regr_T_M,U_CORE,S_CORE,V_CORE);
        
        printVector("S_CORE",S_CORE);
        
        
        Vector base_vector;
        Vector imag_vector;
        for(int l = 0; l < rank_T; l++ ) {
            cout << "Considering " << l << "th base of the identifiable subspace of T (s: " << S_T[l] << " ) " << endl;
            base_vector = V1_T.getCol(l);
            imag_vector = diagonalMatrix(S1_A)*V1_A.transposed()*base_vector;
            cout << "Norm of the image of it transformed with A: " << norm(imag_vector) << endl;
        }
        
        
        Matrix M;
        
        M = V2_A.transposed()*V1_T; //In case V1_T is a subspace of V1_A, this product should give a zero matrix
        cout << "Rank A " << rank_A << " rank T " << rank_T << endl;
        cout << "M size " << M.rows() << " " << M.cols() << endl;
        //printMatrix("M",M);
    }*/
}

Matrix inertiaObserver_thread::diagonalMatrix(Vector S)
{
    Matrix ret = Matrix(S.size(),S.size());
    ret.zero();
    for(unsigned int i=0;i<S.size();i++ ) {
        ret(i,i) = S[i];
    }
    return ret;
}

Matrix inertiaObserver_thread::getOnlyDynamicParam(Matrix all_param, double tol)
{
    YARP_ASSERT(all_param.rows() % 10 == 0);
    Matrix U,V;
    Vector S;
    
    //All mass/center of masses are not only dynamic
    Vector zero_row(all_param.cols());
    zero_row.zero();
    for(int i = 0; i < all_param.rows(); i++ ) {
        if( i%10 < 4 ) {
            all_param.setRow(i,zero_row);
        }
    }
    
    SVD(all_param,U,S,V);
    
    if( tol < 0 ) {
        tol = max(all_param.rows(),all_param.cols())*numeric_limits<double>::epsilon()*S[0];
    }
    
    int rank;
    
    for(rank = 0; rank < (int)S.size(); rank++ ) {
        if( S[rank] < tol ) {
            break;
        }
    }
    
    
    Matrix U1;
    U1 = U.submatrix(0,U.rows()-1,0,rank-1);
    
    return U1;
    
    
}


void inertiaObserver_thread::debug_generate_yarpscope_xml_only_param(iCubFT ft) {
    
    ofstream xml_file;
    vector<string> colors;
    vector<string> param_colors;
    colors.push_back("Red");
    colors.push_back("Green");
    colors.push_back("Blue");
    colors.push_back("gold01");
    colors.push_back("chocolate1");
    colors.push_back("pink01");
    colors.push_back("orchid1");
    colors.push_back("OliveDrab1");
    colors.push_back("12D4A1");
    colors.push_back("B25421");
    colors.push_back("246701");
    colors.push_back("2345B2");
    colors.push_back("khaki1");
    colors.push_back("DeepSkyBlue1");
    colors.push_back("turquoise1");
    colors.push_back("cyan2");
    colors.push_back("MistyRose1");
    colors.push_back("LightSlateGray");
    colors.push_back("lavander");
    colors.push_back("ForestGreen");
    colors.push_back("goldenrod");
    colors.push_back("gray95");
    colors.push_back("tomato1");
    colors.push_back("sienna1");
    colors.push_back("DodgerBlue1");
    colors.push_back("ivory1");
    colors.push_back("SpringGreen");
    colors.push_back("DarkMagenta");
    colors.push_back("tan1");
    colors.push_back("IndianRed1");
    colors.push_back("snow1");
    colors.push_back("MediumSeaGreen");
    colors.push_back("WhiteSmoke");
    colors.push_back("firebrick4");
    colors.push_back("RosyBrown1");
    colors.push_back("purple4");
    colors.push_back("DarkGrey");
    colors.push_back("burlywood1");
    colors.push_back("brown2");
    colors.push_back("DarkOrange3");
    colors.push_back("DeepPink4");
    colors.push_back("thistle4");



    
    printf("Generating yarpscope xml only for param...\n");

    
    
    xml_file.open ("debug_inertiaObserver_only_param.xml");
    xml_file << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
    xml_file << "<portscope rows=\"2\" columns=\"2\">" << endl;


    xml_file << "<plot gridx=\"" << 0 << "\" " 
                       << "gridy=\"" << 0 << "\" " 
                       << "hspan=\"" << 2 << "\" " 
                       << "vspan=\"" << 2 << "\" " 
                       << "title=\"" << "parameters" << "\">" << endl;
    for( unsigned int i = 0; i != (unsigned) identifiable_parameters[ft].cols(); i++ ) {
        for(unsigned int j = 0; j != paramEstimators[ft].size(); j++ ) {
            xml_file << "<graph remote=\"" << "/"+local_name+"/"+FTNames[ft]+"/FT_"+paramEstimators[ft][j]->getName()+"_estimated_parameters:o" << "\" " << "index = \"" << i << "\" " << "color = \"" << colors[(i+j)%colors.size()] << "\" title=\"" << paramEstimators[ft][j]->getName() << " param " << i <<  "\" /> " << endl;
        }
    }
            xml_file << "</plot>" << endl;


    
    xml_file << "</portscope>" << endl;
    xml_file.close();
}


