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

#include <chrono>
#include <limits>
#include <vector>
#include <array>
#include <unordered_set>
#include <boost/core/noncopyable.hpp>

#include <ros/ros.h>
#include <tf2_ros/transform_broadcaster.h>
#include <geometry_msgs/TransformStamped.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>
#include <actionlib/client/simple_action_client.h>

#include <carla/client/BlueprintLibrary.h>
#include <carla/client/Vehicle.h>
#include <carla/client/Client.h>
#include <carla/client/Map.h>
#include <carla/client/Waypoint.h>
//#include <carla/client/Sensor.h>
//#include <carla/client/TimeoutException.h>
#include <carla/client/World.h>
#include <carla/geom/Transform.h>
//#include <carla/image/ImageIO.h>
//#include <carla/image/ImageView.h>
//#include <carla/sensor/data/Image.h>

#include <conformal_lattice_planner/EgoPlanAction.h>
#include <conformal_lattice_planner/AgentPlanAction.h>

using namespace std;
namespace bst = boost;
namespace cc = carla::client;
namespace cg = carla::geom;
namespace crpc = carla::rpc;
//namespace csd = carla::sensor::data;
namespace clp = conformal_lattice_planner;

namespace carla {

/// Prototypes for creating visualization msgs.
visualization_msgs::MarkerPtr createWaypointMsg(
    const vector<carla::SharedPtr<cc::Waypoint>>&);
visualization_msgs::MarkerPtr createJunctionMsg(
    const cc::Map::TopologyList&);
visualization_msgs::MarkerPtr createVehicleMarkerMsg(
    const carla::SharedPtr<cc::Actor>&);
geometry_msgs::TransformStampedPtr createVehicleTransformMsg(
    const carla::SharedPtr<cc::Actor>&, const std::string&);

class CarlaSimulatorNode : private bst::noncopyable {

public:

  using Ptr = bst::shared_ptr<CarlaSimulatorNode>;
  using ConstPtr = bst::shared_ptr<const CarlaSimulatorNode>;

private:

  SharedPtr<cc::World> world_ = nullptr;
  SharedPtr<cc::Client> client_ = nullptr;
  size_t ego_;
  std::unordered_set<size_t> agents_;

  bool ego_ready_ = true;
  bool agents_ready_ = true;
  bool timer_ready_ = true;

  /// ROS interface.
  mutable ros::NodeHandle nh_;
  mutable tf2_ros::TransformBroadcaster tf_broadcaster_;
  mutable ros::Publisher map_pub_;
  mutable ros::Publisher ego_marker_pub_;
  //ros::Publisher agent_markers_pub_;
  mutable ros::Timer sim_timer_;

  mutable actionlib::SimpleActionClient<clp::EgoPlanAction> ego_client_;
  //actionlib::SimpleActionClient<clp::AgentPlanAction> agents_client;

public:

  CarlaSimulatorNode(ros::NodeHandle nh) :
    nh_(nh), ego_client_(nh_, "ego_plan", false) {}

  /// Initialize the simulator ros node.
  bool initialize();

private:

  /// Spawn the ego vehicle.
  void spawnEgo();

  /// Manage agents around the ego vehicle.
  /// Add agents if there is empty space around the ego.
  /// Delete agents that are too far from the ego.
  void manageAgents();

  /// Publish the map visualization markers.
  void publishMap() const;

  /// Publish the vehicle visualization markers.
  void publishTraffic() const;

  /// Timer callback.
  void timerCallback(const ros::TimerEvent& event);

  /**
   * @name Ego action callbacks
   */
  /// @{
  /// Send the goal for the ego.
  void sendEgoGoal();

  /// Done callback for ego_client.
  void egoPlanDoneCallback(
      const actionlib::SimpleClientGoalState& state,
      const clp::EgoPlanResultConstPtr& result);

  /// Action callback for ego_client.
  void egoPlanActiveCallback() {}

  /// Feedback callback for ego_client.
  void egoPlanFeedbackCallback(
      const clp::EgoPlanFeedbackConstPtr& feedback) {}
  /// @}

  /**
   * @name Agent plan callbacks
   */
  /// @{
  /// Send the goal for the agents.
  void sendAgentsGoal();

