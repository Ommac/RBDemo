/* 
 * Copyright (C) 2014 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Raffaello Camoriano
 * email: raffaello.camoriano@iit.it
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

/** 
\defgroup handCtrl
 
Example of grasping module based upon \ref ActionPrimitives 
library. 

Copyright (C) 2010 RobotCub Consortium
 
Author: Raffaello Camoriano

CopyPolicy: Released under the terms of the GNU GPL v2.0. 

\section intro_sec Description 
A module that makes use of \ref ActionPrimitives 
library to open and close either of the hands.

Scenario:

1) Both hands are opened at startup

2) A bottle containing the code of the hand to be closed
is received
 
3) The corresponding hand is closed by calling the close_hand sequence

4) The hand is opened again by the user via rpc:i
 
\section lib_sec Libraries 
- YARP libraries. 
- \ref ActionPrimitives library.  
- \ref SkinDyn library.  

#\section parameters_sec Parameters
 
\section portsa_sec Ports Accessed
The robot interface is assumed to be operative.
 
\section portsc_sec Ports Created 
Aside from the internal ports created by \ref ActionPrimitives 
library, we also have: 
 
- \e /handCtrl/handToBeClosed:i receives a bottle containing the code associated to the hand to close

 
- \e /handCtrl/rpc:i remote procedure call.

Recognized remote commands:
    - 'open_left_hand'
    - 'open_right_hand'
    - 'close_left_hand'
    - 'close_right_hand'

\section in_files_sec Input Data Files
None.

\section out_data_sec Output Data Files 
None. 
 
\section conf_file_sec Configuration Files 
--grasp_model_type \e type 
- specify the grasp model type according to the \ref 
  ActionPrimitives documentation.
 
--grasp_model_left_file \e file 
- specify the path to the file containing the grasp model 
  options for the left hand.

--grasp_model_right_file \e file 
- specify the path to the file containing the grasp model 
  options for the right hand.
 
--handCtrl_hand_sequences_file \e file 
- specify the path to the file containing the hand motion 
  sequences relative to the current context ( \ref
  ActionPrimitives ).
 
--from \e file 
- specify the configuration file (use \e --context option to 
  select the current context).
 
The configuration file passed through the option \e --from
should look like as follows:
 
\code 
[general]
// options used to open a ActionPrimitives object 
robot                           icub
thread_period                   50
default_exec_time               3.0
reach_tol                       0.007
verbosity                       on 
torso_pitch                     on
torso_roll                      off
torso_yaw                       on
torso_pitch_max                 30.0 
tracking_mode                   off 
verbosity                       on 
\endcode 

\section tested_os_sec Tested OS
Windows

\author Raffaello Camoriano
*/ 

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <deque>

#include <yarp/os/Network.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Bottle.h>
#include <yarp/os/BufferedPort.h>
#include <yarp/sig/Vector.h>
#include <yarp/os/Vocab.h>
#include <yarp/math/Math.h>
#include <yarp/dev/Drivers.h>
#include <yarp/conf/system.h>
#include <iCub/perception/models.h>
#include <iCub/action/actionPrimitives.h>
#include <iCub/skinDynLib/common.h>            // Common skin parts codes

#define AFFACTIONPRIMITIVESLAYER    ActionPrimitivesLayer1

YARP_DECLARE_DEVICES(icubmod)

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::dev;
using namespace yarp::math;
using namespace iCub::perception;
using namespace iCub::action;
//using namespace iCub::skinDynLib;



/************************************************************************/
class handCtrl: public RFModule
{
protected:
    AFFACTIONPRIMITIVESLAYER *actionL;    // Action list associated to the left hand
    AFFACTIONPRIMITIVESLAYER *actionR;    // Action list associated to the right hand
    BufferedPort<Bottle>      inPort;
    Port                      rpcPort;

    bool firstRun;

    // Mutex declaration

    Mutex actionMutex;  //Protects  the access to actionsQueue

    // Timeout for the hands to open after a closure required via inPort
    double leftHandTimeOut;
    double rightHandTimeOut;

public:
    /************************************************************************/
    handCtrl()
    {
        actionL=NULL;
        actionR=NULL;
        firstRun=true;

        leftHandTimeOut = 5.0;
        rightHandTimeOut = 5.0;
    }

