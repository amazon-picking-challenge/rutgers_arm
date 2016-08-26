/**
 * @file manipulation_tp.cpp
 *
 * @copyright Software License Agreement (BSD License)
 * Copyright (c) 2013, Rutgers the State University of New Jersey, New Brunswick
 * All Rights Reserved.
 * For a full description see the file named LICENSE.
 *
 * Authors: Andrew Dobson, Andrew Kimmel, Athanasios Krontiris, Zakary Littlefield, Rahul Shome, Kostas Bekris
 *
 * Email: pracsys@googlegroups.com
 */

#include "planning/task_planners/manipulator_tp.hpp"
#include "planning/problem_specifications/manipulator_specification.hpp"
#include "planning/queries/manipulator_query.hpp"
#include "planning/queries/manipulator_mp_query.hpp"
#include "planning/modules/obstacle_aware_astar.hpp"

#include "prx/utilities/definitions/string_manip.hpp"
#include "prx/utilities/definitions/random.hpp"
#include "prx/utilities/math/configurations/config.hpp"
#include "prx/utilities/distance_metrics/ann_metric/ann_distance_metric.hpp"
#include "prx/utilities/goals/goal.hpp"
#include "prx/utilities/math/configurations/bounds.hpp"
#include "prx/utilities/goals/multiple_goal_states.hpp"
#include "prx/utilities/statistics/statistics.hpp"

#include "prx/planning/world_model.hpp"
#include "prx/planning/modules/validity_checkers/validity_checker.hpp"
#include "prx/planning/modules/local_planners/local_planner.hpp"
#include "prx/planning/problem_specifications/motion_planning_specification.hpp"
#include "prx/planning/queries/motion_planning_query.hpp"
#include "prx/planning/modules/stopping_criteria/stopping_criteria.hpp"
#include "prx/planning/motion_planners/motion_planner.hpp"
#include "prx/planning/motion_planners/prm_star/prm_star_statistics.hpp"

#include "../../../manipulation/planning/modules/samplers/manip_sampler.hpp"

#include <pluginlib/class_list_macros.h>
#include <boost/assign/list_of.hpp>
#include <boost/graph/subgraph.hpp>
#include <boost/range/adaptor/map.hpp>

#include <boost/graph/connected_components.hpp>
#include <boost/graph/biconnected_components.hpp>
#include <boost/graph/compressed_sparse_row_graph.hpp>
#include "prx/planning/motion_planners/prm_star/prm_star.hpp"
#include "planning/queries/manipulator_mp_query.hpp"
#include "planning/problem_specifications/manipulation_mp_specification.hpp"
#include "planning/modules/smoothing_info.hpp"

#include <sstream>

PLUGINLIB_EXPORT_CLASS(prx::packages::rearrangement_manipulation::manipulator_tp_t, prx::plan::planner_t)

namespace prx
{
    using namespace util;
    using namespace sim;
    using namespace plan;

    namespace packages
    {
        using namespace manipulation;

        namespace rearrangement_manipulation
        {

            manipulator_tp_t::manipulator_tp_t() { }

            manipulator_tp_t::~manipulator_tp_t() { }

