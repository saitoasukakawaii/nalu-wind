// Copyright 2017 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS), National Renewable Energy Laboratory, University of Texas Austin,
// Northwest Research Associates. Under the terms of Contract DE-NA0003525
// with NTESS, the U.S. Government retains certain rights in this software.
//
// This software is released under the BSD 3-clause license. See LICENSE file
// for more details.
//

#include <AlgorithmDriver.h>
#include <AuxFunctionAlgorithm.h>
#include <EquationSystems.h>
#include <EquationSystem.h>
#include <NaluEnv.h>
#include <NaluParsing.h>
#include <Realm.h>
#include <PostProcessingData.h>
#include <Simulation.h>
#include <SolutionOptions.h>
#include <AlgorithmDriver.h>

// all concrete EquationSystem's
#include <EnthalpyEquationSystem.h>
#include <LowMachEquationSystem.h>

#ifdef NALU_HAS_MATRIXFREE
#include <MatrixFreeHeatCondEquationSystem.h>
#include <MatrixFreeLowMachEquationSystem.h>
#endif
#include <ShearStressTransportEquationSystem.h>
#include <ChienKEpsilonEquationSystem.h>
#include <WilcoxKOmegaEquationSystem.h>
#include <TurbKineticEnergyEquationSystem.h>
#include <WallDistEquationSystem.h>
#include <VolumeOfFluidEquationSystem.h>

#include <overset/UpdateOversetFringeAlgorithmDriver.h>

#include <vector>

#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/MetaData.hpp>

// stk_topo
#include <stk_topology/topology.hpp>

