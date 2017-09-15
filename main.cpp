#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <condition_variable>
#include "IK.h"
#include "Serial.h"
#include "cam.h"
#include "tricks.h"
#include "PathPlanning.h"
#include "Routines.h"
#include <algorithm>
#include <chrono>
#include <math.h>
#include "doublefann.h"

#define x_comp      0
#define y_comp      1
#define z_comp      2

#define sx_comp     3
#define sy_comp     4
#define sz_comp     5

#define ax_comp     6
#define ay_comp     7
#define az_comp     8

#define d1  12.5   //ground to q1
#define d6  12 //gripper to wrist
#define a2 15.0    //q1 to q2
#define d4 19.2  //q2 to wrist

#define degtorad 0.01745329251994329576923690768488612713
#define radtodeg 57.2957795130823208767981548141051703324
#define pi  3.14159265358979323846264338327950288419716939937510

#define PI (3.141592653589793)
#define HALF_PI (1.570796326794897)
#define DOF 6 /* degrees of freedom in the system */

using namespace std;
/* I feel safer globally defining these*/
mutex mu,grabmu;
condition_variable cond;

char const * portName = "\\\\.\\COM3";
Tricks tricks = Tricks();
PathPlanning pp = PathPlanning();
cam CAM = cam(0,30); /* 30 is as high as she'll go*/
IK ik = IK();
Serial *arduino;
Routines rout = Routines();

double w[3][3]= {{0,1,0},    //the target rotation matrix
                 {0,0,1},
                 {1,0,0}};

struct Pos{
    double x; double y; double z;
    double alpha; double beta; double gamma; /* euler angles for the target orientation */
    int grip;
};

void forwardKinematics(double *angles, double *pos){
    double q1 =  (angles[0] + 1)*HALF_PI;           /* [0,PI] */
    double q2 =  (angles[1] + 1)*HALF_PI;           /* [0,PI] */
    double q3 =  -(angles[2] + 1)*HALF_PI + HALF_PI;/* [PI/2,-PI/2] */
    double q4 =  angles[3]*PI;                      /* [-PI,PI] */
    double q5 = (angles[4] + 1)*HALF_PI;            /* [0,PI] */
    double q6 = angles[5]*PI;                       /* [-PI,PI] */
    double temp = a2 + d1 + d4 + d6;

    double sx = cos(q6)*(cos(q4)*sin(q1) - cos(q1)*cos(q2 + q3)*sin(q4)) - (cos(q5)*sin(q1)*sin(q4) + cos(q1)*(cos(q2 + q3)*cos(q4)*cos(q5) - sin(q2 + q3)*sin(q5)))*sin(q6);
    double sy = cos(q1)*(-cos(q4)*cos(q6) + cos(q5)*sin(q4)*sin(q6)) - sin(q1)*(-sin(q2 + q3)*sin(q5)*sin(q6) + cos(q2 + q3)*(cos(q6)*sin(q4) + cos(q4)*sin(q5)*sin(q6)));
    double sz = -cos(q6)*sin(q2 + q3)*sin(q4) - (cos(q4)*cos(q5)*sin(q2 + q3) + cos(q2 + q3)*sin(q5))*sin(q6);
    double ax = sin(q1)*sin(q4)*sin(q5) + cos(q1)*(cos(q5)*sin(q2 + q3) + cos(q2 + q3)*cos(q4)*sin(q5));
    double ay = cos(q5)*sin(q1)*sin(q2 + q3) + (cos(q2 + q3)*cos(q4)*sin(q1) - cos(q1)*sin(q4))*sin(q5);
    double az = -cos(q2 + q3)*cos(q5) + cos(q4)*sin(q2 + q3)*sin(q5);

    pos[x_comp] = (1.0/temp)*(d6*sin(q1)*sin(q4)*sin(q5) + cos(q1)*(a2*cos(q2) + (d4 + d6*cos(q5))*sin(q2 + q3) + d6*cos(q2 + q3)*cos(q4)*sin(q5)));
    pos[y_comp] = (1.0/temp)*(cos(q3)*(d4 + d6*cos(q5))*sin(q1)*sin(q2) - d6*(cos(q4)*sin(q1)*sin(q2)*sin(q3) + cos(q1)*sin(q4))*sin(q5) + cos(q2)*sin(q1)*(a2 + (d4 + d6*cos(q5))*sin(q3) + d6*cos(q3)*cos(q4)*sin(q5)));
    pos[z_comp] = (1.0/temp)*(d1 - cos(q2 + q3)*(d4 + d6*cos(q5)) + a2*sin(q2) + d6*cos(q4)*sin(q2 + q3)*sin(q5));
    pos[sx_comp] = sx;
    pos[sy_comp] = sy;
    pos[sz_comp] = sz;
    pos[ax_comp] = ax;
    pos[ay_comp] = ay;
    pos[az_comp] = az;
}

