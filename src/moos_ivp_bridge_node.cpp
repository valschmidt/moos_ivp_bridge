// Roland Arsenault
// Center for Coastal and Ocean Mapping
// University of New Hampshire
// Copyright 2017, All rights reserved.

#include "ros/ros.h"
#include "ros/package.h"

#include "geometry_msgs/PoseStamped.h"
#include "std_msgs/String.h"
#include "std_msgs/Bool.h"
#include "std_msgs/Int32.h"
#include "geometry_msgs/TwistStamped.h"
#include "marine_msgs/NavEulerStamped.h"
#include "MOOS/libMOOS/Comms/MOOSAsyncCommClient.h"
#include "geographic_msgs/GeoPoint.h"

#include "project11/gz4d_geo.h"
#include "project11/mutex_protected_bag_writer.h"

#include <fstream>
#include <regex>
#include <iomanip>
#include "boost/date_time/posix_time/posix_time.hpp"

ros::Publisher waypoint_index_pub;
ros::Publisher desired_heading_pub;
ros::Publisher desired_speed_pub;
ros::Publisher appcast_pub;
ros::Publisher view_point_pub;
ros::Publisher view_polygon_pub;
ros::Publisher view_seglist_pub;
ros::Publisher ivphelm_update_result_pub;

MutexProtectedBagWriter log_bag;

MOOS::MOOSAsyncCommClient comms;

bool initializedMOOS = false;
bool simulatedTime = false;

bool OnConnect(void * param)
{
    CMOOSCommClient* c = reinterpret_cast<CMOOSCommClient*>(param);
    c->Register("WPT_INDEX", 0.0);
    c->Register("DESIRED_HEADING",0.0);
    c->Register("DESIRED_SPEED",0.0);
    c->Register("APPCAST",0.0);
    c->Register("VIEW_POINT",0.0);
    c->Register("VIEW_POLYGON",0.0);
    c->Register("VIEW_SEGLIST",0.0);
    c->Register("IVPHELM_UPDATE_RESULT",0.0);
    comms.Notify("MOOS_MANUAL_OVERIDE","false");
    return true;
}

bool OnMail(void *param)
{
    CMOOSCommClient* c = reinterpret_cast<CMOOSCommClient*>(param);
    MOOSMSG_LIST ml;
    c->Fetch(ml);
    for(auto m: ml)
    {
        
        auto t = m.GetTime();
        if(simulatedTime)
            t = ros::Time::now().toSec();
        if(m.IsName("DESIRED_HEADING"))
        {
            marine_msgs::NavEulerStamped nes;
            nes.orientation.heading = m.GetDouble();
            nes.header.stamp.fromSec(t);
            desired_heading_pub.publish(nes);
            if(ros::Time::now() > ros::TIME_MIN)
                log_bag.write("/moos/desired_heading",ros::Time::now(),nes);

        }
        if(m.IsName("DESIRED_SPEED"))
        {
            geometry_msgs::TwistStamped ts;
            ts.twist.linear.x = m.GetDouble();
            ts.header.stamp.fromSec(t);
            desired_speed_pub.publish(ts);
            //std::cerr << ts << std::endl;
            if(ros::Time::now() > ros::TIME_MIN)
                log_bag.write("/moos/desired_speed",ros::Time::now(),ts);
        }
        if(m.IsName("APPCAST"))
        {
            std_msgs::String s;
            s.data = m.GetAsString();
            appcast_pub.publish(s);
            if(ros::Time::now() > ros::TIME_MIN)
                log_bag.write("/moos/appcast",ros::Time::now(),s);
        }
        if(m.IsName("VIEW_POINT"))
        {
            std_msgs::String s;
            s.data = m.GetAsString();
            view_point_pub.publish(s);
            if(ros::Time::now() > ros::TIME_MIN)
                log_bag.write("/moos/view_point",ros::Time::now(),s);
        }
        if(m.IsName("VIEW_POLYGON"))
        {
            std_msgs::String s;
            s.data = m.GetAsString();
            view_polygon_pub.publish(s);
            if(ros::Time::now() > ros::TIME_MIN)
                log_bag.write("/moos/view_polygon",ros::Time::now(),s);
        }
        if(m.IsName("VIEW_SEGLIST"))
        {
            std_msgs::String s;
            s.data = m.GetAsString();
            view_seglist_pub.publish(s);
            if(ros::Time::now() > ros::TIME_MIN)
                log_bag.write("/moos/view_seglist",ros::Time::now(),s);
        }
	// LMD ADDED!
	if(m.IsName("WPT_INDEX"))
        {
            std_msgs::Int32 wptIndex;
            wptIndex.data = m.GetDouble();
            waypoint_index_pub.publish(wptIndex);
            log_bag.write("/moos/wpt_index",ros::Time::now(),wptIndex);

        }
        if(m.IsName("IVPHELM_UPDATE_RESULT"))
	{
            std_msgs::String s;
            s.data = m.GetAsString();
            ivphelm_update_result_pub.publish(s);
            log_bag.write("/moos/ivphelm_update_result",ros::Time::now(),s);
 	}
    }
    
    return true;
}