            void manipulator_tp_t::init(const parameter_reader_t* reader, const parameter_reader_t* template_reader)
            {
                task_planner_t::init(reader, template_reader);
                PRX_INFO_S("Initializing Manipulator task planner inside rearrangement package...");

                ungrasped_name = parameters::get_attribute("ungrasped_name", reader, template_reader);
                grasped_name = parameters::get_attribute("grasped_name", reader, template_reader);

                if( parameters::has_attribute("phase_queries", reader, template_reader) )
                {
                    const parameter_reader_t* query_template_reader = NULL;
                    std::string element = "phase_queries/reach_phase";
                    if( parameters::has_attribute(element, reader, template_reader) )
                    {
                        if( parameters::has_attribute(element + "/template", reader, template_reader) )
                        {
                            query_template_reader = new parameter_reader_t(ros::this_node::getName() + "/" + reader->get_attribute(element + "/template"));
                        }
                        output_queries[reach_phase] = parameters::initialize_from_loader<query_t > ("prx_planning", reader, element, query_template_reader, "");
                        if( query_template_reader != NULL )
                        {
                            delete query_template_reader;
                            query_template_reader = NULL;
                        }
                    }
                    else
                        PRX_FATAL_S("Missing " << element << " query!");

                    element = "phase_queries/transfer_phase";
                    if( parameters::has_attribute(element, reader, template_reader) )
                    {
                        if( parameters::has_attribute(element + "/template", reader, template_reader) )
                        {
                            query_template_reader = new parameter_reader_t(ros::this_node::getName() + "/" + reader->get_attribute(element + "/template"));
                        }
                        output_queries[transfer_phase] = parameters::initialize_from_loader<query_t > ("prx_planning", reader, element, query_template_reader, "");
                        if( query_template_reader != NULL )
                        {
                            delete query_template_reader;
                            query_template_reader = NULL;
                        }
                    }
                    else
                        PRX_FATAL_S("Missing " << element << " query!");

                    element = "phase_queries/retract_phase";
                    if( parameters::has_attribute(element, reader, template_reader) )
                    {
                        if( parameters::has_attribute(element + "/template", reader, template_reader) )
                        {
                            query_template_reader = new parameter_reader_t(ros::this_node::getName() + "/" + reader->get_attribute(element + "/template"));
                        }
                        output_queries[retract_phase] = parameters::initialize_from_loader<query_t > ("prx_planning", reader, element, query_template_reader, "");
                        if( query_template_reader != NULL )
                        {
                            delete query_template_reader;
                            query_template_reader = NULL;
                        }
                    }
                    else
                        PRX_FATAL_S("Missing " << element << " query!");

                    element = "phase_queries/smoothing_phase";
                    if( parameters::has_attribute(element, reader, template_reader) )
                    {
                        if( parameters::has_attribute(element + "/template", reader, template_reader) )
                        {
                            query_template_reader = new parameter_reader_t(ros::this_node::getName() + "/" + reader->get_attribute(element + "/template"));
                        }
                        smoothing_query = dynamic_cast<manipulator_mp_query_t*>(parameters::initialize_from_loader<query_t > ("prx_planning", reader, element, query_template_reader, ""));
                        if( query_template_reader != NULL )
                        {
                            delete query_template_reader;
                            query_template_reader = NULL;
                        }
                    }
                    else
                        PRX_FATAL_S("Missing " << element << " query!");
                }
                else
                    PRX_FATAL_S("Missing the initialization of the output queries!");

                visualize_graph = parameters::get_attribute_as<bool>("visualize_graph", reader, template_reader, false);
            }

            void manipulator_tp_t::reset()
            {
                retract_path.clear();
                retract_plan.clear();
            }

            const statistics_t* manipulator_tp_t::get_statistics()
            {
                PRX_WARN_S("Get statistics for manipulation tp is not implemented!");
                return new statistics_t();
            }

            void manipulator_tp_t::link_specification(specification_t* new_spec)
            {
                manipulation_tp_t::link_specification(new_spec);
                in_specs = static_cast<manipulator_specification_t*>(new_spec);

                PRX_ASSERT(in_specs->transit_graph_specification != NULL);
                PRX_ASSERT(in_specs->transfer_graph_specification != NULL);
                planners[ungrasped_name]->link_specification(in_specs->transit_graph_specification);
                planners[grasped_name]->link_specification(in_specs->transfer_graph_specification);

                transit_constraints = static_cast<manipulation_mp_specification_t*>(in_specs->transit_graph_specification)->valid_constraints;
            }

            void manipulator_tp_t::link_query(query_t* new_query)
            {
                //                PRX_DEBUG_POINT("Linking Query");
                manipulation_tp_t::link_query(new_query);
                in_query = static_cast<manipulator_query_t*>(new_query);
//                PRX_DEBUG_COLOR("Manipulator tp query collision: " << in_query->q_collision_type, PRX_TEXT_LIGHTGRAY);
            }