namespace sierra {
namespace nalu {

//==========================================================================
// Class Definition
//==========================================================================
// EquationSystems - base class equation system
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
EquationSystems::EquationSystems(Realm& realm)
  : realm_(realm),
    oversetUpdater_(new UpdateOversetFringeAlgorithmDriver(realm))
{
}

//--------------------------------------------------------------------------
//-------- destructor ------------------------------------------------------
//--------------------------------------------------------------------------
EquationSystems::~EquationSystems()
{
  for (size_t ie = 0; ie < equationSystemVector_.size(); ++ie)
    delete equationSystemVector_[ie];

  for (auto it = preIterAlgDriver_.begin(); it != preIterAlgDriver_.end();
       ++it) {
    delete (*it);
  }

  for (auto it = postIterAlgDriver_.begin(); it != postIterAlgDriver_.end();
       ++it) {
    delete (*it);
  }
}

//--------------------------------------------------------------------------
//-------- load -----------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::load(const YAML::Node& y_node)
{
  const YAML::Node y_equation_system = expect_map(y_node, "equation_systems");
  {
    get_required(y_equation_system, "name", name_);
    get_required(y_equation_system, "max_iterations", maxIterations_);

    // Get global settings for decoupled overset, individual equation systems
    // will override this when they process their own yaml nodes
    if (realm_.query_for_overset()) {
      get_if_present(
        y_equation_system, "decoupled_overset_solve",
        decoupledOversetGlobalFlag_, decoupledOversetGlobalFlag_);
      get_if_present(
        y_equation_system, "num_overset_correctors", numOversetItersDefault_,
        numOversetItersDefault_);
    }

    const YAML::Node y_solver =
      expect_map(y_equation_system, "solver_system_specification");
    solverSpecMap_ = y_solver.as<std::map<std::string, std::string>>();

    const YAML::Node y_systems = expect_sequence(y_equation_system, "systems");
    {
      for (size_t isystem = 0; isystem < y_systems.size(); ++isystem) {
        const YAML::Node y_system = y_systems[isystem];
        EquationSystem* eqSys = 0;
        YAML::Node y_eqsys;

        if (expect_map(y_system, "LowMachEOM", true)) {
          y_eqsys = expect_map(y_system, "LowMachEOM", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0()
              << "eqSys = LowMachEOM " << std::endl;
          bool elemCont = (realm_.realmUsesEdges_) ? false : true;
          get_if_present_no_default(
            y_eqsys, "element_continuity_eqs", elemCont);

          if (realm_.matrix_free()) {
#ifdef NALU_HAS_MATRIXFREE
            eqSys = new MatrixFreeLowMachEquationSystem(*this);
#endif
          } else {
            eqSys = new LowMachEquationSystem(*this, elemCont);
          }
        } else if (expect_map(y_system, "VolumeOfFluid", true)) {

          y_eqsys = expect_map(y_system, "VolumeOfFluid", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0()
              << "eqSys = VolumeOfFluid" << std::endl;
          eqSys = new VolumeOfFluidEquationSystem(*this);

        } else if (expect_map(y_system, "ShearStressTransport", true)) {
          y_eqsys = expect_map(y_system, "ShearStressTransport", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0() << "eqSys = tke/sdr " << std::endl;
          eqSys = new ShearStressTransportEquationSystem(*this);
        } else if (expect_map(y_system, "ChienKEpsilon", true)) {
          y_eqsys = expect_map(y_system, "ChienKEpsilon", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0() << "eqSys = tke/tdr " << std::endl;
          eqSys = new ChienKEpsilonEquationSystem(*this);
        } else if (expect_map(y_system, "WilcoxKOmega", true)) {
          y_eqsys = expect_map(y_system, "WilcoxKOmega", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0() << "eqSys = tke/sdr " << std::endl;
          eqSys = new WilcoxKOmegaEquationSystem(*this);
        } else if (expect_map(y_system, "TurbKineticEnergy", true)) {
          y_eqsys = expect_map(y_system, "TurbKineticEnergy", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0() << "eqSys = tke " << std::endl;
          eqSys = new TurbKineticEnergyEquationSystem(*this);
        } else if (expect_map(y_system, "Enthalpy", true)) {
          y_eqsys = expect_map(y_system, "Enthalpy", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0() << "eqSys = enthalpy " << std::endl;
          double minT = 250.0;
          double maxT = 3000.0;
          get_if_present_no_default(y_eqsys, "minimum_temperature", minT);
          get_if_present_no_default(y_eqsys, "maximum_temperature", maxT);
          bool ouputClipDiag = true;
          get_if_present_no_default(
            y_eqsys, "output_clipping_diagnostic", ouputClipDiag);
          eqSys = new EnthalpyEquationSystem(*this, minT, maxT, ouputClipDiag);
        } else if (expect_map(y_system, "HeatConduction", true)) {
          y_eqsys = expect_map(y_system, "HeatConduction", true);
          if (NaluEnv::self().debug())
            NaluEnv::self().naluOutputP0()
              << "eqSys = HeatConduction " << std::endl;
#ifdef NALU_HAS_MATRIXFREE
          if (realm_.matrix_free()) {
            eqSys = new MatrixFreeHeatCondEquationSystem(*this);
          } else {
            throw std::runtime_error(
              "HeatConduction only supported for matrix-free");
          }
#endif
        } else if (expect_map(y_system, "WallDistance", true)) {
          y_eqsys = expect_map(y_system, "WallDistance", true);
          eqSys = new WallDistEquationSystem(*this);
        } else {
          if (!NaluEnv::self().parallel_rank()) {
            std::cout << "Error: parsing at "
                      << NaluParsingHelper::info(y_system)
                      << "... at parent ... " << NaluParsingHelper::info(y_node)
                      << std::endl;
          }
          throw std::runtime_error(
            "parser error EquationSystem::load: unknown equation system type");
        }

        // Pass the global settings for the overset decoupled solves to equation
        // systems and let user override for individual equation systems in the
        // input file
        eqSys->decoupledOverset_ = decoupledOversetGlobalFlag_;
        eqSys->numOversetIters_ = numOversetItersDefault_;

        // load; particular equation system push back to vector is controlled by
        // the constructor
        eqSys->load(y_eqsys);
      }
    }
  }
}

//--------------------------------------------------------------------------
//-------- get_solver_block_name -------------------------------------------
//--------------------------------------------------------------------------
std::string
EquationSystems::get_solver_block_name(const std::string eqName) const
{
  std::string solverName = "n_a";
  std::map<std::string, std::string>::const_iterator iter =
    solverSpecMap_.find(eqName);
  if (iter != solverSpecMap_.end()) {
    solverName = (*iter).second;
  } else {
    NaluEnv::self().naluOutputP0()
      << "Missed equation solver block specification for " << eqName
      << std::endl;
    throw std::runtime_error("issue with solver name mapping; none supplied");
  }
  return solverName;
}

void
EquationSystems::breadboard()
{
  // nothing as of yet
}

Simulation*
EquationSystems::root()
{
  return parent()->root();
}
Realm*
EquationSystems::parent()
{
  return &realm_;
}

namespace {
stk::mesh::PartVector
create_part_vec(
  stk::mesh::MetaData& meta_data,
  const std::vector<std::string>& targetNames,
  const bool element_rank = false)
{
  stk::mesh::PartVector part_vec;
  part_vec.reserve(targetNames.size());
  for (const auto& part_name : targetNames) {
    part_vec.push_back(meta_data.get_part(part_name));
    if (nullptr == part_vec.back()) {
      NaluEnv::self().naluOutputP0()
        << "Trouble with part " << part_name << std::endl;
      throw std::runtime_error(
        "Sorry, no part name found by the name " + part_name);
    }
    if (element_rank) {
      if (stk::topology::ELEMENT_RANK != part_vec.back()->primary_entity_rank())
        throw std::runtime_error(
          "Sorry, parts need to be elements.. " + part_name);
    }
  }

  return part_vec;
}
} // namespace
//--------------------------------------------------------------------------
//-------- register_nodal_fields -------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_nodal_fields(
  const std::vector<std::string>& targetNames)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();
  const stk::mesh::PartVector part_vec =
    create_part_vec(meta_data, targetNames);
  realm_.register_nodal_fields(part_vec);
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->register_nodal_fields(part_vec);
}

//--------------------------------------------------------------------------
//-------- register_edge_fields --------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_edge_fields(
  const std::vector<std::string>& targetNames)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();
  const stk::mesh::PartVector part_vec =
    create_part_vec(meta_data, targetNames);
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->register_edge_fields(part_vec);
}

//--------------------------------------------------------------------------
//-------- register_element_fields -----------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_element_fields(
  const std::vector<std::string>& targetNames)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();
  ScalarFieldType& elemVolume = meta_data.declare_field<ScalarFieldType>(
    stk::topology::ELEMENT_RANK, "element_volume");

  const stk::mesh::PartVector part_vec =
    create_part_vec(meta_data, targetNames, true);
  for (auto targetPart : part_vec) {
    const stk::mesh::PartVector part(1, targetPart);
    const stk::topology the_topo = targetPart->topology();
    EquationSystemVector::iterator ii;
    for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
         ++ii)
      (*ii)->register_element_fields(part, the_topo);
  }
  stk::mesh::Selector selector = stk::mesh::selectUnion(part_vec);
  stk::mesh::put_field_on_mesh(elemVolume, selector, 1, nullptr);
}

//--------------------------------------------------------------------------
//-------- register_interior_algorithm -------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_interior_algorithm(
  const std::vector<std::string> targetNames)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  for (size_t itarget = 0; itarget < targetNames.size(); ++itarget) {
    stk::mesh::Part* targetPart = meta_data.get_part(targetNames[itarget]);
    if (NULL == targetPart) {
      throw std::runtime_error(
        "Sorry, no part name found by the name " + targetNames[itarget]);
    } else {
      // found the part; no need to subset
      if (stk::topology::ELEMENT_RANK != targetPart->primary_entity_rank()) {
        throw std::runtime_error(
          "Sorry, parts need to be elements.. " + targetNames[itarget]);
      }

      realm_.register_interior_algorithm(targetPart);
      EquationSystemVector::iterator ii;
      for (ii = equationSystemVector_.begin();
           ii != equationSystemVector_.end(); ++ii)
        (*ii)->register_interior_algorithm(targetPart);
    }
  }
}

