#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
int main(int argc, char **argv)
{
ros::init(argc,argv,"robot0_commander"); // name of this node 
ros::NodeHandle nh; // two lines to create a publisher object that can talk to ROS
//stdr "robot0" is expecting to receive commands on topic: /robot0/cmd_vel
// commands are of type geometry_msgs/Twist, but they use only velocity in x dir and
//  yaw rate in z-dir; other 4 fields will be ignored
ros::Publisher cmd_publisher = nh.advertise<geometry_msgs::Twist>("/robot0/cmd_vel",1);
// change topic to command abby...
//ros::Publisher cmd_publisher = nh.advertise<geometry_msgs::Twist>("abby/cmd_vel",1);
ros::Rate sleep_timer(100); //let's make a 100Hz timer

//create a variable of type "Twist", as defined in: /opt/ros/hydro/share/geometry_msgs
// any message published on a ROS topic must have a pre-defined format, so subscribers know how to
// interpret the serialized data transmission
geometry_msgs::Twist twist_cmd;
// look at the components of a message of type geometry_msgs::Twist by typing:
// rosmsg show geometry_msgs/Twist
// It has 6 fields.  Let's fill them all in with some data:
twist_cmd.linear.x = 0.0;
twist_cmd.linear.y = 0.0;
twist_cmd.linear.z = 0.0;
twist_cmd.angular.x = 0.0;
twist_cmd.angular.y = 0.0;
twist_cmd.angular.z = 0.0;

twist_cmd.linear.x = 0.4;


// timer test...print out a message every 1 second
ROS_INFO("count-down");
for (int j=3;j>0;j--) {
    ROS_INFO("%d",j);
    for (int i = 0; i<100;i++)
        sleep_timer.sleep();
}

int niters = 1200; //1000 iters at 100Hz is 10 seconds;
//iteration counter; at 10ms/iteration, and 0.2m/sec, expect 2mm/iter
// should move by 2m over 10 sec
for (int i=0;i<niters;i++) {
    cmd_publisher.publish(twist_cmd); // really, should only need to publish this once, but no hard done
    sleep_timer.sleep(); // sleep for (remainder of) 10m
}
twist_cmd.linear.x = 0.0;
twist_cmd.angular.z = -0.314;
niters=500; // 5 sec
ROS_INFO("Time to rotate negative");
for (int i=0;i<niters;i++) {
    cmd_publisher.publish(twist_cmd); // really, should only need to publish this once, but no hard done
    sleep_timer.sleep(); // sleep for (remainder of) 10m
}
twist_cmd.linear.x = 0.0;
twist_cmd.angular.z = 0;
cmd_publisher.publish(twist_cmd); // and halt
//}

//Now move forward for 15.5 seconds at 0.8 m/s to get down to the hall
twist_cmd.linear.x = 0.8;
niters = 1550;
for (int i = 0; i < niters; i++) {
    cmd_publisher.publish(twist_cmd); // really, should only need to publish this once, but no hard done
    sleep_timer.sleep();
}

//Now turn right by 90 degrees; same as the code above that was turned the robot at the first intersection

twist_cmd.linear.x = 0.0;
twist_cmd.angular.z = -0.314;
niters=500; // 5 sec
ROS_INFO("Time to rotate negative (again)!");
for (int i=0;i<niters;i++) {
    cmd_publisher.publish(twist_cmd); // really, should only need to publish this once, but no hard done
    sleep_timer.sleep(); // sleep for (remainder of) 10m
}


//Now move forward again to get to the vending machines
twist_cmd.angular.z = 0.0;
twist_cmd.linear.x = 0.4;
niters = 2200;
for (int i = 0; i < niters; i++) {
    cmd_publisher.publish(twist_cmd); // really, should only need to publish this once, but no hard done
    sleep_timer.sleep();
}

//Now rotate right again to face the vending machines
twist_cmd.linear.x = 0.0;
twist_cmd.angular.z = -0.314;
niters=500; // 5 sec
ROS_INFO("Time to rotate negative (again)!");
for (int i=0;i<niters;i++) {
    cmd_publisher.publish(twist_cmd); // really, should only need to publish this once, but no hard done
    sleep_timer.sleep(); // sleep for (remainder of) 10m
}

//Finally, move forward a bit, to get close to the vending machines
twist_cmd.angular.z = 0.0;
twist_cmd.linear.x = 0.4;
niters = 200;
for (int i = 0; i < niters; i++) {
    cmd_publisher.publish(twist_cmd); // really, should only need to publish this once, but no hard done
    sleep_timer.sleep();
}

ROS_INFO("Our work here is done!");
twist_cmd.linear.x = 0.0;
twist_cmd.angular.z = 0;
cmd_publisher.publish(twist_cmd); // and halt


return 0;
} 