            void manipulator_tp_t::setup()
            {
                PRX_DEBUG_COLOR("Setup manipulator tp ...", PRX_TEXT_CYAN);

                _manipulator = in_specs->manipulator;
                _object = in_specs->object;
                pc_name_manipulator_only = in_specs->pc_name_manipulator_only;
                pc_name_object_only = in_specs->pc_name_object_only;
                pc_name_manipulator_with_object = in_specs->pc_name_manipulator_with_object;
                pc_name_manipulator_with_active_object = in_specs->pc_name_manipulator_with_active_object;
                pc_name_transit_inform = in_specs->pc_name_transit_inform;
                pc_name_transfer_inform = in_specs->pc_name_transfer_inform;
                pc_name_transit_planning = in_specs->pc_name_transit_planning;
                pc_name_transfer_planning = in_specs->pc_name_transfer_planning;


                PRX_DEBUG_COLOR("manipulator :  " << _manipulator->get_pathname(), PRX_TEXT_RED);
                PRX_DEBUG_COLOR("object :  " << _object->get_pathname(), PRX_TEXT_RED);

                ////////////////////
                //                Testing the planning contexts.
                //                model->use_context(pc_name_transit_planning);
                //                PRX_DEBUG_COLOR("pc_name_transit_planning: (" << model->get_state_space()->get_dimension() << " , " << model->get_active_space()->get_dimension() << ") (" << model->get_state_space()->get_space_name() << " , " << model->get_active_space()->get_space_name(),PRX_TEXT_GREEN);
                //
                //                model->use_context(pc_name_transfer_planning);
                //                PRX_DEBUG_COLOR("pc_name_transfer_planning: (" << model->get_state_space()->get_dimension() << " , " << model->get_active_space()->get_dimension() << ") (" << model->get_state_space()->get_space_name() << " , " << model->get_active_space()->get_space_name(),PRX_TEXT_GREEN);
                //
                //                model->use_context(pc_name_manipulator_with_object);
                //                PRX_DEBUG_COLOR("pc_name_manipulator_with_object: (" << model->get_state_space()->get_dimension() << " , " << model->get_active_space()->get_dimension() << ") (" << model->get_state_space()->get_space_name() << " , " << model->get_active_space()->get_space_name(),PRX_TEXT_GREEN);
                //
                //                model->use_context(pc_name_object_only);
                //                PRX_DEBUG_COLOR("pc_name_object_only: (" << model->get_state_space()->get_dimension() << " , " << model->get_active_space()->get_dimension() << ") (" << model->get_state_space()->get_space_name() << " , " << model->get_active_space()->get_space_name(),PRX_TEXT_GREEN);
                //
                //                model->use_context(pc_name_manipulator_only);
                //                PRX_DEBUG_COLOR("pc_name_manipulator_only: (" << model->get_state_space()->get_dimension() << " , " << model->get_active_space()->get_dimension() << ") (" << model->get_state_space()->get_space_name() << " , " << model->get_active_space()->get_space_name(),PRX_TEXT_GREEN);
                ///////////////////

                //Initializing the spaces.
                model->use_context(pc_name_transfer_planning);
                mo_space = model->get_state_space();
                other_objects_space = model->get_active_space();

                model->use_context(pc_name_transit_planning);
                real_object_space = model->get_active_space();
                manip_state_space = model->get_state_space();
                manip_control_space = model->get_control_space();

                model->use_context(pc_name_object_only);
                object_state_space = model->get_state_space();

                //Allocating the helping state/control point variables.
                manip_state = manip_state_space->alloc_point();
                safe_state = manip_state_space->alloc_point();
                released_point = manip_state_space->alloc_point();
                grasped_point = mo_space->alloc_point();
                object_state = object_state_space->alloc_point();
                initial_pose = object_state_space->alloc_point();
                goal_pose = object_state_space->alloc_point();
                real_initial_state = real_object_space->alloc_point();

                manip_ctrl = manip_control_space->alloc_point();
                safe_control = manip_control_space->alloc_point();

                manip_control_vec.resize(manip_control_space->get_dimension());

                retract_path.link_space(manip_state_space);
                retract_plan.link_control_space(manip_control_space);

                if( manip_sampler == NULL )
                    manip_sampler = in_specs->manip_sampler;
                else
                    manip_sampler->link_info(_manipulator, manip_state_space, object_state_space, mo_space);

                in_specs->link_spaces(manip_state_space, manip_control_space);
                in_specs->setup(model);
                manip_state_space->copy_vector_to_point(in_specs->safe_position, safe_state);
                manip_control_space->copy_vector_to_point(in_specs->safe_position, safe_control);

                //Setup the output queries
                reach_query = dynamic_cast<motion_planning_query_t*>(output_queries[reach_phase]);
                reach_query->link_spaces(manip_state_space, manip_control_space);
                //For the reach_phase query the starting point will always be the safe state.
                reach_query->copy_start(safe_state);
                reach_goal = dynamic_cast<multiple_goal_states_t*>(reach_query->get_goal());

                transfer_query = dynamic_cast<motion_planning_query_t*>(output_queries[transfer_phase]);
                transfer_query->link_spaces(mo_space, manip_control_space);
                transfer_goal = dynamic_cast<multiple_goal_states_t*>(transfer_query->get_goal());

                retract_query = dynamic_cast<motion_planning_query_t*>(output_queries[retract_phase]);
                retract_query->link_spaces(manip_state_space, manip_control_space);
                //For the retraction face the goal will always be the safe state.
                ((multiple_goal_states_t*)retract_query->get_goal())->copy_goal_state(safe_state);

                smoothing_query->link_spaces(manip_state_space, manip_control_space);

                validate();
            }

