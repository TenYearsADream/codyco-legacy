#!/xde
#
# Copyright (C) 2013-7 CODYCO Project
# Author: Serena Ivaldi, Joseph Salini
# email:  serena.ivaldi@isir.upmc.fr
#
# Permission is granted to copy, distribute, and/or modify this program
# under the terms of the GNU General Public License, version 2 or any
# later version published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License for more details
#

import xde_world_manager as xwm
import xde_robot_loader  as xrl
import xde_resources     as xr
import xde_isir_controller as xic
import lgsm
import time
import pylab as pl
import numpy
import os
from common.timeobserver import TimeObserver

###################### PARAMETERS ##############################
pi = lgsm.np.pi
rad2deg = 180.0/pi
deg2rad = pi/180.0
# height of the waist
waist_z = 0.58


# distance of the wall
wall_x = -0.35
# height of contact on the table
contact_z = waist_z+0.3
contact_x = wall_x
contact_y = 0.2

######## SIMULATION #############################"
# if False, it makes the simulation without output
simulation_with_graphic_visualization = True
# dt of the simulation
dt = 0.01
# minimum distance for computing the local distance between shapes in possible collision
local_minimal_distance = 0.02 #0.05


#### WORLD PARAMETERS

## GROUND
ground_flat = True
ground_uneven = False
#
ground_flat_init_pose = [0,0,0,1,0,0,0]
ground_uneven_init_pose = [0,0.2,0.52,1,0,0,0]

## ROBOT
robot_icub = False
robot_icub_init_pose = [0,0,0.6,1,0,0,0]
    
robot_name = "robot"
robot_fixed_base = False
robot_remove_joint_limits = False


robot_kuka = False

## SETTINGS WORLD
world_parque = True
world_movingWall = False
world_sofa = False
world_cubes = False
world_chairs = False

world_parque_init_pose = [-0.95,-2,0,0.707,0,0,0.707]

#### CONTACTS
contact_law_type = 1 # = 0=no friction, 1=cone, 2=cone+friction in rotation
contact_law_coeff_friction = 1.5 # = mu=coeff of friction

contact_enable_robot_environment = True
contact_show_feet_environment = True
contact_show_body_environment = False
contact_show_hands_environment = True

####### WEIGHTS OF THE TASKS #####################

Weight_head_stabilization_task = 0.05
Weight_waist_task = 0.4
Weight_COM_task = 1.0
Weight_hand_task = 1.0
Weight_hand_task_or = 4

# weight of the force task = 1.0 by default because we don't know how to weight it wrt the other tasks
Weight_force_task = 1.0

#### FORCE TASK

# the desired force is expressed in the world frame
force_desired = lgsm.vector(0,0,0,-10,0,0) # mux, muy, muz, fx, fy,fz
is_expressed_in_world_frame = True

########### CONTROLLER ###############

# formulation of the control problem
# False= use full problem (ddq, Fext, tau) => more stable, but slower (not inverse of mass matrix)
# True= use reduced problem (Fext, tau) => faster
use_reduced_problem_no_ddq = False

# solver for the quadratic problem 
# qld = more accurate
solver = "qld" # "quadprog"

###################### PARAMETERS ##############################





################################################################
##      
##           CREATION OF DYNAMICAL AGENTS
##
################################################################



##### AGENTS
wm = xwm.WorldManager()
wm.createAllAgents(dt, lmd_max=local_minimal_distance, create_graphic=simulation_with_graphic_visualization)
wm.resizeWindow("mainWindow",  640, 480, 50, 50)


##### WORLD

### GROUND
if ground_flat == True: #flat ground
    groundWorld = xrl.createWorldFromUrdfFile(xr.ground, "ground", ground_flat_init_pose, True, 0.001, 0.001)
    wm.addWorld(groundWorld)
    environmentItems = ["ground.ground"]
elif ground_uneven == True: #uneven ground
        groundWorld = xrl.createWorldFromDae("resources/uneven_surface_with_walls.dae", "ground", ground_uneven_init_pose, True, True, None, 0.001, 1.0, 1.0, None, None, 'material.concrete')
        wm.addWorld(groundWorld)
        environmentItems = ["ground"]
else:
    print "Only ground==flat or uneven is supported for now"
    exit()