//--------------------------------------------------------------------------
//-------- register_wall_bc ------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_wall_bc(
  const std::string targetName, const WallBoundaryConditionData& wallBCData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  stk::mesh::Part* targetPart = meta_data.get_part(targetName);
  if (NULL == targetPart) {
    NaluEnv::self().naluOutputP0()
      << "Sorry, no part name found by the name " << targetName << std::endl;
  } else {
    // found the part
    const std::vector<stk::mesh::Part*>& mesh_parts = targetPart->subsets();
    for (std::vector<stk::mesh::Part*>::const_iterator i = mesh_parts.begin();
         i != mesh_parts.end(); ++i) {
      ThrowRequire(*i != nullptr);
      stk::mesh::Part* const part = *i;
      const stk::topology the_topo = part->topology();

      if (!(meta_data.side_rank() == part->primary_entity_rank())) {
        NaluEnv::self().naluOutputP0()
          << "Sorry, part is not a face " << targetName;
      } else {
        realm_.register_wall_bc(part, the_topo);
        EquationSystemVector::iterator ii;
        for (ii = equationSystemVector_.begin();
             ii != equationSystemVector_.end(); ++ii)
          (*ii)->register_wall_bc(part, the_topo, wallBCData);
      }
    }
  }
}

//--------------------------------------------------------------------------
//-------- register_inflow_bc ----------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_inflow_bc(
  const std::string targetName, const InflowBoundaryConditionData& inflowBCData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  stk::mesh::Part* targetPart = meta_data.get_part(targetName);
  if (NULL == targetPart) {
    NaluEnv::self().naluOutputP0()
      << "Sorry, no part name found by the name " << targetName << std::endl;
  } else {
    // found the part
    const std::vector<stk::mesh::Part*>& mesh_parts = targetPart->subsets();
    for (std::vector<stk::mesh::Part*>::const_iterator i = mesh_parts.begin();
         i != mesh_parts.end(); ++i) {
      stk::mesh::Part* const part = *i;
      const stk::topology the_topo = part->topology();

      if (!(meta_data.side_rank() == part->primary_entity_rank())) {
        NaluEnv::self().naluOutputP0()
          << "Sorry, part is not a face " << targetName;
      } else {
        realm_.register_inflow_bc(part, the_topo);
        EquationSystemVector::iterator ii;
        for (ii = equationSystemVector_.begin();
             ii != equationSystemVector_.end(); ++ii)
          (*ii)->register_inflow_bc(part, the_topo, inflowBCData);
      }
    }
  }
}