    bool respond(const Bottle &      command,
                 Bottle &      reply)
    {
        // This method is called when a command string is sent via RPC

        // Get command string
        string receivedCmd = command.get(0).asString().c_str();
       
        bool f;
        int responseCode;   //Will contain Vocab-encoded response

        reply.clear();  // Clear reply bottle

        
        if (receivedCmd == "open_left_hand")
        {
            actionMutex.lock(); //Protected area beginning

            // Stop current motion and clear actions queue
            actionL->stopControl();

            actionL->pushAction("open_hand");
            actionL->checkActionsDone(f,true);
            actionL->areFingersInPosition(f);    // Check for obstructing (grasped) objects

            actionMutex.unlock(); //Protected area ending

            if (!f)
            {
                //Encode response
                responseCode = Vocab::encode("nack");
                reply.addVocab(responseCode);
            }
            else
            {
                //Encode response
                responseCode = Vocab::encode("ack");
                reply.addVocab(responseCode);
            }
        }

        else if (receivedCmd == "open_right_hand")
        {

            actionMutex.lock(); //Protected area beginning

            // Stop current motion and clear actions queue
            actionR->stopControl();

            actionR->pushAction("open_hand");
            actionR->checkActionsDone(f,true);
            actionR->areFingersInPosition(f);    // Check for obstructing (grasped) objects

            actionMutex.unlock(); //Protected area ending

            if (!f)
            {
                 //Encode response
                responseCode = Vocab::encode("nack");
                reply.addVocab(responseCode);
            }
            else
            {
                 //Encode response
                responseCode = Vocab::encode("ack");
                reply.addVocab(responseCode);
            }
        }

        else if (receivedCmd == "close_right_hand")
        {
            actionMutex.lock(); //Protected area beginning

            // Stop current motion and clear actions queue
            actionR->stopControl();

            actionR->pushAction("close_hand");
            actionR->checkActionsDone(f,true);
            actionR->areFingersInPosition(f);    // Check for obstructing (grasped) objects

            actionMutex.unlock(); //Protected area ending

            if (!f)
            {
                 //Encode response
                responseCode = Vocab::encode("nack");
                reply.addVocab(responseCode);
            }
            else
            {
                 //Encode response
                responseCode = Vocab::encode("ack");
                reply.addVocab(responseCode);
            }
        }

        else if (receivedCmd == "close_left_hand")
        {
            actionMutex.lock(); //Protected area beginning

            // Stop current motion and clear actions queue
            actionL->stopControl();

            actionL->pushAction("close_hand");
            actionL->checkActionsDone(f,true);
            actionL->areFingersInPosition(f);    // Check for obstructing (grasped) objects

            actionMutex.unlock(); //Protected area ending

            if (!f)
            {
                 //Encode response
                responseCode = Vocab::encode("nack");
                reply.addVocab(responseCode);
            }
            else
            {
                 //Encode response
                responseCode = Vocab::encode("ack");
                reply.addVocab(responseCode);
            }
        }
        else if (receivedCmd == "help")
        {
            reply.addVocab(Vocab::encode("many"));
            reply.addString("Available commands are:");
            reply.addString("help");
            reply.addString("quit");
            reply.addString("open_left_hand");
            reply.addString("open_right_hand");
            reply.addString("close_left_hand");
            reply.addString("close_right_hand");
        }
        else if (receivedCmd == "quit")
        {
            reply.addString("Quitting.");
            return false; //note also this
        }
        else
            reply.addString("Invalid command, type [help] for a list of accepted commands.");

        return true;
    }

