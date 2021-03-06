/* 
 * Copyright: (C) 2014 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Matej Hoffmann
 * email:  matej.hoffmann@iit.it
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


/** 
\defgroup reachCtrl
 
@ingroup icub_module  
 
Module that makes the iCub reach with its hand to a point specified in Cartesian coordinates and then sends a trigger for grasp on the output.
 
Author: Matej Hoffmann

CopyPolicy: Released under the terms of the GNU GPL v2.0. 

\section intro_sec Description 
Module that makes the iCub reach with its hand to a point specified in Cartesian coordinates and then sends a trigger for grasp on the output.
The reaching part is modified from demoGrasp_IIT_ISR.

1) The input is specified on the input port - stream or rpc.  
 
2) If there is a new target, the robot picks the arm in the ipsilateral space and reaches there.

3) If successful, it sends a signal on the output port telling a grasping module which hand to use to grasp.

There are timeouts for the reach and grasp, then the robot goes back home.
 
\section lib_sec Libraries 
- YARP libraries. 
- \ref icubmod library. 

\section parameters_sec Parameters
 
\section portsa_sec Ports Accessed
The robot interface is assumed to be operative.
Cartesian interface is required. 
 
\section portsc_sec Ports Created 

Input ports
- \e /<modName>/reachingTarget:i receives a bottle containing three coordinates of the point to be reached in robot root FoR
- \e /<modName>/reachCtrl/rpc:i can be used to issue the targets for the robot in the format "go x y z"; the "home" command is also recognized 

Output ports
- \e /<modName>/handToBeClosed:o sends an int derived from the SkinPart enum class - SKIN_LEFT_HAND == 1 or SKIN_RIGHT_HAND == 4

\section in_files_sec Input Data Files
None.

\section out_data_sec Output Data Files 
None. 

\section tested_os_sec Tested OS
Linux

\author Matej Hoffmann

*/

#include <gsl/gsl_math.h>

#include <yarp/os/all.h>
#include <yarp/dev/all.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Network.h>
#include <yarp/sig/Vector.h>
#include <yarp/math/Math.h>
#include <iCub/ctrl/neuralNetworks.h>
#include <iCub/skinDynLib/common.h> 

#define DEFAULT_THR_PER     20

#define NOARM               0
#define LEFTARM             1
#define RIGHTARM            2
#define USEDARM             3

#define OPENHAND            0
#define CLOSEHAND           1

#define STATE_IDLE              0
#define STATE_REACH             1
#define STATE_CHECKMOTIONDONE   2
#define STATE_RELEASE           3
#define STATE_WAIT              4
#define STATE_WAIT_FOR_GRASP    5
#define STATE_GO_HOME           6

YARP_DECLARE_DEVICES(icubmod)

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::dev;
using namespace yarp::math;
using namespace iCub::ctrl;

// List of the parts composing the skin of iCub
//alternatively - instead of (re-)defining here, include skinDynLib/common.h
#ifndef __COMMON_H__
enum SkinPart {
SKIN_PART_UNKNOWN=0,
SKIN_LEFT_HAND, SKIN_LEFT_FOREARM, SKIN_LEFT_UPPER_ARM,
SKIN_RIGHT_HAND, SKIN_RIGHT_FOREARM, SKIN_RIGHT_UPPER_ARM,
SKIN_FRONT_TORSO,
SKIN_PART_ALL, SKIN_PART_SIZE
};
#else
 using namespace iCub::skinDynLib;
#endif

class reachCtrlThread: public RateThread
{
protected:
    ResourceFinder &rf;
    
    string name;
    string robot;
    
    bool useLeftArm;
    bool useRightArm;
    int  armSel;
    int thread_period_ms;
    
    PolyDriver *drvTorso, *drvLeftArm, *drvRightArm;
    PolyDriver *drvCartLeftArm, *drvCartRightArm;
    
    IEncoders         *encTorso;
    IPositionControl  *posTorso;
    IEncoders         *encArm;
    IPositionControl  *posArm;
    ICartesianControl *cartArm;
    
    BufferedPort<Bottle> inportTargetCoordinates;
    Bottle                 *targetCoordinates;
    BufferedPort<Bottle> outportHandToBeClosed;
    