//--------------------------------------------------------------------------
//-------- register_open_bc ------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_open_bc(
  const std::string targetName, const OpenBoundaryConditionData& openBCData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  stk::mesh::Part* targetPart = meta_data.get_part(targetName);
  if (NULL == targetPart) {
    NaluEnv::self().naluOutputP0()
      << "Sorry, no part name found by the name " << targetName << std::endl;
  } else {
    // found the part
    const std::vector<stk::mesh::Part*>& mesh_parts = targetPart->subsets();
    for (std::vector<stk::mesh::Part*>::const_iterator i = mesh_parts.begin();
         i != mesh_parts.end(); ++i) {
      stk::mesh::Part* const part = *i;
      const stk::topology the_topo = part->topology();
      if (!(meta_data.side_rank() == part->primary_entity_rank())) {
        NaluEnv::self().naluOutputP0()
          << "Sorry, part is not a face " << targetName;
      } else {
        realm_.register_open_bc(part, the_topo);
        EquationSystemVector::iterator ii;
        for (ii = equationSystemVector_.begin();
             ii != equationSystemVector_.end(); ++ii)
          (*ii)->register_open_bc(part, the_topo, openBCData);
      }
    }
  }
}

//--------------------------------------------------------------------------
//-------- register_symmetry_bc --------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_symmetry_bc(
  const std::string targetName,
  const SymmetryBoundaryConditionData& symmetryBCData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  stk::mesh::Part* targetPart = meta_data.get_part(targetName);
  if (NULL == targetPart) {
    NaluEnv::self().naluOutputP0()
      << "Sorry, no part name found by the name " << targetName << std::endl;
    throw std::runtime_error("Symmetry::fatal_error()");
  } else {
    // found the part
    const std::vector<stk::mesh::Part*>& mesh_parts = targetPart->subsets();
    for (std::vector<stk::mesh::Part*>::const_iterator i = mesh_parts.begin();
         i != mesh_parts.end(); ++i) {
      stk::mesh::Part* const part = *i;
      const stk::topology the_topo = part->topology();
      if (!(meta_data.side_rank() == part->primary_entity_rank())) {
        NaluEnv::self().naluOutputP0()
          << "Sorry, part is not a face " << targetName;
        throw std::runtime_error("Symmetry::fatal_error()");
      } else {
        realm_.register_symmetry_bc(part, the_topo);
        EquationSystemVector::iterator ii;
        for (ii = equationSystemVector_.begin();
             ii != equationSystemVector_.end(); ++ii)
          (*ii)->register_symmetry_bc(part, the_topo, symmetryBCData);
      }
    }
  }
}