    bool configure(ResourceFinder &rf)
    {
        string name=rf.find("name").asString().c_str();
        setName(name.c_str());

        Property config;
        config.fromConfigFile(rf.findFile("from").c_str());
        Bottle &bGeneral=config.findGroup("general");
        if (bGeneral.isNull())
        {
            printf("Error: group general is missing!\n");

            return false;
        }

        // Set closure timeouts. Default value: 5 seconds

        leftHandTimeOut = rf.findGroup("general").check("closure_timeout_left",Value(5.0)).asDouble();
        rightHandTimeOut = rf.findGroup("general").check("closure_timeout_right",Value(5.0)).asDouble();

        // Parsing general config options for both hands

        Property optionL(bGeneral.toString().c_str());    // Left
        optionL.put("local",name.c_str());    //module name
        optionL.put("robot",rf.findGroup("general").find("robot").asString().c_str());
        optionL.put("part","left_arm");
        optionL.put("grasp_model_type",rf.find("grasp_model_type").asString().c_str());
        optionL.put("grasp_model_file",rf.findFile("grasp_model_left_file").c_str());
        optionL.put("hand_sequences_file",rf.findFile("hand_sequences_file").c_str());  

        printf("-- Option Left: %s\n", optionL.toString().c_str());
        //cout << "" << optionL.toString() << endl;
        
        Property optionR(bGeneral.toString().c_str());    // Right
        optionR.put("local",name.c_str());    //module name
        optionR.put("robot",rf.findGroup("general").find("robot").asString().c_str());
        optionR.put("part","right_arm");
        optionR.put("grasp_model_type",rf.find("grasp_model_type").asString().c_str());
        optionR.put("grasp_model_file",rf.findFile("grasp_model_right_file").c_str());
        optionR.put("hand_sequences_file",rf.findFile("hand_sequences_file").c_str());    

        printf("-- Option Right: %s\n", optionR.toString().c_str());
        //cout << "-- Option Right: " << optionR.toString() << endl;

        printf("***** Instantiating primitives for left hand\n");
        actionL = new AFFACTIONPRIMITIVESLAYER(optionL);
        printf("***** Instantiating primitives for right hand\n");
        actionR=new AFFACTIONPRIMITIVESLAYER(optionR);
        if (!actionR->isValid() || !actionL->isValid() )
        {
            delete actionR;
            actionR = NULL;
            delete actionL;
            actionL = NULL;
            return false;
        }
        
        // Get available hand sequence keys for left hand and print them
        deque<string> qL = actionL->getHandSeqList();
        printf("***** List of available left hand sequence keys:\n");
        for (size_t i=0; i<qL.size(); i++)
            printf("%s\n", qL[i].c_str());
            //cout<<qL[i]<<endl;
            
        // Get available hand sequence keys for right hand and print them
        deque<string> qR = actionR->getHandSeqList();
        printf("***** List of available right hand sequence keys:\n");
        for (size_t i=0; i<qR.size(); i++)
            printf("%s\n", qR[i].c_str());
            //cout<<qR[i]<<endl;
            
        // Open ports
        string fwslash="/";
        inPort.open((fwslash+name+"/handToBeClosed:i").c_str());
        printf("inPort opened\n");
        rpcPort.open((fwslash+name+"/rpc:i").c_str());
        printf("rpcPort opened\n");

        // Attach rpcPort to the respond() method
        attach(rpcPort);
        
        // Check whether the grasp model is calibrated,
        // otherwise calibrate it and save the results
        
        // Left hand
        Model *modelL;
        actionL->getGraspModel(modelL);
        if (modelL!=NULL)
        {
            if (!modelL->isCalibrated())
            {
                printf("################### modelL is not calibrated\n");
                Property prop("(finger all_parallel)");
                modelL->calibrate(prop);
                string fileName=rf.getHomeContextPath();
                fileName+="/";
                fileName+=optionL.find("grasp_model_left_file").asString().c_str();

                ofstream fout;
                fout.open(fileName.c_str());
                modelL->toStream(fout);
                fout.close();
            }
        }

        // Right hand
        Model *modelR;
        actionR->getGraspModel(modelR);
        if (modelR!=NULL)
        {
            if (!modelR->isCalibrated())
            {
                printf("################### modelR is not calibrated\n");
                Property prop("(finger all_parallel)");
                modelR->calibrate(prop);

                string fileName=rf.getHomeContextPath();
                fileName+="/";
                fileName+=optionR.find("grasp_model_right_file").asString().c_str();

                ofstream fout;
                fout.open(fileName.c_str());
                modelR->toStream(fout);
                fout.close();
            }
        }

        return true;
    }

    /************************************************************************/
    bool close()
    {
        // Deallocate actions
        if (actionL!=NULL)
        {
            delete actionL;
            actionL = NULL;
        }
         
        if (actionR!=NULL)
        {
            delete actionR;
            actionR = NULL;
        }
        
        // Close ports
        inPort.close();
        printf("inPort closed\n");
        rpcPort.close();
        printf("rpcPort closed\n");

        return true;
    }

    /************************************************************************/
    double getPeriod()
    {
        return 0.1;
    }

    /************************************************************************/
    void init()
    {
        bool f;

        // Open the hands at startup
        
        // Left hand
        actionL->pushAction("open_hand");
        // Right hand
        actionR->pushAction("open_hand");
        //Wait for completion of both
        actionL->checkActionsDone(f,true);
        actionR->checkActionsDone(f,true);
    }