void generateData(){
    double anglesInternal[6] = {0,0,0,0,-1,0};
    double angles[7];
    double anglesNN[7];
    double x[100],y[100],z[100],t;
    double r = 10.0;

    FILE *file;
    char data[1024];
    sprintf(data, "data_analysis/angles_data.dat");
    file = fopen(data,"wb");

    for(int i = 0; i < 100; i++){
        t = i/100.0;

        y[i] = 30;
        x[i] = r*sin(2*pi*t);
        z[i] = r*cos(2*pi*t) + 15;

        ik.inverseKinematicsNNRawDelta(x[i],y[i],z[i],w,anglesInternal,anglesNN);
        ik.inverseKinematicsRaw(x[i],y[i],z[i],w,angles,0);

        fprintf(file,"%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\n",t,angles[1],anglesNN[1],angles[2],anglesNN[2],angles[3],anglesNN[3],angles[4],anglesNN[4],angles[5],anglesNN[5],angles[6],anglesNN[6]);
    }
    fclose(file);
}

void compareTime(){
    std::random_device rd;
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 gen(seed);
    std::uniform_real_distribution<> dis(-1, 1);

    double angles[7];
    double anglesInternal[6] = {0,0,0,0,-1,0};

    double x,y,z;

    auto begin = std::chrono::high_resolution_clock::now();
    for(int i=0; i<1000; i++){
        x = dis(gen)*20;
        y  = (dis(gen)+1)*10 + 15;
        z = (dis(gen)+1)*20;
        //ik.inverseKinematicsRaw(x,y,z,w,angles,1);
        ik.inverseKinematicsNNRawDelta(x,y,z,w,anglesInternal,angles);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end-begin).count();
    cout << "1.000 calcs took " << time << "ms" << endl;

}

void compareAngles(double x, double y, double z){
    double anglesInternal[6] = {0,0,0,0,-1,0};
    double angles[7];
    double anglesNN[7];
    double pos[9];
    double temp = a2 + d1 + d4 + d6;

    for(int j=0; j<5; j++){
        ik.inverseKinematicsNNRawDelta(x,y,z,w,anglesInternal,anglesNN);
        ik.inverseKinematicsRaw(x,y,z,w,angles,0);

        forwardKinematics(anglesInternal,pos);

        for(int i=0; i<3; i++)
            cout << pos[i]*temp << endl;

        cout << "_______________" << endl;

        for(int i=3; i<9; i++)
            cout << pos[i] << endl;

        cout << "_______________" << endl;

        for(int i=1; i<7; i++){
            if(abs(angles[i]) < 0.1 )
                angles[i] =0;
            if(abs(anglesNN[i]) < 0.1)
                anglesNN[i] = 0;
            cout << angles[i]*radtodeg << "   " << anglesNN[i]*radtodeg << endl;
        }
    tricks.wait();
    }

}



int main(void){
//    arduino = new Serial(portName);
//    cout << "is connected: " << arduino->IsConnected() << endl;

//    ik.inverseKinematicsNNRawDelta(0,30,20,w,anglesInternal,angles);

//    int flip = 1;

//    rout.stacking(15,flip);
//    rout.stackingOO(15,flip);
//    rout.monkeySeeMonkeyDo();
//    rout.showOff(15);
//    rout.showOffNN(15);

//    generateData();

    return 0;
}
