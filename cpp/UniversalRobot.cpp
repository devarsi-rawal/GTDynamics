/**
 * @file UniversalRobot.h
 * @brief Robot structure.
 * @Author: Frank Dellaert, Mandy Xie, and Alejandro Escontrela
 */

#include <UniversalRobot.h>

// using namespace robot;
using namespace std;
using namespace gtsam;

namespace robot {

LinkBodyJointPair extract_structure_from_urdf(
    const urdf::ModelInterfaceSharedPtr urdf_ptr,
    const boost::optional<std::vector<robot::LinkJointParams>> joint_params) {
  
  std::map<std::string, robot::LinkBodySharedPtr> name_to_link_body;
  std::map<std::string, robot::LinkJointSharedPtr> name_to_link_joint;

  // Loop through all links in the urdf interface and construct LinkBody objects
  // without parents or children.
  for (auto&& link : urdf_ptr->links_)
    name_to_link_body.insert(std::make_pair(
      link.first,
      std::make_shared<robot::LinkBody>(robot::LinkBody(std::get<1>(link)))
    ));

  robot::LinkJointParams default_params;

  // Create LinkJoint objects and update list of parent and child links/joints.
  for (auto&& joint : urdf_ptr->joints_) {

    robot::LinkBodySharedPtr parent_link_strong = name_to_link_body[(joint.second)->parent_link_name];
    robot::LinkBodySharedPtr child_link_strong = name_to_link_body[(joint.second)->child_link_name];
    robot::LinkBodyWeakPtr child_link_weak = name_to_link_body[(joint.second)->child_link_name]->getWeakPtr();

    // Obtain joint params.
    robot::LinkJointParams jps;
    if (joint_params) {
      auto jparams = std::find_if(
        joint_params.get().begin(), joint_params.get().end(),
        [=] (const robot::LinkJointParams & jps) {
          return (jps.name == joint.first);
      });
      jps = jparams == joint_params.get().end() ? default_params : *jparams;
    } else {
      jps = default_params;
    }

    // Construct LinkJoint and insert into name_to_link_joint.
    robot::LinkJointSharedPtr link_joint_strong = std::make_shared<robot::LinkJoint>(
      robot::LinkJoint(
        joint.second, jps.jointEffortType, jps.springCoefficient,
        jps.jointLimitThreshold, jps.velocityLimitThreshold, jps.accelerationLimit,
        jps.accelerationLimitThreshold, jps.torqueLimitThreshold, parent_link_strong,
        child_link_weak));

    name_to_link_joint.insert(std::make_pair(joint.first, link_joint_strong));
    robot::LinkJointWeakPtr link_joint_weak = link_joint_strong->getWeakPtr();

    // Update list of parent and child links/joints for each LinkBody.
    parent_link_strong->addChildLink(child_link_weak);
    parent_link_strong->addChildJoint(link_joint_weak);
    child_link_strong->addParentLink(parent_link_strong);
    child_link_strong->addParentJoint(link_joint_strong);
  }

  std::vector<robot::LinkBodySharedPtr> link_bodies;
  for (auto name_link_pair : name_to_link_body)
    link_bodies.push_back(name_link_pair.second);

  std::vector<robot::LinkJointSharedPtr> link_joints;
  for (auto name_joint_pair : name_to_link_joint)
    link_joints.push_back(name_joint_pair.second);

  return std::make_pair(link_bodies, link_joints);
}

UniversalRobot::UniversalRobot(const LinkBodyJointPair urdf_links_and_joints,
                               const std::string base_name,
                               const gtsam::Pose3 &base)
                               : link_bodies_(urdf_links_and_joints.first),
                                 link_joints_(urdf_links_and_joints.second),
                                 base_name_(base_name),
                                 base_(base) {
  for (auto&& link_body : link_bodies_)
    name_to_link_body_.insert(std::make_pair(
      link_body->name(), link_body));
  
  for (auto&& link_joint : link_joints_)
    name_to_link_joint_.insert(std::make_pair(
      link_joint->name(), link_joint));
}

const gtsam::Pose3& UniversalRobot::base() const { return base_; }

LinkBodySharedPtr UniversalRobot::getLinkByName(std::string name) {
  return name_to_link_body_[name];
}

LinkJointSharedPtr UniversalRobot::getJointByName(std::string name) {
  return name_to_link_joint_[name];
}

int UniversalRobot::numLinks() const { return link_bodies_.size(); }

int UniversalRobot::numJoints() const { return link_joints_.size(); }

std::map<std::string, gtsam::Vector6> UniversalRobot::screwAxes() const {
  std::map<std::string, gtsam::Vector6> screw_axes;

  for (auto&& link_joint : link_joints_)
    screw_axes.insert(std::make_pair(
      link_joint->name(), link_joint->screwAxis()));
  
  return screw_axes;
}

std::map<std::string, double> UniversalRobot::jointLowerLimits() const {
  std::map<std::string, double> joint_lower_limits;

  for (auto&& link_joint : link_joints_)
    joint_lower_limits.insert(std::make_pair(
      link_joint->name(), link_joint->jointLowerLimit()));

  return joint_lower_limits;
}

std::map<std::string, double> UniversalRobot::jointUpperLimits() const {
  std::map<std::string, double> joint_upper_limits;

  for (auto&& link_joint : link_joints_)
    joint_upper_limits.insert(std::make_pair(
      link_joint->name(), link_joint->jointUpperLimit()));

  return joint_upper_limits;
}

std::map<std::string, double> UniversalRobot::jointLimitThresholds() const {
  std::map<std::string, double> joint_limit_threshold;

  for (auto&& link_joint : link_joints_)
    joint_limit_threshold.insert(std::make_pair(
      link_joint->name(), link_joint->jointLimitThreshold()));

  return joint_limit_threshold;
}

std::map<std::string, std::map<std::string, gtsam::Pose3>> UniversalRobot::linkTransforms(
        boost::optional<std::map<std::string, double>> joint_name_to_angle
    ) const {

  std::map<std::string, std::map<std::string, gtsam::Pose3>> link_transforms;
  std::map<std::string, double> q_map = joint_name_to_angle.get();
  for (auto&& link_body : link_bodies_) {
    // No parents.
    if (!link_body->getParentJoints().size()) continue;

    // Transform from parent link(s) to the link_body.
    std::map<std::string, gtsam::Pose3> parent_to_link_transforms;

    // Cycle through parent joints and compute transforms.
    for (auto&& link_parent_joint : link_body->getParentJoints()) {
      double q = 0.0;
      if (joint_name_to_angle) {
        if (q_map.find(link_parent_joint->name()) != q_map.end())
          q = q_map[link_parent_joint->name()];
      }

      parent_to_link_transforms.insert(std::make_pair(
        link_parent_joint->parentLink()->name(),
        link_parent_joint->pTc(q)
      ));
    }

    link_transforms.insert(std::make_pair(
      link_body->name(), parent_to_link_transforms));
  }
  return link_transforms;
}

gtsam::Pose3 UniversalRobot::cTpCOM(std::string name, double q) {
  LinkJointSharedPtr link_joint = name_to_link_joint_[name];
  gtsam::Pose3 pTcom = link_joint->parentLink()->centerOfMass();
  gtsam::Pose3 cTcom = link_joint->childLink().lock()->centerOfMass();

  // Return relative pose between pTc_com and pTcom,
  // in pTc_com coordinate frame.
  gtsam::Pose3 pTc_com = link_joint->pTc(q) * cTcom;
  return pTc_com.between(pTcom);
}

// gtsam::Pose3 UniversalRobot::cTpCOM(std::string parent_name, std::string child_name, double q) {
//   std::string j;
//   for (auto&& link_joint : link_joints_) {
//     if (link_joint->parentLink()->name() == parent_name) {
//       if (link_joint->childLink().lock()->name() == child_name) {
//         j = link_joint->name();
//         break;
//       }
//     }
//   }

//   if (!j.empty())
//     return cTpCOM(j, q);
//   else
//     throw std::runtime_error(
//       "Joint from parent " << parent_name " to child " << child_name
//       << " not found");

// }

std::map<std::string, std::map<std::string, gtsam::Pose3>> UniversalRobot::cTpCOMs(
        boost::optional<std::map<std::string, double>> joint_name_to_angle
) {

  std::map<std::string, std::map<std::string, gtsam::Pose3>> cTp_COMs;
  std::map<std::string, double> q_map = joint_name_to_angle.get();

  for (auto&& link_joint : link_joints_)
  {
    // Insert map to contain transform from parent link to child link if not already
    // present.
    if (cTp_COMs.find(link_joint->childLink().lock()->name()) == cTp_COMs.end()) {
      std::map<std::string, gtsam::Pose3> parent_to_link_com_transforms;
      cTp_COMs.insert(std::make_pair(
        link_joint->childLink().lock()->name(),
        parent_to_link_com_transforms
      ));
    }

    double q = 0.0;
    if (joint_name_to_angle) {
      if (q_map.find(link_joint->name()) != q_map.end())
        q = q_map[link_joint->name()];
    }

    cTp_COMs[link_joint->childLink().lock()->name()].insert(std::make_pair(
      link_joint->parentLink()->name(),
      cTpCOM(link_joint->name(), q)
    ));
  }
  return cTp_COMs;
}

std::map<std::string, gtsam::Pose3> UniversalRobot::COMFrames(
        boost::optional<std::map<std::string, double>> joint_name_to_angle
) {
  // TODO(aescontrela): Extend this method to handle the case where the
  // specified base link has parent links.

  std::map<std::string, gtsam::Pose3> com_frames;
  std::map<std::string, double> q_map = joint_name_to_angle.get();

  // Initial transform.
  com_frames.insert(std::make_pair(base_name_, base_));

  std::vector<LinkJointWeakPtr> joints_to_visit = getLinkByName(
    base_name_)->getChildJoints();
  std::vector<std::string> joints_visited;
  
  while (joints_to_visit.size()) {

    LinkJointSharedPtr curr_joint = joints_to_visit.back().lock();
    joints_to_visit.pop_back();

    if (std::find(joints_visited.begin(), joints_visited.end(),
        curr_joint->name()) != joints_visited.end())
      // This joint has already been accounted for.
      continue;
    else
      joints_visited.push_back(curr_joint->name());
    
    LinkBodySharedPtr p_link = curr_joint->parentLink();
    LinkBodySharedPtr c_link = curr_joint->childLink().lock();

    double q = 0.0;
    if (joint_name_to_angle) {
      if (q_map.find(curr_joint->name()) != q_map.end())
        q = q_map[curr_joint->name()];
    }

    // Add the wTcom transform.
    com_frames.insert(std::make_pair(
      c_link->name(),
      com_frames[p_link->name()] * cTpCOM(curr_joint->name(), q)
    ));

    joints_to_visit.insert(joints_to_visit.begin(),
      c_link->getChildJoints().begin(), c_link->getChildJoints().end());
  }

  return com_frames;
}

std::map<std::string, std::map<std::string, gtsam::Pose3>> UniversalRobot::jTiTransforms(
        boost::optional<std::map<std::string, double>> joint_name_to_angle) {
  
  std::map<std::string, std::map<std::string, gtsam::Pose3>> jTi_transforms;
  std::map<std::string, double> q_map = joint_name_to_angle.get();

  std::map<std::string, gtsam::Pose3> COM_frames = COMFrames(joint_name_to_angle);

  for (auto&& COM_frame : COM_frames) {
    std::string link_name = COM_frame.first;
    gtsam::Pose3 COM_transform = COM_frame.second;

    // Insert map to contain transform from parent link to child link if not already
    // present.
    if (jTi_transforms.find(link_name) == jTi_transforms.end()) {
      std::map<std::string, gtsam::Pose3> link_to_parent_transforms;
      jTi_transforms.insert(std::make_pair(
        link_name,
        link_to_parent_transforms
      ));
    }

    // Calculate transforms from COM frame i relative to COM frame j, for all
    // parents i of j.
    for (auto&& link_parent : getLinkByName(link_name)->getParentLinks()) {
      jTi_transforms[link_name].insert(std::make_pair(
        link_parent->name(),
        COM_transform.between(COM_frames[link_parent->name()])
      ));
    }
  }
  return jTi_transforms;
}

std::map<std::string, gtsam::Vector6> UniversalRobot::spatialScrewAxes() {

  std::map<std::string, gtsam::Vector6> joint_to_spatial_screw_axis;

  std::map<std::string, gtsam::Vector6> screw_axes = screwAxes();
  std::map<std::string, gtsam::Pose3> COM_frames = COMFrames();

  for (auto&& link_joint : link_joints_)
    joint_to_spatial_screw_axis.insert(std::make_pair(
      link_joint->name(),
      COM_frames[link_joint->childLink().lock()->name()].AdjointMap() *
        screw_axes[link_joint->name()]
    ));

  return joint_to_spatial_screw_axis;
}

} // namespace robot.