### ENVIRONMENT 
if world_movingWall == True: # MOVING WALL
    robotWorld = xrl.createWorldFromUrdfFile("resources/moving_wall.xml", "moving_wall", [wall_x,0.2,0.85,1,0,0,0], True, 0.001, 0.01)
    wm.addWorld(robotWorld)
    dynModel_moving_wall = xrl.getDynamicModelFromWorld(robotWorld)
    environmentItems.append("moving_wall.moving_wall")

if world_parque == True: # PLAY PARK
    playParkWorld = xrl.createWorldFromDae("resources/parque.dae", "playPark", world_parque_init_pose, True, True, None, 0.001, 1.0, 1.0, None, None, 'material.metal')
    wm.addWorld(playParkWorld)
    environmentItems.append("playPark")

### 4 CUBES 
#cubesWorld = xrl.createWorldFromDae("resources/fourCubes.dae", "fourCubes", [0,0.2,0.52,1,0,0,0], True, True, None, 0.001, 1.0, 1.0, None, None, 'material.metal')
#wm.addWorld(cubesWorld)
#
### CHAIRS AND TABLE
#chairsTableWorld = xrl.createWorldFromDae("resources/ext_chairs_and_table.dae", "chairsTable", [0,0.2,0.52,1,0,0,0], True, True, None, 0.001, 1.0, 1.0, None, None, 'material.metal')
#wm.addWorld(chairsTableWorld)
#
### SOFA w POSAPIEDI
#sofaWorld = xrl.createWorldFromDae("resources/clear_sofa.dae", "sofa", [0,0.2,0.52,1,0,0,0], True, True, None, 0.001, 1.0, 1.0, None, None, 'material.metal')
#wm.addWorld(sofaWorld)

## ROBOT
if robot_icub == True:
    robotWorld = xrl.createWorldFromUrdfFile(xr.icub_simple, robot_name, robot_icub_init_pose, robot_fixed_base, 0.001, 0.001)
else:
    print "Only robot==icub is supported for the moment"
    exit()
    
wm.addWorld(robotWorld)
robot = wm.phy.s.GVM.Robot(robot_name)
robot.enableGravity(True)
N  = robot.getJointSpaceDim()
# get dynamic model of the robot
dynModel = xrl.getDynamicModelFromWorld(robotWorld)
jmap     = xrl.getJointMapping(xr.icub_simple, robot)

if robot_remove_joint_limits == True:
    # suppress joint limits of the icub => to allow more movement
    robot.setJointPositionsMin(-4.0*lgsm.ones(N))
    robot.setJointPositionsMax(4.0*lgsm.ones(N))


##### SET CONTACT TYPES
#set contact law
# = material 1
# = material 2
# = 0=no friction, 1=cone, 2=cone+friction in rotation
# = mu=coeff of friction
wm.ms.setContactLawForMaterialPair("material.metal", "material.concrete", contact_law_type, contact_law_coeff_friction)
wm.ms.setContactLawForMaterialPair("material.metal", "material.metal", contact_law_type, contact_law_coeff_friction)

# if contacts are enabled
if contact_enable_robot_environment == True:
    # enable contacts between robot and environment (ground + other items)
    for e in environmentItems:
        print "Enabling contact between robot and "+e
        robot.enableContactWithBody(e,True)
    # visualize contacts between feet and environment (ground + other items)
    if contact_show_feet_environment == True:
        for e in environmentItems:
            for b in ["l_foot", "r_foot"]:
                print "Enabling visualization of contacts between "+b+" and "+e
                wm.contact.showContacts([(robot_name+"."+b,e)])   
    # visualize contacts between the whole body and environment (ground + other items)
    if contact_show_body_environment == True:
        for e in environmentItems:
            for b in robot.getSegmentNames():
                print "Enabling visualization of contacts between "+b+" and "+e
                wm.contact.showContacts([(robot_name+"."+b,e)])              
    # visualize contacts between the hands and environment (ground + other items)
    if contact_show_hands_environment == True:
        for e in environmentItems:
            for b in ["l_hand", "r_hand"]:
                print "Enabling visualization of contacts between "+b+" and "+e
                wm.contact.showContacts([(robot_name+"."+b,e)])            


##### SET INITIAL STATE OF THE ROBOT
qinit = lgsm.zeros(N)
# correspond to:    l_elbow_pitch     r_elbow_pitch     l_knee             r_knee             l_ankle_pitch      r_ankle_pitch      l_shoulder_roll          r_shoulder_roll
for name, val in [("l_elbow_pitch", pi/2.), 
                  ("r_elbow_pitch", pi/2.), 
                  ("l_knee", -0.05), 
                  ("r_knee", -0.05), 
                  ("l_ankle_pitch", -0.05), 
                  ("r_ankle_pitch", -0.05), 
                  ("l_shoulder_roll", pi/2.), 
                  ("r_shoulder_roll", pi/2.)]:
    qinit[jmap[robot_name+"."+name]] = val 

