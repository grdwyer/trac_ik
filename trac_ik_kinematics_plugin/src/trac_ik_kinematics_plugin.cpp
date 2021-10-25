/********************************************************************************
Copyright (c) 2015, TRACLabs, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
********************************************************************************/


#include <rclcpp/rclcpp.hpp>
#include <urdf/model.h>
#include <tf2_kdl/tf2_kdl.h>
#include <algorithm>
#include <kdl/tree.hpp>
#include <kdl_parser/kdl_parser.hpp>
#include <trac_ik/trac_ik.hpp>
#include <trac_ik/trac_ik_kinematics_plugin.hpp>
#include <limits>

namespace trac_ik_kinematics_plugin
{
    static rclcpp::Logger LOGGER = rclcpp::get_logger("moveit_trac_ik_kinematics_plugin.trac_ik_kinematics_plugin");

    bool TRAC_IKKinematicsPlugin::initialize(const rclcpp::Node::SharedPtr& node, const moveit::core::RobotModel& robot_model,
                                             const std::string& group_name, const std::string& base_frame,
                                             const std::vector<std::string>& tip_frames, double search_discretization)
    {
        node_ = node;
        storeValues(robot_model, group_name, base_frame, tip_frames, search_discretization);
        joint_model_group_ = robot_model_->getJointModelGroup(group_name);
        if (!joint_model_group_)
            return false;

        if (!joint_model_group_->isChain())
        {
            RCLCPP_ERROR(LOGGER, "Group '%s' is not a chain", group_name.c_str());
            return false;
        }
        if (!joint_model_group_->isSingleDOFJoints())
        {
            RCLCPP_ERROR(LOGGER, "Group '%s' includes joints that have more than 1 DOF", group_name.c_str());
            return false;
        }

        RCLCPP_DEBUG_STREAM(LOGGER, "Reading joints and links from URDF");

        KDL::Tree tree;
        urdf::ModelInterface urdf = *robot_model_->getURDF().get();
        if (!kdl_parser::treeFromUrdfModel(urdf, tree))
        {
            RCLCPP_FATAL(LOGGER, "Failed to extract kdl tree from xml robot description");
            return false;
        }

        if (!tree.getChain(getBaseFrame(), getTipFrame(), chain_))
        {
            RCLCPP_FATAL_STREAM(LOGGER, "Couldn't find chain " << getBaseFrame() << " to " << getTipFrame());
            return false;
        }

        num_joints_ = chain_.getNrOfJoints();

        std::vector<KDL::Segment> chain_segs = chain_.segments;

        urdf::JointConstSharedPtr joint;

        std::vector<double> l_bounds, u_bounds;

        joint_min_.resize(num_joints_);
        joint_max_.resize(num_joints_);

        uint joint_num = 0;
        for (unsigned int i = 0; i < chain_segs.size(); ++i)
        {

            link_names_.push_back(chain_segs[i].getName());
            joint = urdf.getJoint(chain_segs[i].getJoint().getName());
            if (joint->type != urdf::Joint::UNKNOWN && joint->type != urdf::Joint::FIXED)
            {
                joint_num++;
                assert(joint_num <= num_joints_);
                float lower, upper;
                int hasLimits;
                joint_names_.push_back(joint->name);
                if (joint->type != urdf::Joint::CONTINUOUS)
                {
                    if (joint->safety)
                    {
                        lower = std::max(joint->limits->lower, joint->safety->soft_lower_limit);
                        upper = std::min(joint->limits->upper, joint->safety->soft_upper_limit);
                    }
                    else
                    {
                        lower = joint->limits->lower;
                        upper = joint->limits->upper;
                    }
                    hasLimits = 1;
                }
                else
                {
                    hasLimits = 0;
                }
                if (hasLimits)
                {
                    joint_min_(joint_num - 1) = lower;
                    joint_max_(joint_num - 1) = upper;
                }
                else
                {
                    joint_min_(joint_num - 1) = std::numeric_limits<float>::lowest();
                    joint_max_(joint_num - 1) = std::numeric_limits<float>::max();
                }
                RCLCPP_INFO_STREAM(LOGGER, "IK Using joint " << chain_segs[i].getName() << " " << joint_min_(joint_num - 1) << " " << joint_max_(joint_num - 1));
            }
        }

        RCLCPP_INFO(LOGGER, "Looking in common namespaces for param name: %s", (group_name + ".position_only_ik").c_str());
        lookupParam(node_,"position_only_ik", position_ik_, false);
        RCLCPP_INFO(LOGGER, "Looking in common namespaces for param name: %s", (group_name + ".solve_type").c_str());
        lookupParam(node_, "solve_type", solve_type_, std::string("Speed"));
        RCLCPP_INFO(LOGGER, "Using solve type %s", solve_type_.c_str());
        lookupParam(node_,"epsilon", epsilon_, 1e-5);
        RCLCPP_INFO(LOGGER, "Using epsilon value of %g", epsilon_);

        active_ = true;
        return true;
    }


