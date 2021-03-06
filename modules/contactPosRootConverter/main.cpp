/* 
 * Copyright (C) 2014 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Giulia Pasquale
 * email: giulia.pasquale@iit.it
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
\defgroup contactPosRootConverter
 
@ingroup icub_module  
 
This module converts the position of a skin contact 
into the root reference frame.

Copyright (C) 2014 iCub Facility - Istituto Italiano di Tecnologia
 
Author: Giulia Pasquale

\section intro_sec Description 
This module receives a \ref skinContactList from the \ref skinManager, 
takes the first \ref skinContact above a threshold on the pressure 
and converts its position into the root reference frame.
 
\section lib_sec Libraries 
- YARP libraries
- \ref iKinFwd library
- \ref SkinDynLib library

\section parameters_sec Parameters

\section portsa_sec Ports Accessed

- \e /skinManager/skin_events:o 

\section portsc_sec Ports Created 
 
- \e /contactPosRootConverter/skin_events:i

- \e /contactPosRootConverter/contact_info:o

- \e /contactPosRootConverter/contact_pos:o

- \e /contactPosRootConverter/rpc:i remote procedure call. 
    Recognized remote commands:
    -'help'
    -'set press_thresh <n>'
    -'get press_thresh'

\section in_files_sec Input Data Files
None.

\section out_data_sec Output Data Files 
None. 
 
\section conf_file_sec Configuration Files 
--from \e file 
- specify the configuration file (use \e --context option to 
  select the current context).

The configuration file passed through the option \e --from
should look like as follows:

\code 
name                            contactPosRootConverter
robot                           icub
in_skin_events                  /skin_events:i
out_contact_pos                 /contact_pos:o
out_contact_info                /contact_info:o
pressure_thresh                 15
\endcode 

\section tested_os_sec Tested OS
Windows, Linux

\author Giulia Pasquale
*/

#include <stdio.h>
#include <iostream>
#include <string>

#include <yarp/os/all.h>
#include <yarp/os/Network.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Thread.h>
#include <yarp/os/BufferedPort.h>

#include <yarp/sig/all.h>
#include <yarp/sig/Vector.h>
#include <yarp/sig/Matrix.h>

#include <yarp/dev/all.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/CartesianControl.h>
#include <yarp/dev/Drivers.h>

#include <yarp/math/Math.h>

#include <gsl/gsl_math.h>

#include <iCub/skinDynLib/skinContactList.h>
#include <iCub/iKin/iKinFwd.h>

#define PRESSURE_THRESHOLD 15.0

using namespace std;

using namespace yarp;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace yarp::dev;

using namespace iCub::skinDynLib;
using namespace iCub::iKin;

class contactPosRootConverterWorker {

protected:

// CLASS VARIABLES - contact related

    int cntctDetect;

    Vector cntctPosWRF;         // Position in world RF
    Vector cntctPosLinkRF;      // Position in i-th link RF 
    Matrix Twl;                 // RT matrix to convert the position from i-th link to world RF

    int cntctLinkNum;           // Link number
    SkinPart cntctSkinPart;     // Skin part
    string cntctSkinPartV;      // Skin part verbose

    double cntcPressure;        // Average output of activated taxels
    unsigned int cntcTaxelNum;  // Number of activated taxels
    
    double pressureTh;          // Threshold on cntcPressure for a contact to be detected

    Mutex mut;

// CLASS VARIABLES - encoders related

    PolyDriver ddR;
    PolyDriver ddL;
    PolyDriver ddT;

    IEncoders *iencsR;
    IEncoders *iencsL;
    IEncoders *iencsT;
    Vector encsR;
    Vector encsL;
    Vector encsT;
    int jntsR;
    int jntsL;
    int jntsT;

    iCubArm *armR;  // Left arm model
    iCubArm *armL;  // Right arm model

// MODULE PARAMETERS and PORTS

    string name;
    string robot;

    string inSkinEventsName;
    string outContactPosName;
    string outContactInfoName;
    BufferedPort<skinContactList> inSkinEvents;   // Reads from /skinManager/skin_events:o
    BufferedPort<Bottle> outContactPos;           // Writes the Bottle (SkinPart (cntcPosWRF))
    BufferedPort<Bottle> outContactInfo;          // Writes the Bottle (cntcPosWRF)

public:

    contactPosRootConverterWorker(const string &_name, const string &_robot,
                                  const string &_inSkinEventsName, const string &_outContactPosName, const string &_outContactInfoName,
                                  const double _pressureTh) {

        name = _name;
        robot = _robot;

        inSkinEventsName = _inSkinEventsName;
        outContactPosName = _outContactPosName;
        outContactInfoName = _outContactInfoName;

        setPressureTh(_pressureTh);

        cntctDetect = -1;

        cntctPosLinkRF = zeros(3); 
        cntctPosWRF = zeros(3); 
        Twl = eye(4);

        cntctLinkNum = -1;
        cntctSkinPart = SKIN_PART_UNKNOWN; 
        cntctSkinPartV = "";

        cntcPressure = -1;
        cntcTaxelNum = -1;

        armR = new iCubArm("right");
        armL = new iCubArm("left");

        iencsR = NULL;
        iencsL = NULL;
        iencsT = NULL;

        jntsR = -1;
        jntsL = -1;
        jntsT = -1;

    }

    bool open() {
        
        if (!inSkinEvents.open(inSkinEventsName.c_str())) {
            fprintf(stdout, "%s: unable to open port %s\n", name.c_str(), inSkinEventsName.c_str());
            return false;
        }

        if (!outContactPos.open(outContactPosName.c_str())) {
            fprintf(stdout, "%s: unable to open port %s\n", name.c_str(), outContactPosName.c_str());
            return false;
        }

        if (!outContactInfo.open(outContactInfoName.c_str())) {
            fprintf(stdout, "%s: unable to open port %s\n", name.c_str(), outContactInfoName.c_str());
            return false;
        }
        
        Property OptR;
        OptR.put("robot",  robot.c_str());
        OptR.put("part",   "right_arm");
        OptR.put("device", "remote_controlboard");
        OptR.put("remote",("/"+robot+"/right_arm").c_str());
        OptR.put("local", ("/"+name +"/right_arm").c_str());

        Property OptL;
        OptL.put("robot",  robot.c_str());
        OptL.put("part",   "left_arm");
        OptL.put("device", "remote_controlboard");
        OptL.put("remote",("/"+robot+"/left_arm").c_str());
        OptL.put("local", ("/"+name +"/left_arm").c_str());

        Property OptT;
        OptT.put("robot",  robot.c_str());
        OptT.put("part",   "torso");
        OptT.put("device", "remote_controlboard");
        OptT.put("remote",("/"+robot+"/torso").c_str());
        OptT.put("local", ("/"+name +"/torso").c_str());

        if (!ddR.open(OptR)) {
            fprintf(stdout,"ERROR: could not open right_arm PolyDriver!\n");
            return false;
        }

        bool ok = 1;
        if (ddR.isValid())
            ok = ok && ddR.view(iencsR);
        if (!ok) {
            fprintf(stdout,"ERROR: Problems acquiring right_arm interfaces!\n");
            return false;
        }

        if (!ddL.open(OptL)) {
            fprintf(stdout,"ERROR: could not open left_arm PolyDriver!\n");
            return false;
        }

        ok = 1;
        if (ddL.isValid())
            ok = ok && ddL.view(iencsL);
        if (!ok) {
            fprintf(stdout,"ERROR: Problems acquiring left_arm interfaces!\n");
            return false;
        }

        if (!ddT.open(OptT)) {
            fprintf(stdout,"ERROR: could not open torso PolyDriver!\n");
            return false;
        }

        ok = 1;
        if (ddT.isValid())
            ok = ok && ddT.view(iencsT);
        if (!ok) {
            fprintf(stdout,"ERROR: Problems acquiring torso interfaces!\n");
            return false;
        }

        iencsR->getAxes(&jntsR);
        encsR = zeros(jntsR);
        iencsL->getAxes(&jntsL);
        encsL = zeros(jntsL);
        iencsT->getAxes(&jntsT);
        encsT = zeros(jntsT);

        armR->releaseLink(0);
        armR->releaseLink(1);
        armR->releaseLink(2);
    
        armL->releaseLink(0);
        armL->releaseLink(1);
        armL->releaseLink(2);

        return true;
    }

