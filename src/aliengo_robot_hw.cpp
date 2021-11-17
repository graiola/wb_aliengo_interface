#include "wb_aliengo_interface/aliengo_robot_hw.hpp"

namespace aliengo2ros
{

using namespace hardware_interface;

int64_t utime_now() {

    struct timeval timeofday;
    gettimeofday(&timeofday,NULL);
    if (timeofday.tv_sec < 0 || timeofday.tv_sec > UINT_MAX)
        throw std::runtime_error("Timeofday is out of dual signed 32-bit range");
    uint32_t sec	= timeofday.tv_sec;
    uint32_t nsec = timeofday.tv_usec * 1000;

    return (int64_t) (((uint64_t)sec)*1000000 + ((uint64_t)nsec) / 1000);
}


AliengoRobotHw::AliengoRobotHw()
{
    robot_name_ = "aliengo";
}


AliengoRobotHw::~AliengoRobotHw()
{

}


void AliengoRobotHw::init()
{

    auto joint_names = loadJointNamesFromSRDF();
    if(!WbRobotHwInterface::initializeInterfaces(joint_names))
    {
        ROS_ERROR_NAMED(CLASS_NAME,"Initialization of WbRobotHwInterface failed.");
        return;
    }

    registerInterfaces();

    aliengo_interface_.InitCmdData(aliengo_lowcmd_);
    startup_routine();
}

bool AliengoRobotHw::registerInterfaces()
{
    if(isInitialized())
    {
        // Register interfaces
        registerInterface(&joint_state_interface_);
        registerInterface(&imu_sensor_interface_);
        registerInterface(&ground_truth_interface_);
        registerInterface(&contact_sensor_interface_);
        registerInterface(&joint_effort_interface_);
    }
    return true;
}

void AliengoRobotHw::read()
{
    // Get robot data
    aliengo_state_ = aliengo_interface_.ReceiveObservation();

    // ------
    // Joints
    // ------
    for (unsigned int jj = 0; jj < n_dof_; ++jj)
    {
        joint_position_[jj] = static_cast<double>(aliengo_state_.motorState[aliengo_motor_idxs_[jj]].q)     ;
        joint_velocity_[jj] = static_cast<double>(aliengo_state_.motorState[aliengo_motor_idxs_[jj]].dq)    ;
        joint_effort_[jj]   = static_cast<double>(aliengo_state_.motorState[aliengo_motor_idxs_[jj]].tauEst);

        // NOTE: We report the previous commanded PD values. Because aliengohal doesn't provide these values
        joint_p_gain_[jj] = joint_p_gain_command_[jj];
        joint_d_gain_[jj] = joint_d_gain_command_[jj];
        joint_i_gain_[jj] = 0.0; // Aliengo does not use an I gain
    }

    // ---
    // IMU
    // ---
    imu_orientation_[0] = static_cast<double>(aliengo_state_.imu.quaternion[0]);  // w
    imu_orientation_[1] = static_cast<double>(aliengo_state_.imu.quaternion[1]);  // x
    imu_orientation_[2] = static_cast<double>(aliengo_state_.imu.quaternion[2]);  // y
    imu_orientation_[3] = static_cast<double>(aliengo_state_.imu.quaternion[3]);  // z

    imu_ang_vel_[0] = static_cast<double>(aliengo_state_.imu.gyroscope[0]);
    imu_ang_vel_[1] = static_cast<double>(aliengo_state_.imu.gyroscope[1]);
    imu_ang_vel_[2] = static_cast<double>(aliengo_state_.imu.gyroscope[2]);

    imu_lin_acc_[0] = static_cast<double>(aliengo_state_.imu.accelerometer[0]);
    imu_lin_acc_[1] = static_cast<double>(aliengo_state_.imu.accelerometer[1]);
    imu_lin_acc_[2] = static_cast<double>(aliengo_state_.imu.accelerometer[2]);

    // ---------------------------
    // Robot Base ("Ground Truth")
    // ---------------------------
    // TODO: See if we really need this fake ground truth!!
    //Ground Truth://this is an hack remove it
    quaterniond_tmp_.w() = base_orientation_[0] = imu_orientation_[0];
    quaterniond_tmp_.x() = base_orientation_[1] = imu_orientation_[1];
    quaterniond_tmp_.y() = base_orientation_[2] = imu_orientation_[2];
    quaterniond_tmp_.z() = base_orientation_[3] = imu_orientation_[3];

    world_R_base_ = quaterniond_tmp_.toRotationMatrix();

    // These vars are from imu in the base frame we should rotate to world frame
    vector3d_tmp_ << static_cast<double>(aliengo_state_.imu.gyroscope[0]),
                     static_cast<double>(aliengo_state_.imu.gyroscope[1]),
                     static_cast<double>(aliengo_state_.imu.gyroscope[2]);
    world_ang_vel_base_ = world_R_base_ * vector3d_tmp_;

    vector3d_tmp_ << static_cast<double>(aliengo_state_.imu.accelerometer[0]),
                     static_cast<double>(aliengo_state_.imu.accelerometer[1]),
                     static_cast<double>(aliengo_state_.imu.accelerometer[2]);
    world_lin_acc_base_ = world_R_base_ * vector3d_tmp_;

    base_ang_vel_[0] = world_ang_vel_base_(0);
    base_ang_vel_[1] = world_ang_vel_base_(1);
    base_ang_vel_[2] = world_ang_vel_base_(2);

    base_lin_acc_[0] = world_lin_acc_base_(0);
    base_lin_acc_[1] = world_lin_acc_base_(1);
    base_lin_acc_[2] = world_lin_acc_base_(2);

}

void AliengoRobotHw::write()
{
    for (unsigned int jj = 0; jj < n_dof_; ++jj)
    {
        aliengo_lowcmd_.motorCmd[aliengo_motor_idxs_[jj]].q   = static_cast<float>(joint_position_command_[jj]);
        aliengo_lowcmd_.motorCmd[aliengo_motor_idxs_[jj]].dq  = static_cast<float>(joint_velocity_command_[jj]);
        aliengo_lowcmd_.motorCmd[aliengo_motor_idxs_[jj]].tau = static_cast<float>(joint_effort_command_[jj]  );
        aliengo_lowcmd_.motorCmd[aliengo_motor_idxs_[jj]].Kp  = static_cast<float>(joint_p_gain_command_[jj]  );
        aliengo_lowcmd_.motorCmd[aliengo_motor_idxs_[jj]].Kd  = static_cast<float>(joint_d_gain_command_[jj]  );

        // aliengo_lowcmd_.motorCmd[aliengo_motor_idxs_[jj]].q = aliengo_state_.motorState[aliengo_motor_idxs_[jj]].q;
    }

    aliengo_interface_.SendLowCmd(aliengo_lowcmd_);
}

void AliengoRobotHw::send_zero_command()
{
    std::array<float, 60> zero_command = {0};
    // aliengo_interface_->SendCommand(zero_command);
    aliengo_interface_.SendCommand(zero_command);
}

void AliengoRobotHw::startup_routine()
{
    send_zero_command();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

} // namespace
