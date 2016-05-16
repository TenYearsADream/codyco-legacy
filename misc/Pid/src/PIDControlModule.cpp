/*
 * Copyright (C) 2014 CoDyCo
 * Author: Daniele Pucci, Francesco Romano
 * email:  daniele.pucci@iit.it, francesco.romano@iit.it
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
#include "PIDControlModule.h"

#include "PIDControlConstants.h"
#include "PIDControlThread.h"

#include <paramHelp/paramHelperServer.h>

#ifndef ADAPTIVECONTROL_TORQUECONTROL
#include <paramHelp/paramHelperClient.h>
#include <motorFrictionIdentificationLib/jointTorqueControlParams.h>
#endif

#include <cstdio>
#include <string>

using namespace yarp::os;
using namespace paramHelp;

namespace adaptiveControl {
    AdaptiveControlModule::AdaptiveControlModule()
    {
        
    }
    
    bool AdaptiveControlModule::configure(ResourceFinder &rf)
    {
        //-------------------------------------------------- PARAMETER HELPER SERVER ---------------------------------------------------------
        _parameterServer = new ParamHelperServer(adaptiveControlParamDescriptors, adaptiveControlParamDescriptorsSize,
                                                 adaptiveControlCommandDescriptors, adaptiveControlCommandDescriptorsSize);
        if (!_parameterServer) {
            error_out("Could not initialize parameter server. Closing module");
            return false;
        }
        _parameterServer->linkParam(AdaptiveControlParamIDModuleName, &_moduleName);
        _parameterServer->linkParam(AdaptiveControlParamIDPeriod, &_period);
        _parameterServer->linkParam(AdaptiveControlParamIDRobotName, &_robotName);
        _parameterServer->linkParam(AdaptiveControlParamIDRobotPartName, &_robotPart);
        _parameterServer->linkParam(AdaptiveControlParamIDLinkLengths, _linkLengths.data());
        _parameterServer->linkParam(AdaptiveControlParamIDInitialPiHat, _initialPiHat.data());
        _parameterServer->linkParam(AdaptiveControlParamIDInitialXi1, &_initialXi1);
#ifndef ADAPTIVECONTROL_TORQUECONTROL
        _parameterServer->linkParam(AdaptiveControlParamIDJointTorqueControlModuleName, &_torqueControlModuleName);
#endif
        
        _parameterServer->registerCommandCallback(AdaptiveControlCommandIDHelp, this);
        _parameterServer->registerCommandCallback(AdaptiveControlCommandIDQuit, this);
        _parameterServer->registerCommandCallback(AdaptiveControlCommandIDReset, this);
        
        // Read parameters from configuration file (or command line)
        Bottle initMsg;
        _parameterServer->initializeParams(rf, initMsg);
        info_out("*** Parsing configuration file...\n%s\n", initMsg.toString().c_str());
        
        // Open ports for communicating with other modules
        if(!_parameterServer->init(_moduleName)) {
            error_out("Error while initializing parameter server. Closing module.\n");
            return false;
        }
        
        _rpcPort.open(("/" + _moduleName + "/rpc").c_str());
        setName(_moduleName.c_str());
        attach(_rpcPort);
        initMsg.clear();
        
#ifndef ADAPTIVECONTROL_TORQUECONTROL
        _parameterClient = new ParamHelperClient(jointTorqueControl::jointTorqueControlParamDescr, jointTorqueControl::PARAM_ID_SIZE,
                                                 jointTorqueControl::jointTorqueControlCommandDescr, jointTorqueControl::COMMAND_ID_SIZE); //todo
        if (!_parameterClient || !_parameterClient->init(_moduleName, _torqueControlModuleName, initMsg)) {
            error_out("Error while initializing parameter client. Closing module.\n");
            return false;
        }
#endif
        
        
        //--------------------------CONTROL THREAD--------------------------
        _controlThread = new AdaptiveControlThread(_moduleName,
                                                   _robotName,
                                                   _robotPart,
                                                   _period,
                                                   *_parameterServer,
#ifndef ADAPTIVECONTROL_TORQUECONTROL
                                                   *_parameterClient,
#endif
                                                   _linkLengths);
        if (!_controlThread || !_controlThread->start()) {
            error_out("Error while initializing control thread. Closing module.\n");
            return false;
        }
        _controlThread->setInitialConditions(_initialPiHat, _initialXi1);
        
        info_out("Adaptive control module correctly initialized\n");
        return true;
    }
    
    bool AdaptiveControlModule::respond(const Bottle& cmd, Bottle& reply)
    {
        _parameterServer->lock();
        if(!_parameterServer->processRpcCommand(cmd, reply))
            reply.addString((std::string("Command ")+ cmd.toString().c_str() + " not recognized.").c_str());
        _parameterServer->unlock();
        
        // if reply is empty put something into it, otherwise the rpc communication gets stuck
        if(reply.size()==0)
            reply.addString((std::string("Command ") + cmd.toString().c_str() + " received.").c_str());
        return true;
    }
    
    void AdaptiveControlModule::commandReceived(const CommandDescription &cd, const Bottle &params, Bottle &reply)
    {
        switch(cd.id)
        {
            case AdaptiveControlCommandIDHelp:
                _parameterServer->getHelpMessage(reply);
                break;
            case AdaptiveControlCommandIDQuit:
                stopModule();
                reply.addString("Quitting module.");
                break;
            case AdaptiveControlCommandIDReset:
                if (_controlThread && !_controlThread->controlEnabled()) {
                    //set initial conditions
                    _controlThread->resetState();
                    bool result = _controlThread->setInitialConditions(_initialPiHat, _initialXi1);
                    reply.addString((std::string("Reset thread returned ") + (result ? "success" : "failure")).c_str());
                }
                else {
                    reply.addString("Could not reset control thread.");
                }
                break;
        }
    }
    
    bool AdaptiveControlModule::interruptModule()
    {
        if(_controlThread)  _controlThread->stop();
        _rpcPort.interrupt();
        return true;
    }
    
    bool AdaptiveControlModule::close()
    {
        //stop threads
        if(_controlThread) {
            _controlThread->stop();
            delete _controlThread;
            _controlThread = NULL;
        }
        if(_parameterServer) {
            _parameterServer->close();
            delete _parameterServer;
            _parameterServer = NULL;
        }

#ifndef ADAPTIVECONTROL_TORQUECONTROL
        if(_parameterClient) {
            _parameterClient->close();
            delete _parameterClient;
            _parameterClient = NULL;
        }
#endif
        //closing ports
        _rpcPort.close();
        info_out("about to close\n");
        return true;
    }
    
    bool AdaptiveControlModule::updateModule()
    {
        if (!_controlThread) {
            error_out("%s: Error. Control thread pointer is zero.\n", _moduleName.c_str());
            return false;
        }
        double periodMean = 0, periodStdDeviation = 0;
        double usedMean = 0, usedStdDeviation = 0;
        
        _controlThread->getEstPeriod(periodMean, periodStdDeviation);
        _controlThread->getEstUsed(usedMean, usedStdDeviation);
        
        if(periodMean > 1.3 * _period)
        {
            info_out("[WARNING] Control loop is too slow. Real period: %3.3f+/-%3.3f. Expected period %d.\n", periodMean, periodStdDeviation, _period);
            info_out("Duration of 'run' method: %3.3f+/-%3.3f.\n", usedMean, usedStdDeviation);
        }
        
        return true;
    }
}
