/*
 * Copyright (C) 2014 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Tanis Mar
 * email:  Tanis Mar
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

#ifndef __NEARTHINGSDETECTOR_H__
#define __NEARTHINGSDETECTOR_H__

#include <yarp/os/BufferedPort.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Network.h>
#include <yarp/os/Thread.h>
#include <yarp/os/RateThread.h>
#include <yarp/os/Time.h>
#include <yarp/os/Semaphore.h>
#include <yarp/os/Stamp.h>
#include <yarp/sig/Vector.h>
#include <yarp/sig/Image.h>

#include <time.h>
#include <string>
#include <iostream>
#include <iomanip>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <cv.h>
#include <highgui.h>


class NearThingsDetector : public yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelBgr> >
{
private:

    std::string moduleName;                     //string containing module name
    std::string worldInPortName;                //string containing world in info port name
    std::string dispInPortName;                 //string containing disparity image port name
    std::string targetOutPortName;              //string containing output target port name
    std::string imageOutPortName;               //string containing output image port name
    
    yarp::os::Port									rpcPort;							// rpc port (receive commands via rpc), returns bounding box of selected blob
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelRgbFloat> > worldInPort;	// input Port with info of 3D world
    //yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelBgr> >	dispInPort;		// Receives disparity greyscale image
    yarp::os::BufferedPort<yarp::os::Bottle>							targetOutPort;	// Send coordinates of closest point.
    yarp::os::BufferedPort<yarp::sig::ImageOf<yarp::sig::PixelBgr> >	imageOutPort;	// output image Port with info drawn over  

    /* Pointer to the RF module */
    yarp::os::ResourceFinder *moduleRF;

    /* Algorithm Variables */
    int backgroundThresh;
    int cannyThresh;
    int minBlobSize;
    
    int dispThreshRatioLow;
    int dispThreshRatioHigh;
    
    int gaussSize;
    float range;
    cv::Scalar origin;
	
public:
    /**
     * constructor
     * @param moduleName is passed to the thread in order to initialise all the ports correctly
     */
    NearThingsDetector( const std::string &moduleName, yarp::os::ResourceFinder &module );
    ~NearThingsDetector();

    bool setOrigin(std::vector<double> origin);
    bool setRange(double range);
    
    bool        open();
    void        close();
    void        onRead( yarp::sig::ImageOf<yarp::sig::PixelBgr> &img );
    void        interrupt();
    
    yarp::os::Semaphore         mutex;          //semaphore for accessing/modifying within the callback
       
};

class NearDetectorModule:public yarp::os::RFModule
{
    /* module parameters */
    std::string             moduleName;
    std::string             handlerPortName;
    yarp::os::RpcServer     rpcPort;

    /* pointer to a new thread */
    NearThingsDetector      *detector;
    bool                    closing;

public:

    bool configure(yarp::os::ResourceFinder &rf); // configure all the module parameters and return true if successful
    bool interruptModule();                       // interrupt, e.g., the ports
    bool close();                                 // close and shut down the module

    double getPeriod();
    bool updateModule();
    bool respond(const yarp::os::Bottle &command, yarp::os::Bottle &reply);
};


#endif
//empty line to make gcc happy