  /// Done callback for agents_client.
  void agentsPlanDoneCallback(
      const actionlib::SimpleClientGoalState& state,
      const clp::AgentPlanResultConstPtr& result);

  /// Action callback for agents_client.
  void agentsPlanActiveCallback() {}

  /// Feedback callback for agents_client.
  void agentsPlanFeedbackCallback(
      const clp::AgentPlanFeedbackConstPtr& feedback) {}
  /// @}

}; // End class CarlaSimulatorNode.

bool CarlaSimulatorNode::initialize() {
  // Create publishers.
  map_pub_ = nh_.advertise<visualization_msgs::MarkerArray>("town_map", 1, true);
  ego_marker_pub_ = nh_.advertise<visualization_msgs::Marker>("ego_object", 1, true);

  // TODO: load the variables from parameters.
  double time_step = 0.05;
  //sim_timer_ = nh_.createTimer(ros::Duration(time_step), &CarlaSimulatorNode::timerCallback, this);

  // TODO: load the variables from parameters.
  string host = "localhost";
  uint16_t port = 2000;

  // Get the world.
  ROS_INFO_NAMED("carla_simulator", "connect to the server.");
  client_ = bst::make_shared<cc::Client>(host, port);
  client_->SetTimeout(std::chrono::seconds(10));
  world_ = bst::make_shared<cc::World>(client_->GetWorld());

  // Set to synchronous mode.
  ROS_INFO_NAMED("carla_simulator", "set to synchronous mode.");
  crpc::EpisodeSettings settings = world_->GetSettings();
  if (settings.fixed_delta_seconds) {
    ROS_INFO_NAMED("carla_simulator",
        "old settings: fixed_delta_seconds:N/A no_rendering_mode:%d synchronous_mode:%d",
        settings.no_rendering_mode, settings.synchronous_mode);
  } else {
    ROS_INFO_NAMED("carla_simulator",
        "old settings: fixed_delta_seconds:%f no_rendering_mode:%d synchronous_mode:%d",
        *(settings.fixed_delta_seconds), settings.no_rendering_mode, settings.synchronous_mode);
  }
  settings.fixed_delta_seconds = time_step;
  settings.no_rendering_mode = true;
  settings.synchronous_mode = true;
  world_->ApplySettings(settings);
  ROS_INFO_NAMED("carla_simulator",
      "new settings: fixed_delta_seconds:%f no_rendering_mode:%d synchronous_mode:%d",
      *(settings.fixed_delta_seconds), settings.no_rendering_mode, settings.synchronous_mode);

  // Publish the map.
  ROS_INFO_NAMED("carla_simulator", "publish global map.");
  publishMap();

  // Initialize the ego vehicle.
  ROS_INFO_NAMED("carla_simulator", "spawn the ego vehicle.");
  spawnEgo();

  world_->Tick();
  // Publish the ego vehicle marker.
  ROS_INFO_NAMED("carla_simulator", "publish ego and agents.");
  publishTraffic();

  // Wait for the planner servers.
  ROS_INFO_NAMED("carla_simulator", "waiting for action servers.");
  ego_client_.waitForServer(ros::Duration(5.0));

  // Send out the first goal of ego.
  ROS_INFO_NAMED("carla_simulator", "send the first goals to action servers");
  sendEgoGoal();

  ROS_INFO_NAMED("carla_simulator", "initialization finishes.");
  return true;
}

void CarlaSimulatorNode::spawnEgo() {

  SharedPtr<cc::BlueprintLibrary> blueprint_library =
    world_->GetBlueprintLibrary();
  //SharedPtr<cc::BlueprintLibrary> vehicle_library =
  //  blueprint_library->Filter("vehicle");
  //for (const auto& actor_blueprint : *vehicle_library) {
  //  cout << actor_blueprint.GetId() << endl;
  //}

  // TODO: Load a specific vehicle blueprint.
  const std::string vehicle_name = "vehicle.audi.tt";
  auto ego_blueprint = blueprint_library->at(vehicle_name);
  //auto ego_blueprint = (*vehicle_library)[0];

  // TODO: Load the deired ego initial state as parameters.
  array<float, 3> ego_pt{0, 0, 0};
  vector<cg::Transform> spawn_points = world_->GetMap()->GetRecommendedSpawnPoints();
  cg::Transform ego_transform;
  float min_distance_sq = numeric_limits<float>::max();
  // Find the available spawn point cloest to the given ego initial state.
  for (const auto pt : spawn_points) {
    const float x_diff = pt.location.x - ego_pt[0];
    const float y_diff = pt.location.y - ego_pt[1];
    const float z_diff = pt.location.z - ego_pt[2];
    const float distance_sq = x_diff*x_diff + y_diff*y_diff + z_diff*z_diff;
    if (distance_sq < min_distance_sq) {
      ego_transform = pt;
      min_distance_sq = distance_sq;
    }
  }
  ROS_INFO_NAMED("carla_simulator", "Initial Ego x:%f y:%f z:%f r:%f p:%f y:%f",
      ego_transform.location.x, ego_transform.location.y, ego_transform.location.z,
      ego_transform.rotation.roll, ego_transform.rotation.pitch, ego_transform.rotation.yaw);
  SharedPtr<cc::Actor> ego_actor = world_->SpawnActor(ego_blueprint, ego_transform);
  ego_actor->SetSimulatePhysics(false);
  ego_ = ego_actor->GetId();

  return;
}

void CarlaSimulatorNode::publishMap() const {

  visualization_msgs::MarkerPtr waypoints_msg =
    createWaypointMsg(world_->GetMap()->GenerateWaypoints(5.0));
  visualization_msgs::MarkerPtr junctions_msg =
    createJunctionMsg(world_->GetMap()->GetTopology());

  visualization_msgs::MarkerArrayPtr map_msg(
      new visualization_msgs::MarkerArray);
  map_msg->markers.push_back(*waypoints_msg);
  map_msg->markers.push_back(*junctions_msg);

  map_pub_.publish(map_msg);
  return;
}

void CarlaSimulatorNode::publishTraffic() const {

  // Publish the ego marker and tf.
  ego_marker_pub_.publish(createVehicleMarkerMsg(world_->GetActor(ego_)));
  tf_broadcaster_.sendTransform(*(createVehicleTransformMsg(world_->GetActor(ego_), "ego")));

  // TODO: Publish the agents' markers.
  return;
}

void CarlaSimulatorNode::sendEgoGoal() {

  clp::EgoPlanGoal goal;
  goal.ego = ego_;
  for (const size_t agent : agents_)
    goal.agents.push_back(agent);

  ego_client_.sendGoal(
      goal,
      bst::bind(&CarlaSimulatorNode::egoPlanDoneCallback, this, _1, _2),
      bst::bind(&CarlaSimulatorNode::egoPlanActiveCallback, this),
      bst::bind(&CarlaSimulatorNode::egoPlanFeedbackCallback, this, _1));

  ego_ready_ = false;
  timer_ready_ = false;

  return;
}

void CarlaSimulatorNode::egoPlanDoneCallback(
    const actionlib::SimpleClientGoalState& state,
    const clp::EgoPlanResultConstPtr& result) {

  ROS_INFO_NAMED("carla_simulator", "egoPlanDoneCallback().");
  ROS_INFO_NAMED("carla_simulator", "tick world.");
  world_->Tick();
  publishTraffic();
  sendEgoGoal();

  ego_ready_ = true;
  return;
}

void CarlaSimulatorNode::timerCallback(const ros::TimerEvent& event) {

  //ROS_INFO_NAMED("carla_simulator", "timerCallback().");
  //return;
}

using CarlaSimulatorNodePtr = CarlaSimulatorNode::Ptr;
using CarlaSimulatorNodeConstPtr = CarlaSimulatorNode::ConstPtr;
} // End namespace carla.


int main(int argc, char** argv) {
  ros::init(argc, argv, "carla_simulator_node");
  ros::NodeHandle nh;

  carla::CarlaSimulatorNodePtr carla_sim =
    bst::make_shared<carla::CarlaSimulatorNode>(nh);
  if (!carla_sim->initialize()) {
    ROS_ERROR("Cannot initialize the CARLA simulator.");
  }

  ros::spin();
  return 0;
}