robot.setJointPositions(qinit)
dynModel.setJointPositions(qinit)
robot.setJointVelocities(lgsm.zeros(N))
dynModel.setJointVelocities(lgsm.zeros(N))

head_init = dynModel.getSegmentPosition(dynModel.getSegmentIndex(robot_name+".head"))


##### CONTROLLERS

### controller for the moving wall
if world_movingWall == True:
    ctrl_moving_wall = xic.ISIRController(dynModel_moving_wall, "moving_wall", wm.phy, wm.icsync, solver, use_reduced_problem_no_ddq)
    gposdes = lgsm.zeros(1)
    gveldes = lgsm.zeros(1)
    ## create full task with a very low weight for reference posture
    fullTaskMovWall = ctrl_moving_wall.createFullTask("zero_wall", w=1., kp=50)  
    ## lower if the spring of the wall is too rigid
    fullTaskMovWall.set_q(gposdes)
    fullTaskMovWall.set_qdot(gveldes)


## controller for the robot
ctrl = xic.ISIRController(dynModel, robot_name, wm.phy, wm.icsync, solver, use_reduced_problem_no_ddq)


##### SET TASKS

## full task: bring joints to the initial position
#default: Kd= 2*sqrt(Kp)
fullTask = ctrl.createFullTask("full", "INTERNAL", w=0.0001, kp=0., kd=9., q_des=qinit)

## waist task: better balance
waistTask   = ctrl.createFrameTask("waist", robot_name+'.waist', lgsm.Displacement(), "RXYZ", Weight_waist_task, kp=36., pos_des=lgsm.Displacement(0,0,.58,1,0,0,0))

## back task: to keep the back stable
Weight_back_task=0.001
back_name   = [robot_name+"."+n for n in ['lap_belt_1', 'lap_belt_2', 'chest']]
backTask    = ctrl.createPartialTask("back", back_name, Weight_back_task, kp=16., pos_des=lgsm.zeros(3))

## shoulder task: avoid colliding to its body
Weight_shoulder_task=0.001
shoulder_name   = [robot_name+"."+n for n in ['l_shoulder_1']]
shoulder_pos_des = lgsm.vector(45*deg2rad)
shoulderTask = ctrl.createPartialTask("leftshoulder_task",shoulder_name,Weight_shoulder_task,kp=16.,pos_des=shoulder_pos_des)

## head stabilization task: 
headTask   = ctrl.createFrameTask("head_task", robot_name+'.head', lgsm.Displacement(), "R", Weight_head_stabilization_task, kp=1., pos_des=head_init )

## COM task
CoMTask     = ctrl.createCoMTask("com", "XY", Weight_COM_task, kp=0.) #, kd=0.
# controller that generates the ZMP reference trajectory for stabilizing the COM
COM_reference_position = [[-0.0,0.0]]
ctrl.updater.register( xic.task_controller.ZMPController( CoMTask, dynModel, COM_reference_position, RonQ=1e-6, horizon=1.8, dt=dt, H_0_planeXY=lgsm.Displacement(), stride=3, gravity=9.81) )

## contact tasks for the feet
sqrt2on2 = lgsm.np.sqrt(2.)/2.
RotLZdown = lgsm.Quaternion(-sqrt2on2,0.0,-sqrt2on2,0.0) * lgsm.Quaternion(0.0,1.0,0.0,0.0)
RotRZdown = lgsm.Quaternion(0.0, sqrt2on2,0.0, sqrt2on2) * lgsm.Quaternion(0.0,1.0,0.0,0.0)

ctrl.createContactTask("CLF0", robot_name+".l_foot", lgsm.Displacement([-.039,-.027,-.031]+ RotLZdown.tolist()), 1.5, 0.) # mu, margin
ctrl.createContactTask("CLF1", robot_name+".l_foot", lgsm.Displacement([-.039, .027,-.031]+ RotLZdown.tolist()), 1.5, 0.) # mu, margin
ctrl.createContactTask("CLF2", robot_name+".l_foot", lgsm.Displacement([-.039, .027, .099]+ RotLZdown.tolist()), 1.5, 0.) # mu, margin
ctrl.createContactTask("CLF3", robot_name+".l_foot", lgsm.Displacement([-.039,-.027, .099]+ RotLZdown.tolist()), 1.5, 0.) # mu, margin

