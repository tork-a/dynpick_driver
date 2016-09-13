#include <ros/ros.h>
#include <std_srvs/Empty.h>
#include <geometry_msgs/WrenchStamped.h>


#include	<stdio.h>
#include	<fcntl.h>
#include	<time.h>
#include	<termios.h>
#include	<string.h>
#include	<unistd.h>

#define true		1
#define false		0

int SetComAttr(int fdc)
	{
	int			n;

	struct termios	term;


	// Set baud rate
	n = tcgetattr(fdc, &term);
	if (n < 0)
		goto over;

	bzero(&term, sizeof(term));

	term.c_cflag = B921600 | CS8 | CLOCAL | CREAD;
	term.c_iflag = IGNPAR;
	term.c_oflag = 0;
	term.c_lflag = 0;/*ICANON;*/
 
	term.c_cc[VINTR]    = 0;     /* Ctrl-c */ 
	term.c_cc[VQUIT]    = 0;     /* Ctrl-? */
	term.c_cc[VERASE]   = 0;     /* del */
	term.c_cc[VKILL]    = 0;     /* @ */
	term.c_cc[VEOF]     = 4;     /* Ctrl-d */
	term.c_cc[VTIME]    = 0;
	term.c_cc[VMIN]     = 0;
	term.c_cc[VSWTC]    = 0;     /* '?0' */
	term.c_cc[VSTART]   = 0;     /* Ctrl-q */ 
	term.c_cc[VSTOP]    = 0;     /* Ctrl-s */
	term.c_cc[VSUSP]    = 0;     /* Ctrl-z */
	term.c_cc[VEOL]     = 0;     /* '?0' */
	term.c_cc[VREPRINT] = 0;     /* Ctrl-r */
	term.c_cc[VDISCARD] = 0;     /* Ctrl-u */
	term.c_cc[VWERASE]  = 0;     /* Ctrl-w */
	term.c_cc[VLNEXT]   = 0;     /* Ctrl-v */
	term.c_cc[VEOL2]    = 0;     /* '?0' */

//	tcflush(fdc, TCIFLUSH);
	n = tcsetattr(fdc, TCSANOW, &term);
over :
	return (n);
	}


bool do_reset = false;
int offset[6];
bool reset(std_srvs::Empty::Request  &req,
           std_srvs::Empty::Response &res )
{
  do_reset = true;
}

int main(int argc, char **argv) {
    int fdc;
    int clock = 0;
    double rate;
    std::string devname, frame_id;

    fdc = -1;

    ros::init(argc, argv, "dynpick_driver");
    ros::NodeHandle n, nh("~");
    nh.param<std::string>("device", devname, "/dev/ttyUSB0");
    nh.param<std::string>("frame_id", frame_id, "/sensor");
    nh.param<double>("rate", rate, 1000);

    ros::Publisher pub = n.advertise<geometry_msgs::WrenchStamped>("force", 1000);

    ros::ServiceServer service = n.advertiseService("reset_offset", reset);

    // offset
    nh.param<int>("offset_0", (offset[0]), 8192);
    nh.param<int>("offset_1", (offset[1]), 8192);
    nh.param<int>("offset_2", (offset[2]), 8192);
    nh.param<int>("offset_3", (offset[3]), 8192);
    nh.param<int>("offset_4", (offset[4]), 8192);
    nh.param<int>("offset_5", (offset[5]), 8192);
    ROS_INFO("initialize offset : %d %d %d %d %d %d", offset[0], offset[1], offset[2], offset[3], offset[4], offset[5]);

    // Open COM port
    ROS_INFO("Open %s", devname.c_str());

    fdc = open(devname.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fdc < 0) {
        ROS_ERROR("could not open %s\n", devname.c_str());
        return -1;
    }

    // Obtain sampling rate
    ROS_INFO("Sampling time = %f ms\n", 1.0/rate);

    // Set baud rate of COM port
    SetComAttr(fdc);

    // Request for initial single data
    write(fdc, "R", 1);

    ros::Rate loop_rate(rate);
    while (ros::ok()) {
        int c, len;
        char str[256];
        int tick;
        unsigned short data[6];

        geometry_msgs::WrenchStamped msg;

        // Request for initial data (2nd round)
        write(fdc, "R", 1);

        // Obtain single data
#define DATA_LENGTH 27
        len = 0;
        while ( len < DATA_LENGTH ) {
                c = read(fdc, str+len, DATA_LENGTH-len);
                if (c > 0) {
                    len += c;
                } else {
                    ROS_DEBUG("=== need to read more data ... n = %d (%d) ===", c, len);
                    continue;
                }
            }
        if ( len != DATA_LENGTH ) {
            ROS_WARN("=== error reciving data ... n = %d ===", c);
        }

        sscanf(str, "%1d%4hx%4hx%4hx%4hx%4hx%4hx",
               &tick, &data[0], &data[1], &data[2], &data[3], &data[4], &data[5]);

        msg.header.frame_id = frame_id;
        msg.header.stamp = ros::Time::now();
        msg.header.seq = clock++;

        if (do_reset) {
          offset[0] = data[0];
          offset[1] = data[1];
          offset[2] = data[2];
          offset[3] = data[3];
          offset[4] = data[4];
          offset[5] = data[5];
          nh.setParam("offset_0", offset[0]);
          nh.setParam("offset_1", offset[1]);
          nh.setParam("offset_2", offset[2]);
          nh.setParam("offset_3", offset[3]);
          nh.setParam("offset_4", offset[4]);
          nh.setParam("offset_5", offset[5]);
          ROS_INFO("update offset : %d %d %d %d %d %d", offset[0], offset[1], offset[2], offset[3], offset[4], offset[5]);
          do_reset = false;
        }

        msg.wrench.force.x = (data[0]-offset[0])/1000.0;
        msg.wrench.force.y = (data[1]-offset[1])/1000.0;
        msg.wrench.force.z = (data[2]-offset[2])/1000.0;
        msg.wrench.torque.x = (data[3]-offset[3])/1000.0;
        msg.wrench.torque.y = (data[4]-offset[4])/1000.0;
        msg.wrench.torque.z = (data[5]-offset[5])/1000.0;

        pub.publish(msg);
        ros::spinOnce();

        loop_rate.sleep();
    }

    return 0;
}