    void interrupt() {
    
        inSkinEvents.interrupt();
        outContactPos.interrupt();
        outContactInfo.interrupt();

    }

    void loop() {
 
        skinContactList *cntctVec = inSkinEvents.read(false);
            
        /* Process the skinContactList:
        case 0: NULL --> no new data --> doing nothing
        case 1: empty --> no contact detected --> doing nothing
        case 2: not empty --> at least 1 contact above threshold --> sending: cntctSkinPart (optional) + cntctPosWRF
        case 3: not empty --> 0 contact above threshold --> doing nothind
        */

        if (cntctVec!=NULL && !cntctVec->empty()) {
            
            mut.lock();

            for (skinContactList::iterator it = cntctVec->begin(); it!=cntctVec->end(); it++) {
                        
                cntctPosLinkRF = it->getCoP();
                cntctLinkNum = it->getLinkNumber();
                cntcPressure = it->getPressure();
                cntcTaxelNum = it->getActiveTaxels();
                cntctSkinPart  = it->getSkinPart();
                cntctSkinPartV = it->getSkinPartName();

                if (cntcPressure>=pressureTh)
                    break;
            }

            if (cntcPressure>=pressureTh)
                cntctDetect = 0;
            else 
                cntctDetect = -1;

            mut.unlock();

            if (cntctDetect==0) {

                // Prepare the output bottle

                Bottle &outputInfo = outContactInfo.prepare();
                Bottle &outputPos = outContactPos.prepare();
                outputInfo.clear();
                outputPos.clear();

                outputInfo.addInt(cntctSkinPart);

                // Convert the coordinates
                
                switch (cntctSkinPart) {

                    case SKIN_LEFT_HAND:
                    case SKIN_LEFT_FOREARM:
                    case SKIN_LEFT_UPPER_ARM: {

                        iencsT->getEncoders(encsT.data());

                        double tmp = encsT[0];
                        encsT[0] = encsT[2];
                        encsT[2] = tmp;

                        iencsL->getEncoders(encsL.data());

                        Vector encsLnoHand = encsL.subVector(0,6);

                        Vector qTL(encsT.size()+encsLnoHand.size(),0.0);
                        qTL.setSubvector(0,encsT);
                        qTL.setSubvector(encsT.size(),encsLnoHand);

                        armL -> setAng(qTL*CTRL_DEG2RAD);

                        Twl = armL -> getH(cntctLinkNum + encsT.size(), true);

                        break;
                    }

                    case SKIN_RIGHT_HAND:
                    case SKIN_RIGHT_FOREARM:
                    case SKIN_RIGHT_UPPER_ARM: {

                        iencsT->getEncoders(encsT.data());
                        
                        double tmp = encsT[0];
                        encsT[0] = encsT[2];
                        encsT[2] = tmp;
                        
                        iencsR->getEncoders(encsR.data());
                        
                        Vector encsRnoHand = encsR.subVector(0,6);
                        
                        Vector qTR(encsT.size()+encsRnoHand.size(),0.0);
                        qTR.setSubvector(0,encsT);
                        qTR.setSubvector(encsT.size(),encsRnoHand);

                        armR -> setAng(qTR*CTRL_DEG2RAD);

                        Twl = armR -> getH(cntctLinkNum + encsT.size(), true);
                        
                        break;
                    } 

                    case SKIN_FRONT_TORSO: {
                            
                        iencsT->getEncoders(encsT.data());
                                
                        double tmp = encsT[0];
                        encsT[0] = encsT[2];
                        encsT[2] = tmp;
                            
                        armR -> setAng(encsT*CTRL_DEG2RAD);

                        Twl = armR -> getH(cntctLinkNum, true);

                        break;
                    }

                    default: {

                        fprintf(stdout, "SkinPart: %s\n", cntctSkinPartV.c_str());

                        break;
                    }

                }

                cntctPosLinkRF.push_back(1);
                cntctPosWRF.push_back(1);

                cntctPosWRF = Twl * cntctPosLinkRF;

                cntctPosLinkRF.pop_back();
                cntctPosWRF.pop_back();

                fprintf(stdout,"CONTACT! skinPart: %s Link: %i Position: %s\n", cntctSkinPartV.c_str(), cntctLinkNum,cntctPosWRF.toString().c_str());

                Bottle outputCoord;
                outputCoord.addDouble(cntctPosWRF[0]);
                outputCoord.addDouble(cntctPosWRF[1]);
                outputCoord.addDouble(cntctPosWRF[2]);
                outputInfo.addList() = outputCoord;

                outputPos.addDouble(cntctPosWRF[0]);
                outputPos.addDouble(cntctPosWRF[1]);
                outputPos.addDouble(cntctPosWRF[2]);

                outContactPos.write();
                outContactInfo.write();

            } 

        }

    }