ctrl.createContactTask("CRF0", robot_name+".r_foot", lgsm.Displacement([-.039,-.027, .031]+ RotRZdown.tolist()), 1.5, 0.) # mu, margin
ctrl.createContactTask("CRF1", robot_name+".r_foot", lgsm.Displacement([-.039, .027, .031]+ RotRZdown.tolist()), 1.5, 0.) # mu, margin
ctrl.createContactTask("CRF2", robot_name+".r_foot", lgsm.Displacement([-.039, .027,-.099]+ RotRZdown.tolist()), 1.5, 0.) # mu, margin
ctrl.createContactTask("CRF3", robot_name+".r_foot", lgsm.Displacement([-.039,-.027,-.099]+ RotRZdown.tolist()), 1.5, 0.) # mu, margin

## frame task for putting the hand in contact with the wall
# we have to split rotation and xyz posing - because controlling both at same time generates 
# rototranslation vector (xyz+o4)
# beginning of the wrist = where the hand is attached to
H_hand_tool = lgsm.Displacement(0, 0.0, 0, 1, 0, 0, 0)
# inside the hand -> this helps when in contact with a cylindric bar
#H_hand_tool = lgsm.Displacement(0.02, 0, 0, 1, 0, 0, 0)

# rotation matrix hand in world frame
R_right_hand = numpy.matrix([[0, 0, -1],[0, -1, 0],[-1,0,0]])
o_right_hand = lgsm.Quaternion.fromMatrix(R_right_hand)
Task_hand_controlled_var = "RXYZ" # "RXYZ" "R" "XY" "Z" ..

# it is a frame task to displace the hand
#touchWall_task = ctrl.createFrameTask("touchWall", robot_name+'.r_hand', H_hand_tool, "XYZ", Weight_hand_task, kp=36., pos_des=lgsm.Displacement([-0.25,0.15,waist_z+0.25]+o_right_hand.tolist()))
touchWall_task = ctrl.createFrameTask("touchWall", robot_name+'.r_hand', H_hand_tool, "XYZ", Weight_hand_task, kp=15., pos_des=lgsm.Displacement([contact_x,contact_y,contact_z]+o_right_hand.tolist()))


# it is a frame task to rotate the hand
touchWall_orient_task = ctrl.createFrameTask("touchWall_orient", robot_name+'.r_hand', H_hand_tool, "R", Weight_hand_task_or, kp=36., pos_des=lgsm.Displacement([contact_x,contact_y,contact_z]+o_right_hand.tolist()))


## force task for touching the wall
# create task
EETask = ctrl.createForceTask("EE", robot_name+".r_hand", H_hand_tool, Weight_force_task)
# update the task goal
EETask.update(force_desired, is_expressed_in_world_frame)


# if we want to transform a task into a constraint
# it will be a more priority task..... not a priori always verified, but..
#for n in ["CL"+str(s) for s in range(1,5)] + ["CR"+str(s) for s in range(1,5)]:
#    ctrl.s.activateTaskAsConstraint(n)

#######################################################
##### OBSERVERS

## CAMERA FOR RECORDING
# from behind
#cam_traj = [xic.observers.lookAt(lgsm.vector(2,1.5,1.5), lgsm.vector(-0.5,0,0.6), lgsm.vector(0,0,1))]
# lateral view
#cam_traj = [xic.observers.lookAt(lgsm.vector(-0.25,2,1), lgsm.vector(-0.25,0,0.5), lgsm.vector(0,0,1))]
#screenobs = ctrl.updater.register(xic.observers.ScreenShotObserver(wm, "rec_07", cam_traj=cam_traj))

## OBSERVERS COM
cpobs = ctrl.updater.register(xic.observers.CoMPositionObserver(dynModel))
fpobs = ctrl.updater.register(xic.observers.FramePoseObserver(dynModel, robot_name+'.waist', lgsm.Displacement()))
obs_l_foot = ctrl.updater.register(xic.observers.FramePoseObserver(dynModel, robot_name+'.l_foot', lgsm.Displacement()))
obs_r_foot = ctrl.updater.register(xic.observers.FramePoseObserver(dynModel, robot_name+'.r_foot', lgsm.Displacement()))

## OBSERVERS ANKLE
all_joints_obs = ctrl.updater.register(xic.observers.JointPositionsObserver(dynModel))