    int TRAC_IKKinematicsPlugin::getKDLSegmentIndex(const std::string &name) const
    {
        int i = 0;
        while (i < (int)chain_.getNrOfSegments())
        {
            if (chain_.getSegment(i).getName() == name)
            {
                return i + 1;
            }
            i++;
        }
        return -1;
    }


    bool TRAC_IKKinematicsPlugin::getPositionFK(const std::vector<std::string> &link_names,
                                                const std::vector<double> &joint_angles,
                                                std::vector<geometry_msgs::msg::Pose> &poses) const
    {
        if (!active_)
        {
            RCLCPP_ERROR(LOGGER, "kinematics not active");
            return false;
        }
        poses.resize(link_names.size());
        if (joint_angles.size() != num_joints_)
        {
            RCLCPP_ERROR(LOGGER, "Joint angles vector must have size: %d", num_joints_);
            return false;
        }

        KDL::Frame p_out;
        geometry_msgs::msg::PoseStamped pose;

        KDL::JntArray jnt_pos_in(num_joints_);
        for (unsigned int i = 0; i < num_joints_; i++)
        {
            jnt_pos_in(i) = joint_angles[i];
        }

        KDL::ChainFkSolverPos_recursive fk_solver(chain_);

        bool valid = true;
        for (unsigned int i = 0; i < poses.size(); i++)
        {
            RCLCPP_DEBUG(LOGGER, "End effector index: %d", getKDLSegmentIndex(link_names[i]));
            if (fk_solver.JntToCart(jnt_pos_in, p_out, getKDLSegmentIndex(link_names[i])) >= 0)
            {
                poses[i] = tf2::toMsg(p_out);
            }
            else
            {
                RCLCPP_ERROR(LOGGER, "Could not compute FK for %s", link_names[i].c_str());
                valid = false;
            }
        }

        return valid;
    }


    bool TRAC_IKKinematicsPlugin::getPositionIK(const geometry_msgs::msg::Pose &ik_pose,
                                                const std::vector<double> &ik_seed_state,
                                                std::vector<double> &solution,
                                                moveit_msgs::msg::MoveItErrorCodes &error_code,
                                                const kinematics::KinematicsQueryOptions &options) const
    {
        const IKCallbackFn solution_callback = 0;
        std::vector<double> consistency_limits;

        return searchPositionIK(ik_pose,
                                ik_seed_state,
                                default_timeout_,
                                solution,
                                solution_callback,
                                error_code,
                                consistency_limits,
                                options);
    }

    bool TRAC_IKKinematicsPlugin::searchPositionIK(const geometry_msgs::msg::Pose &ik_pose,
                                                   const std::vector<double> &ik_seed_state,
                                                   double timeout,
                                                   std::vector<double> &solution,
                                                   moveit_msgs::msg::MoveItErrorCodes &error_code,
                                                   const kinematics::KinematicsQueryOptions &options) const
    {
        const IKCallbackFn solution_callback = 0;
        std::vector<double> consistency_limits;

        return searchPositionIK(ik_pose,
                                ik_seed_state,
                                timeout,
                                solution,
                                solution_callback,
                                error_code,
                                consistency_limits,
                                options);
    }

    bool TRAC_IKKinematicsPlugin::searchPositionIK(const geometry_msgs::msg::Pose &ik_pose,
                                                   const std::vector<double> &ik_seed_state,
                                                   double timeout,
                                                   const std::vector<double> &consistency_limits,
                                                   std::vector<double> &solution,
                                                   moveit_msgs::msg::MoveItErrorCodes &error_code,
                                                   const kinematics::KinematicsQueryOptions &options) const
    {
        const IKCallbackFn solution_callback = 0;
        return searchPositionIK(ik_pose,
                                ik_seed_state,
                                timeout,
                                solution,
                                solution_callback,
                                error_code,
                                consistency_limits,
                                options);
    }