    Vector leftArmReachOffs;
    Vector leftArmHandOrien;
    Vector leftArmJointsStiffness;
    Vector leftArmJointsDamping;

    Vector rightArmReachOffs;
    Vector rightArmHandOrien;
    Vector rightArmJointsStiffness;
    Vector rightArmJointsDamping;
    
    Vector *armReachOffs;
    Vector *armHandOrien;
    
    Vector homePoss, homeVels;
    
    bool wentHome;
    bool leftArmImpVelMode;
    bool rightArmImpVelMode;
    
    bool newTargetFromRPC;
    bool newTargetFromPort;

    double trajTime;
    double reachTol;
    double reachTimer, reachTmo;
    double graspTimer, graspTmo;
    
    struct {
        double minX, maxX;
        double minY, maxY;
        double minZ, maxZ;
    } reachableSpace;
  
    Vector openHandPoss;
    Vector handVels;
    
    Vector torso;
    
    Vector targetPosFromPort;
    Vector targetPosFromRPC;
    Vector targetPos;
    
    Matrix R,Rx,Ry,Rz;

    int state;
    int startup_context_id_left;
    int startup_context_id_right;
    
     void getTorsoOptions(Bottle &b, const char *type, const int i, Vector &sw, Matrix &lim)
    {
        if (b.check(type))
        {
            Bottle &grp=b.findGroup(type);
            sw[i]=grp.get(1).asString()=="on"?1.0:0.0;

            if (grp.check("min","Getting minimum value"))
            {
                lim(i,0)=1.0;
                lim(i,1)=grp.find("min").asDouble();
            }

            if (grp.check("max","Getting maximum value"))
            {
                lim(i,2)=1.0;
                lim(i,3)=grp.find("max").asDouble();
            }
        }
    }

    void getArmOptions(Bottle &b, Vector &reachOffs, Vector &orien, bool &impVelMode,
                       Vector &impStiff, Vector &impDamp)
    {
        if (b.check("reach_offset","Getting reaching offset"))
        {
            Bottle &grp=b.findGroup("reach_offset");
            int sz=grp.size()-1;
            int len=sz>3?3:sz;

            for (int i=0; i<len; i++)
                reachOffs[i]=grp.get(1+i).asDouble();
        }

        if (b.check("hand_orientation","Getting hand orientation"))
        {
            Bottle &grp=b.findGroup("hand_orientation");
            int sz=grp.size()-1;
            int len=sz>4?4:sz;

            for (int i=0; i<len; i++)
                orien[i]=grp.get(1+i).asDouble();
        }

        impVelMode=b.check("impedance_velocity_mode",Value("off"),"Getting arm impedance-velocity-mode").asString()=="on"?true:false;

        if (b.check("impedance_stiffness","Getting joints stiffness"))
        {
            Bottle &grp=b.findGroup("impedance_stiffness");
            size_t sz=grp.size()-1;
            size_t len=sz>impStiff.length()?impStiff.length():sz;

            for (size_t i=0; i<len; i++)
                impStiff[i]=grp.get(1+i).asDouble();
        }

        if (b.check("impedance_damping","Getting joints damping"))
        {
            Bottle &grp=b.findGroup("impedance_damping");
            size_t sz=grp.size()-1;
            size_t len=sz>impDamp.length()?impDamp.length():sz;

            for (size_t i=0; i<len; i++)
                impDamp[i]=grp.get(1+i).asDouble();
        }
    }