            bool manipulator_tp_t::serialize()
            {
                bool serialized = false;
                if( serialize_ungrasped_graph )
                {
                    model->use_context(pc_name_manipulator_only);
                    motion_planner_t* motion_planner = dynamic_cast<motion_planner_t*>(planners[ungrasped_name]);
                    motion_planner->set_param("", "serialize_flag", serialize_ungrasped_graph);
                    motion_planner->set_param("", "serialization_file", ungrasped_graph_file_name);
                    motion_planner->serialize();
                    serialized = true;
                }
                if( serialize_grasped_graph )
                {
                    model->use_context(pc_name_manipulator_with_object);
                    motion_planner_t* motion_planner = dynamic_cast<motion_planner_t*>(planners[grasped_name]);
                    motion_planner->set_param("", "serialize_flag", serialize_grasped_graph);
                    motion_planner->set_param("", "serialization_file", grasped_graph_file_name);
                    motion_planner->serialize();
                    serialized = true;
                }

                return serialized;
            }

            bool manipulator_tp_t::deserialize()
            {
                if( deserialize_ungrasped_graph )
                {
                    model->use_context(pc_name_manipulator_only);
                    dynamic_cast<motion_planner_t*>(planners[ungrasped_name])->deserialize();
                }
                if( deserialize_grasped_graph )
                {
                    model->use_context(pc_name_manipulator_with_object);
                    dynamic_cast<motion_planner_t*>(planners[grasped_name])->deserialize();
                }
                return true;
            }

            bool manipulator_tp_t::execute()
            {
                model->use_context(pc_name_transit_inform);
                planners[ungrasped_name]->setup();
                planners[ungrasped_name]->execute();
                if( visualize_graph )
                    planners[ungrasped_name]->update_visualization();
                model->use_context(pc_name_transfer_inform);
                planners[grasped_name]->setup();
                planners[grasped_name]->execute();
                if( visualize_graph )
                    planners[grasped_name]->update_visualization();
                return true;
            }

