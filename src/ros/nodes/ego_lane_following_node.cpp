/*
 * Copyright [2019] [Ke Sun]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vector>
#include <memory>
#include <boost/core/noncopyable.hpp>
#include <boost/timer/timer.hpp>

#include <ros/ros.h>
#include <actionlib/server/simple_action_server.h>

#include <conformal_lattice_planner/lane_follower.h>
#include <conformal_lattice_planner/EgoPlanAction.h>

using namespace std;
namespace bst = boost;
namespace cc = carla::client;
namespace cg = carla::geom;
//namespace crpc = carla::rpc;
//namespace csd = carla::sensor::data;
namespace clp = conformal_lattice_planner;
using namespace planner;

namespace carla {

class EgoLaneFollowingNode : private bst::noncopyable {

public:

  using Ptr = bst::shared_ptr<EgoLaneFollowingNode>;
  using ConstPtr = bst::shared_ptr<const EgoLaneFollowingNode>;

private:

  SharedPtr<cc::Client> client_ = nullptr;
  SharedPtr<LaneFollower> planner_ = nullptr;

  mutable ros::NodeHandle nh_;
  mutable actionlib::SimpleActionServer<clp::EgoPlanAction> server_;

public:

  EgoLaneFollowingNode(ros::NodeHandle& nh) :
    nh_(nh),
    server_(nh, "ego_plan", bst::bind(&EgoLaneFollowingNode::executeCallback, this, _1), false) {}

  bool initialize();

private:

  void executeCallback(const clp::EgoPlanGoalConstPtr& goal);

};

bool EgoLaneFollowingNode::initialize() {

  bool all_param_exist = true;

  string host = "localhost";
  int port = 2000;
  all_param_exist &= nh_.param<std::string>("host", host, "localhost");
  all_param_exist &= nh_.param<int>("port", port, 2000);

  // Get the world.
  ROS_INFO_NAMED("ego_lane_following_planner", "connect to the server.");
  client_ = bst::make_shared<cc::Client>(host, port);
  client_->SetTimeout(std::chrono::seconds(10));

  // Initialize the planner.
  ROS_INFO_NAMED("ego_lane_following_planner", "initialize lane following planner.");
  double fixed_delta_seconds = 0.05;
  all_param_exist &= nh_.param<double>("fixed_delta_seconds", fixed_delta_seconds, 0.05);
  planner_ = bst::make_shared<LaneFollower>(fixed_delta_seconds);

  //std::array<double, 3> longitudinal_gains;
  //std::array<double, 3> lateral_gains;
  //all_param_exist &= nh_.param<double>("longitudinal/kp", longitudinal_gains[0], 5.0);
  //all_param_exist &= nh_.param<double>("longitudinal/ki", longitudinal_gains[1], 0.0);
  //all_param_exist &= nh_.param<double>("longitudinal/kd", longitudinal_gains[2], 0.0);
  //all_param_exist &= nh_.param<double>("lateral/kp", lateral_gains[0], 5.0);
  //all_param_exist &= nh_.param<double>("lateral/ki", lateral_gains[1], 0.0);
  //all_param_exist &= nh_.param<double>("lateral/kd", lateral_gains[2], 0.0);
  //planner_->setControllerGains(longitudinal_gains, lateral_gains);

  // Start the action server.
  ROS_INFO_NAMED("ego_lane_following_planner", "start action server.");
  server_.start();

  ROS_INFO_NAMED("ego_lane_following_planner", "initialization finishes.");
  return all_param_exist;
}

void EgoLaneFollowingNode::executeCallback(
    const clp::EgoPlanGoalConstPtr& goal) {

  ROS_INFO_NAMED("ego_lane_following_planner", "executeCallback()");

  // Update the world for planner.
  SharedPtr<cc::World> world = bst::make_shared<cc::World>(client_->GetWorld());
  planner_->updateWorld(world);

  // Plan for the ego vehicle.
  const size_t ego = goal->ego;
  const vector<size_t> agents(goal->agents.begin(), goal->agents.end());
  planner_->plan(ego, agents);

  // Inform the client the result of plan.
  clp::EgoPlanResult result;
  result.success = true;
  server_.setSucceeded(result);

  return;
}

using EgoLaneFollowingNodePtr = EgoLaneFollowingNode::Ptr;
using EgoLaneFollowingNodeConstPtr = EgoLaneFollowingNode::ConstPtr;

} // End namespace carla.

int main(int argc, char** argv) {
  ros::init(argc, argv, "~");
  ros::NodeHandle nh("~");

  if(ros::console::set_logger_level(
        ROSCONSOLE_DEFAULT_NAME,
        ros::console::levels::Info)) {
    ros::console::notifyLoggerLevelsChanged();
  }

  carla::EgoLaneFollowingNodePtr planner =
    bst::make_shared<carla::EgoLaneFollowingNode>(nh);
  if (!planner->initialize()) {
    ROS_ERROR("Cannot initialize the ego lane following planner.");
  }

  ros::spin();
  return 0;
}