//--------------------------------------------------------------------------
//-------- register_abltop_bc --------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_abltop_bc(
  const std::string targetName, const ABLTopBoundaryConditionData& abltopBCData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  stk::mesh::Part* targetPart = meta_data.get_part(targetName);
  if (NULL == targetPart) {
    NaluEnv::self().naluOutputP0()
      << "Sorry, no part name found by the name " << targetName << std::endl;
    throw std::runtime_error("ABLTop::fatal_error()");
  } else {
    // found the part
    const std::vector<stk::mesh::Part*>& mesh_parts = targetPart->subsets();
    for (std::vector<stk::mesh::Part*>::const_iterator i = mesh_parts.begin();
         i != mesh_parts.end(); ++i) {
      stk::mesh::Part* const part = *i;
      const stk::topology the_topo = part->topology();
      if (!(meta_data.side_rank() == part->primary_entity_rank())) {
        NaluEnv::self().naluOutputP0()
          << "Sorry, part is not a face " << targetName;
        throw std::runtime_error("ABLTop::fatal_error()");
      } else {
        realm_.register_abltop_bc(part, the_topo);
        EquationSystemVector::iterator ii;
        for (ii = equationSystemVector_.begin();
             ii != equationSystemVector_.end(); ++ii)
          (*ii)->register_abltop_bc(part, the_topo, abltopBCData);
      }
    }
  }
}

//--------------------------------------------------------------------------
//-------- register_periodic_bc --------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_periodic_bc(
  const std::string targetNameMaster,
  const std::string targetNameSlave,
  const PeriodicBoundaryConditionData& periodicBCData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  stk::mesh::Part* masterMeshPart = meta_data.get_part(targetNameMaster);
  stk::mesh::Part* slaveMeshPart = meta_data.get_part(targetNameSlave);
  if (NULL == masterMeshPart) {
    NaluEnv::self().naluOutputP0() << "Sorry, no part name found by the name "
                                   << targetNameMaster << std::endl;
    throw std::runtime_error("EquationSystems::fatal_error()");
  } else if (NULL == slaveMeshPart) {
    NaluEnv::self().naluOutputP0() << "Sorry, no part name found by the name "
                                   << targetNameSlave << std::endl;
    throw std::runtime_error("EquationSystems::fatal_error()");
  } else {
    // error check on size of subsets
    const std::vector<stk::mesh::Part*>& masterMeshParts =
      masterMeshPart->subsets();
    const std::vector<stk::mesh::Part*>& slaveMeshParts =
      slaveMeshPart->subsets();

    if (masterMeshParts.size() != slaveMeshParts.size())
      NaluEnv::self().naluOutputP0()
        << "Mesh part subsets for master slave do not match in size"
        << std::endl;

    if (masterMeshParts.size() > 1)
      NaluEnv::self().naluOutputP0() << "Surface has subsets active; please "
                                        "make sure that the topologies match"
                                     << std::endl;

    // extract data and search tolerance
    PeriodicUserData userData = periodicBCData.userData_;
    const double searchTolerance = userData.searchTolerance_;
    const std::string searchMethodName = userData.searchMethodName_;
    realm_.register_periodic_bc(
      masterMeshPart, slaveMeshPart, searchTolerance, searchMethodName);
  }
}

