
// try this, e.g. with roslaunch stdr_launchers server_with_map_and_gui_plus_robot.launch
// or:  roslaunch cwru_376_launchers stdr_glennan_2.launch 
// watch resulting velocity commands with: rqt_plot /robot0/cmd_vel/linear/x (or jinx/cmd_vel...)

//intent of this program: modulate the velocity command to comply with a speed limit, v_max,
// acceleration limits, +/-a_max, and come to a halt gracefully at the end of
// an intended line segment

// notes on quaternions:
/*
From:
http://www.euclideanspace.com/maths/geometry/rotations/conversions/angleToQuaternion/

qx = ax * sin(angle/2)
qy = ay * sin(angle/2)
qz = az * sin(angle/2)
qw = cos(angle/2)


so, quaternion in 2-D plane (x,y,theta):
ax=0, ay=0, az = 1.0

qx = 0;
qy = 0;
qz = sin(angle/2)
qw = cos(angle/2)

therefore, theta = 2*atan2(qz,qw)
*/

#include <ros/ros.h>
#include <std_msgs/Float64.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <math.h>


// set some dynamic limits...
const double v_max = 5.0; //1m/sec is a slow walk
const double v_min = 0.1; // if command velocity too low, robot won't move
const double a_max = 0.1; //1m/sec^2 is 0.1 g's
//const double a_max_decel = 0.1; // TEST
const double omega_max = 1.0; //1 rad/sec-> about 6 seconds to rotate 1 full rev
const double alpha_max = 0.5; //0.5 rad/sec^2-> takes 2 sec to get from rest to full omega
const double DT = 0.050; // choose an update rate of 20Hz; go faster with actual hardware

// globals for communication w/ callbacks:
double odom_vel_ = 0.0; // measured/published system speed
double odom_omega_ = 0.0; // measured/published system yaw rate (spin)
double odom_x_ = 0.0;
double odom_y_ = 0.0;
double odom_phi_ = 0.0;
double dt_odom_ = 0.0;
ros::Time t_last_callback_;
double dt_callback_=0.0;

int segments_done = 0;
double segment_length_done = 0.0; // need to compute actual distance travelled within the current segment
double start_x = 0.0;
double start_y = 0.0;
double start_phi = 0.0;
//double rotate_angle = -1.5;

// receive odom messages and strip off the components we want to use
// tested this OK w/ stdr

// receive the pose and velocity estimates from the simulator (or the physical robot)
// copy the relevant values to global variables, for use by "main"
// Note: stdr updates odom only at 10Hz; Jinx is 50Hz (?)
void odomCallback(const nav_msgs::Odometry& odom_rcvd) {
    //here's a trick to compute the delta-time between successive callbacks:
    dt_callback_ = (ros::Time::now() - t_last_callback_).toSec();
    t_last_callback_ = ros::Time::now(); // let's remember the current time, and use it next iteration

    if (dt_callback_ > 0.15) { // on start-up, and with occasional hiccups, this delta-time can be unexpectedly large
        dt_callback_ = 0.1; // can choose to clamp a max value on this, if dt_callback is used for computations elsewhere
        ROS_WARN("large dt; dt = %lf", dt_callback_); // let's complain whenever this happens
    }
    
    // copy some of the components of the received message into global vars, for use by "main()"
    // we care about speed and spin, as well as position estimates x,y and heading
    odom_vel_ = odom_rcvd.twist.twist.linear.x;
    odom_omega_ = odom_rcvd.twist.twist.angular.z;
    odom_x_ = odom_rcvd.pose.pose.position.x;
    odom_y_ = odom_rcvd.pose.pose.position.y;
    //odom publishes orientation as a quaternion.  Convert this to a simple heading
    // see notes above for conversion for simple planar motion
    double quat_z = odom_rcvd.pose.pose.orientation.z;
    double quat_w = odom_rcvd.pose.pose.orientation.w;
    odom_phi_ = 2.0*atan2(quat_z, quat_w); // cheap conversion from quaternion to heading for planar motion

    // the output below could get annoying; may comment this out, but useful initially for debugging
    ROS_INFO("odom CB: x = %f, y= %f, phi = %f, v = %f, omega = %f", odom_x_, odom_y_, odom_phi_, odom_vel_, odom_omega_);
}