    void getHomeOptions(Bottle &b, Vector &poss, Vector &vels)
    {
        if (b.check("poss","Getting home poss"))
        {
            Bottle &grp=b.findGroup("poss");
            int sz=grp.size()-1;
            int len=sz>7?7:sz;

            for (int i=0; i<len; i++)
                poss[i]=grp.get(1+i).asDouble();
        }

        if (b.check("vels","Getting home vels"))
        {
            Bottle &grp=b.findGroup("vels");
            int sz=grp.size()-1;
            int len=sz>7?7:sz;

            for (int i=0; i<len; i++)
                vels[i]=grp.get(1+i).asDouble();
        }
    }

    
    void initCartesianCtrl(Vector &sw, Matrix &lim, const int sel=USEDARM)
    {
        ICartesianControl *icart=cartArm;
        Vector dof;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
            {
                drvCartLeftArm->view(icart);
                icart->storeContext(&startup_context_id_left);
                icart->restoreContext(0);
            }
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
            {
                drvCartRightArm->view(icart);
                icart->storeContext(&startup_context_id_right);
                icart->restoreContext(0);
            }
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Initializing %s controller ...\n",type.c_str());

        icart->setTrackingMode(false);
        icart->setTrajTime(trajTime);
        icart->setInTargetTol(reachTol);
        icart->getDOF(dof);

        for (size_t j=0; j<sw.length(); j++)
        {
            dof[j]=sw[j];

            if (sw[j] && (lim(j,0) || lim(j,2)))
            {
                double min, max;
                icart->getLimits(j,&min,&max);

                if (lim(j,0))
                    min=lim(j,1);

                if (lim(j,2))
                    max=lim(j,3);

                icart->setLimits(j,min,max);
                fprintf(stdout,"jnt #%d in [%g, %g] deg\n",(int)j,min,max);
            }
        }

        icart->setDOF(dof,dof);

        fprintf(stdout,"DOF's=( ");
        for (size_t i=0; i<dof.length(); i++)
            fprintf(stdout,"%s ",dof[i]>0.0?"on":"off");
        fprintf(stdout,")\n");
    }
    
    void getSensorData()
    {
         if (encTorso->getEncoders(torso.data())){
            R=rotx(torso[1])*roty(-torso[2])*rotz(-torso[0]);
         }
    }
    
    bool checkPosFromPortInput(Vector &target_pos)
    {
        if (Bottle *targetPosNew=inportTargetCoordinates.read(false))
        {
            target_pos[0]=targetPosNew->get(0).asDouble();
            target_pos[1]=targetPosNew->get(1).asDouble();
            target_pos[2]=targetPosNew->get(2).asDouble();
            return true;
           
        }
        else{
            return false;   
        }
    }
    
    void moveHand(const int action, const int sel=USEDARM)
    {
        IPositionControl *ipos=posArm;
        Vector *poss=NULL;
        string actionStr, type;

        switch (action)
        {
        case OPENHAND:
                poss=&openHandPoss;
                actionStr="Opening";
                break;

        case CLOSEHAND:
                printf("ERROR: CLOSEHAND is not suppported by this module.");
                return;
                break;

        default:
            return;
        }

        if (sel==LEFTARM)
        {    
            drvLeftArm->view(ipos);
            type="left_hand";
        }
        else if (sel==RIGHTARM)
        {    
            drvRightArm->view(ipos);
            type="right_hand";
        }
        else
            type=armSel==LEFTARM?"left_hand":"right_hand";

        fprintf(stdout,"*** %s %s\n",actionStr.c_str(),type.c_str());

        for (size_t j=0; j<handVels.length(); j++)
        {
            int k=homeVels.length()+j;

            ipos->setRefSpeed(k,handVels[j]);
            ipos->positionMove(k,(*poss)[j]);
        }
    }

    
    void openHand(const int sel=USEDARM)
    {
        moveHand(OPENHAND,sel);
    }
    
    void selectArm()
    {
        if (useLeftArm && useRightArm)
        {
           if(targetPos[1]>0.0){
               armSel=RIGHTARM; 
               printf("Target in right space - selecting right arm.\n");
               drvRightArm->view(encArm);
               drvRightArm->view(posArm);
               drvCartRightArm->view(cartArm);
               armReachOffs=&rightArmReachOffs;
               armHandOrien=&rightArmHandOrien;
           }
           else{
               armSel=LEFTARM; 
               printf("Target in left space - selecting left arm.\n");
               drvLeftArm->view(encArm);
               drvLeftArm->view(posArm);
               drvCartLeftArm->view(cartArm);
               armReachOffs=&leftArmReachOffs;
               armHandOrien=&leftArmHandOrien;
           }
         }
    }
    
    void doReach()
    {
        if (useLeftArm || useRightArm)
        {
            if (state==STATE_REACH)
            {
                Vector x=R.transposed()*(targetPos+*armReachOffs);
                limitRange(x);
                x=R*x;
    
                cartArm->goToPose(x,*armHandOrien);
            }
        }
    }
         
    void doIdle()
    {
    }
    