    bool close() {    
        
        inSkinEvents.close();
        outContactPos.close();
        outContactInfo.close();
        fprintf(stdout, "%s: closed ports %s, %s and %s\n", name.c_str(), inSkinEventsName.c_str(), outContactPosName.c_str(), outContactInfoName.c_str());

        ddR.close();
        ddL.close();
        ddT.close();
        fprintf(stdout, "%s: closed device drivers\n", name.c_str());

        delete armR;
        delete armL;
        fprintf(stdout, "%s: deleted arm models\n", name.c_str());

        return true;

    }

    double setPressureTh(const double _pressureTh) {

        mut.lock();

        pressureTh = _pressureTh;
        if (pressureTh<0 || pressureTh>255) {
            pressureTh = PRESSURE_THRESHOLD;
            fprintf(stdout, "ATTENTION: requested pressure_thresh out of valid range\n");
        }

        mut.unlock();

        return pressureTh;

    }

    double getPressureTh() {

        return pressureTh;

    }

};

class contactPosRootConverter: public RFModule {

protected:

    string name;
    string robot;

    Port handlerPort;
    string handlerPortSubfix, handlerPortName;

    string inputSkinEventsSubfix, inputSkinEventsName;
    string outputContactPosSubfix, outputContactPosName;
    string outputContactInfoSubfix, outputContactInfoName;

    double pressureThreshold;

    bool onlyCoordinates;

    contactPosRootConverterWorker *workerClass;

public:

    contactPosRootConverter() {
        
        name = "contactPosRootConverter";
        robot = "icub";
        
        inputSkinEventsSubfix = "/skin_events:i";
        outputContactPosSubfix    = "/contact_pos:o";
        outputContactInfoSubfix    = "/contact_info:o";

        handlerPortSubfix = "/rpc:i";

        pressureThreshold = PRESSURE_THRESHOLD;

        onlyCoordinates = false;

        workerClass = NULL;

    }

    bool configure(yarp::os::ResourceFinder &rf) {
        
        // module name
        name = rf.check("name", Value(name), "name (string)").asString();
        setName(name.c_str()); 

        // robot
        robot = rf.check("robot", Value(robot), "robot (string)").asString();

        // rpc port name
        handlerPortName = "/";
        handlerPortName.append(getName().c_str());
        handlerPortName.append(handlerPortSubfix);
 
        // open & attach rpc port
        if (!handlerPort.open(handlerPortName.c_str())) {
            fprintf(stdout, "%s: Unable to open port %s\n", (getName()).c_str(), handlerPortName.c_str());
            return false;
        }
        attach(handlerPort);

        // input & output port names
        inputSkinEventsName   = "/";
        inputSkinEventsName.append(getName(rf.check("in_skin_events", Value(inputSkinEventsSubfix), "input_skin_events (string)").asString()));
        outputContactPosName  = "/" ;
        outputContactPosName.append(getName(rf.check("out_contact_pos", Value(outputContactPosSubfix), "out_contact_pos (string)").asString()));
        outputContactInfoName  = "/";
        outputContactInfoName.append(getName(rf.check("out_contact_info", Value(outputContactInfoSubfix), "out_contact_info (string)").asString()));

        // pressure threshold (set from either cmd line or .ini file or default defined value)
        pressureThreshold = rf.check("pressure_thresh",Value(PRESSURE_THRESHOLD),"pressure_thresh (double)").asDouble();
        
        // instantiate worker class
        workerClass = new contactPosRootConverterWorker(name, robot, inputSkinEventsName, outputContactPosName, outputContactInfoName, pressureThreshold);
        bool startOk = workerClass->open();
        if (!startOk) {
            delete workerClass;
            workerClass = NULL;
            fprintf(stdout, "ERROR: contactPosRootConverterWorker not instantiated\n");
            return false;
        }

        return true;  
    }