void rotateRampFunction(double rotate_angle, double start_phi, ros::Publisher vel_cmd_publisher, geometry_msgs::Twist cmd_vel, ros::Rate rtimer)
{
	//double start_phi = starting_phi;
	//ros::Publisher vel_cmd_publisher = publisher;
	//geometry_msgs::Twist cmd_vel = comd_vel;
	//double rotate_angle = angle;
	//Some properties of the trapezoidal rotation plan:

	double Tr_acc = omega_max / alpha_max;
	double Tr_dec = omega_max / alpha_max;
	double angle_accel = 0.5 * alpha_max * (Tr_acc * Tr_acc);
	double angle_decel = 0.5 * alpha_max * (Tr_dec * Tr_dec);
	double angle_const_omega = rotate_angle - angle_accel - angle_decel;
	double T_const_omega = angle_const_omega / omega_max;
	double T_angle_total = Tr_acc + Tr_dec + T_const_omega;
	double scheduled_rotate = 0.0;

	
	//Compute angle rotated so far:
		double delta_angle = odom_phi_ - start_phi;
		double radians_to_go = fabs(rotate_angle) - fabs(delta_angle);
		double new_cmd_omega = 0.0;
		//use the total rotate angle to calculate when to stop
		if (radians_to_go<= 0.0) { // at goal, or overshot; stop!
		    scheduled_rotate =0.0;
		}
		else if (radians_to_go <= angle_decel) { //possibly should be braking to a halt
		    // dist = 0.5*a*t_halt^2; so t_halt = sqrt(2*dist/a);   v = a*t_halt
		    // so v = a*sqrt(2*dist/a) = sqrt(2*dist*a)
		    scheduled_rotate = sqrt(2 * radians_to_go * alpha_max);
		    ROS_INFO("braking zone: rotate scheduled: = %f",scheduled_rotate);
		}
		else { // not ready to decel, so target omega is omega_max, either accel to it or hold it
		    scheduled_rotate = omega_max;
		}

		if (rotate_angle < 0)
		{
			scheduled_rotate = -scheduled_rotate;
		}
		
		//how does the current velocity compare to the scheduled vel?
		if (abs(odom_omega_) < abs(scheduled_rotate)) {  // maybe we halted, e.g. due to estop or obstacle;
		    // may need to ramp up to v_max; do so within accel limits
			if(rotate_angle < 0)  //If we are rotating negative
			{
				double r_test = odom_omega_ - alpha_max*dt_callback_;
				new_cmd_omega = (r_test > scheduled_rotate) ? r_test : scheduled_rotate; //choose lesser of two options
			}
			else {   //Otherwise, we are rotating in the positive direction
		    		double r_test = odom_omega_ + alpha_max*dt_callback_; // if callbacks are slow, this could be abrupt
		    		// operator:  c = (a>b) ? a : b;
		    		new_cmd_omega = (r_test < scheduled_rotate) ? r_test : scheduled_rotate; //choose lesser of two options
		    		// this prevents overshooting scheduled_vel
			}
		} else if (abs(odom_omega_) > abs(scheduled_rotate)) { //travelling too fast--this could be trouble
		    // ramp down to the scheduled velocity.  However, scheduled velocity might already be ramping down at a_max.
		    // need to catch up, so ramp down even faster than a_max.  Try 1.2*a_max.
		    //ROS_INFO("odom vel: %f; sched vel: %f",odom_vel_,scheduled_vel); //debug/analysis output; can comment this out
		    
			if(rotate_angle < 0)  //If we are rotating negative 
			{
				double r_test = odom_omega_ + 1.2 * alpha_max*dt_callback_; //moving too fast--try decelerating faster than nominal a_max
				new_cmd_omega = (r_test < scheduled_rotate) ? r_test : scheduled_rotate; // choose larger of two options...don't overshoot scheduled_vel
			}
			else {
				double r_test = odom_omega_ - 1.2 * alpha_max*dt_callback_; //moving too fast--try decelerating faster than nominal a_max
		    		new_cmd_omega = (r_test > scheduled_rotate) ? r_test : scheduled_rotate; // choose larger of two options...don't overshoot scheduled_vel
			}
		    
		} else {
		    new_cmd_omega = scheduled_rotate; //silly third case: this is already true, if here.  Issue the scheduled velocity
		}

		cmd_vel.angular.z = new_cmd_omega;
		vel_cmd_publisher.publish(cmd_vel); // publish the command to robot0/cmd_vel
		rtimer.sleep(); // sleep for remainder of timed iteration

		
		if(radians_to_go <= 0) 
		{

			cmd_vel.angular.z = 0.0;
			segments_done++;
			segment_length_done = 0.0;
			//When we are finished with this segment, set the initial parameters to the current odom measurements
			start_x = odom_x_;
			start_y = odom_y_;
			start_phi = odom_phi_;
		}
	

}