void startMOOS()
{
    std::string missionFileTemplate = ros::package::getPath("moos_ivp_bridge")+"/missions/ros.moos.template";
    std::string bhvFile = ros::package::getPath("moos_ivp_bridge")+"/missions/ros.bhv";

    std::ifstream infile(missionFileTemplate);
    std::stringstream incontent;
    incontent << infile.rdbuf();
    
    std::regex br("BEHAVIORS");
    
    
    std::string outcontent = std::regex_replace(incontent.str(),br,bhvFile);

    std::string missionFile = "ros.moos";
    
    std::ofstream outfile(missionFile);
    
    outfile << outcontent;
    
    boost::posix_time::ptime now = ros::WallTime::now().toBoost();
    std::string iso_now = std::regex_replace(boost::posix_time::to_iso_extended_string(now),std::regex(":"),"-");
    
    std::string missionFileStamped = "ros-"+iso_now+".moos";
    
    std::ofstream outfileStamped(missionFileStamped);
    outfileStamped << outcontent;
    
    char* argv[3];
    argv[2] = nullptr;
    argv[1] = const_cast<char *>(missionFile.c_str());
    
    std::string pa = "pAntler";
    argv[0] = const_cast<char *>(pa.c_str());
    
    int pid = fork();
    if(pid == 0)
    {
        execvp("pAntler",argv);
    }
    initializedMOOS = true;
}

void positionCallback(const geometry_msgs::PoseStamped::ConstPtr& inmsg)
{
    if(initializedMOOS)
    {
        // trying to solve clock skew errors in moos when simulating time
        double t = inmsg->header.stamp.toSec();
        if (simulatedTime)
            t = ros::WallTime::now().toSec();
        
        comms.Notify("NAV_X",inmsg->pose.position.x,t);
        comms.Notify("NAV_Y",inmsg->pose.position.y,t);
    }
    if(ros::Time::now() > ros::TIME_MIN)
        log_bag.write("/position_map",ros::Time::now(),*inmsg);

}

void headingCallback(const marine_msgs::NavEulerStamped::ConstPtr& inmsg)
{
    double t = inmsg->header.stamp.toSec();
    if(simulatedTime)
        t = ros::WallTime::now().toSec();

    comms.Notify("NAV_HEADING",inmsg->orientation.heading,t);
    if(ros::Time::now() > ros::TIME_MIN)
        log_bag.write("/heading",ros::Time::now(),*inmsg);
}

void sogCallback(const geometry_msgs::TwistStamped::ConstPtr& inmsg)
{
    double t = inmsg->header.stamp.toSec();
    if (simulatedTime)
        t = ros::WallTime::now().toSec();

    double sog = sqrt( inmsg->twist.linear.x*inmsg->twist.linear.x + inmsg->twist.linear.y*inmsg->twist.linear.y);
    comms.Notify("NAV_SPEED",sog,t);

    if(ros::Time::now() > ros::TIME_MIN)
        log_bag.write("/sog",ros::Time::now(),*inmsg);
}

void waypointUpdateCallback(const std_msgs::String::ConstPtr& inmsg)
{
    comms.Notify("WPT_UPDATE",inmsg->data);
    if(ros::Time::now() > ros::TIME_MIN)
        log_bag.write("/moos/wpt_updates",ros::Time::now(),*inmsg);
}