    double getPeriod() { 
        
        return 0.05; // [s]
    
    }

    bool updateModule() { 
        
        workerClass->loop();

        return true; 
    
    }

    bool interruptModule() { 

        handlerPort.interrupt();

        if (workerClass) {
            workerClass->interrupt();
        }

        return true;
    }

    bool close() {

        handlerPort.close();
        fprintf(stdout, "%s: closed port %s\n", name.c_str(), handlerPortName.c_str());

        bool closeValue = true;

        if (workerClass) {
            closeValue = workerClass->close();
            fprintf(stdout, "%s: stopped worker class\n", name.c_str());
            delete workerClass;
            workerClass=NULL;
        }

        return closeValue;
    }

    bool respond(const Bottle& command, Bottle& reply) {

        int ack = Vocab::encode("ack");
        int nack = Vocab::encode("nack");
        
        reply.clear(); 

        if (command.size()>0) {

            switch (command.get(0).asVocab()) {

            case VOCAB4('h','e','l','p'): { 

                reply.addVocab(Vocab::encode("many"));
                reply.addString("Available commands are:");
                reply.addString("help");
                reply.addString("set pressure_thresh <n> (double)");
                reply.addString("get pressure_thresh (double)");

                break;

            } case VOCAB3('s','e','t'): {
                
                if (command.get(1).asString() == "pressure_thresh") {
                    
                    reply.addVocab(ack);

                    // set threshold at runtime
                    pressureThreshold = command.get(2).asDouble();
                    pressureThreshold = workerClass->setPressureTh(pressureThreshold);
                    fprintf(stdout, "pressure_thresh set to %f\n", pressureThreshold);

                } else {

                    reply.addVocab(nack);

                }

                break;

            } case VOCAB3('g','e','t'): {
                
                if (command.get(1).asString() == "pressure_thresh") {

                    reply.addVocab(ack);

                    // return current threshold
                    pressureThreshold = workerClass->getPressureTh();
                    reply.addDouble(pressureThreshold);

                } else {

                    reply.addVocab(nack);

                }

                break;

            } default: {

                reply.addVocab(nack);

            }

            }

        } else {

        reply.addVocab(nack);

        }

        return true;

    }

};

int main(int argc, char *argv[]) {

    ResourceFinder rf;

    rf.setVerbose(true);

    rf.setDefaultConfigFile( "contactPosRootConverter.ini" );   //overridden by --from parameter
    rf.setDefaultContext( "RBDemo" );                           //overridden by --context parameter

    rf.configure(argc, argv );

    string optMessage = "Options:\n";
    optMessage += "--context           path: where to find the called resource (default RBDemo)\n";
    optMessage += "--from              from: name of the .ini file (default contactPosRootConverter.ini)\n";
    optMessage += "--name              name: name of the module (default contactPosRootConverter)\n";
    optMessage += "--robot             robot: name of the robot (default icub)\n";
    optMessage += "--in_skin_events    in_skin_events: subfix of input port (default /skin_events:i)\n";
    optMessage += "--out_contact_pos   out_contact_pos: subfix of output port 1 (default /contact_pos:o)\n";
    optMessage += "--out_contact_info  out_contact_info: subfix of output port 2 (default /contact_info:o)\n";
    optMessage += "--pressure_thresh   pressure_thresh: threshold on the avg pressure for a contact to be detected (default 15)\n";

    if (rf.check("help")) {
        fprintf(stdout, "%s", optMessage.c_str());
        return 0;
    }
   
    Network yarp;

    if (!yarp.checkNetwork()) {
        fprintf(stdout,"ERROR: yarp server does not seem available\n");
        return -1;
    }

    contactPosRootConverter mod;

    return mod.runModule(rf);
}