//The following is the function that is called to move the robot forward, and whose velocity follows the trapezoidal velocity profile
void moveForwardFunction(double segment_length, double segment_length_done, double start_x, double start_y, double scheduled_vel, double new_cmd_vel, double new_cmd_omega, ros::Publisher vel_cmd_publisher, geometry_msgs::Twist cmd_vel, ros::Rate rtimer)
{
	double T_accel = v_max / a_max; //...assumes start from rest
    	double T_decel = v_max / a_max; //(for same decel as accel); assumes brake to full halt
    	double dist_accel = 0.5 * a_max * (T_accel * T_accel); //distance rqd to ramp up to full speed
    	double dist_decel = 0.5 * a_max * (T_decel * T_decel);; //same as ramp-up distance
    	double dist_const_v = segment_length - dist_accel - dist_decel; //if this is <0, never get to full spd
    	double T_const_v = dist_const_v / v_max; //will be <0 if don't get to full speed
    	double T_segment_tot = T_accel + T_decel + T_const_v; // expected duration of this move
	// compute distance travelled so far:
		double delta_x = odom_x_ - start_x;
		double delta_y = odom_y_ - start_y;
		segment_length_done = sqrt(delta_x * delta_x + delta_y * delta_y);
		ROS_INFO("dist travelled: %f", segment_length_done);
		double dist_to_go = segment_length - segment_length_done;

		//use segment_length_done to decide what vel should be, as per plan
		if (dist_to_go<= 0.0) { // at goal, or overshot; stop!
		    scheduled_vel=0.0;
		}
		else if (dist_to_go <= dist_decel) { //possibly should be braking to a halt
		    // dist = 0.5*a*t_halt^2; so t_halt = sqrt(2*dist/a);   v = a*t_halt
		    // so v = a*sqrt(2*dist/a) = sqrt(2*dist*a)
		    scheduled_vel = sqrt(2 * dist_to_go * a_max);
		    ROS_INFO("braking zone: v_sched = %f",scheduled_vel);
		}
		else { // not ready to decel, so target vel is v_max, either accel to it or hold it
		    scheduled_vel = v_max;
		}
		
	  

		//how does the current velocity compare to the scheduled vel?
		if (odom_vel_ < scheduled_vel) {  // maybe we halted, e.g. due to estop or obstacle;
		    // may need to ramp up to v_max; do so within accel limits
		    double v_test = odom_vel_ + a_max*dt_callback_; // if callbacks are slow, this could be abrupt
		    // operator:  c = (a>b) ? a : b;
		    new_cmd_vel = (v_test < scheduled_vel) ? v_test : scheduled_vel; //choose lesser of two options
		    // this prevents overshooting scheduled_vel
		} else if (odom_vel_ > scheduled_vel) { //travelling too fast--this could be trouble
		    // ramp down to the scheduled velocity.  However, scheduled velocity might already be ramping down at a_max.
		    // need to catch up, so ramp down even faster than a_max.  Try 1.2*a_max.
		    ROS_INFO("odom vel: %f; sched vel: %f",odom_vel_,scheduled_vel); //debug/analysis output; can comment this out
		    
		    double v_test = odom_vel_ - 1.2 * a_max*dt_callback_; //moving too fast--try decelerating faster than nominal a_max

		    new_cmd_vel = (v_test > scheduled_vel) ? v_test : scheduled_vel; // choose larger of two options...don't overshoot scheduled_vel
		} else {
		    new_cmd_vel = scheduled_vel; //silly third case: this is already true, if here.  Issue the scheduled velocity
		}
		ROS_INFO("cmd vel: %f",new_cmd_vel); // debug output

		cmd_vel.linear.x = new_cmd_vel;
		cmd_vel.angular.z = new_cmd_omega; // spin command; always zero, in this example
		vel_cmd_publisher.publish(cmd_vel); // publish the command to robot0/cmd_vel
		rtimer.sleep(); // sleep for remainder of timed iteration

		if (dist_to_go <= 0.0) { //uh-oh...went too far already!
		    cmd_vel.linear.x = 0.0;  //command vel=0
			segments_done++;
			segment_length_done = 0.0;
			//When we are finished with this segment, set the initial parameters to the current odom measurements
			start_x = odom_x_;
			start_y = odom_y_;
			start_phi = odom_phi_;
		}
}