    void steerTorsoToHome()
    {
        Vector homeTorso(3);
        homeTorso.zero();

        Vector velTorso(3);
        velTorso=10.0;

        fprintf(stdout,"*** Homing torso\n");

        posTorso->setRefSpeeds(velTorso.data());
        posTorso->positionMove(homeTorso.data());
    }
    
    void checkTorsoHome(const double timeout=10.0)
    {
        fprintf(stdout,"*** Checking torso home position... ");

        bool done=false;
        double t0=Time::now();
        while (!done && (Time::now()-t0<timeout))
        {
            posTorso->checkMotionDone(&done);
            Time::delay(0.1);
        }

        fprintf(stdout,"*** done\n");
    }

    
    void stopArmJoints(const int sel=USEDARM)
    {
        IEncoders        *ienc=encArm;
        IPositionControl *ipos=posArm;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
            {
                drvLeftArm->view(ienc);
                drvLeftArm->view(ipos);
            }
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
            {
                drvRightArm->view(ienc);
                drvRightArm->view(ipos);
            }
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Stopping %s joints\n",type.c_str());
        for (size_t j=0; j<homeVels.length(); j++)
        {
            double fb;

            ienc->getEncoder(j,&fb);
            ipos->positionMove(j,fb);
        }
    }
     
    void steerArmToHome(const int sel=USEDARM)
    {
        IPositionControl *ipos=posArm;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
                drvLeftArm->view(ipos);
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
                drvRightArm->view(ipos);
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Homing %s\n",type.c_str());
        for (size_t j=0; j<homeVels.length(); j++)
        {
            ipos->setRefSpeed(j,homeVels[j]);
            ipos->positionMove(j,homePoss[j]);
        }

        openHand(sel);
    }
    
     void checkArmHome(const int sel=USEDARM, const double timeout=10.0)
    {
        IPositionControl *ipos=posArm;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
                drvLeftArm->view(ipos);
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
                drvRightArm->view(ipos);
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Checking %s home position... ",type.c_str());

        bool done=false;
        double t0=Time::now();
        while (!done && (Time::now()-t0<timeout))
        {
            ipos->checkMotionDone(&done);
            Time::delay(0.1);
        }

        fprintf(stdout,"*** done\n");
    }
    
    void stopControl()
    {
        if (useLeftArm || useRightArm)
        {
            fprintf(stdout,"stopping control\n");
            cartArm->stopControl();
            Time::delay(0.1);
        }        
    }

    void limitRange(Vector &x)
    {               
        x[0]=x[0]>-0.1 ? -0.1 : x[0];       
    }
    
    Matrix &rotx(const double theta)
    {
        double t=CTRL_DEG2RAD*theta;
        double c=cos(t);
        double s=sin(t);

        Rx(1,1)=Rx(2,2)=c;
        Rx(1,2)=-s;
        Rx(2,1)=s;

        return Rx;
    }

    Matrix &roty(const double theta)
    {
        double t=CTRL_DEG2RAD*theta;
        double c=cos(t);
        double s=sin(t);

        Ry(0,0)=Ry(2,2)=c;
        Ry(0,2)=s;
        Ry(2,0)=-s;

        return Ry;
    }

    Matrix &rotz(const double theta)
    {
        double t=CTRL_DEG2RAD*theta;
        double c=cos(t);
        double s=sin(t);

        Rz(0,0)=Rz(1,1)=c;
        Rz(0,1)=-s;
        Rz(1,0)=s;

        return Rz;
    }
    
    bool reachableTarget(const Vector target_pos){
        if( (target_pos[0] < reachableSpace.minX) || (target_pos[0] > reachableSpace.maxX) ||
            (target_pos[1] < reachableSpace.minY) || (target_pos[1] > reachableSpace.maxY) ||
            (target_pos[2] < reachableSpace.minZ) || (target_pos[2] > reachableSpace.maxZ)){
            printf("Warning: target %f %f %f is outside of reachable area.\n",target_pos[0],target_pos[1],target_pos[2]);
            return false;          
        }
        else{
                return true;
        }
    }
    