    bool TRAC_IKKinematicsPlugin::searchPositionIK(const geometry_msgs::msg::Pose &ik_pose,
                                                   const std::vector<double> &ik_seed_state,
                                                   double timeout,
                                                   std::vector<double> &solution,
                                                   const IKCallbackFn &solution_callback,
                                                   moveit_msgs::msg::MoveItErrorCodes &error_code,
                                                   const kinematics::KinematicsQueryOptions &options) const
    {
        std::vector<double> consistency_limits;
        return searchPositionIK(ik_pose,
                                ik_seed_state,
                                timeout,
                                solution,
                                solution_callback,
                                error_code,
                                consistency_limits,
                                options);
    }

    bool TRAC_IKKinematicsPlugin::searchPositionIK(const geometry_msgs::msg::Pose &ik_pose,
                                                   const std::vector<double> &ik_seed_state,
                                                   double timeout,
                                                   const std::vector<double> &consistency_limits,
                                                   std::vector<double> &solution,
                                                   const IKCallbackFn &solution_callback,
                                                   moveit_msgs::msg::MoveItErrorCodes &error_code,
                                                   const kinematics::KinematicsQueryOptions &options) const
    {
        return searchPositionIK(ik_pose,
                                ik_seed_state,
                                timeout,
                                solution,
                                solution_callback,
                                error_code,
                                consistency_limits,
                                options);
    }

    bool TRAC_IKKinematicsPlugin::searchPositionIK(const geometry_msgs::msg::Pose &ik_pose,
                                                   const std::vector<double> &ik_seed_state,
                                                   double timeout,
                                                   std::vector<double> &solution,
                                                   const IKCallbackFn &solution_callback,
                                                   moveit_msgs::msg::MoveItErrorCodes &error_code,
                                                   const std::vector<double> &consistency_limits,
                                                   const kinematics::KinematicsQueryOptions &options) const
    {
        RCLCPP_DEBUG_STREAM(LOGGER, "getPositionIK");

        if (!active_)
        {
            RCLCPP_ERROR(LOGGER, "kinematics not active");
            error_code.val = error_code.NO_IK_SOLUTION;
            return false;
        }

        if (ik_seed_state.size() != num_joints_)
        {
            RCLCPP_ERROR_STREAM(LOGGER, "Seed state must have size " << num_joints_ << " instead of size " << ik_seed_state.size());
            error_code.val = error_code.NO_IK_SOLUTION;
            return false;
        }

        KDL::Frame frame;
        tf2::fromMsg(ik_pose, frame);

        KDL::JntArray in(num_joints_), out(num_joints_);

        for (uint z = 0; z < num_joints_; z++)
            in(z) = ik_seed_state[z];

        KDL::Twist bounds = KDL::Twist::Zero();

        if (position_ik_)
        {
            bounds.rot.x(std::numeric_limits<float>::max());
            bounds.rot.y(std::numeric_limits<float>::max());
            bounds.rot.z(std::numeric_limits<float>::max());
        }

        TRAC_IK::SolveType solvetype;

        if (solve_type_ == "Manipulation1")
            solvetype = TRAC_IK::Manip1;
        else if (solve_type_ == "Manipulation2")
            solvetype = TRAC_IK::Manip2;
        else if (solve_type_ == "Distance")
            solvetype = TRAC_IK::Distance;
        else
        {
            if (solve_type_ != "Speed")
            {
                RCLCPP_WARN_STREAM(LOGGER, solve_type_ << " is not a valid solve_type; setting to default: Speed");
            }
            solvetype = TRAC_IK::Speed;
        }

        TRAC_IK::TRAC_IK ik_solver(chain_, joint_min_, joint_max_, timeout, epsilon_, solvetype);

        int rc = ik_solver.CartToJnt(in, frame, out, bounds);


        solution.resize(num_joints_);

        if (rc >= 0)
        {
            for (uint z = 0; z < num_joints_; z++)
                solution[z] = out(z);

            // check for collisions if a callback is provided
            if (!solution_callback.empty())
            {
                solution_callback(ik_pose, solution, error_code);
                if (error_code.val == moveit_msgs::msg::MoveItErrorCodes::SUCCESS)
                {
                    RCLCPP_DEBUG_STREAM(LOGGER, "Solution passes callback");
                    return true;
                }
                else
                {
                    RCLCPP_DEBUG_STREAM(LOGGER, "Solution has error code " << error_code.val);
                    return false;
                }
            }
            else
                return true; // no collision check callback provided
        }

        error_code.val = moveit_msgs::msg::MoveItErrorCodes::NO_IK_SOLUTION;
        return false;
    }
} // end namespace

//register TRAC_IKKinematicsPlugin as a KinematicsBase implementation
#include <pluginlib/class_list_macros.hpp>
PLUGINLIB_EXPORT_CLASS(trac_ik_kinematics_plugin::TRAC_IKKinematicsPlugin, kinematics::KinematicsBase);