int main(int argc, char **argv) {
    ros::init(argc, argv, "vel_scheduler"); // name of this node will be "minimal_publisher1"
    ros::NodeHandle nh; // get a ros nodehandle; standard yadda-yadda
    //create a publisher object that can talk to ROS and issue twist messages on named topic;
    // note: this is customized for stdr robot; would need to change the topic to talk to jinx, etc.
    ros::Publisher vel_cmd_publisher = nh.advertise<geometry_msgs::Twist>("robot0/cmd_vel", 1);
    ros::Subscriber sub = nh.subscribe("/robot0/odom", 1, odomCallback);
    ros::Rate rtimer(1 / DT); // frequency corresponding to chosen sample period DT; the main loop will run this fast

    // here is a crude description of one segment of a journey.  Will want to generalize this to handle multiple segments
    // define the desired path length of this segment
    
    //here's a subtlety:  might be tempted to measure distance to the goal, instead of distance from the start.
    // HOWEVER, will NEVER satisfy distance to goal = 0 precisely, but WILL eventually move far enought to satisfy distance travelled condition
    segment_length_done = 0.0; // need to compute actual distance travelled within the current segment
    start_x = 0.0; // fill these in with actual values once odom message is received
    start_y = 0.0; // subsequent segment start coordinates should be specified relative to end of previous segment
    
    start_phi = 0.0;

    double scheduled_vel = 0.0; //desired vel, assuming all is per plan
    double new_cmd_vel = 0.1; // value of speed to be commanded; update each iteration
    double new_cmd_omega = 0.0; // update spin rate command as well

    geometry_msgs::Twist cmd_vel; //create a variable of type "Twist" to publish speed/spin commands

    cmd_vel.linear.x = 0.0; // initialize these values to zero
    cmd_vel.linear.y = 0.0;
    cmd_vel.linear.z = 0.0;
    cmd_vel.angular.x = 0.0;
    cmd_vel.angular.y = 0.0;
    cmd_vel.angular.z = 0.0;

    // let's wait for odom callback to start getting good values...
    odom_omega_ = 1000000; // absurdly high
    ROS_INFO("waiting for valid odom callback...");
    t_last_callback_ = ros::Time::now(); // initialize reference for computed update rate of callback
    while (odom_omega_ > 1000) {
        rtimer.sleep();
        ros::spinOnce();
    }
    ROS_INFO("received odom message; proceeding");
    start_x = odom_x_;
    start_y = odom_y_;
    start_phi = odom_phi_;
    ROS_INFO("start pose: x %f, y= %f, phi = %f", start_x, start_y, start_phi);

    // compute some properties of trapezoidal velocity profile plan:
    /*double T_accel = v_max / a_max; //...assumes start from rest
    double T_decel = v_max / a_max; //(for same decel as accel); assumes brake to full halt
    double dist_accel = 0.5 * a_max * (T_accel * T_accel); //distance rqd to ramp up to full speed
    double dist_decel = 0.5 * a_max * (T_decel * T_decel);; //same as ramp-up distance
    double dist_const_v = segment_length - dist_accel - dist_decel; //if this is <0, never get to full spd
    double T_const_v = dist_const_v / v_max; //will be <0 if don't get to full speed
    double T_segment_tot = T_accel + T_decel + T_const_v; // expected duration of this move*/

	//Some properties of the trapezoidal rotation plan:
	/*double Tr_acc = omega_max / alpha_max;
	double Tr_dec = omega_max / alpha_max;
	double angle_accel = 0.5 * alpha_max * (Tr_acc * Tr_acc);
	double angle_decel = 0.5 * alpha_max * (Tr_dec * Tr_dec);
	double angle_const_omega = rotate_angle - angle_accel - angle_decel;
	double T_const_omega = angle_const_omega / omega_max;
	double T_angle_total = Tr_acc + Tr_dec + T_const_omega;
	double scheduled_rotate = 0.0;*/

    bool toggleSegment = false;

    //dist_decel*= 2.0; // TEST TEST TEST
    while (ros::ok()) // do work here in infinite loop (desired for this example), but terminate if detect ROS has faulted (or ctl-C)
    {
        ros::spinOnce(); // allow callbacks to populate fresh data

	//Beginning of the function calls for each segment (0 - 4)
	if (segments_done == 0) 
	{
		moveForwardFunction(4.5, segment_length_done, start_x, start_y, scheduled_vel, new_cmd_vel, new_cmd_omega, vel_cmd_publisher, cmd_vel, rtimer);
		
	}
	       
	if (segments_done == 1)
	{
		rotateRampFunction(-1.5079, start_phi, vel_cmd_publisher, cmd_vel, rtimer);
	}
	if (segments_done == 2) {
		moveForwardFunction(12.1, segment_length_done, start_x, start_y, scheduled_vel, new_cmd_vel, new_cmd_omega, vel_cmd_publisher, cmd_vel, rtimer);
	}
	if (segments_done == 3) {
		rotateRampFunction(-1.508, start_phi, vel_cmd_publisher, cmd_vel, rtimer);
	}
	if (segments_done == 4) {
		moveForwardFunction(8.5, segment_length_done, start_x, start_y, scheduled_vel, new_cmd_vel, new_cmd_omega, vel_cmd_publisher, cmd_vel, rtimer);
	}




        //if (dist_to_go <= 0.0) break; // halt this node when this segment is complete.
        // will want to generalize this to handle multiple segments
        // ideally, will want to receive segments dynamically as publications from a higher-level planner
    }
    ROS_INFO("completed move distance");

	
}