    void close()
    {
        delete drvTorso;
        delete drvLeftArm;
        delete drvRightArm;
        delete drvCartLeftArm;
        delete drvCartRightArm;
        
        inportTargetCoordinates.interrupt();
        inportTargetCoordinates.close();
        outportHandToBeClosed.interrupt();
        outportHandToBeClosed.close();
      
    }
    
public:
    reachCtrlThread(const string &_name, ResourceFinder &_rf) : 
                  RateThread(DEFAULT_THR_PER), name(_name), rf(_rf)
    {        
        drvTorso=drvLeftArm=drvRightArm=NULL;
        drvCartLeftArm=drvCartRightArm=NULL;
               
    }
    
    bool threadInit()
    {
        
        //getting values from config file
        // general part
        Bottle &bGeneral=rf.findGroup("general");
        bGeneral.setMonitor(rf.getMonitor());
        robot=bGeneral.check("robot",Value("icub"),"Getting robot name").asString().c_str();
        setRate(bGeneral.check("thread_period",Value(DEFAULT_THR_PER),"Getting thread period [ms]").asInt());
        useLeftArm=bGeneral.check("left_arm",Value("on"),"Getting left arm use flag").asString()=="on"?true:false;
        useRightArm=bGeneral.check("right_arm",Value("on"),"Getting right arm use flag").asString()=="on"?true:false;
        trajTime=bGeneral.check("traj_time",Value(2.0),"Getting trajectory time").asDouble();
        reachTol=bGeneral.check("reach_tol",Value(0.01),"Getting reaching tolerance").asDouble();
        reachTmo=bGeneral.check("reach_tmo",Value(5.0),"Getting reach timeout").asDouble();
        graspTmo=bGeneral.check("grasp_tmo",Value(5.0),"Getting grasp timeout").asDouble();
        
        // torso part
        Bottle &bTorso=rf.findGroup("torso");
        bTorso.setMonitor(rf.getMonitor());

        Vector torsoSwitch(3);   torsoSwitch.zero();
        Matrix torsoLimits(3,4); torsoLimits.zero();

        getTorsoOptions(bTorso,"pitch",0,torsoSwitch,torsoLimits);
        getTorsoOptions(bTorso,"roll",1,torsoSwitch,torsoLimits);
        getTorsoOptions(bTorso,"yaw",2,torsoSwitch,torsoLimits);    
               
         // arm parts
        Bottle &bLeftArm=rf.findGroup("left_arm");
        Bottle &bRightArm=rf.findGroup("right_arm");
        bLeftArm.setMonitor(rf.getMonitor());
        bRightArm.setMonitor(rf.getMonitor());
        
        // arm parts
        leftArmReachOffs.resize(3,0.0);
        leftArmHandOrien.resize(4,0.0);
        leftArmJointsStiffness.resize(5,0.0);
        leftArmJointsDamping.resize(5,0.0);
        rightArmReachOffs.resize(3,0.0);
        rightArmHandOrien.resize(4,0.0);
        rightArmJointsStiffness.resize(5,0.0);
        rightArmJointsDamping.resize(5,0.0);

        getArmOptions(bLeftArm,leftArmReachOffs,leftArmHandOrien,leftArmImpVelMode,
                      leftArmJointsStiffness,leftArmJointsDamping);
        getArmOptions(bRightArm,rightArmReachOffs,rightArmHandOrien,rightArmImpVelMode,
                      rightArmJointsStiffness,rightArmJointsDamping);
       
        // home part
        Bottle &bHome=rf.findGroup("home_arm");
        bHome.setMonitor(rf.getMonitor());
        homePoss.resize(7,0.0); homeVels.resize(7,0.0);
        getHomeOptions(bHome,homePoss,homeVels);
      
        targetPosFromPort.resize(3,0.0);
        targetPosFromRPC.resize(3,0.0);
        targetPos.resize(3,0.0);
       
        //TODO add to config file
        reachableSpace.minX=-0.4; reachableSpace.maxX=-0.2;
        reachableSpace.minY=-0.3; reachableSpace.maxY=0.3;
        reachableSpace.minZ=-0.1; reachableSpace.maxZ=0.1;
        
         // open ports
        inportTargetCoordinates.open((name+"/reachingTarget:i").c_str());
        outportHandToBeClosed.open((name+"/handToBeClosed:o").c_str());
       
        string fwslash="/";

        // open remote_controlboard drivers
        Property optTorso("(device remote_controlboard)");
        Property optLeftArm("(device remote_controlboard)");
        Property optRightArm("(device remote_controlboard)");

        optTorso.put("remote",(fwslash+robot+"/torso").c_str());
        optTorso.put("local",(name+"/torso").c_str());

        optLeftArm.put("remote",(fwslash+robot+"/left_arm").c_str());
        optLeftArm.put("local",(name+"/left_arm").c_str());

        optRightArm.put("remote",(fwslash+robot+"/right_arm").c_str());
        optRightArm.put("local",(name+"/right_arm").c_str());

        drvTorso=new PolyDriver;
        if (!drvTorso->open(optTorso))
        {
            close();
            return false;
        }

        if (useLeftArm)
        {
            drvLeftArm=new PolyDriver;
            if (!drvLeftArm->open(optLeftArm))
            {
                close();
                return false;
            }
        }

        if (useRightArm)
        {
            drvRightArm=new PolyDriver;
            if (!drvRightArm->open(optRightArm))
            {
                close();
                return false;
            }
        }

        // open cartesiancontrollerclient drivers
        Property optCartLeftArm("(device cartesiancontrollerclient)");
        Property optCartRightArm("(device cartesiancontrollerclient)");
       
        optCartLeftArm.put("remote",(fwslash+robot+"/cartesianController/left_arm").c_str());
        optCartLeftArm.put("local",(name+"/left_arm/cartesian").c_str());
    
        optCartRightArm.put("remote",(fwslash+robot+"/cartesianController/right_arm").c_str());
        optCartRightArm.put("local",(name+"/right_arm/cartesian").c_str());

      
        if (useLeftArm)
        {
            drvCartLeftArm=new PolyDriver;
            if (!drvCartLeftArm->open(optCartLeftArm))
            {
                close();
                return false;
            }
            if (leftArmImpVelMode)
            {
                IControlMode      *imode;
                IImpedanceControl *iimp;

                drvLeftArm->view(imode);
                drvLeftArm->view(iimp);

                int len=leftArmJointsStiffness.length()<leftArmJointsDamping.length()?
                        leftArmJointsStiffness.length():leftArmJointsDamping.length();

                for (int j=0; j<len; j++)
                {
                    imode->setImpedanceVelocityMode(j);
                    iimp->setImpedance(j,leftArmJointsStiffness[j],leftArmJointsDamping[j]);
                }
            }
        }

        if (useRightArm)
        {
            drvCartRightArm=new PolyDriver;
            if (!drvCartRightArm->open(optCartRightArm))
            {
                close();
                return false;
            }

            if (rightArmImpVelMode)
            {
                IControlMode      *imode;
                IImpedanceControl *iimp;

                drvRightArm->view(imode);
                drvRightArm->view(iimp);

                int len=rightArmJointsStiffness.length()<rightArmJointsDamping.length()?
                        rightArmJointsStiffness.length():rightArmJointsDamping.length();

                for (int j=0; j<len; j++)
                {
                    imode->setImpedanceVelocityMode(j);
                    iimp->setImpedance(j,rightArmJointsStiffness[j],rightArmJointsDamping[j]);
                }
            }
        }

        // open views
      
        drvTorso->view(encTorso);
        drvTorso->view(posTorso);
              
        if (useLeftArm)
        {
            drvLeftArm->view(encArm);
            drvLeftArm->view(posArm);
            drvCartLeftArm->view(cartArm);
            armReachOffs=&leftArmReachOffs;
            armHandOrien=&leftArmHandOrien;
            armSel=LEFTARM;
        }
        else if (useRightArm)
        {
            drvRightArm->view(encArm);
            drvRightArm->view(posArm);
            drvCartRightArm->view(cartArm);
            armReachOffs=&rightArmReachOffs;
            armHandOrien=&rightArmHandOrien;
            armSel=RIGHTARM;
        }
        else
        {
            encArm=NULL;
            posArm=NULL;
            cartArm=NULL;
            armReachOffs=NULL;
            armHandOrien=NULL;
            armSel=NOARM;
        }

        // init
        int torsoAxes;
        encTorso->getAxes(&torsoAxes);
        torso.resize(torsoAxes,0.0);

        targetPos.resize(3,0.0);
        R=Rx=Ry=Rz=eye(3,3);

        initCartesianCtrl(torsoSwitch,torsoLimits,LEFTARM);
        initCartesianCtrl(torsoSwitch,torsoLimits,RIGHTARM);

        // steer the robot to the initial configuration
        stopControl();
        steerTorsoToHome();
        steerArmToHome(LEFTARM);
        steerArmToHome(RIGHTARM);
     
        wentHome=false;
        state=STATE_IDLE;

        return true;
    }