            void manipulator_tp_t::resolve_query()
            {
                if( manip_query->mode == manipulation_query_t::PRX_TRANSIT )
                {
                    std::string old_context = model->get_current_context();
                    model->use_context(pc_name_transit_planning);
                    reach_query->clear();
                    dynamic_cast<manipulator_mp_query_t*>(reach_query)->q_collision_type = manip_query->q_collision_type;
                    reach_query->copy_start(manip_query->get_start_state());
                    unsigned goals_size = manip_query->get_goal()->size();
                    unsigned tmp_size;
                    dynamic_cast<multiple_goal_states_t*>(reach_query->get_goal())->copy_multiple_goal_states(manip_query->get_goal()->get_goal_points(tmp_size),goals_size);
                    planners[ungrasped_name]->link_query(reach_query);
                    planners[ungrasped_name]->resolve_query();
                    if( reach_query->plan.size() == 0 )
                    {
                        add_null_path();
                    }
                    else
                    {
                        //                        manip_state_space->copy_from_point(manip_query->get_start_state());
                        //                        manip_state_space->copy_to_vector(manip_control_vec);
                        //                        manip_control_space->copy_vector_to_point(manip_control_vec, manip_ctrl);
                        //                        reach_query->plan.copy_onto_front(manip_ctrl, reach_query->plan[0].duration);
                        update_query_information(reach_query);
                    }
                    model->use_context(old_context);
                    return;
                }
                else if( manip_query->mode == manipulation_query_t::PRX_TRANSFER )
                {
                    std::string old_context = model->get_current_context();
                    model->use_context(pc_name_transfer_planning);
                    transfer_query->clear();
                    dynamic_cast<manipulator_mp_query_t*>(transfer_query)->q_collision_type = manip_query->q_collision_type;
                    transfer_query->copy_start(manip_query->get_start_state());
                    unsigned goals_size = manip_query->get_goal()->size();
                    unsigned tmp_size;
                    ((multiple_goal_states_t*)transfer_query->get_goal())->copy_multiple_goal_states(manip_query->get_goal()->get_goal_points(tmp_size),goals_size);
                    planners[grasped_name]->link_query(transfer_query);
                    planners[grasped_name]->resolve_query();
                    if( transfer_query->plan.size() == 0 )
                    {
                        add_null_path();
                    }
                    else
                    {
                        //                        mo_space->copy_from_point(manip_query->get_start_state());
                        //                        manip_state_space->copy_to_vector(manip_control_vec);
                        //                        manip_control_space->copy_vector_to_point(manip_control_vec, manip_ctrl);
                        //                        PRX_ASSERT(manip_ctrl->memory.back() == 1);
                        //                        transfer_query->plan.copy_onto_front(manip_ctrl, transfer_query->plan[0].duration);
                        update_query_information(transfer_query);
                    }
                    model->use_context(old_context);
                    return;
                }
                manipulation_tp_t::resolve_query();
            }

            bool manipulator_tp_t::reach(pose_t& pose, query_t* query, int grasp_index)
            {
                std::string old_context = model->get_current_context();
                model->use_context(pc_name_transit_planning);
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(query);

                dynamic_cast<multiple_goal_states_t*>(manip_mp_query->get_goal())->copy_goal_state(pose.ungrasped_set[grasp_index]);
                manip_mp_query->clear();
                planners[ungrasped_name]->link_query(manip_mp_query);
                planners[ungrasped_name]->resolve_query();

                if( manip_mp_query->plan.size() == 0 )
                {
                    model->use_context(old_context);
                    return false;
                }

                //                manip_mp_query->plan.copy_onto_front(safe_control, manip_mp_query->plan[0].duration);
                //                manip_mp_query->retracted_point = manip_mp_query->plan.size();
                //                manip_mp_query->plan += pose.reaching_plans[grasp_index];
                model->use_context(old_context);
                return true;
            }

            bool manipulator_tp_t::tranfer(pose_t& start_pose, pose_t& target_pose, plan::query_t* query, int start_grasp_index, int target_grasp_index)
            {
                std::string old_context = model->get_current_context();
                model->use_context(pc_name_transfer_planning);
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(query);
                manip_mp_query->clear();
                manip_mp_query->copy_start(start_pose.grasped_set[start_grasp_index]);
                ((multiple_goal_states_t*)manip_mp_query->get_goal())->copy_goal_state(target_pose.grasped_set[target_grasp_index]);
                planners[grasped_name]->link_query(manip_mp_query);
                planners[grasped_name]->resolve_query();

                if( manip_mp_query->plan.size() == 0 )
                {
                    model->use_context(old_context);
                    return false;
                }

//                manip_state_space->copy_point_to_vector(start_pose.ungrasped_set[start_grasp_index], manip_control_vec);
//                manip_control_space->copy_vector_to_point(manip_control_vec, manip_ctrl);
//                manip_ctrl->memory.back() = 1;
//                graph_query->plan.copy_onto_front(manip_ctrl, graph_query->plan[0].duration);
//#ifndef NDEBUG
//                unsigned sz = graph_query->plan.size();
//                for( unsigned i = 0; i < sz; ++i )
//                    PRX_ASSERT(graph_query->plan[i].control->memory.back() == 1);
//#endif
                model->use_context(old_context);
                return true;
            }