//--------------------------------------------------------------------------
//-------- register_non_conformal_bc ---------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_non_conformal_bc(
  const NonConformalBoundaryConditionData& nonConformalBCData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  // extract current part vector
  stk::mesh::PartVector currentMeshPartVec;
  for (size_t k = 0; k < nonConformalBCData.currentPartNameVec_.size(); ++k) {
    stk::mesh::Part* currentMeshPart =
      meta_data.get_part(nonConformalBCData.currentPartNameVec_[k]);
    if (NULL == currentMeshPart) {
      NaluEnv::self().naluOutputP0()
        << "Sorry, no part name found by the name "
        << nonConformalBCData.currentPartNameVec_[k] << std::endl;
    }
    currentMeshPartVec.push_back(currentMeshPart);
  }

  // extract opposing part vector
  stk::mesh::PartVector opposingMeshPartVec;
  for (size_t k = 0; k < nonConformalBCData.opposingPartNameVec_.size(); ++k) {
    stk::mesh::Part* opposingMeshPart =
      meta_data.get_part(nonConformalBCData.opposingPartNameVec_[k]);
    if (NULL == opposingMeshPart) {
      NaluEnv::self().naluOutputP0()
        << "Sorry, no part name found by the name "
        << nonConformalBCData.opposingPartNameVec_[k] << std::endl;
    }
    opposingMeshPartVec.push_back(opposingMeshPart);
  }

  // set up the non-conformal bc, e.g., manager, parts, etc.
  realm_.setup_non_conformal_bc(
    currentMeshPartVec, opposingMeshPartVec, nonConformalBCData);

  // subset the current part for current part explosed surface field
  // registration and algorithm creation
  for (size_t k = 0; k < currentMeshPartVec.size(); ++k) {

    const std::vector<stk::mesh::Part*>& mesh_parts =
      currentMeshPartVec[k]->subsets();
    for (std::vector<stk::mesh::Part*>::const_iterator i = mesh_parts.begin();
         i != mesh_parts.end(); ++i) {
      stk::mesh::Part* const part = *i;
      const stk::topology the_topo = part->topology();
      if (!(meta_data.side_rank() == part->primary_entity_rank())) {
        NaluEnv::self().naluOutputP0()
          << "Sorry, part is not a face " << part->name();
        throw std::runtime_error("NonConformal::fatal_error()");
      } else {
        realm_.register_non_conformal_bc(part, the_topo);
        EquationSystemVector::iterator ii;
        for (ii = equationSystemVector_.begin();
             ii != equationSystemVector_.end(); ++ii)
          (*ii)->register_non_conformal_bc(part, the_topo);
      }
    }
  }
}

//--------------------------------------------------------------------------
//-------- register_overset_bc ---------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_overset_bc(const OversetBoundaryConditionData&)
{
  // register algs on the equation system
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->register_overset_bc(/*nothing required as of yet*/);
}

//--------------------------------------------------------------------------
//-------- register_surface_pp_algorithm ----------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_surface_pp_algorithm(
  const PostProcessingData& theData)
{
  stk::mesh::MetaData& meta_data = realm_.meta_data();

  stk::mesh::PartVector partVector;
  std::vector<std::string> targetNames = theData.targetNames_;
  for (size_t in = 0; in < targetNames.size(); ++in) {
    stk::mesh::Part* targetPart = meta_data.get_part(targetNames[in]);
    if (NULL == targetPart) {
      NaluEnv::self().naluOutputP0()
        << "SurfacePP: can not find part with name: " << targetNames[in];
    } else {
      // found the part
      const std::vector<stk::mesh::Part*>& mesh_parts = targetPart->subsets();
      for (std::vector<stk::mesh::Part*>::const_iterator i = mesh_parts.begin();
           i != mesh_parts.end(); ++i) {
        stk::mesh::Part* const part = *i;
        if (!(meta_data.side_rank() == part->primary_entity_rank())) {
          NaluEnv::self().naluOutputP0()
            << "SurfacePP: part is not a face: " << targetNames[in];
        }
        partVector.push_back(part);
      }
    }
  }

  // call through to equation systems
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->register_surface_pp_algorithm(theData, partVector);
}