    bool setTargetFromRPC(const Vector target_pos){
        for(int i=0;i<target_pos.size();i++)
        {
            targetPos[i]=target_pos[i];
        }
        //printf("reachingThread::setTargetFromRPC: Setting targetPos from RPC to %f %f %f.\n",targetPos[0],targetPos[1],targetPos[2]);
        newTargetFromRPC = true;
        return true;
    }
    
    bool setHomeFromRPC(){
    // steer the robot to the initial configuration
        state = STATE_GO_HOME;
        return true;
    }
   
    void run()
    {
        bool newTarget = false;
       
        getSensorData();
        
        newTargetFromPort =  checkPosFromPortInput(targetPosFromPort);
        if(newTargetFromPort || newTargetFromRPC){ //target from RPC is set asynchronously
            newTarget = true;
            if (newTargetFromRPC){ //RPC has priority
                printf("run: setting new target from rpc: %f %f %f \n",targetPos[0],targetPos[1],targetPos[2]);
                newTargetFromRPC = false;
            }
            else{
                targetPos = targetPosFromPort;
                printf("run: setting new target from port: %f %f %f \n",targetPos[0],targetPos[1],targetPos[2]);
                newTargetFromPort = false;
            }
        }
        
        if (state==STATE_IDLE){
            if (newTarget){
                if (reachableTarget(targetPos)){
                    printf("--- Got new target => REACHING\n"); 
                    state = STATE_REACH;
                    reachTimer = Time::now();
                }
                else{
                    printf("Target (%f %f %f) is outside of reachable space - x:<%f %f>, y:<%f %f>, z:<%f %f>\n",
                           targetPos[0],targetPos[1],targetPos[2],reachableSpace.minX,reachableSpace.maxX,reachableSpace.minY,
                           reachableSpace.maxY,reachableSpace.minZ,reachableSpace.maxZ);
                }
            }
            //else - remain idle
        }
        else if(state==STATE_REACH){
                selectArm();
                doReach();
                state = STATE_CHECKMOTIONDONE;
        }
          
        else if(state == STATE_CHECKMOTIONDONE)
        {
           bool done;
           cartArm->checkMotionDone(&done);
                if (!done)
                {
                    if( ((Time::now()-reachTimer)>reachTmo)){
                           fprintf(stdout,"--- Reach timeout => go home\n");
                           state = STATE_GO_HOME;
                    }
                }
                else{ //done
                    fprintf(stdout,"--- Reach done => wait for grasp\n");
                    state = STATE_WAIT_FOR_GRASP;
                    graspTimer = Time::now();
                    Bottle& b = outportHandToBeClosed.prepare();
                    b.clear();
                    cout << "######### Bottle cleared" << endl;
                    if(armSel==LEFTARM){
                        b.addInt(static_cast<int>(SKIN_LEFT_HAND)); 
                    }
                    else{
                        b.addInt(static_cast<int>(SKIN_RIGHT_HAND));
                    }
                    outportHandToBeClosed.write();
                }
        }
        else if (state == STATE_WAIT_FOR_GRASP){
             if( ((Time::now()-graspTimer)>graspTmo)){
                           fprintf(stdout,"--- Grasp timeout => go home\n");
                           state = STATE_GO_HOME;
             }
        }   
        else if(state==STATE_GO_HOME){
            stopControl();
            steerTorsoToHome();
            steerArmToHome(LEFTARM);
            steerArmToHome(RIGHTARM);
            fprintf(stdout,"--- I'm home => go idle\n");
            state=STATE_IDLE;
        }
    }