void loiterUpdateCallback(const std_msgs::String::ConstPtr& inmsg)
{
    comms.Notify("LOITER_UPDATE",inmsg->data);
    if(ros::Time::now() > ros::TIME_MIN)
        log_bag.write("/moos/loiter_updates",ros::Time::now(),*inmsg);
}

void activeCallback(const std_msgs::Bool::ConstPtr& inmsg)
{
    if(inmsg->data)
        comms.Notify("ACTIVE","true");
    else
        comms.Notify("ACTIVE","false");
    if(ros::Time::now() > ros::TIME_MIN)
        log_bag.write("/active",ros::Time::now(),*inmsg);
}

void helmModeCallback(const std_msgs::String::ConstPtr& inmsg)
{
    comms.Notify("HELM_MODE",inmsg->data);
    if(ros::Time::now() > ros::TIME_MIN)
        log_bag.write("/helm_mode",ros::Time::now(),*inmsg);
}


void appcastRequestCallback(const ros::WallTimerEvent& event)
{
    std::stringstream req;
    
    req << "node=moos_ivp_ros,app=pHelmIvP,duration=3.0,key=moos_ivp_bridge,thresh=any";
    comms.Notify("APPCAST_REQ",req.str());
}

void originCallback(const geographic_msgs::GeoPoint::ConstPtr& inmsg)
{
    comms.Notify("LatOrigin", inmsg->latitude);
    comms.Notify("LongOrigin", inmsg->longitude);
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "moos_ivp_bridge_node");
    ros::NodeHandle n;
    n.param<bool>("/use_sim_time", simulatedTime, false);
    
    
    desired_heading_pub = n.advertise<marine_msgs::NavEulerStamped>("/moos/desired_heading",1);
    desired_speed_pub = n.advertise<geometry_msgs::TwistStamped>("/moos/desired_speed",1);
    appcast_pub = n.advertise<std_msgs::String>("/moos/appcast",1);
    view_point_pub = n.advertise<std_msgs::String>("/moos/view_point",1);
    view_polygon_pub = n.advertise<std_msgs::String>("/moos/view_polygon",1);
    view_seglist_pub = n.advertise<std_msgs::String>("/moos/view_seglist",1);
    // LMD ADDED!
    waypoint_index_pub = n.advertise<std_msgs::Int32>("/moos/wpt_index",1);
    ivphelm_update_result_pub = n.advertise<std_msgs::String>("/moos/ivphelm_update_result",1);

    ros::Subscriber psub = n.subscribe("/position_map",10,positionCallback);
    ros::Subscriber hsub = n.subscribe("/heading",10,headingCallback);
    ros::Subscriber sogsub = n.subscribe("/sog",10,sogCallback);
    ros::Subscriber wptUpdatesub = n.subscribe("/moos/wpt_updates",10,waypointUpdateCallback);
    ros::Subscriber loiterUpdatesub = n.subscribe("/moos/loiter_updates",10,loiterUpdateCallback);
    ros::Subscriber activesub = n.subscribe("/active",10,activeCallback);
    ros::Subscriber helmmodesub = n.subscribe("/helm_mode",10,helmModeCallback);
    ros::Subscriber originsub = n.subscribe("/origin",10,originCallback);
    
    boost::posix_time::ptime now = ros::WallTime::now().toBoost();
    std::string iso_now = std::regex_replace(boost::posix_time::to_iso_extended_string(now),std::regex(":"),"-");
    
    std::string log_filename = "nodes/moos_ivp_bridge-"+iso_now+".bag";
    log_bag.open(log_filename, rosbag::bagmode::Write);
    
    ros::WallTimer appcastRequestTimer = n.createWallTimer(ros::WallDuration(1.0),appcastRequestCallback);

    comms.SetOnMailCallBack(OnMail,&comms);
    comms.SetOnConnectCallBack(OnConnect,&comms);
    
    comms.Run("localhost",9000,"moos_ivp_bridge");
    
    startMOOS();
    
    ros::spin();
    
    log_bag.close();
    
    return 0;
    
}