## OBSERVER MOVING WALL
#tpobs = ctrl_moving_wall.updater.register(xic.observers.TorqueObserver(ctrl_moving_wall))

## TIME OBSERVER
time_obs = ctrl.updater.register(TimeObserver(dt))



## POINT OF VIEW CAMERA
cameraFromBehind=xic.observers.lookAt(lgsm.vector(2,1.5,1.5), lgsm.vector(-0.5,0,0.6), lgsm.vector(0,0,1))
wm.graph_scn.CameraInterface.setDisplacementRelative("mainViewportBaseCamera", cameraFromBehind )


#######################################################
##### SIMULATE
ctrl.s.start()
#ctrl_moving_wall.s.start()

# prepare tasks

EETask.deactivate()
touchWall_orient_task.deactivate()
touchWall_task.deactivate()
#headTask.deactivate()
#shoulderTask.deactivate()

# launch the simulation
wm.startAgents()
wm.phy.s.agent.triggerUpdate()

#import xdefw.interactive
#xdefw.interactive.shell_console()()

print "prepare hand"

touchWall_task.activateAsObjective()

#time.sleep(5.)
while time_obs.getElapsedTime() < 0.1:
    time.sleep(0.001)
time_obs.reset()

print "prepare orientation"
touchWall_orient_task.activateAsObjective()

#time.sleep(5.)
while time_obs.getElapsedTime() < 3.0:
    time.sleep(0.001)
time_obs.reset()

print "activate force task" 

#backTask.deactivate()
EETask.activateAsObjective()

#while time_obs.getElapsedTime() < 0.3:
#    time.sleep(0.001)
#time_obs.reset()

#print "deactivate touching wall" 
#touchWall_task.deactivate()

#time.sleep(5.)
#time.sleep(15.)
while time_obs.getElapsedTime() < 4.0:
    time.sleep(0.001)
time_obs.reset()


wm.stopAgents()
ctrl.s.stop()
#ctrl_moving_wall.s.stop()

##### RESULTS
comtraj = numpy.array(cpobs.get_record())
wtraj = numpy.array(fpobs.get_record())
jtraj = numpy.array(all_joints_obs.get_record())
rf_traj = numpy.array(obs_r_foot.get_record())
lf_traj = numpy.array(obs_l_foot.get_record())
#tpos = tpobs.get_record()

pl.figure()
pl.plot(comtraj[:,0],label="com x")
pl.plot(comtraj[:,1],label="com y")
pl.plot(comtraj[:,2],label="com z")
pl.legend()
#pl.savefig("results/touch_wall_1N_COM.pdf", bbox_inches='tight' )

#pl.figure()
#pl.plot(wtraj[:,0],label="waist x")
#pl.plot(wtraj[:,1],label="waist y")
#pl.plot(wtraj[:,2],label="waist z")
#pl.legend()

#pl.figure()
#pl.plot(rf_traj[:,0],label="right foot x")
#pl.plot(rf_traj[:,1],label="right foot y")
#pl.plot(rf_traj[:,2],label="right foot z")
#pl.legend()

#pl.figure()
#pl.plot(lf_traj[:,0],label="left foot x")
#pl.plot(lf_traj[:,1],label="left foot y")
#pl.plot(lf_traj[:,2],label="left foot z")
#pl.legend()

#pl.figure()
#pl.plot(jtraj[:,0]*rad2deg,label="0")
#pl.plot(jtraj[:,1]*rad2deg,label="1")
#pl.plot(jtraj[:,2]*rad2deg,label="2")
#pl.plot(jtraj[:,3]*rad2deg,label="3")
#pl.plot(jtraj[:,4]*rad2deg,label="4")
#pl.plot(jtraj[:,5]*rad2deg,label="5")
#pl.title("left leg")
#pl.legend()

#pl.figure()
#pl.plot(jtraj[:,26]*rad2deg,label="26")
#pl.plot(jtraj[:,27]*rad2deg,label="27")
#pl.plot(jtraj[:,28]*rad2deg,label="28")
#pl.plot(jtraj[:,29]*rad2deg,label="29")
#pl.plot(jtraj[:,30]*rad2deg,label="30")
#pl.plot(jtraj[:,31]*rad2deg,label="31")
#pl.legend()
#pl.title("right leg")

#pl.figure()
#pl.plot(tpos)
#pl.legend()
#pl.title("force on wall")
#pl.savefig("results/touch_wall_1N_force.pdf", bbox_inches='tight' )

#the last to show all plots
pl.show()