    void threadRelease()
    {
        stopControl();
        steerTorsoToHome();
        steerArmToHome(LEFTARM);
        steerArmToHome(RIGHTARM);

        checkTorsoHome(3.0);
        checkArmHome(LEFTARM,3.0);
        checkArmHome(RIGHTARM,3.0);

        if (useLeftArm)
        {
            ICartesianControl *icart;
            drvCartLeftArm->view(icart);
            icart->restoreContext(startup_context_id_left);

            if (leftArmImpVelMode)
            {
                IControlMode *imode;
                drvLeftArm->view(imode);

                int len=leftArmJointsStiffness.length()<leftArmJointsDamping.length()?
                        leftArmJointsStiffness.length():leftArmJointsDamping.length();

                for (int j=0; j<len; j++)
                    imode->setVelocityMode(j);
            }
        }

        if (useRightArm)
        {
            ICartesianControl *icart;
            drvCartRightArm->view(icart);
            icart->restoreContext(startup_context_id_right);

            if (rightArmImpVelMode)
            {
                IControlMode *imode;
                drvRightArm->view(imode);

                int len=rightArmJointsStiffness.length()<rightArmJointsDamping.length()?
                        rightArmJointsStiffness.length():rightArmJointsDamping.length();

                for (int j=0; j<len; j++)
                    imode->setVelocityMode(j);
            }
        }

        close();
    }
    
};