    // we don't need a thread since the actions library already
    // incapsulates one inside dealing with all the tight time constraints
    /************************************************************************/
    bool updateModule()
    {
        // do it only once
        if (firstRun)
        {
            init();
            firstRun=false;
        }

        // Wait for closure command
        Bottle *b = inPort.read();    // blocking call

        if (b!=NULL)
        {
            iCub::skinDynLib::SkinPart handSide;
            bool fr;
            bool fl;
            
            // Get the code of the hand to be closed
            handSide = static_cast<iCub::skinDynLib::SkinPart>(b->get(0).asInt());

            /*        NOTE: We are using the following enums from iCub/skinDynLib/common.h
            enum SkinPart { 
               SKIN_PART_UNKNOWN=0, 
               SKIN_LEFT_HAND,
               SKIN_LEFT_FOREARM,
               SKIN_LEFT_UPPER_ARM, 
               SKIN_RIGHT_HAND,
               SKIN_RIGHT_FOREARM,
               SKIN_RIGHT_UPPER_ARM, 
               SKIN_FRONT_TORSO, 
               SKIN_PART_ALL,
               SKIN_PART_SIZE
            };
            */

            // Grasp with either of the hands (wait until it's done)

            if ( handSide == iCub::skinDynLib::SKIN_LEFT_HAND)  // Left hand
            {
                actionMutex.lock(); //Protected area beginning

                // Push closure action and wait for completion
                actionL->pushAction("close_hand");
                actionL->checkActionsDone(fl,true);

                // Check for obstructing (grasped) objects
                actionL->areFingersInPosition(fl);
                
                // If fingers are not in position,
                // it's likely that we've just grasped
                // something, so print it!
                if (!fl)
                    printf("Left hand obstructed while closing\n");
                else
                    printf("Left hand fully closed\n");
            
                // Wait $leftHandTimeOut seconds
                actionL->pushWaitState(leftHandTimeOut);    
                
                // Push opening action and wait for completion
                actionL->pushAction("open_hand");
                actionL->checkActionsDone(fl,true);

                // Check for obstructing objects
                actionL->areFingersInPosition(fl);

                actionMutex.unlock(); //Protected area ending

                if (!fl)
                    printf("Left hand obstructed while opening\n");
                else
                    printf("Left hand fully opened\n");
            }
            else if ( handSide == iCub::skinDynLib::SKIN_RIGHT_HAND)
            {
 
                actionMutex.lock(); //Protected area beginning
                
                // Right hand
                actionR->pushAction("close_hand");
                actionR->checkActionsDone(fr,true);
                actionR->areFingersInPosition(fr);          // Check for obstructing (grasped) objects

                // Check for obstructing (grasped) objects
                actionR->areFingersInPosition(fr);
                
                // If fingers are not in position,
                // it's likely that we've just grasped
                // something, so print it!
                if (!fr)
                    printf("Right hand obstructed while closing\n");
                else
                    printf("Right hand fully closed\n");

                // Wait $rightHandTimeOut seconds
                actionR->pushWaitState(rightHandTimeOut);   
                
                // Push opening action and wait for completion
                actionR->pushAction("open_hand");
                actionR->checkActionsDone(fr,true);

                // Check for obstructing (grasped) objects
                actionR->areFingersInPosition(fr);

                actionMutex.unlock(); //Protected area ending

                if (!fr)
                    printf("Right hand obstructed while opening\n");
                else
                    printf("Right hand fully opened\n");
            }              
        }

        return true;
    }

    /************************************************************************/
    bool interruptModule()
    {
        // Interrupt any blocking reads on the input port
        inPort.interrupt();
        printf("inPort interrupted\n");

        // Interrupt any blocking reads on the input port        
        rpcPort.interrupt();
        printf("rpcPort interrupted\n");

        actionMutex.unlock(); // Unlock the mutex to avoid deadlocks and allow a smooth stop

        // since a call to checkActionsDone() blocks
        // the execution until it's done, we need to 
        // take control and exit from the waiting state

        actionL->syncCheckInterrupt(true);
        actionR->syncCheckInterrupt(true);

        return true;
    }
};


/************************************************************************/
int main(int argc, char *argv[])
{
    Network yarp;
    if (!yarp.checkNetwork())
    {
        printf("YARP server not available!\n");
        return -1;
    }

    YARP_REGISTER_DEVICES(icubmod)

    ResourceFinder rf;
    rf.setVerbose(true);
    rf.setDefaultConfigFile("handCtrl_config.ini");
    rf.setDefaultContext("RBDemo");
    rf.setDefault("grasp_model_type","tactile");    // Check this parameter, does it correspond to the one stored in grasp_model_* -> name?
                                                    // If so, one default option for each hand may be needed.
    rf.setDefault("grasp_model_left_file","grasp_model_left.ini");
    rf.setDefault("grasp_model_right_file","grasp_model_right.ini");
    rf.setDefault("hand_sequences_file","handCtrl_hand_sequences.ini");
    rf.setDefault("name","handCtrl");
    rf.configure(argc,argv);

    handCtrl mod;
    return mod.runModule(rf);
}
