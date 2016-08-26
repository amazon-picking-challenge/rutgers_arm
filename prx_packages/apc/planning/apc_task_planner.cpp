/**
 * @file apc_task_planner.cpp
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

#include "planning/apc_task_planner.hpp"
#include "planning/modules/manipulation_validity_checker.hpp"
#include "planning/specifications/manipulation_specification.hpp"

#include "prx/utilities/definitions/string_manip.hpp"
#include "prx/utilities/definitions/random.hpp"
#include "prx/utilities/math/configurations/config.hpp"
#include "prx/utilities/statistics/statistics.hpp"
#include "utilities/definitions/manip_defs.hpp"

#include "simulation/workspace_trajectory.hpp"

#include <boost/range/adaptor/map.hpp>
#include <boost/assign/list_of.hpp>
#include <pluginlib/class_list_macros.h>


#include "prx/planning/modules/validity_checkers/world_model_validity_checker.hpp"

#include <time.h>

#define MAX_TRIES 9999999999

PLUGINLIB_EXPORT_CLASS(prx::packages::apc::apc_task_planner_t ,prx::plan::planner_t)

namespace prx
{
    using namespace util;
    using namespace sim;
    using namespace plan;

    namespace packages
    {
        using namespace manipulation;
        namespace apc
        {

            apc_task_planner_t::apc_task_planner_t()
            {

            }

            apc_task_planner_t::~apc_task_planner_t()
            {

            }

            void apc_task_planner_t::init(const parameter_reader_t* reader, const parameter_reader_t* template_reader)
            {
                failedmanualretr1 = 0;
                failedmanualretr2 = 0;
                transferfailed = 0;
                movefailed = 0;

                PRX_PRINT("Initializing apc_task_planner_t!", PRX_TEXT_CYAN);
                task_planner_t::init(reader,template_reader);
                left_context_name = parameters::get_attribute("left_context_name", reader, template_reader);
                right_context_name = parameters::get_attribute("right_context_name", reader, template_reader);
                left_camera_context_name = parameters::get_attribute("left_camera_context_name", reader, template_reader);
                left_camera_jac_context_name = parameters::get_attribute("left_camera_jac_context_name", reader, template_reader);
                right_camera_jac_context_name = parameters::get_attribute("right_camera_jac_context_name", reader, template_reader);
                right_camera_context_name = parameters::get_attribute("right_camera_context_name", reader, template_reader);
                manipulation_task_planner_name = parameters::get_attribute("manipulation_task_planner_name", reader, template_reader);
                bin_trajectories = reader->get_attribute_as<bool>("bin_trajectories", false);
                smooth_paths = reader->get_attribute_as<bool>("smooth_paths", false);
                jk_prm_star = reader->get_attribute_as<bool>("jk_prm_star", false);

                manip_planner = dynamic_cast<manipulation_tp_t*>(planners[manipulation_task_planner_name]);

                float gripper_offset = 0.0;
                float lens_offset = 0.18;
                first_stage_offset = 0.24;
                examination_profile_t* profile = new examination_profile_t();
                profile->focus.push_back(0.769-first_stage_offset);
                profile->focus.push_back(0.218+lens_offset);
                profile->focus.push_back(1.515);
                profile->base_viewpoint.set(-0.546, 0.567, -0.447, 0.424);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['A'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.777-first_stage_offset);
                profile->focus.push_back(-0.079+lens_offset);
                profile->focus.push_back(1.511);
                profile->base_viewpoint.set(-0.546, 0.567, -0.447, 0.424);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['B'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.796-first_stage_offset);
                profile->focus.push_back(-0.274+lens_offset);
                profile->focus.push_back(1.527);
                profile->base_viewpoint.set(-0.546, 0.567, -0.447, 0.424);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['C'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.762-first_stage_offset);
                profile->focus.push_back(0.220+lens_offset);
                profile->focus.push_back(1.304);
                profile->base_viewpoint.set(-0.560, 0.600, -0.431, 0.375);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['D'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.755-first_stage_offset);
                profile->focus.push_back(-0.062+lens_offset);
                profile->focus.push_back(1.305);
                profile->base_viewpoint.set(-0.560, 0.600, -0.431, 0.375);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['E'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.758-first_stage_offset);
                profile->focus.push_back(-0.344+lens_offset);
                profile->focus.push_back(1.295);
                profile->base_viewpoint.set(-0.560, 0.600, -0.431, 0.375);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['F'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.758-first_stage_offset);
                profile->focus.push_back(0.236+lens_offset);
                profile->focus.push_back(1.069);
                profile->base_viewpoint.set(-0.567, 0.608, -0.420, 0.364);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['G'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.752-first_stage_offset);
                profile->focus.push_back(-0.047+lens_offset);
                profile->focus.push_back(1.072);
                profile->base_viewpoint.set(-0.558, 0.599, -0.434, 0.377);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['H'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.755-first_stage_offset);
                profile->focus.push_back(-0.344+lens_offset);
                profile->focus.push_back(1.071);
                profile->base_viewpoint.set(-0.558, 0.599, -0.434, 0.377);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['I'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.75-first_stage_offset);
                profile->focus.push_back( 0.236+lens_offset);
                profile->focus.push_back(0.860);
                profile->base_viewpoint.set(-0.582, 0.626, -0.393, 0.339);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['J'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.760-first_stage_offset);
                profile->focus.push_back(-0.058+lens_offset);
                profile->focus.push_back( 0.867);
                profile->base_viewpoint.set(-0.582, 0.626, -0.393, 0.339);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['K'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.766-first_stage_offset);
                profile->focus.push_back(-0.386+lens_offset);
                profile->focus.push_back(0.861);
                profile->base_viewpoint.set(-0.582, 0.626, -0.393, 0.339);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['L'] = profile;

                profile = new examination_profile_t();
                profile->focus.push_back(0.40);
                profile->focus.push_back(0.0);
                profile->focus.push_back(0.80);
                profile->base_viewpoint.set(0, 0, -0.70711,0.70711);
                // profile->base_viewpoint.set(0, 0.70711, 0,0.70711);
                profile->base_viewpoint.normalize();
                profile->offsets.push_back(profile->base_viewpoint);
                profile->distance = .3;
                camera_positions['T'] = profile;

                // left_arm_order_bin  = {-1.57,1.661659,0.677508,0,-1.120185,0,-0.165669,0,0};
                left_arm_order_bin  = {-1.57,1.5618822574615479, 0.6113936305046082, 0.0, -0.5987081527709961, -0.01734461449086666, -0.49032706022262573, 0.016362464055418968};
                // right_arm_order_bin = { 1.57,1.661659,0.677508,0,-1.120185,0,-0.165669,0};
                right_arm_order_bin = {1.57, 1.59486, 0.30705, 0.058093, -0.995599, 0.173795, -0.3270568, -0.361237};
                // right_arm_order_bin = { 1.57,1.5,0.5,0,-1.120185,0,-0.165669,0};
                left_arm_home = {0.00000,1.57000,0.00000,0.00000,-1.70000,0.00000,0.00000,0.00000};
                // right_arm_home= {0.00000,1.57000,0.00000,0.00000,-1.70000,0.00000,0.00000,0.00000};

                PRX_PRINT("Completed APC Task Planner Init!", PRX_TEXT_GREEN);
            }

            void apc_task_planner_t::link_world_model(world_model_t * const model)
            {
                task_planner_t::link_world_model(model);
                manipulation_model = dynamic_cast<manipulation_world_model_t*>(model);
                if(manipulation_model == NULL)
                    PRX_FATAL_S("The manipulation task planner can work only with manipulation world model!");
            }

            void apc_task_planner_t::link_query(query_t* new_query)
            {
                task_planner_t::link_query(new_query);
                in_query = static_cast<apc_task_query_t*>(new_query);
            }

            void apc_task_planner_t::setup()
            {
                // TODO: This should probably link all the underlying task planner's specifications, not just manip tp...
                // set the manip tp spec
                output_specifications[manip_planner->get_name()]->setup( manipulation_model );
                // PRX_PRINT("ASDF"<<manip_planner->get_name(), PRX_TEXT_RED);
                manip_planner->link_specification(output_specifications[manipulation_task_planner_name]);
                foreach(planner_t* planner, planners | boost::adaptors::map_values)
                {
                    PRX_INFO_S("setup planner: " << planner->get_name());
                    planner->setup();
                }
            }

            bool apc_task_planner_t::execute()
            {
                foreach(planner_t* planner, planners | boost::adaptors::map_values)
                {                    
                    PRX_INFO_S("execute planner: " << planner->get_name());
                    planner->execute();
                    // planner->update_visualization();
                }
                left_manipulation_query = new manipulation_query_t();
                right_manipulation_query = new manipulation_query_t();


                manipulation_model->use_context(left_context_name);
                manipulator = manipulation_model->get_current_manipulation_info()->manipulator;
                full_manipulator_state_space = manipulation_model->get_current_manipulation_info()->full_manipulator_state_space;
                full_manipulator_control_space = manipulation_model->get_current_manipulation_info()->full_manipulator_control_space;

                manipulation_specification_t* manip_spec = dynamic_cast<manipulation_specification_t*>(output_specifications[manipulation_task_planner_name]);
                left_manipulation_query->path_constraints = manip_spec->validity_checker->alloc_constraint();
                left_manipulation_query->set_valid_constraints(manip_spec->validity_checker->alloc_constraint());
                left_manipulation_query->set_search_mode(LAZY_SEARCH);
                right_manipulation_query->path_constraints = manip_spec->validity_checker->alloc_constraint();
                right_manipulation_query->set_valid_constraints(manip_spec->validity_checker->alloc_constraint());
                right_manipulation_query->set_search_mode(LAZY_SEARCH);


                return true;
            }

            bool apc_task_planner_t::succeeded() const
            {
                return true;
            }

            const util::statistics_t* apc_task_planner_t::get_statistics()
            {
                return NULL;
            }
            bool apc_task_planner_t::move()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER-> Current Task: Move\n", PRX_TEXT_MAGENTA);

                manipulation_specification_t* manip_spec = dynamic_cast<manipulation_specification_t*>(output_specifications[manipulation_task_planner_name]);
                //taking a full manipulator state, move both arms in sequence to that position
                state_t* full_init = full_manipulator_state_space->alloc_point();
                // PRX_PRINT ("FULL INIT: " << full_manipulator_state_space->print_point(full_init), PRX_TEXT_CYAN);
                manipulation_model->use_context(left_context_name);
                space_t* left_space =  manipulation_model->get_state_space();
                manipulation_model->use_context(right_context_name);
                space_t* right_space =  manipulation_model->get_state_space();


                state_t* initial_state_left = left_space->alloc_point();
                state_t* left_goal = left_space->alloc_point();
                state_t* initial_state_right = right_space->alloc_point();
                state_t* right_goal = right_space->alloc_point();

                //Logic to decide which hand to plan for first
                std::vector<double> initial_state_left_vec;
                left_space->copy_point_to_vector(initial_state_left, initial_state_left_vec);
                std::vector<double> initial_state_right_vec;
                right_space->copy_point_to_vector(initial_state_right, initial_state_right_vec);
                double eps = 0.001;

                bool plan_for_left_first = false;
                for(int i=1; i<left_arm_home.size();i++)
                {
                    if(left_arm_home.at(i) - initial_state_left->at(i) > eps)
                    {
                        plan_for_left_first = true;
                        break;
                    }
                }
                in_query->move_plan.clear();
                if(plan_for_left_first)
                {
                    manipulation_model->convert_spaces(left_space,left_goal,full_manipulator_state_space,in_query->goal_state);
                    //in order to make sure the right movement takes into account any movement on the left, this update is needed
                    full_manipulator_state_space->copy_from_point(full_init);
                    manipulation_model->convert_spaces(right_space,initial_state_right,left_space,left_goal);
                    manipulation_model->convert_spaces(right_space,right_goal,full_manipulator_state_space,in_query->goal_state);

                    full_manipulator_state_space->free_point(full_init);
                    PRX_PRINT("Planning for Left hand and then for Right", PRX_TEXT_CYAN);
                    if(move_left_hand(initial_state_left, left_goal))
                    {
                        PRX_PRINT("Left hand planning Success",PRX_TEXT_GREEN);
                        manipulation_model->get_state_space()->free_point(initial_state_left);
                        manipulation_model->get_state_space()->free_point(left_goal);

                        if(move_right_hand(initial_state_right, right_goal)){
                            PRX_PRINT("Right hand planning Success",PRX_TEXT_GREEN);
                            manipulation_model->get_state_space()->free_point(initial_state_right);
                            manipulation_model->get_state_space()->free_point(right_goal);
                            return true;
                        }
                        else
                        {
                            PRX_PRINT("Right hand planning Failure",PRX_TEXT_RED);
                            manipulation_model->get_state_space()->free_point(initial_state_left);
                            manipulation_model->get_state_space()->free_point(left_goal);
                            manipulation_model->get_state_space()->free_point(initial_state_right);
                            manipulation_model->get_state_space()->free_point(right_goal);
                            return false;
                        }
                    }
                    else
                    {
                        PRX_PRINT("Left hand planning Failure",PRX_TEXT_RED);
                        manipulation_model->get_state_space()->free_point(initial_state_left);
                        manipulation_model->get_state_space()->free_point(left_goal);
                        manipulation_model->get_state_space()->free_point(initial_state_right);
                        manipulation_model->get_state_space()->free_point(right_goal);
                        return false;
                    }
                }
                else
                {
                    manipulation_model->convert_spaces(right_space,right_goal,full_manipulator_state_space,in_query->goal_state);
                    //in order to make sure the right movement takes into account any movement on the left, this update is needed
                    full_manipulator_state_space->copy_from_point(full_init);
                    manipulation_model->convert_spaces(left_space,initial_state_left,right_space,right_goal);
                    manipulation_model->convert_spaces(left_space,left_goal,full_manipulator_state_space,in_query->goal_state);
                    PRX_PRINT("Planning for Right hand and then for Left", PRX_TEXT_CYAN);
                    if(move_right_hand(initial_state_right, right_goal))
                    {
                        PRX_PRINT("Right hand planning Success",PRX_TEXT_GREEN);
                        manipulation_model->get_state_space()->free_point(initial_state_right);
                        manipulation_model->get_state_space()->free_point(right_goal);

                        if(move_left_hand(initial_state_left, left_goal)){
                            PRX_PRINT("Left hand planning Success",PRX_TEXT_GREEN);
                            manipulation_model->get_state_space()->free_point(initial_state_left);
                            manipulation_model->get_state_space()->free_point(left_goal);
                            return true;
                        }
                        else
                        {
                            PRX_PRINT("Left hand planning Failure",PRX_TEXT_RED);
                            manipulation_model->get_state_space()->free_point(initial_state_left);
                            manipulation_model->get_state_space()->free_point(left_goal);
                            manipulation_model->get_state_space()->free_point(initial_state_right);
                            manipulation_model->get_state_space()->free_point(right_goal);
                            return false;
                        }
                    }
                    else
                    {
                        PRX_PRINT("Right hand planning Failure",PRX_TEXT_RED);
                        manipulation_model->get_state_space()->free_point(initial_state_left);
                        manipulation_model->get_state_space()->free_point(left_goal);
                        manipulation_model->get_state_space()->free_point(initial_state_right);
                        manipulation_model->get_state_space()->free_point(right_goal);
                        return false;
                    }
                }
            }

            bool apc_task_planner_t::move_and_detect()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Move And Detect\n", PRX_TEXT_MAGENTA);
                if(in_query->hand=="left")
                {
                    manipulation_model->use_context(left_camera_context_name);
                }
                else
                {
                    manipulation_model->use_context(right_camera_context_name);   
                }
                
                state_t* full_init = full_manipulator_state_space->alloc_point();
                //prepare for the grasp
                config_t gripper_config;

                //3-STAGE TRAJ
                //gripper_config.set_position(camera_positions[in_query->bin]->focus[0],camera_positions[in_query->bin]->focus[1],camera_positions[in_query->bin]->focus[2]);
                //gripper_config.set_orientation(camera_positions[in_query->bin]->base_viewpoint);
                //3-STAGE TRAJ
                //2-STAGE TRAJ
                double RGB_cam_offset = 0.08;
                double bin_A_offset = 0.0;
                if(in_query->hand == "left" && (in_query->bin == 'A' || in_query->bin == 'J') )
                {
                    bin_A_offset = 0.08;
                }
                else
                {
                    bin_A_offset = 0.0;
                }
                gripper_config.set_position(camera_positions[in_query->bin]->focus[0]+first_stage_offset- bin_A_offset ,camera_positions[in_query->bin]->focus[1]+RGB_cam_offset+0.10,camera_positions[in_query->bin]->focus[2]);
                gripper_config.set_orientation(camera_positions[in_query->bin]->base_viewpoint);
                //2-STAGE TRAJ
                PRX_PRINT("\n\n\nGRIPPER CONFIG: "<<gripper_config.print(),PRX_TEXT_RED);

                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();
                state_t* seed_state = manipulation_model->get_state_space()->alloc_point();

                int i=0;
                bool found = false;
                while(i<20 && !found)
                {
                    found = manipulation_model->IK(result_state,seed_state,gripper_config);
                    manipulation_model->get_state_space()->uniform_sample(seed_state);
                    PRX_DEBUG_COLOR("SEED: "<<seed_state<<"  i = "<<i,PRX_TEXT_RED);
                    i++;
                    if(jk_prm_star)
                    {
                        found = true;
                    }
                }
                if(!found)
                {
                    manipulation_model->get_state_space()->free_point(initial_state);
                    manipulation_model->get_state_space()->free_point(result_state);
                    manipulation_model->get_state_space()->free_point(seed_state);
                    return false;
                }
                if(bin_trajectories)
                {
                    int last_index = manipulation_model->get_state_space()->get_dimension();
                    if(in_query->hand == "left")
                    {
                        result_state->at(last_index-2)=0;
                        result_state->at(last_index-3)=0;
                    }
                    else
                    {
                        result_state->at(last_index-2)=0;
                    }
                    // PRX_PRINT("FINAL: "<< manipulation_model->get_state_space()->print_point(result_state,2),PRX_TEXT_GREEN);
                }

                PRX_PRINT("@Move and Detect:: Current Context: "<<manipulation_model->get_current_context(), PRX_TEXT_CYAN);

                //NO Jacobian 
                if(!jk_prm_star)
                {
                    manipulation_query->setup_move(manipulation_model->get_current_context(),initial_state,result_state); 
                }
                else
                {
                    //Jacobian
                    PRX_PRINT("USING JKPRM*...",PRX_TEXT_RED);
                    manipulation_query->setup_move_to_config(manipulation_model->get_current_context(),initial_state,gripper_config);
                    
                }
                
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                in_query->move_gripper_to_bin.clear();

                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                manipulation_model->get_state_space()->free_point(seed_state);
                bool success = manipulation_query->found_path;

                // workspace_trajectory_t ws_trajectory;
                // manipulation_query->plan.clear();
                // in_query->move_gripper_to_bin.clear();
                // bool success = manipulation_model->jac_steering(manipulation_query->plan, ws_trajectory, result_state, initial_state, gripper_config);
                
                if(in_query->hand=="left")
                {
                    manipulation_model->use_context(left_context_name);
                }
                else if(in_query->hand=="right")
                {
                    manipulation_model->use_context(right_context_name);
                }
                if(manipulation_query->found_path)
                {
                    manipulation_model->convert_plan(in_query->move_gripper_to_bin, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                    // PRX_FATAL_S("TESTING");
                }
                return success;
            }

            bool apc_task_planner_t::move_and_detect_second_stage()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Move And Detect 2nd Stage\n", PRX_TEXT_MAGENTA);
                if(in_query->hand=="left")
                {
                    manipulation_model->use_context(left_camera_jac_context_name);
                }
                else
                {
                    manipulation_model->use_context(right_camera_jac_context_name);   
                }
                
                state_t* full_init = full_manipulator_state_space->alloc_point();
                //prepare for the grasp
                config_t gripper_config;

                double random_Y_offset = ((double)rand()/(double)RAND_MAX)*0.2 -0.1;
                double RGB_cam_offset = 0.08;
                gripper_config.set_position(camera_positions[in_query->bin]->focus[0]+first_stage_offset,camera_positions[in_query->bin]->focus[1]+RGB_cam_offset+0.10,camera_positions[in_query->bin]->focus[2]);
                gripper_config.set_orientation(camera_positions[in_query->bin]->base_viewpoint);

                PRX_PRINT("\n\n\nGRIPPER CONFIG: "<<gripper_config.print(),PRX_TEXT_RED);
                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();

                if(bin_trajectories)
                {
                    int last_index = manipulation_model->get_state_space()->get_dimension();
                    if(in_query->hand == "left")
                    {
                        result_state->at(last_index-2)=0;
                        result_state->at(last_index-3)=0;
                    }
                    else
                    {
                        result_state->at(last_index-2)=0;
                    }
                }

                PRX_PRINT("@Move and Detect:: Current Context: "<<manipulation_model->get_current_context(), PRX_TEXT_CYAN);
                
                workspace_trajectory_t ws_trajectory;

                manipulation_query->plan.clear();
                in_query->move_gripper_to_bin.clear();
                
                bool success = manipulation_model->jac_steering(manipulation_query->plan, ws_trajectory, result_state, initial_state, gripper_config);
               
                if(in_query->hand=="left")
                {
                    manipulation_model->use_context(left_context_name);
                }
                else if(in_query->hand=="right")
                {
                    manipulation_model->use_context(right_context_name);
                }
                if(success)
                {
                    manipulation_model->convert_plan(in_query->move_gripper_to_bin, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                }
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                return success;
            }

            bool apc_task_planner_t::move_and_detect_third_stage()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Move And Detect 3rd Stage\n", PRX_TEXT_MAGENTA);
                if(in_query->hand=="left")
                {
                    manipulation_model->use_context(left_camera_jac_context_name);
                }
                else
                {
                    manipulation_model->use_context(right_camera_jac_context_name);   
                }
                
                state_t* full_init = full_manipulator_state_space->alloc_point();
                //prepare for the grasp
                config_t gripper_config;

                
                double RGB_cam_offset = 0.08;
                gripper_config.set_position(camera_positions[in_query->bin]->focus[0]+first_stage_offset,camera_positions[in_query->bin]->focus[1]+RGB_cam_offset-0.10,camera_positions[in_query->bin]->focus[2]);
                gripper_config.set_orientation(camera_positions[in_query->bin]->base_viewpoint);
                PRX_PRINT("\n\n\nGRIPPER CONFIG: "<<gripper_config.print(),PRX_TEXT_RED);

                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();

                if(bin_trajectories)
                {
                    int last_index = manipulation_model->get_state_space()->get_dimension();
                    if(in_query->hand == "left")
                    {
                        result_state->at(last_index-2)=0;
                        result_state->at(last_index-3)=0;
                    }
                    else
                    {
                        result_state->at(last_index-2)=0;
                    }
                }

                PRX_PRINT("@Move and Detect:: Current Context: "<<manipulation_model->get_current_context(), PRX_TEXT_CYAN);
                
                workspace_trajectory_t ws_trajectory;

                manipulation_query->plan.clear();
                in_query->move_gripper_to_bin.clear();


                bool success = manipulation_model->jac_steering(manipulation_query->plan, ws_trajectory, result_state, initial_state, gripper_config);
                
                if(in_query->hand=="left")
                {
                    manipulation_model->use_context(left_context_name);
                }
                else if(in_query->hand=="right")
                {
                    manipulation_model->use_context(right_context_name);
                }
                if(success)
                {
                    manipulation_model->convert_plan(in_query->move_gripper_to_bin, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                }
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                return success;
            }

            bool apc_task_planner_t::move_and_detect_tote()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Move And Detect Tote\n", PRX_TEXT_MAGENTA);
                //prepare for the grasp
                config_t gripper_config;

                if(in_query->hand=="left")
                {
                    manipulation_model->use_context(left_camera_context_name);
                    
                }
                else
                {
                    manipulation_model->use_context(right_camera_context_name);   
                }

                state_t* full_init = full_manipulator_state_space->alloc_point();
                gripper_config.set_position(camera_positions[in_query->bin]->focus[0],camera_positions[in_query->bin]->focus[1],camera_positions[in_query->bin]->focus[2]);
                gripper_config.set_orientation(camera_positions[in_query->bin]->base_viewpoint);    

                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();
                state_t* seed_state = manipulation_model->get_state_space()->alloc_point();

                int i=0;
                bool found = false;
                while(i<20 && !found)
                {
                    found = manipulation_model->IK(result_state,seed_state,gripper_config);
                    PRX_PRINT("State: "<<manipulation_model->get_state_space()->print_point(result_state,2),PRX_TEXT_MAGENTA);
                    manipulation_model->get_state_space()->uniform_sample(seed_state);
                    PRX_PRINT("SEED: "<<seed_state<<"  i = "<<i,PRX_TEXT_RED);
                    i++;
                }
                if(!found)
                {
                    manipulation_model->get_state_space()->free_point(initial_state);
                    manipulation_model->get_state_space()->free_point(result_state);
                    manipulation_model->get_state_space()->free_point(seed_state);
                    return false;
                }
                if(bin_trajectories)
                {
                    int last_index = manipulation_model->get_state_space()->get_dimension();
                    if(in_query->hand == "left")
                    {
                        result_state->at(last_index-2)=0;
                        result_state->at(last_index-3)=0;
                    }
                    else
                    {
                        result_state->at(last_index-2)=0;
                    }
                    // PRX_PRINT("FINAL: "<< manipulation_model->get_state_space()->print_point(result_state,2),PRX_TEXT_GREEN);
                }

                PRX_PRINT("@Move and Detect Tote:: Current Context: "<<manipulation_model->get_current_context(), PRX_TEXT_CYAN);

                //NO Jacobian 
                if(!jk_prm_star)
                {
                    manipulation_query->setup_move(manipulation_model->get_current_context(),initial_state,result_state); 
                }
                else
                {
                    //Jacobian
                    PRX_PRINT("USING JKPRM*...",PRX_TEXT_RED);
                    manipulation_query->setup_move_to_config(manipulation_model->get_current_context(),initial_state,gripper_config);
                    
                }
                
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                in_query->move_gripper_to_bin.clear();

                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                manipulation_model->get_state_space()->free_point(seed_state);
                bool success = manipulation_query->found_path;
                if(manipulation_query->found_path)
                {
                    manipulation_model->convert_plan(in_query->move_gripper_to_bin, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                }
                return success;
            }

            bool apc_task_planner_t::perform_grasp()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Perform Grasp\n", PRX_TEXT_MAGENTA);
                //pick the object 
                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();
                PRX_PRINT("in_query->object: "<<in_query->object->get_object_type(), PRX_TEXT_GREEN);
                const space_t* object_space = in_query->object->get_state_space();
                state_t* stored_object_state = object_space->alloc_point();
                state_t* initial_object = object_space->alloc_point();
                // state_t* initial_object = object_space->set_from_vector(in_query->object_state);
                // initial_object->set_from_vector(in_query->object_state);

                //left arm pick and place
                config_t retract_config;
                // if(in_query->hand=="left")
                // {
                //     retract_config.set_position(0,0,-.02);
                //     retract_config.set_orientation(0,0,0,1);
                //     manipulation_model->use_context(left_context_name);
                // }
                // else if(in_query->hand=="right")
                // {
                //     retract_config.set_position(0,0,-.02);
                //     retract_config.set_orientation(0,0,0,1);
                //     manipulation_model->use_context(right_context_name);
                // }
                // else
                {
                    // TODO: THIS IS PROBABLY NOT CORRECT FOR A GENERAL SOLUTION
                    retract_config.set_position(0,0,-.025);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(in_query->hand);
                }

                bool success = false;
                int previous_grasp_mode = manipulation_model->get_current_grasping_mode();

                state_t* final_state = manipulation_model->get_state_space()->alloc_point();

                state_t* arm_final = manipulation_model->get_current_manipulation_info()->arm_state_space->alloc_point();
                if(in_query->hand.find("left") != std::string::npos)
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(left_arm_order_bin,arm_final);
                else if (in_query->hand.find("right") != std::string::npos)
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(right_arm_order_bin,arm_final);
                else
                {
                    PRX_FATAL_S ("Neither left nor right detected in context name: " << in_query->hand);
                }
                manipulation_model->convert_spaces(manipulation_model->get_state_space(),final_state,manipulation_model->get_current_manipulation_info()->arm_state_space,arm_final);

                manipulation_model->get_current_manipulation_info()->arm_state_space->free_point(arm_final);

                PRX_INFO_S("APC Query requests use of the "<<in_query->hand<<" hand.");

                // Make sure the initial and target manipulator state have the same GRASP mode

                // manipulation_model->push_state(final_state);
                // bool closed = manipulation_model->get_current_manipulation_info()->manipulator->is_end_effector_closed( manipulation_model->get_current_manipulation_info()->end_effector_index );
                // if( !closed )
                // {
                //     PRX_DEBUG_COLOR("Target state was not set to be a grasping state. Fixing...", PRX_TEXT_BROWN);
                //     // The target state in a PICK_AND_MOVE must also be in a grasped state
                //     manipulation_model->engage_grasp(manipulation_query->plan, pick_data->relative_grasp->grasping_mode, true);
                //     manipulation_model->get_state_space()->copy_to_point(manipulator_target_state);
                // }
                //manipulation_query->plan.clear();
                 manipulation_query->setup_pick_via_config_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, 1, retract_config, initial_state, final_state, initial_object);
               
                // manipulation_query->setup_pick_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, 1, retract_config, initial_state, final_state, initial_object);
                // // manipulation_query->setup_pick_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, final_state, initial_object);
                
                manipulation_query->ik_steer_paths = true;
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                success = manipulation_query->found_path;
                if(success)
                {
                    PRX_PRINT("Initial state of the arm: "<<manipulation_model->get_state_space()->print_point(initial_state,3), PRX_TEXT_MAGENTA);
                    PRX_PRINT("Initial state of the object: "<<object_space->print_point(initial_object,3), PRX_TEXT_MAGENTA);
                    trajectory_t temp_path(manipulation_model->get_state_space());
                    object_space->copy_from_point(initial_object);
                    manipulation_model->propagate_plan(initial_state, manipulation_query->plan, temp_path);
                    PRX_PRINT("Final state of the arm: "<<manipulation_model->get_state_space()->print_point(temp_path[temp_path.size()-1],3), PRX_TEXT_MAGENTA);
                    PRX_PRINT("Final state of the object: "<<object_space->print_memory(3), PRX_TEXT_MAGENTA);
                    validity_checker = new world_model_validity_checker_t();
                    validity_checker->link_model(manipulation_model);
                    PRX_PRINT("\n\n\n----------------------------------\nAPC task planner: Checking validity of the computed plan::: "<<validity_checker->is_valid(temp_path)<<"\n\n", PRX_TEXT_GREEN);

                    PRX_PRINT("\n\n\n\n\n\n\n\n\n\n\n\n\n SUCCESFUL PICK AND MOVE \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",PRX_TEXT_GREEN);
                    manipulation_model->convert_plan(in_query->retrieve_object, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                }
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                manipulation_model->get_state_space()->free_point(final_state);
                object_space->free_point(initial_object);
                object_space->free_point(stored_object_state);
                return success;
            }

            bool apc_task_planner_t::retry_grasp()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Perform Grasp\n", PRX_TEXT_MAGENTA);
                //pick the object 
                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();
                PRX_PRINT("in_query->object: "<<in_query->object->get_object_type(), PRX_TEXT_GREEN);
                const space_t* object_space = in_query->object->get_state_space();
                state_t* stored_object_state = object_space->alloc_point();
                state_t* initial_object = object_space->alloc_point();
                // state_t* initial_object = object_space->set_from_vector(in_query->object_state);
                // initial_object->set_from_vector(in_query->object_state);

                //left arm pick and place
                config_t retract_config;
                if(in_query->hand=="left")
                {
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(left_context_name);
                }
                else if(in_query->hand=="right")
                {
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(right_context_name);
                }
                else
                {
                    // TODO: THIS IS PROBABLY NOT CORRECT
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(in_query->hand);
                }

                bool success = false;
                int previous_grasp_mode = manipulation_model->get_current_grasping_mode();

                state_t* final_state = manipulation_model->get_state_space()->alloc_point();

                state_t* arm_final = manipulation_model->get_current_manipulation_info()->arm_state_space->alloc_point();
                if(in_query->hand.find("left") != std::string::npos)
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(left_arm_order_bin,arm_final);
                else if (in_query->hand.find("right") != std::string::npos)
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(right_arm_order_bin,arm_final);
                else
                {
                    PRX_FATAL_S ("Neither left nor right detected in context name: " << in_query->hand);
                }
                manipulation_model->convert_spaces(manipulation_model->get_state_space(),final_state,manipulation_model->get_current_manipulation_info()->arm_state_space,arm_final);

                manipulation_model->get_current_manipulation_info()->arm_state_space->free_point(arm_final);

                PRX_INFO_S("APC Query requests use of the "<<in_query->hand<<" hand.");

                // Make sure the initial and target manipulator state have the same GRASP mode

                // manipulation_model->push_state(final_state);
                // bool closed = manipulation_model->get_current_manipulation_info()->manipulator->is_end_effector_closed( manipulation_model->get_current_manipulation_info()->end_effector_index );
                // if( !closed )
                // {
                //     PRX_DEBUG_COLOR("Target state was not set to be a grasping state. Fixing...", PRX_TEXT_BROWN);
                //     // The target state in a PICK_AND_MOVE must also be in a grasped state
                //     manipulation_model->engage_grasp(manipulation_query->plan, pick_data->relative_grasp->grasping_mode, true);
                //     manipulation_model->get_state_space()->copy_to_point(manipulator_target_state);
                // }
                //manipulation_query->plan.clear();
                 manipulation_query->setup_pick_via_config_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, 1, retract_config, initial_state, final_state, initial_object);
               
                // manipulation_query->setup_pick_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, 1, retract_config, initial_state, final_state, initial_object);
                // // manipulation_query->setup_pick_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, final_state, initial_object);
                
                manipulation_query->ik_steer_paths = true;
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                success = manipulation_query->found_path;
                if(success)
                {
                    PRX_PRINT("Initial state of the arm: "<<manipulation_model->get_state_space()->print_point(initial_state,3), PRX_TEXT_MAGENTA);
                    PRX_PRINT("Initial state of the object: "<<object_space->print_point(initial_object,3), PRX_TEXT_MAGENTA);
                    trajectory_t temp_path(manipulation_model->get_state_space());
                    object_space->copy_from_point(initial_object);
                    manipulation_model->propagate_plan(initial_state, manipulation_query->plan, temp_path);
                    PRX_PRINT("Final state of the arm: "<<manipulation_model->get_state_space()->print_point(temp_path[temp_path.size()-1],3), PRX_TEXT_MAGENTA);
                    PRX_PRINT("Final state of the object: "<<object_space->print_memory(3), PRX_TEXT_MAGENTA);
                    validity_checker = new world_model_validity_checker_t();
                    validity_checker->link_model(manipulation_model);
                    PRX_PRINT("\n\n\n----------------------------------\nAPC task planner: Checking validity of the computed plan::: "<<validity_checker->is_valid(temp_path)<<"\n\n", PRX_TEXT_GREEN);

                    PRX_PRINT("\n\n\n\n\n\n\n\n\n\n\n\n\n SUCCESFUL PICK AND MOVE \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",PRX_TEXT_GREEN);
                    manipulation_model->convert_plan(in_query->retrieve_object, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                }
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                manipulation_model->get_state_space()->free_point(final_state);
                object_space->free_point(initial_object);
                object_space->free_point(stored_object_state);
                return success;
            }

            bool apc_task_planner_t::remove_from_tote()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Remove From Tote...\n", PRX_TEXT_MAGENTA);
                //pick the object 
                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();

                PRX_PRINT("in_query->object: "<<in_query->object->get_object_type(), PRX_TEXT_GREEN);
                const space_t* object_space = in_query->object->get_state_space();
                state_t* stored_object_state = object_space->alloc_point();
                state_t* initial_object = object_space->alloc_point();

                //left arm pick and place
                config_t retract_config;
                if(in_query->hand=="left")
                {
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(left_context_name);
                }
                else if(in_query->hand=="right")
                {
                    retract_config.set_position(0,0,-.07);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(right_context_name);
                }
                else
                {
                    // TODO: THIS IS PROBABLY NOT CORRECT
                    retract_config.set_position(0,0,-.05);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(in_query->hand);
                }

                bool success = false;
                int previous_grasp_mode = manipulation_model->get_current_grasping_mode();

                state_t* final_state = manipulation_model->get_state_space()->alloc_point();

                state_t* arm_final = manipulation_model->get_current_manipulation_info()->arm_state_space->alloc_point();

                config_t ee_to_head_camera_config;
                if(in_query->hand.find("left") != std::string::npos)
                {
                    ee_to_head_camera_config.set_position(.5,0.0,1.4);
                    ee_to_head_camera_config.set_orientation(0.70711,0.0,0.0,0.70711);
                    // manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(left_arm_order_bin,arm_final);
                }
                else if (in_query->hand.find("right") != std::string::npos)
                {
                    ee_to_head_camera_config.set_position(.5, 0.0, 1.4);
                    ee_to_head_camera_config.set_orientation(-0.70711,0.0,0.0,0.70711);
                    // manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(right_arm_order_bin,arm_final);
                }               
                else
                {
                    PRX_FATAL_S ("Neither left nor right detected in context name: " << in_query->hand);
                }

                PRX_PRINT("Selected Config is: "<<ee_to_head_camera_config.print(), PRX_TEXT_MAGENTA);

                state_t* seed_state = manipulation_model->get_state_space()->alloc_point();

                int i=0;
                bool found = false;
                while(i<20 && !found)
                {
                    found = manipulation_model->IK(result_state,seed_state,ee_to_head_camera_config);
                    manipulation_model->get_state_space()->uniform_sample(seed_state);
                    PRX_DEBUG_COLOR("SEED: "<<seed_state<<"  i = "<<i,PRX_TEXT_RED);
                    i++;
                }
                if(!found)
                {
                    manipulation_model->get_state_space()->free_point(initial_state);
                    manipulation_model->get_state_space()->free_point(result_state);
                    manipulation_model->get_state_space()->free_point(seed_state);
                    return false;
                }

                PRX_PRINT("Result State is: "<<manipulation_model->get_state_space()->print_point(result_state,2), PRX_TEXT_MAGENTA);

                //We do not want to leave the torso free. 
                result_state->at(0)=0;

                std::vector<double> arm_state_head_camera;
                
                if(in_query->hand.find("left") != std::string::npos)
                {
                    //we do not want movement on the ee hinge
                    result_state->at(8)=0;
                    manipulation_model->get_state_space()->copy_point_to_vector(result_state,arm_state_head_camera);
                    for(int i=0; i<arm_state_head_camera.size(); i++)
                    {
                        PRX_PRINT("value BEFORE: "<<arm_state_head_camera.at(i),PRX_TEXT_RED);
                    }
                    arm_state_head_camera.erase(arm_state_head_camera.begin()+9,arm_state_head_camera.begin()+10);
                    for(int i=0; i<arm_state_head_camera.size(); i++)
                    {
                        PRX_PRINT("value AFTER: "<<arm_state_head_camera.at(i),PRX_TEXT_GREEN);
                    }
                }
                else if (in_query->hand.find("right") != std::string::npos)
                {
                    manipulation_model->get_state_space()->copy_point_to_vector(result_state,arm_state_head_camera);
                    for(int i=0; i<arm_state_head_camera.size(); i++)
                    {
                        PRX_PRINT("value BEFORE: "<<arm_state_head_camera.at(i),PRX_TEXT_RED);
                    }
                    arm_state_head_camera.erase(arm_state_head_camera.begin()+8,arm_state_head_camera.begin()+11);
                    for(int i=0; i<arm_state_head_camera.size(); i++)
                    {
                        PRX_PRINT("value AFTER: "<<arm_state_head_camera.at(i),PRX_TEXT_GREEN);
                    }
                } 

                PRX_PRINT("Result State is: "<<manipulation_model->get_state_space()->print_point(result_state,2), PRX_TEXT_MAGENTA);

                // manipulation_model->get_current_manipulation_info()->arm_state_space->copy_from_point(result_state);
                // config_t test_config;
                // manipulation_model->FK(test_config);
                // PRX_PRINT("TEST CONFIG: "<<test_config.print(), PRX_TEXT_RED);
                manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(arm_state_head_camera,arm_final);

                manipulation_model->convert_spaces(manipulation_model->get_state_space(),final_state,manipulation_model->get_current_manipulation_info()->arm_state_space,arm_final);

                manipulation_model->get_current_manipulation_info()->arm_state_space->free_point(arm_final);

                PRX_INFO_S("APC Query requests use of the "<<in_query->hand<<" hand.");

                // Make sure the initial and target manipulator state have the same GRASP mode

                // manipulation_model->push_state(final_state);
                // bool closed = manipulation_model->get_current_manipulation_info()->manipulator->is_end_effector_closed( manipulation_model->get_current_manipulation_info()->end_effector_index );
                // if( !closed )
                // {
                //     PRX_DEBUG_COLOR("Target state was not set to be a grasping state. Fixing...", PRX_TEXT_BROWN);
                //     // The target state in a PICK_AND_MOVE must also be in a grasped state
                //     manipulation_model->engage_grasp(manipulation_query->plan, pick_data->relative_grasp->grasping_mode, true);
                //     manipulation_model->get_state_space()->copy_to_point(manipulator_target_state);
                // }
                //manipulation_query->plan.clear();
                manipulation_query->setup_pick_via_config_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, 1, retract_config, initial_state, final_state, initial_object);
               
                // manipulation_query->setup_pick_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, 1, retract_config, initial_state, final_state, initial_object);
                // manipulation_query->setup_pick_and_move( manipulation_model->get_current_context(), GRASP_GREEDY, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, final_state, initial_object);
                
                manipulation_query->ik_steer_paths = true;
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                success = manipulation_query->found_path;
                if(success)
                {
                    PRX_PRINT("Initial state of the arm: "<<manipulation_model->get_state_space()->print_point(initial_state,3), PRX_TEXT_MAGENTA);
                    PRX_PRINT("Initial state of the object: "<<object_space->print_point(initial_object,3), PRX_TEXT_MAGENTA);
                    trajectory_t temp_path(manipulation_model->get_state_space());
                    object_space->copy_from_point(initial_object);
                    manipulation_model->propagate_plan(initial_state, manipulation_query->plan, temp_path);
                    PRX_PRINT("Final state of the arm: "<<manipulation_model->get_state_space()->print_point(temp_path[temp_path.size()-1],3), PRX_TEXT_MAGENTA);
                    PRX_PRINT("Final state of the object: "<<object_space->print_memory(3), PRX_TEXT_MAGENTA);
                    validity_checker = new world_model_validity_checker_t();
                    validity_checker->link_model(manipulation_model);
                    PRX_PRINT("\n\n\n----------------------------------\nAPC task planner: Checking validity of the computed plan::: "<<validity_checker->is_valid(temp_path)<<"\n\n", PRX_TEXT_GREEN);

                    PRX_PRINT("\n\n\n\n\n\n\n\n\n\n\n\n\n SUCCESFUL PICK AND MOVE \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",PRX_TEXT_GREEN);
                    manipulation_model->convert_plan(in_query->retrieve_object, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                }
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                manipulation_model->get_state_space()->free_point(final_state);
                object_space->free_point(initial_object);
                object_space->free_point(stored_object_state);
                return success;
            }

            bool apc_task_planner_t::move_to_other_bin()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Move To Other Bin\n", PRX_TEXT_MAGENTA);
 
                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();
                const space_t* object_space = in_query->object->get_state_space();
                state_t* stored_object_state = object_space->alloc_point();
                state_t* initial_object = object_space->alloc_point();
                
                PRX_PRINT("in_query->final_obj_state: "<<in_query->final_obj_state, PRX_TEXT_GREEN);
                state_t* final_object = object_space->alloc_point();
                object_space->copy_vector_to_point(in_query->final_obj_state, final_object);
                
                config_t retract_config;
                if(in_query->hand=="left")
                {
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(left_context_name);
                }
                else
                {
                    retract_config.set_position(0,0,-.07);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(right_context_name);
                }

                bool success = false;
                int previous_grasp_mode = manipulation_model->get_current_grasping_mode();
                state_t* final_state = manipulation_model->get_state_space()->alloc_point();
                state_t* arm_final = manipulation_model->get_current_manipulation_info()->arm_state_space->alloc_point();

                if(in_query->hand == "left")
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(left_arm_order_bin,arm_final);
                else
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(right_arm_order_bin,arm_final);

                manipulation_model->convert_spaces(manipulation_model->get_state_space(),final_state,manipulation_model->get_current_manipulation_info()->arm_state_space,arm_final);
                manipulation_model->get_current_manipulation_info()->arm_state_space->free_point(arm_final);
                manipulation_query->setup_pick_and_place( manipulation_model->get_current_context(), true, GRASP_GREEDY, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, initial_object, final_object);

                manipulation_query->ik_steer_paths = true;
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);

                manip_planner->resolve_query();

                //release the object
                manipulation_model->engage_grasp(manipulation_query->plan,1,false);
                in_query->retrieve_object.clear();
                
                success = manipulation_query->found_path;
                if(success)
                {
                    
                    PRX_PRINT("\n\n\n\n\n\n\n\n\n\n\n\n\n SUCESSFUL MOVE TO OTHER BIN \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",PRX_TEXT_GREEN);
                    manipulation_model->convert_plan(in_query->retrieve_object, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                   
                }
                //cleanup
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                manipulation_model->get_state_space()->free_point(final_state);
                object_space->free_point(initial_object);
                object_space->free_point(stored_object_state);
                return success;
            }


            bool apc_task_planner_t::adjust_ee()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Adjust End Effector\n", PRX_TEXT_MAGENTA);

                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* result_state = manipulation_model->get_state_space()->alloc_point();
                const space_t* object_space = in_query->object->get_state_space();
                state_t* stored_object_state = object_space->alloc_point();
                state_t* initial_object = object_space->alloc_point();

                config_t retract_config;
                if(in_query->hand=="left")
                {
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(left_context_name);
                }
                else
                {
                    retract_config.set_position(0,0,-.07);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(right_context_name);
                }

                bool success = false;
                int previous_grasp_mode = manipulation_model->get_current_grasping_mode();
                
                state_t* final_state = manipulation_model->get_state_space()->alloc_point();

                state_t* arm_final = manipulation_model->get_current_manipulation_info()->arm_state_space->alloc_point();
                if(in_query->hand == "left")
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(left_arm_order_bin,arm_final);
                else
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(right_arm_order_bin,arm_final);

                manipulation_model->convert_spaces(manipulation_model->get_state_space(),final_state,manipulation_model->get_current_manipulation_info()->arm_state_space,arm_final);

                manipulation_model->get_current_manipulation_info()->arm_state_space->free_point(arm_final);

                manipulation_query->setup_pick( manipulation_model->get_current_context(),true, GRASP_GREEDY, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, initial_object);

                manipulation_query->ik_steer_paths = true;
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                success = manipulation_query->found_path;
                // }
                if(success)
                {
                    
                    PRX_PRINT("\n\n\n\n\n\n\n\n\n\n\n\n\n SUCCESFUL PICK \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",PRX_TEXT_GREEN);
                    manipulation_model->convert_plan(in_query->retrieve_object, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                    
                }
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(result_state);
                manipulation_model->get_state_space()->free_point(final_state);
                object_space->free_point(initial_object);
                object_space->free_point(stored_object_state);
                return success;
            }



            bool apc_task_planner_t::move_to_order_bin()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Move To Order Bin\n", PRX_TEXT_MAGENTA);
                //move the object to the target location

                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* final_state = manipulation_model->get_state_space()->alloc_point();
                PRX_PRINT("move_to_order_bin: initial_state: "<<manipulation_model->get_state_space()->print_point(initial_state,2), PRX_TEXT_CYAN);
                
                state_t* arm_final = manipulation_model->get_current_manipulation_info()->arm_state_space->alloc_point();
                bool success = false;
                if(in_query->hand == "left")
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(left_arm_order_bin,arm_final);
                else
                    manipulation_model->get_current_manipulation_info()->arm_state_space->set_from_vector(right_arm_order_bin,arm_final);

                manipulation_model->convert_spaces(manipulation_model->get_state_space(),final_state,manipulation_model->get_current_manipulation_info()->arm_state_space,arm_final);

                manipulation_model->get_current_manipulation_info()->arm_state_space->free_point(arm_final);

                manipulation_query->setup_move(manipulation_model->get_current_context(),initial_state,final_state);
                manipulation_query->object = in_query->object;

                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);

                manip_planner->resolve_query();
                //release the object
                manipulation_model->engage_grasp(manipulation_query->plan,1,false);
                in_query->move_to_order_bin.clear();
                success = manipulation_query->found_path;
                if(success)
                {
                    manipulation_model->convert_plan(in_query->move_to_order_bin, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                }

                //cleanup
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(final_state);
                return success;
            }

            bool apc_task_planner_t::move_outside_bin()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Move Outside of Bin\n", PRX_TEXT_MAGENTA);

                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* final_state = manipulation_model->get_state_space()->alloc_point();
                PRX_PRINT("in_query->object: "<<in_query->object->get_object_type(), PRX_TEXT_GREEN);
                const space_t* object_space = in_query->object->get_state_space();
                
                PRX_PRINT("in_query->final_obj_state: "<<in_query->final_obj_state, PRX_TEXT_GREEN);
                state_t* final_object = object_space->alloc_point();
                object_space->copy_vector_to_point(in_query->final_obj_state, final_object);
                
                config_t retract_config;
                if(in_query->hand=="left")
                {
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(left_context_name);
                }
                else
                {
                    retract_config.set_position(0,0,-.07);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(right_context_name);
                }

                manipulation_model->push_state(initial_state);
                config_t current_ee_config;
                manipulation_model->FK(current_ee_config);
                retract_config.relative_to_global(current_ee_config);

                bool success = false;

                PRX_DEBUG_COLOR("initial_state: "<< manipulation_model->get_state_space()->print_point(initial_state), PRX_TEXT_GREEN);
                PRX_DEBUG_COLOR("retract_config: "<< retract_config.print(), PRX_TEXT_GREEN);
                //set up input_retraction_flag to false so it won't drop the obj at the end of the plan
                if(!jk_prm_star)
                {
                    ///////////////?ZP
                    //WE MISS THE SETUP_MOVE OR SMT HERE
                    // manipulation_query->ik_steer_paths = true;
                    // manipulation_query->setup_place(manipulation_model->get_current_context(), false, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, final_object, NULL);
                    // manipulation_query->setup_pick_and_place( manipulation_model->get_current_context(), true, GRASP_GREEDY, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, initial_object, final_object);
                    // virtual void setup_place( std::string input_manipulation_context_name, bool input_retraction_flag, movable_body_plant_t* input_object, int open_end_effector_mode, util::config_t& input_retract_config, sim::state_t* input_manipulator_initial_state, sim::state_t* input_object_target_state, grasp_t* input_suggested_grasp, bool set_constraints = false );
                    // virtual void setup_pick_and_place( std::string input_manipulation_context_name,  bool input_retraction_flag, grasp_evaluation_type_t input_evaluation_mode, movable_body_plant_t* input_object, int open_end_effector_mode, util::config_t& input_retract_config, sim::state_t* input_manipulator_initial_state, sim::state_t* input_object_initial_state, sim::state_t* input_object_target_state, grasp_t* input_suggested_grasp = NULL, bool set_constraints = false);
                }
                else
                {
                    PRX_PRINT("USING JKPRM*...",PRX_TEXT_RED);
                    manipulation_query->setup_move_to_config( manipulation_model->get_current_context(), initial_state, retract_config);
                }
                
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                success = manipulation_query->found_path;
                if(success)
                {
                    
                    PRX_PRINT("\n\n\n\n\n\n\n\n\n\n\n\n\n SUCESSFUL MOVE TO OTHER BIN \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",PRX_TEXT_GREEN);
                    manipulation_model->convert_plan(in_query->retrieve_object, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                   
                }
                //cleanup
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(final_state);
                return success;
            }

            bool apc_task_planner_t::place_inside_bin()
            {
                PRX_PRINT("\n@APC_TASK_PLANNER -> Current Task: Place Inside Bin\n", PRX_TEXT_MAGENTA);

                state_t* initial_state = manipulation_model->get_state_space()->alloc_point();
                state_t* final_state = manipulation_model->get_state_space()->alloc_point();
                PRX_PRINT("in_query->object: "<<in_query->object->get_object_type(), PRX_TEXT_GREEN);
                const space_t* object_space = in_query->object->get_state_space();
                
                PRX_PRINT("in_query->final_obj_state: "<<in_query->final_obj_state, PRX_TEXT_GREEN);
                state_t* final_object = object_space->alloc_point();
                object_space->copy_vector_to_point(in_query->final_obj_state, final_object);
                
                config_t retract_config;
                if(in_query->hand=="left")
                {
                    retract_config.set_position(0,0,-.03);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(left_context_name);
                }
                else
                {
                    retract_config.set_position(0,0,-.07);
                    retract_config.set_orientation(0,0,0,1);
                    manipulation_model->use_context(right_context_name);
                }

                manipulation_model->push_state(initial_state);
                config_t current_ee_config;
                manipulation_model->FK(current_ee_config);
                retract_config.relative_to_global(current_ee_config);

                bool success = false;

                PRX_DEBUG_COLOR("initial_state: "<< manipulation_model->get_state_space()->print_point(initial_state), PRX_TEXT_GREEN);
                PRX_DEBUG_COLOR("retract_config: "<< retract_config.print(), PRX_TEXT_GREEN);
                //set up input_retraction_flag to false so it won't drop the obj at the end of the plan
                if(!jk_prm_star)
                {
                    PRX_PRINT("USING PRM*...",PRX_TEXT_RED);
                    ///////////////?ZP
                    //WE MISS THE SETUP_MOVE OR SMT HERE
                    // manipulation_query->ik_steer_paths = true;
                    manipulation_query->setup_place(manipulation_model->get_current_context(), false, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, final_object, NULL);
                    // manipulation_query->setup_pick_and_place( manipulation_model->get_current_context(), true, GRASP_GREEDY, in_query->object, (in_query->hand=="left"?1:3), retract_config, initial_state, initial_object, final_object);
                    // virtual void setup_place( std::string input_manipulation_context_name, bool input_retraction_flag, movable_body_plant_t* input_object, int open_end_effector_mode, util::config_t& input_retract_config, sim::state_t* input_manipulator_initial_state, sim::state_t* input_object_target_state, grasp_t* input_suggested_grasp, bool set_constraints = false );
                    // virtual void setup_pick_and_place( std::string input_manipulation_context_name,  bool input_retraction_flag, grasp_evaluation_type_t input_evaluation_mode, movable_body_plant_t* input_object, int open_end_effector_mode, util::config_t& input_retract_config, sim::state_t* input_manipulator_initial_state, sim::state_t* input_object_initial_state, sim::state_t* input_object_target_state, grasp_t* input_suggested_grasp = NULL, bool set_constraints = false);
                }
                else
                {
                    PRX_PRINT("USING JKPRM*...",PRX_TEXT_RED);
                    manipulation_query->setup_move_to_config( manipulation_model->get_current_context(), initial_state, retract_config);
                }
                
                manipulation_query->plan.clear();
                manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(manipulation_query);
                manip_planner->resolve_query();
                success = manipulation_query->found_path;
                if(success)
                {
                    
                    PRX_PRINT("\n\n\n\n\n\n\n\n\n\n\n\n\n SUCESSFUL PLACE INSIDE BIN \n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n",PRX_TEXT_GREEN);
                    manipulation_model->convert_plan(in_query->retrieve_object, manipulator->get_control_space(), manipulation_query->plan, manipulation_model->get_control_space());
                   
                }
                //cleanup
                manipulation_model->get_state_space()->free_point(initial_state);
                manipulation_model->get_state_space()->free_point(final_state);
                return success;
            }

            bool apc_task_planner_t::move_left_hand(state_t* initial_state_left, state_t* left_goal)
            {
                PRX_PRINT ("Moving left hand", PRX_TEXT_RED);
                manipulation_model->use_context(left_context_name);
                // PRX_PRINT("@move_left_hand:: Current Context: "<<manipulation_model->get_current_context(), PRX_TEXT_CYAN);
                // PRX_PRINT( "@move_left_hand: initial_state: "<<manipulation_model->get_state_space()->print_point(initial_state_left),PRX_TEXT_MAGENTA);
                // PRX_PRINT( "@move_left_hand: left_goal: "<<manipulation_model->get_state_space()->print_point(left_goal),PRX_TEXT_MAGENTA);
                ///////////////////////ZP//////////////////////////////
                //SINCE WE ARE USING THE MOVE RIGHT/LEFT HAND ONLY FOR MOVE TO HOME QUERIES I THINK THERE IS NO POINT IN HAVING MOVE TO CONFIG
                ///////////////////////ZP//////////////////////////////
                //NO JACOBIAN
                // if(!jk_prm_star)
                // {
                //     left_manipulation_query->setup_move(manipulation_model->get_current_context(),initial_state_left,left_goal);
                // }
                // //JACOBIAN
                // else
                // {
                //     const util::space_t* arm_space = manipulation_model->get_state_space();
                //     arm_space->copy_from_point( left_goal );
                //     config_t ee_config;
                //     manipulation_model->FK(ee_config);
                //     PRX_PRINT("USING JKPRM*...",PRX_TEXT_RED);
                //     left_manipulation_query->setup_move_to_config(manipulation_model->get_current_context(),initial_state_left,ee_config);    
                // }
                left_manipulation_query->plan.clear();
                left_manipulation_query->smooth_paths = smooth_paths;
                left_manipulation_query->setup_move(manipulation_model->get_current_context(),initial_state_left,left_goal);
                left_manipulation_query->plan.clear();
                left_manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(left_manipulation_query);
                manip_planner->resolve_query();

                if(left_manipulation_query->found_path)
                {
                    manipulation_model->convert_plan(in_query->move_plan, manipulator->get_control_space(), left_manipulation_query->plan, manipulation_model->get_control_space());
                    plan_t plan(manipulation_model->get_control_space());
                    ///////////////////////ZP//////////////////////////////
                    //Commenting this out as it doesn't seem to be needed. was causing segmentation 
                    // manipulation_model->engage_grasp(plan,manipulation_model->get_current_manipulation_info()->end_effector_state_space->at(0),manipulation_model->get_current_manipulation_info()->is_end_effector_closed());
                    return true;
                }
                else
                {
                    return false;
                }
            }

            bool apc_task_planner_t::move_right_hand(state_t* initial_state_right, state_t* right_goal)
            {
                PRX_PRINT ("Moving right hand", PRX_TEXT_BLUE);
                //SINCE WE ARE USING THE MOVE RIGHT/LEFT HAND ONLY FOR MOVE TO HOME QUERIES I THINK THERE IS NO POINT IN HAVING MOVE TO CONFIG
                manipulation_model->use_context(right_context_name);
                // PRX_PRINT("@move_right_hand:: Current Context: "<<manipulation_model->get_current_context(), PRX_TEXT_CYAN);
                // PRX_PRINT("@move_right_hand:: full state: "<< manipulation_model->get_full_state_space()->print_memory(), PRX_TEXT_CYAN);
                // PRX_PRINT( "@move_right_hand: initial_state: "<<manipulation_model->get_state_space()->print_point(initial_state_right),PRX_TEXT_MAGENTA);
                // PRX_PRINT( "@move_right_hand: right_goal: "<<manipulation_model->get_state_space()->print_point(right_goal),PRX_TEXT_MAGENTA);
                //NO JACOBIAN
                // if(!jk_prm_star)
                // {
                //     right_manipulation_query->setup_move(manipulation_model->get_current_context(),initial_state_right,right_goal);                                       
                // }
                // else
                // {   
                //     //JACOBIAN
                //     const util::space_t* arm_space = manipulation_model->get_state_space();
                //     arm_space->copy_from_point( right_goal );
                //     config_t ee_config;
                //     manipulation_model->FK(ee_config);
                //     PRX_PRINT("USING JKPRM*...",PRX_TEXT_RED);
                //     right_manipulation_query->setup_move_to_config(manipulation_model->get_current_context(),initial_state_right,ee_config);
                // }
                
                right_manipulation_query->plan.clear();
                right_manipulation_query->smooth_paths = smooth_paths;
                right_manipulation_query->setup_move(manipulation_model->get_current_context(),initial_state_right,right_goal);           
                right_manipulation_query->plan.clear();
                right_manipulation_query->smooth_paths = smooth_paths;
                manip_planner->link_query(right_manipulation_query);
                //PRX_PRINT("FULL state: "<<manipulation_model->get_full_state_space()->print_memory(), PRX_TEXT_GREEN);

                manip_planner->resolve_query(); 
                if(right_manipulation_query->found_path)
                {
                    manipulation_model->convert_plan(in_query->move_plan, manipulator->get_control_space(), right_manipulation_query->plan, manipulation_model->get_control_space());
                    plan_t plan(manipulation_model->get_control_space());
                    ///////////////////////ZP//////////////////////////////
                    //Commenting this out as it doesn't seem to be needed. was causing segmentation 
                    // manipulation_model->engage_grasp(plan,manipulation_model->get_current_manipulation_info()->end_effector_state_space->at(0),manipulation_model->get_current_manipulation_info()->is_end_effector_closed());
                    return true;
                }
                else
                {
                    return false;
                }
            }

            void apc_task_planner_t::resolve_query()
            {
                if(in_query->stage == apc_task_query_t::MOVE)
                {
                    in_query->found_solution = move();
                }
                else
                {
                    PRX_INFO_S("APC Query requests use of the "<<in_query->hand<<" hand.");
                    if(in_query->hand=="right")
                    {
                        manipulation_model->use_context(right_context_name);
                        manipulation_query = left_manipulation_query;
                    }
                    else if(in_query->hand=="left")
                    {
                        manipulation_model->use_context(left_context_name);
                        manipulation_query = left_manipulation_query;
                    }
                    else
                    {
                        //PRX_FATAL_S("Requested hand in the apc_task_query is neither left nor right.");

                        // TESTING: We will pass the context directly.
                        manipulation_model->use_context(in_query->hand);
                        manipulation_query = left_manipulation_query;
                    }

                    if(in_query->stage == apc_task_query_t::PERFORM_GRASP)
                    {
                        in_query->found_solution = perform_grasp();
                    }
                    else if(in_query->stage == apc_task_query_t::MOVE_AND_DETECT)
                    {
                        in_query->found_solution = move_and_detect();
                    }
                    else if(in_query->stage == apc_task_query_t::MOVE_AND_DETECT_TOTE)
                    {
                        in_query->found_solution = move_and_detect_tote();
                    }
                    else if(in_query->stage == apc_task_query_t::REMOVE_FROM_TOTE)
                    {
                        in_query->found_solution = remove_from_tote();
                    }
                    else if(in_query->stage == apc_task_query_t::THREE_STAGE_TRAJECTORY_SECOND)
                    {
                        in_query->found_solution = move_and_detect_second_stage();
                    }
                    else if(in_query->stage == apc_task_query_t::THREE_STAGE_TRAJECTORY_THIRD)
                    {
                        in_query->found_solution = move_and_detect_third_stage();
                    }
                    else if(in_query->stage == apc_task_query_t::ADJUST_EE)
                    {
                        in_query->found_solution = adjust_ee();
                    }
                    else if(in_query->stage == apc_task_query_t::MOVE_TO_ORDER_BIN)
                    {
                        in_query->found_solution = move_to_order_bin();
                    }
                    else if(in_query->stage == apc_task_query_t::MOVE_TO_OTHER_BIN)
                    {
                        in_query->found_solution = move_to_other_bin();
                    }
                    else if(in_query->stage == apc_task_query_t::MOVE_OUTSIDE_BIN)
                    {
                        in_query->found_solution = move_outside_bin();
                    }
                    else if(in_query->stage == apc_task_query_t::PLACE_INSIDE_BIN)
                    {
                        in_query->found_solution = place_inside_bin();
                    }
                    else if(in_query->stage == apc_task_query_t::RETRY_GRASP){
                        in_query->found_solution = retry_grasp();
                    }
                    else
                    {
                        PRX_FATAL_S("Invalid apc task query stage: "<<in_query->stage);
                    }     
                }                  
            }
        }
    }
}