class reachCtrlModule: public RFModule
{
protected:
    reachCtrlThread *thr;    
    Port           rpcPort;

public:
    reachCtrlModule() { }

    bool configure(ResourceFinder &rf)
    {
        thr=new reachCtrlThread(getName().c_str(),rf);
        if (!thr->start())
        {
            delete thr;    
            return false;
        }

        rpcPort.open(getName("/rpc:i"));
        attach(rpcPort);

        return true;
    }

    bool respond(const Bottle &command, Bottle &reply)
    {
        Vector target_pos;
        int ack =Vocab::encode("ack");
        int nack=Vocab::encode("nack");

        target_pos.resize(3,0.0);
        
        if (command.size()>0)
        {
            switch (command.get(0).asVocab())
            {
                //-----------------
                case VOCAB2('g','o'):
                //case VOCAB2('G','O'):
                {
                    
                    //int res=Vocab::encode("new target");
                    if (command.size()==4){ //"go x y z"
                        target_pos(0)=command.get(1).asDouble();
                        target_pos(1)=command.get(2).asDouble();
                        target_pos(2)=command.get(3).asDouble();
                        //printf("reachingModule():respond: Will set target to %f %f %f.\n",target_pos[0],target_pos[1],target_pos[2]);
                        if (thr -> setTargetFromRPC(target_pos))
                        {
                            reply.addVocab(ack);
                            reply.addVocab(VOCAB2('g','o'));
                            reply.addDouble(target_pos(0));
                            reply.addDouble(target_pos(1));
                            reply.addDouble(target_pos(2));
                        }
                        else{
                            reply.addVocab(nack);
                        }
                    }
                    else{
                        reply.addVocab(nack);
                    }
                    
                   // reply.addVocab(res);
                    return true;
                }
                case VOCAB4('h','o','m','e'):
                //case VOCAB4('H','O','M','E'):
                     //int res2=Vocab::encode("home");
                     if (thr -> setHomeFromRPC())
                     {
                            reply.addVocab(ack);
                            reply.addVocab(VOCAB4('h','o','m','e'));
                     }
                     else{
                        reply.addVocab(nack);
                     }
                     return true;
                //-----------------
                default:
                    return RFModule::respond(command,reply);
            }
        }

        reply.addVocab(nack);
        return true;
    }

    
    bool close()
    {
        rpcPort.interrupt();
        rpcPort.close();

        thr->stop();
        delete thr;

        return true;
    }

    double getPeriod()    { return 0.05;  } // 0.05 s = 50 ms ~ 20 Hz
    bool   updateModule() { return true; }
};


int main(int argc, char *argv[])
{
    Network yarp;
    if (!yarp.checkNetwork())
    {
        fprintf(stdout,"YARP server not available!\n");
        return -1;
    }

    YARP_REGISTER_DEVICES(icubmod)
    
    ResourceFinder rf;
    rf.setVerbose(true);
    rf.setDefaultContext("RBDemo");
    rf.setDefaultConfigFile("reachCtrl.ini");
    rf.configure(argc,argv);

    reachCtrlModule mod;
    mod.setName("/reachCtrl");
    
    cout << "Configuring and starting reachCtrl module. \n";
    return mod.runModule(rf);
}