//--------------------------------------------------------------------------
//-------- setup_initial_condition_fcn() -----------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::register_initial_condition_fcn(
  stk::mesh::Part* part, const UserFunctionInitialConditionData& fcnIC)
{
  // call through to equation systems
  for (EquationSystem* eqSys : equationSystemVector_)
    eqSys->register_initial_condition_fcn(
      part, fcnIC.functionNames_, fcnIC.functionParams_);
}

//--------------------------------------------------------------------------
//-------- initialize() ----------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::initialize()
{
  NaluEnv::self().naluOutputP0()
    << "EquationSystems::initialize(): Begin " << std::endl;
  double start_time = NaluEnv::self().nalu_time();
  for (EquationSystem* eqSys : equationSystemVector_) {
    if (realm_.get_activate_memory_diagnostic()) {
      NaluEnv::self().naluOutputP0()
        << "NaluMemory::EquationSystems::initialize(): " << eqSys->name_
        << std::endl;
      realm_.provide_memory_summary();
    }
    double start_time_eq = NaluEnv::self().nalu_time();
    eqSys->initialize();
    double end_time_eq = NaluEnv::self().nalu_time();
    eqSys->timerInit_ += (end_time_eq - start_time_eq);
  }
  double end_time = NaluEnv::self().nalu_time();
  realm_.timerInitializeEqs_ += (end_time - start_time);
  NaluEnv::self().naluOutputP0()
    << "EquationSystems::initialize(): End " << std::endl;

  if (realm_.hasOverset_) {
    NaluEnv::self().naluOutputP0()
      << "EquationSystems: overset solution strategy" << std::endl;
    for (auto* eqsys : equationSystemVector_) {
      // Skip wrapper equations LowMach, SST etc.
      if (eqsys->linsys_ == nullptr)
        continue;
      NaluEnv::self().naluOutputP0()
        << " - " << eqsys->eqnTypeName_ << ": "
        << (eqsys->decoupledOverset_ ? "decoupled" : "coupled") << std::endl;
    }
  }
}

//--------------------------------------------------------------------------
//-------- reinitialize_linear_system()
//----------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::reinitialize_linear_system()
{
  double start_time = NaluEnv::self().nalu_time();
  for (EquationSystem* eqSys : equationSystemVector_) {
    double start_time_eq = NaluEnv::self().nalu_time();
    eqSys->reinitialize_linear_system();
    double end_time_eq = NaluEnv::self().nalu_time();
    eqSys->timerInit_ += (end_time_eq - start_time_eq);
  }
  double end_time = NaluEnv::self().nalu_time();
  realm_.timerInitializeEqs_ += (end_time - start_time);
}

//--------------------------------------------------------------------------
//-------- populate_derived_qauntities() -----------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::populate_derived_quantities()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->populate_derived_quantities();
}

//--------------------------------------------------------------------------
//-------- initial_work() --------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::initial_work()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->initial_work();
}

//--------------------------------------------------------------------------
//-------- solve_and_update ------------------------------------------------
//--------------------------------------------------------------------------
bool
EquationSystems::solve_and_update()
{
  EquationSystemVector::iterator ii;
  // Perform necessary setup tasks before iterations
  pre_iter_work();

  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii) {
    (*ii)->pre_iter_work();
    (*ii)->solve_and_update();
    (*ii)->post_iter_work();
  }

  // memory diagnostic
  if (realm_.get_activate_memory_diagnostic()) {
    NaluEnv::self().naluOutputP0()
      << "NaluMemory::EquationSystem::solve_and_update()" << std::endl;
    realm_.provide_memory_summary();
  }

  // TODO: Refactor code to adhere to pre and post iter_work design
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->post_iter_work_dep();

  // Perform tasks after all EQS have been solved
  post_iter_work();

  // check equations for convergence
  bool overallConvergence = true;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii) {
    const bool systemConverged = (*ii)->system_is_converged();
    if (!systemConverged)
      overallConvergence = false;
  }

  return overallConvergence;
}