            bool manipulator_tp_t::retract(pose_t& pose, plan::query_t* query, int grasp_index)
            {
                if( in_query->mode == manipulation_query_t::PRX_FULL_PATH )
                {
                    transit_constraints->erase(in_query->from_pose);
                    transit_constraints->insert(in_query->to_pose);
                }

                if( manipulation_tp_t::retract(pose, query, grasp_index) )
                {
                    manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(query);
                    manip_mp_query->retracted_point = manip_mp_query->plan.size() - manip_mp_query->retracted_point - 1;
                    return true;
                }
                return false;
            }

            void manipulator_tp_t::update_query_information(query_t* query, int index)
            {
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(query);
                in_query->found_path = true;
                in_query->found_goal_state = manip_mp_query->goal_state;
                in_query->found_start_state = manip_mp_query->found_start_state;
                if( index != -1 )
                {
                    PRX_ASSERT(manip_query->path_quality == manipulation_query_t::PRX_BEST_PATH);
                    (*in_query->plans[index]) = graph_query->plan;
                    in_query->constraints[index].clear();
                    in_query->constraints_in_order[index].clear();
                    in_query->full_constraints[index].clear();
                    in_query->constraints[index].insert(manip_mp_query->constraints.begin(), manip_mp_query->constraints.end());
                    in_query->constraints_in_order[index].insert(in_query->constraints_in_order[index].end(), manip_mp_query->constraints_in_order.begin(), manip_mp_query->constraints_in_order.end());
                    in_query->full_constraints[index].insert(manip_mp_query->full_constraints.begin(), manip_mp_query->full_constraints.end());
                    in_query->solutions_costs[index] = manip_mp_query->solution_cost;
                    in_query->reaching_points[index] = manip_mp_query->retracted_point;
                    in_query->goal_states[index] = manip_mp_query->goal_state;
                    return;
                }

                manip_query->plans.push_back(new plan_t());
                manip_query->plans.back()->link_control_space(manip_control_space);
                (*manip_query->plans.back()) = manip_mp_query->plan;
                in_query->constraints.push_back(manip_mp_query->constraints);
                in_query->constraints_in_order.push_back(manip_mp_query->constraints_in_order);
                in_query->full_constraints.push_back(manip_mp_query->full_constraints);
                in_query->solutions_costs.push_back(manip_mp_query->solution_cost);
                in_query->reaching_points.push_back(manip_mp_query->retracted_point);
                in_query->goal_states.push_back(manip_mp_query->goal_state);
            }

