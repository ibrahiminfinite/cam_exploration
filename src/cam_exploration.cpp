/*
 * MIT License
 *
 * Copyright (c) 2016 Jordi Soler Busquets
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cam_exploration/MapServer.h>
#include <cam_exploration/goalSelector.h>
#include <cam_exploration/replan.h>

#include <geometry_msgs/Vector3.h>

using namespace std;
using namespace cam_exploration::strategy;

/**
 * @file cam_exploration.cpp
 * @brief Top level file for exploration with an RGBA camera.
 * @author Jordi Soler
 * @version 1.0
 * @date 2016-04-19
 */


/**
 * @brief Alias for integer iterator loops
 */
typedef vector<int>::const_iterator viit;


/**
 * @brief Top level namespace of the project cam_exploration
 */
namespace cam_exploration{


//! Robot motion handler
RobotMotion robot;
//! List of all frontiers in the occupancy grid
FrontiersMap fmap;
//! Object that decides the actual robot goal given a certain frontier
strategy::goalSelector* goal_selector;


/**
 * @brief Choose a proper goal pose to sent to move_base
 *
 * @return Goal pose
 */
geometry_msgs::Pose decideGoal()
{
    geometry_msgs::Pose goal;
    FrontiersMap::const_iterator f_it = fmap.begin();
    while(!goal_selector->decideGoal(*f_it, goal)){
    	f_it++;
    }

    MarkerPublisher markers;
    markers.publish("f_goal", f_it->points);
    markers.publish("goal", goal);
    return goal;
}



/**
 * @brief Callback to update the frontiers map according to new MapServer information
 *
 * @param f_in MapServer's frontier map
 */
void getFrontiers(FrontiersMap& f_in)
{
    fmap = f_in;
}


/**
 * @brief Finishes the exploration and kills the node
 */
void finish()
{
    if (robot.isMoving()){
    	robot.cancelGoal();
    }
    ros::shutdown();
}


/**
 * @brief Node's main loop
 */
void spin()
{
    // >> Exploration objects
    MapServer mapServer;
    replan::Replaner replaner;


    ros::spinOnce();
    ros::NodeHandle n;
    ros::NodeHandle nh("~");


    // >> Subscribing map server to Map
    mapServer.subscribeMap("/2Dgrid_map", getFrontiers, ros::NodeHandlePtr(new ros::NodeHandle()),
				ros::NodeHandlePtr(new ros::NodeHandle("~")));
    // >> Subscribing costmap server to Map
    mapServer.subscribeCostMap("/move_base/global_costmap/costmap", ros::NodeHandlePtr(new ros::NodeHandle()),
				ros::NodeHandlePtr(new ros::NodeHandle("~")));

    // >> Setting up replaning function
    vector<string> replan_conditions;
    ros::NodeHandle n_replan(nh, "replaning");
    n_replan.getParam("conditions", replan_conditions);


    for(vector<string>::iterator it = replan_conditions.begin(); it != replan_conditions.end(); ++it){
	map<string, string> parameters;

	ROS_INFO("processing %s", it->c_str());
	if (n_replan.getParam(it->c_str(), parameters)){
	    replaner.addCause(it->c_str(), parameters);
	}
	else{
	    ROS_INFO("No parameters found for replaning cause %s", it->c_str());
	    replaner.addCause(it->c_str());
	}
    }


    // >> decideGoal setup
    string g_selector_name;

    if( nh.getParam("goal_selector/type", g_selector_name)){
	if (g_selector_name == "mid_point"){
	    goal_selector = new midPoint();
	}
	else{
	    ROS_ERROR("String %s does not name a valid goal selector", g_selector_name.c_str());
	}
    }
    else
    	ROS_ERROR("Parameter goal_selector has not been configured");



    // MarkerPublisher setup
    MarkerPublisher markers;
    markers.add("f_goal", "goal_frontier");
    markers.add("goal", "goal_marker");

    // Arrow shape
    geometry_msgs::Vector3 scale;
    scale.x = 0.5;
    scale.y = 0.2;
    scale.z = 0.1;

    int type = visualization_msgs::Marker::ARROW;
    markers.setProperty("goal", scale);
    markers.setProperty("goal", type);

    // ROS main loop
    ros::Rate loop_rate(1);

    bool first_time = true;
    bool exploration_finished=false;
    while (ros::ok())
    {
	if(mapServer.mapReceived())
	{
	    mapServer.setMapReceived();
	    ROS_INFO_ONCE("First map received!");

	    //ROS_INFO("First time? %d, Exploration finished? %d", first_time, exploration_finished);

	    if(exploration_finished)
		finish();
	    else
	    {
		if(robot.refreshPose())
		{
		    if(replaner.replan() || first_time)
		    {
			if (first_time)
			    first_time = false;

			robot.printStatus();
			robot.goTo(decideGoal());
		    }
		}
		else
		    ROS_WARN("Couldn't get robot position!");
	    }
	}
	else
	    ROS_INFO_ONCE("Waiting for first map");

	ros::spinOnce();
	loop_rate.sleep();
    }
}


} /* cam_exploration */



/**
 * @brief Brings up the node
 */
int main(int argc, char** argv)
{
    ROS_INFO("Exploration Node");
    ros::init(argc, argv, "cam_exploration");

    cam_exploration::robot.init();

    cam_exploration::spin();

    return 0;
}