//--------------------------------------------------------------------------
//-------- provide_system_norm ---------------------------------------------
//--------------------------------------------------------------------------
double
EquationSystems::provide_system_norm()
{
  double maxNorm = -1.0e16;
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    maxNorm = std::max(maxNorm, (*ii)->provide_scaled_norm());
  return maxNorm;
}

//--------------------------------------------------------------------------
//-------- provide_mean_system_norm ----------------------------------------
//--------------------------------------------------------------------------
double
EquationSystems::provide_mean_system_norm()
{
  double meanNorm = 0.0;
  double normIncrement = 0.0;
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii) {
    meanNorm += (*ii)->provide_norm();
    normIncrement += (*ii)->provide_norm_increment();
  }
  return meanNorm / normIncrement;
}

//--------------------------------------------------------------------------
//-------- dump_eq_time ----------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::dump_eq_time()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii) {
    (*ii)->dump_eq_time();
  }
}

//--------------------------------------------------------------------------
//-------- predict_state ---------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::predict_state()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->predict_state();
}

//--------------------------------------------------------------------------
//-------- populate_boundary_data ------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::populate_boundary_data()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii) {
    for (size_t k = 0; k < (*ii)->bcDataAlg_.size(); ++k) {
      (*ii)->bcDataAlg_[k]->execute();
    }
  }
}

//--------------------------------------------------------------------------
//-------- post_external_data_transfer_work --------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::post_external_data_transfer_work()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii) {
    for (size_t k = 0; k < (*ii)->bcDataAlg_.size(); ++k) {
      (*ii)->post_external_data_transfer_work();
    }
  }
}

//--------------------------------------------------------------------------
//-------- boundary_data_to_state_data -------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::boundary_data_to_state_data()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii) {
    for (size_t k = 0; k < (*ii)->bcDataMapAlg_.size(); ++k) {
      (*ii)->bcDataMapAlg_[k]->execute();
    }
  }
}

//--------------------------------------------------------------------------
//-------- provide_output --------------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::provide_output()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->provide_output();
}

//--------------------------------------------------------------------------
//-------- pre_timestep_work -----------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::pre_timestep_work()
{
  // do the work
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->pre_timestep_work();
}

//--------------------------------------------------------------------------
//-------- post_converged_work----------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::post_converged_work()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->post_converged_work();
}

//--------------------------------------------------------------------------
//-------- evaluate_properties----------------------------------------------
//--------------------------------------------------------------------------
void
EquationSystems::evaluate_properties()
{
  EquationSystemVector::iterator ii;
  for (ii = equationSystemVector_.begin(); ii != equationSystemVector_.end();
       ++ii)
    (*ii)->evaluate_properties();
}

void
EquationSystems::pre_iter_work()
{
  if (realm_.hasOverset_)
    oversetUpdater_->execute();

  for (auto alg : preIterAlgDriver_) {
    alg->execute();
  }
}

void
EquationSystems::post_iter_work()
{
  for (auto alg : postIterAlgDriver_) {
    alg->execute();
  }
}

void
EquationSystems::register_overset_field_update(
  stk::mesh::FieldBase* field, int nrows, int ncols)
{
  oversetUpdater_->register_overset_field_update(field, nrows, ncols);
}

bool
EquationSystems::all_systems_decoupled() const
{
  // No overset, so there is no concept of decoupled
  if (!realm_.hasOverset_)
    return false;

  // EquationSystems within a realm is defined as decoupled iff all equation
  // systems are solved in decoupled fashion. Even if one of the equation system
  // is solved in a fully coupled fashion, we return false.
  bool hasDecoupled = true;
  for (auto* eqsys : equationSystemVector_)
    hasDecoupled = hasDecoupled && eqsys->is_decoupled();

  return hasDecoupled;
}

} // namespace nalu
} // namespace sierra