            void manipulator_tp_t::combine_queries(unsigned& min_path_size)
            {
                in_query->found_path = true;
                plan_t* plan = new plan_t();
                plan->link_control_space(manip_control_space);
                *plan = reach_query->plan;
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(reach_query);

                int reaching = manip_mp_query->retracted_point;
                //                                    plan->copy_onto_back(face1_plan.back().control, 0.1);
                //                                    plan->back().control->memory.back() = 1;
                *plan += transfer_query->plan;
                //                                    plan->copy_onto_back(face2_plan.back().control, 0.1);
                //                                    plan->back().control->memory.back() = 0;
                *plan += retract_query->plan;
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(retract_query);
                int retracting = reach_query->plan.size() + transfer_query->plan.size() + manip_mp_query->retracted_point;
                //                                    plan->copy_onto_back(safe_control, 0.02);

                PRX_DEBUG_COLOR("Start of face1: " << manip_control_space->print_point(reach_query->plan[0].control, 4), PRX_TEXT_GREEN);
                PRX_DEBUG_COLOR("End of face1: " << manip_control_space->print_point(reach_query->plan.back().control, 4), PRX_TEXT_GREEN);
                PRX_DEBUG_COLOR("Start of face2: " << manip_control_space->print_point(transfer_query->plan[0].control, 4), PRX_TEXT_BROWN);
                PRX_DEBUG_COLOR("End of face2: " << manip_control_space->print_point(transfer_query->plan.back().control, 4), PRX_TEXT_BROWN);
                PRX_DEBUG_COLOR("Start of face3: " << manip_control_space->print_point(retract_query->plan[0].control, 4), PRX_TEXT_RED);
                PRX_DEBUG_COLOR("End of face3: " << manip_control_space->print_point(retract_query->plan.back().control, 4), PRX_TEXT_RED);

                double dist = reach_query->solution_cost + transfer_query->solution_cost + retract_query->solution_cost;

                if( manip_query->path_quality == manipulation_query_t::PRX_BEST_PATH )
                {
                    if( plan->size() < min_path_size )
                    {
                        min_path_size = plan->size();
                        *(in_query->plans[0]) = *plan;

                        if( in_query->constraints.size() == 0 )
                        {
                            in_query->constraints.push_back(std::set<unsigned>());
                            in_query->full_constraints.push_back(std::set<unsigned>());
                            in_query->solutions_costs.push_back(0);
                            in_query->reaching_points.push_back(0);
                            in_query->retracting_points.push_back(0);
                        }
                        in_query->constraints[0].clear();
                        in_query->full_constraints[0].clear();
                        manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(reach_query);
                        in_query->constraints[0] = manip_mp_query->constraints;
                        in_query->full_constraints[0] = manip_mp_query->full_constraints;
                        manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(transfer_query);
                        in_query->constraints[0].insert(manip_mp_query->constraints.begin(), manip_mp_query->constraints.end());
                        in_query->full_constraints[0].insert(manip_mp_query->full_constraints.begin(), manip_mp_query->full_constraints.end());
                        manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(retract_query);
                        in_query->constraints[0].insert(manip_mp_query->constraints.begin(), manip_mp_query->constraints.end());
                        in_query->full_constraints[0].insert(manip_mp_query->full_constraints.begin(), manip_mp_query->full_constraints.end());
                        in_query->solutions_costs[0] = dist;
                        in_query->reaching_points[0] = reaching;
                        in_query->retracting_points[0] = retracting;
                    }
                    delete plan;
                    return;
                }

                //PRX_ALL_PATHS and PRX_FIRST_PATH will do the same things, except that for the PRX_FIRST_PATH
                //this function will be called only once.
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(reach_query);
                std::set<unsigned> constraints = manip_mp_query->constraints;
                std::set<unsigned> full_constraints = manip_mp_query->full_constraints;
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(transfer_query);
                constraints.insert(manip_mp_query->constraints.begin(), manip_mp_query->constraints.end());
                full_constraints.insert(manip_mp_query->full_constraints.begin(), manip_mp_query->full_constraints.end());
                manip_mp_query = dynamic_cast<manipulator_mp_query_t*>(retract_query);
                constraints.insert(manip_mp_query->constraints.begin(), manip_mp_query->constraints.end());
                full_constraints.insert(manip_mp_query->full_constraints.begin(), manip_mp_query->full_constraints.end());


                std::stringstream output(std::stringstream::out);
                foreach(unsigned i, constraints)
                {
                    output << i << " , ";
                }
                PRX_DEBUG_COLOR("current : " << output.str(), PRX_TEXT_MAGENTA);

                std::stringstream output2(std::stringstream::out);

                foreach(unsigned i, full_constraints)
                {
                    output2 << i << " , ";
                }
                PRX_DEBUG_COLOR("Full : " << output2.str(), PRX_TEXT_RED);

                in_query->plans.push_back(plan);
                in_query->constraints.push_back(constraints);
                in_query->full_constraints.push_back(full_constraints);
                in_query->solutions_costs.push_back(dist);
                in_query->reaching_points.push_back(reaching);
                in_query->retracting_points.push_back(retracting);
            }

            void manipulator_tp_t::add_null_path()
            {
                manipulation_tp_t::add_null_path();
                in_query->constraints.push_back(std::set<unsigned>());
                in_query->full_constraints.push_back(std::set<unsigned>());
                in_query->solutions_costs.push_back(-1);
                in_query->reaching_points.push_back(-1);
                in_query->retracting_points.push_back(-1);
            }
        }
    }
}
